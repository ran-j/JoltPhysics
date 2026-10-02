// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-License-Identifier: MIT

#include <Jolt/Jolt.h>
#ifdef JPH_USE_DX11
#include <Jolt/Compute/DX11/ComputeSystemDX11.h>
#include <Jolt/Compute/DX11/ComputeQueueDX11.h>
#include "ComputeShadersDX11.generated.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <map>
#include <vector>

JPH_NAMESPACE_BEGIN

namespace
{
    using Microsoft::WRL::ComPtr;

    template <class ResultType> bool Failed(HRESULT result, ResultType &outResult, const char *operation)
    {
        if (SUCCEEDED(result))
        {
            return false;
        }
        outResult.SetError(String("Compute DX11: ") + operation + " failed, HRESULT=" + std::to_string(result).c_str());
        return true;
    }

    class Shader final : public JPH::ComputeShader
    {
    public:
        Shader(uint32_t x, uint32_t y, uint32_t z) : ComputeShader(x, y, z) {}
        bool Initialize(ID3D11Device *device, const Array<uint8> &code, ComputeShaderResult &outResult)
        {
            if (Failed(device->CreateComputeShader(code.data(), code.size(), nullptr, &shader), outResult, "create shader"))
            {
                return false;
            }
            ComPtr<ID3D11ShaderReflection> reflection;
            if (Failed(D3DReflect(code.data(), code.size(), IID_PPV_ARGS(&reflection)), outResult, "shader reflection"))
            {
                return false;
            }
            D3D11_SHADER_DESC descriptor = {};
            if (Failed(reflection->GetDesc(&descriptor), outResult, "shader descriptor"))
            {
                return false;
            }
            UINT x, y, z;
            reflection->GetThreadGroupSize(&x, &y, &z);
            if (x != GetGroupSizeX() || y != GetGroupSizeY() || z != GetGroupSizeZ())
            {
                outResult.SetError("Compute DX11: shader thread group size does not match requested size");
                return false;
            }
            for (uint32_t index = 0; index < descriptor.BoundResources; ++index)
            {
                D3D11_SHADER_INPUT_BIND_DESC binding = {};
                if (Failed(reflection->GetResourceBindingDesc(index, &binding), outResult, "shader binding"))
                {
                    return false;
                }
                bindings.emplace(binding.Name, binding.BindPoint);
                if (binding.Type == D3D_SIT_STRUCTURED || binding.Type == D3D_SIT_BYTEADDRESS || binding.Type == D3D_SIT_TEXTURE)
                {
                    srvCount = std::max(srvCount, binding.BindPoint + 1);
                }
                if (binding.Type == D3D_SIT_UAV_RWSTRUCTURED || binding.Type == D3D_SIT_UAV_RWTYPED ||
                    binding.Type == D3D_SIT_UAV_RWBYTEADDRESS || binding.Type == D3D_SIT_UAV_APPEND_STRUCTURED ||
                    binding.Type == D3D_SIT_UAV_CONSUME_STRUCTURED || binding.Type == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER)
                {
                    uavCount = std::max(uavCount, binding.BindPoint + 1);
                }
            }
            return true;
        }
        ComPtr<ID3D11ComputeShader> shader;
        std::map<std::string, UINT, std::less<>> bindings;
        UINT srvCount = 0;
        UINT uavCount = 0;
    };

