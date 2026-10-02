// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-License-Identifier: MIT

#include <Jolt/Jolt.h>
#include <Jolt/Compute/ComputeInterop.h>
#include <Jolt/Core/StringTools.h>
#include <Jolt/Compute/DX11/ComputeSystemDX11.h>
#include <Jolt/Compute/DX11/ComputeQueueDX11.h>

JPH_NAMESPACE_BEGIN

namespace
{
    class TextureDX11 final : public ComputeTexture
    {
    public:
        uintptr_t GetNativeHandle() const override
        {
            return reinterpret_cast<uintptr_t>(texture.Get());
        }
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> view;
        Ref<ComputeBuffer> parameters;
        uint32 elementCount = 0;
    };

    class InteropDX11 final : public ComputeInterop
    {
    public:
        bool Initialize(ID3D11Device *device, ComputeInteropResult &result)
        {
            ComputeSystemResult systemResult;
            if (!m_system.Initialize(device, systemResult))
            {
                result.SetError(systemResult.GetError());
                return false;
            }
            ComputeQueueResult queueResult;
            if (!m_queue.Initialize(device, queueResult))
            {
                result.SetError(queueResult.GetError());
                return false;
            }
            auto shaderResult = m_system.CreateComputeShader("CopyFloat3ToTexture", 64, 1, 1);
            if (shaderResult.HasError())
            {
                result.SetError(shaderResult.GetError());
                return false;
            }
            m_copyShader = shaderResult.Get();
            return true;
        }

        ComputeSystem &GetSystem() override
        {
            return m_system;
        }
        ComputeQueue &GetQueue() override
        {
            return m_queue;
        }
        void Submit() override
        {
            m_queue.Submit();
        }

        ComputeTextureResult CreateFloat3Texture(uint32 elementCount, uint32 width) override
        {
            ComputeTextureResult result;
            constexpr uint32 limit = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
            if (elementCount == 0 || width == 0 || width > limit || (uint64(elementCount) + width - 1) / width > limit)
            {
                result.SetError("Jolt DX11: float3 texture dimensions exceed device limits");
                return result;
            }
            Ref<TextureDX11> texture = new TextureDX11;
            texture->elementCount = elementCount;
            D3D11_TEXTURE2D_DESC descriptor = {};
            descriptor.Width = width;
            descriptor.Height = (elementCount + width - 1) / width;
            descriptor.MipLevels = 1;
            descriptor.ArraySize = 1;
            descriptor.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            descriptor.SampleDesc.Count = 1;
            descriptor.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            const auto device = m_system.GetDevice();
            HRESULT status = device->CreateTexture2D(&descriptor, nullptr, &texture->texture);
            if (SUCCEEDED(status))
            {
                status = device->CreateUnorderedAccessView(texture->texture.Get(), nullptr, &texture->view);
            }
            if (FAILED(status))
            {
                result.SetError(String("Jolt DX11: failed to create shared float3 texture, HRESULT=") + ConvertToString(status));
                return result;
            }
            const uint32 parameters[] = {elementCount, width, 0, 0};
            auto bufferResult = m_system.CreateComputeBuffer(ComputeBuffer::EType::ConstantBuffer, 1, sizeof(parameters), parameters);
            if (bufferResult.HasError())
            {
                result.SetError(bufferResult.GetError());
                return result;
            }
            texture->parameters = bufferResult.Get();
            result.Set(texture.GetPtr());
            return result;
        }

        void CopyToTexture(ComputeTexture &destination, const ComputeBuffer &source) override
        {
            auto &texture = static_cast<TextureDX11 &>(destination);
            JPH_ASSERT(source.GetStride() == 3 * sizeof(float) && source.GetSize() == texture.elementCount);
            m_queue.SetShader(m_copyShader);
            m_queue.SetConstantBuffer("Params", texture.parameters);
            m_queue.SetBuffer("gPositions", &source);
            ID3D11UnorderedAccessView *view = texture.view.Get();
            m_queue.GetContext()->CSSetUnorderedAccessViews(0, 1, &view, nullptr);
            m_queue.Dispatch((texture.elementCount + 63) / 64);
        }

    private:
        ComputeSystemDX11 m_system;
        ComputeQueueDX11 m_queue;
        Ref<ComputeShader> m_copyShader;
    };
} // namespace

ComputeInteropResult CreateComputeInteropDX11(void *device)
{
    ComputeInteropResult result;
    Ref<InteropDX11> interop = new InteropDX11;
    if (interop->Initialize(static_cast<ID3D11Device *>(device), result))
    {
        result.Set(interop.GetPtr());
    }
    return result;
}

JPH_NAMESPACE_END