    class Buffer final : public JPH::ComputeBuffer
    {
    public:
        Buffer(ID3D11Device *device, EType type, uint64_t size, uint32_t stride) : ComputeBuffer(type, size, stride), m_device(device) {}
        bool Initialize(const void *data, ComputeBufferResult &outResult)
        {
            ID3D11Device *device = m_device.Get();
            const auto type = mType;
            const auto size = mSize;
            const auto stride = mStride;
            if (stride == 0 || stride % 4 != 0 || size > (std::numeric_limits<uint32_t>::max() - 16u) / stride)
            {
                outResult.SetError("Compute DX11: invalid buffer size or stride");
                return false;
            }
            const uint32_t bytes = static_cast<uint32_t>(std::max<uint64_t>(size, 1) * stride);
            D3D11_BUFFER_DESC descriptor = {};
            descriptor.ByteWidth = type == EType::ConstantBuffer ? (bytes + 15u) & ~15u : bytes;
            if (type == EType::ReadbackBuffer)
            {
                descriptor.Usage = D3D11_USAGE_STAGING;
                descriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            }
            else if (type == EType::ConstantBuffer)
            {
                descriptor.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            }
            else
            {
                descriptor.BindFlags = D3D11_BIND_SHADER_RESOURCE | (type == EType::RWBuffer ? D3D11_BIND_UNORDERED_ACCESS : 0);
                descriptor.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
                descriptor.StructureByteStride = stride;
            }
            if (type == EType::UploadBuffer || type == EType::ConstantBuffer || data != nullptr)
            {
                m_cpuData.resize(descriptor.ByteWidth, 0);
                if (data != nullptr && size != 0)
                {
                    std::memcpy(m_cpuData.data(), data, static_cast<size_t>(size * stride));
                }
            }
            D3D11_SUBRESOURCE_DATA initial = {};
            initial.pSysMem = m_cpuData.data();
            if (Failed(device->CreateBuffer(&descriptor, data ? &initial : nullptr, &resource), outResult, "create buffer"))
            {
                return false;
            }
            if ((descriptor.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0)
            {
                if (Failed(device->CreateShaderResourceView(resource.Get(), nullptr, &srv), outResult, "create buffer SRV"))
                {
                    return false;
                }
            }
            if (type == EType::RWBuffer)
            {
                if (Failed(device->CreateUnorderedAccessView(resource.Get(), nullptr, &uav), outResult, "create buffer UAV"))
                {
                    return false;
                }
            }
            if (type == EType::Buffer || type == EType::RWBuffer)
            {
                m_cpuData.clear();
                m_cpuData.shrink_to_fit();
            }
            return true;
        }

        JPH::ComputeBufferResult CreateReadBackBuffer() const override
        {
            JPH::ComputeBufferResult result;
            Ref<Buffer> buffer = new Buffer(m_device.Get(), EType::ReadbackBuffer, mSize, mStride);
            if (buffer->Initialize(nullptr, result))
            {
                result.Set(buffer.GetPtr());
            }
            return result;
        }

        void Upload(ID3D11DeviceContext *context) const
        {
            if (m_dirty)
            {
                context->UpdateSubresource(resource.Get(), 0, nullptr, m_cpuData.data(), 0, 0);
                m_dirty = false;
            }
        }

        ComPtr<ID3D11Buffer> resource;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11UnorderedAccessView> uav;

    private:
        void *MapInternal(EMode mode) override
        {
            if (mode == EMode::Read && mType == EType::ReadbackBuffer)
            {
                m_device->GetImmediateContext(&m_readContext);
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                const HRESULT status = m_readContext->Map(resource.Get(), 0, D3D11_MAP_READ, 0, &mapped);
                if (FAILED(status))
                {
                    Trace("Compute DX11: readback Map failed, HRESULT=%08X", unsigned(status));
                    JPH_ASSERT(false, "Readback Map failed");
                    m_readContext.Reset();
                    return nullptr;
                }
                return mapped.pData;
            }
            if (mode != EMode::Write || (mType != EType::UploadBuffer && mType != EType::ConstantBuffer))
            {
                JPH_ASSERT(false, "Only upload/constant buffers can be mapped for writing");
                return nullptr;
            }
            return m_cpuData.data();
        }

        void UnmapInternal() override
        {
            if (m_readContext)
            {
                m_readContext->Unmap(resource.Get(), 0);
                m_readContext.Reset();
            }
            else
            {
                m_dirty = true;
            }
        }

        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_readContext;
        std::vector<uint8_t> m_cpuData;
        mutable bool m_dirty = false;
    };
} // namespace

struct ComputeQueueDX11::State
{
    ComPtr<ID3D11DeviceContext> recording;
    ComPtr<ID3D11DeviceContext1> context1;
    ComPtr<ID3DDeviceContextState> computeState;
    ComPtr<ID3DDeviceContextState> previousState;
    bool active = false;

    void Begin()
    {
        if (!active)
        {
            context1->SwapDeviceContextState(computeState.Get(), &previousState);
            active = true;
        }
    }

    void Restore()
    {
        if (active)
        {
            context1->SwapDeviceContextState(previousState.Get(), nullptr);
            previousState.Reset();
            active = false;
        }
    }

    ~State()
    {
        Restore();
    }
    ComPtr<ID3D11DeviceContext> immediate;
    ComPtr<ID3D11Query> completion;
    const Shader *shader = nullptr;
    bool waiting = false;
};

JPH_IMPLEMENT_RTTI_VIRTUAL(ComputeSystemDX11)
{
    JPH_ADD_BASE_CLASS(ComputeSystemDX11, ComputeSystem)
}

bool ComputeSystemDX11::Initialize(ID3D11Device *device, ComputeSystemResult &outResult)
{
    if (device == nullptr || device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0)
    {
        outResult.SetError("Compute DX11 requires feature level 11.0 or newer");
        return false;
    }
    m_device = device;
    mShaderLoader = [](const char *filename, Array<uint8> &data, String &error)
    {
        const std::string_view name(filename);
        constexpr std::string_view extension = ".dxbc";
        if (name.size() > extension.size() && name.substr(name.size() - extension.size()) == extension)
        {
            const auto stem = name.substr(0, name.size() - extension.size());
            for (const auto &shader : ComputeShadersDX11::registry)
            {
                if (stem == shader.name)
                {
                    data.assign(shader.data, shader.data + shader.size);
                    return true;
                }
            }
        }
        error = String("Jolt DX11 shader is not packaged: ") + filename;
        return false;
    };
    return true;
}

void ComputeSystemDX11::Shutdown()
{
    m_device.Reset();
}

ComputeShaderResult ComputeSystemDX11::CreateComputeShader(const char *name, uint32_t x, uint32_t y, uint32_t z)
{
    ComputeShaderResult result;
    Array<uint8> code;
    String error;
    const String filename = String(name) + ".dxbc";
    if (!mShaderLoader(filename.c_str(), code, error))
    {
        result.SetError(error);
        return result;
    }
    Ref<Shader> shader = new Shader(x, y, z);
    if (shader->Initialize(m_device.Get(), code, result))
    {
        result.Set(shader.GetPtr());
    }
    return result;
}

ComputeBufferResult ComputeSystemDX11::CreateComputeBuffer(ComputeBuffer::EType type, uint64_t size, uint32_t stride, const void *data)
{
    ComputeBufferResult result;
    Ref<Buffer> buffer = new Buffer(m_device.Get(), type, size, stride);
    if (buffer->Initialize(data, result))
    {
        result.Set(buffer.GetPtr());
    }
    return result;
}

ComputeQueueResult ComputeSystemDX11::CreateComputeQueue()
{
    ComputeQueueResult result;
    Ref<ComputeQueueDX11> queue = new ComputeQueueDX11;
    if (queue->Initialize(m_device.Get(), result))
    {
        result.Set(queue.GetPtr());
    }
    return result;
}

ID3D11Device *ComputeSystemDX11::GetDevice() const
{
    return m_device.Get();
}

ComputeQueueDX11::ComputeQueueDX11() : m_state(std::make_unique<State>()) {}

bool ComputeQueueDX11::Initialize(ID3D11Device *device, ComputeQueueResult &outResult)
{
    device->GetImmediateContext(&m_state->immediate);
    m_state->recording = m_state->immediate;
    if (Failed(m_state->immediate.As(&m_state->context1), outResult, "D3D11.1 context interface"))
    {
        return false;
    }
    ComPtr<ID3D11Device1> device1;
    if (Failed(device->QueryInterface(IID_PPV_ARGS(&device1)), outResult, "D3D11.1 device interface"))
    {
        return false;
    }
    const D3D_FEATURE_LEVEL featureLevel = device->GetFeatureLevel();
    const UINT stateFlags =
        (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0 ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
    if (Failed(device1->CreateDeviceContextState(
                   stateFlags, &featureLevel, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr, &m_state->computeState),
            outResult, "create isolated compute state"))
    {
        return false;
    }
    const D3D11_QUERY_DESC query = {D3D11_QUERY_EVENT, 0};
    if (Failed(device->CreateQuery(&query, &m_state->completion), outResult, "create completion query"))
    {
        return false;
    }
    return true;
}
ComputeQueueDX11::~ComputeQueueDX11() = default;

void ComputeQueueDX11::SetShader(const JPH::ComputeShader *shader)
{
    if (m_state->waiting)
    {
        JPH_ASSERT(false, "Wait must complete before recording after Execute");
        return;
    }
    m_state->Begin();
    m_state->shader = static_cast<const Shader *>(shader);
    m_state->recording->CSSetShader(m_state->shader->shader.Get(), nullptr, 0);
}

void ComputeQueueDX11::SetConstantBuffer(const char *name, const JPH::ComputeBuffer *source)
{
    const auto binding = m_state->shader->bindings.find(name);
    if (binding == m_state->shader->bindings.end())
    {
        return; // Optimized out by the shader compiler.
    }
    const auto &buffer = static_cast<const Buffer &>(*source);
    buffer.Upload(m_state->recording.Get());
    ID3D11Buffer *resource = buffer.resource.Get();
    m_state->recording->CSSetConstantBuffers(binding->second, 1, &resource);
}

void ComputeQueueDX11::SetBuffer(const char *name, const JPH::ComputeBuffer *source)
{
    const auto binding = m_state->shader->bindings.find(name);
    if (binding == m_state->shader->bindings.end())
    {
        return;
    }
    ID3D11ShaderResourceView *resource = nullptr;
    if (source != nullptr) // Jolt omits collision buffers when no collider intersects the groom bounds.
    {
        const auto &buffer = static_cast<const Buffer &>(*source);
        buffer.Upload(m_state->recording.Get());
        resource = buffer.srv.Get();
    }
    m_state->recording->CSSetShaderResources(binding->second, 1, &resource);
}

void ComputeQueueDX11::SetRWBuffer(const char *name, JPH::ComputeBuffer *source, EBarrier)
{
    const auto binding = m_state->shader->bindings.find(name);
    if (binding == m_state->shader->bindings.end())
    {
        return;
    }
    ID3D11UnorderedAccessView *resource = static_cast<Buffer &>(*source).uav.Get();
    m_state->recording->CSSetUnorderedAccessViews(binding->second, 1, &resource, nullptr);
}

void ComputeQueueDX11::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    m_state->recording->Dispatch(x, y, z);
    // D3D11 guarantees dispatch ordering. Explicitly unbind to transition SRV/UAV usage without binding hazards.
    ID3D11UnorderedAccessView *uavs[D3D11_PS_CS_UAV_REGISTER_COUNT] = {};
    ID3D11ShaderResourceView *srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    m_state->recording->CSSetUnorderedAccessViews(0, m_state->shader->uavCount, uavs, nullptr);
    m_state->recording->CSSetShaderResources(0, m_state->shader->srvCount, srvs);
    m_state->shader = nullptr;
}

void ComputeQueueDX11::ScheduleReadback(JPH::ComputeBuffer *destination, const JPH::ComputeBuffer *source)
{
    m_state->Begin();
    m_state->recording->CopyResource(
        static_cast<Buffer *>(destination)->resource.Get(), static_cast<const Buffer *>(source)->resource.Get());
}

void ComputeQueueDX11::Submit()
{
    if (m_state->waiting)
    {
        JPH_ASSERT(false, "Pending Execute requires Wait");
        return;
    }
    m_state->Restore();
}

void ComputeQueueDX11::Execute()
{
    Submit();
    m_state->immediate->End(m_state->completion.Get());
    m_state->waiting = true;
}

void ComputeQueueDX11::Wait()
{
    if (!m_state->waiting)
    {
        return;
    }
    m_state->immediate->Flush();
    HRESULT result;
    while ((result = m_state->immediate->GetData(m_state->completion.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE)
    {
        SwitchToThread();
    }
    if (FAILED(result))
    {
        Trace("Compute DX11: waiting failed, HRESULT=%08X", unsigned(result));
        JPH_ASSERT(false, "Wait failed");
    }
    m_state->waiting = false;
}

ID3D11DeviceContext *ComputeQueueDX11::GetContext() const
{
    return m_state->recording.Get();
}
JPH_NAMESPACE_END
#endif // JPH_USE_DX11
