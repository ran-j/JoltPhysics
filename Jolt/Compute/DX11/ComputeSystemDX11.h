// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-License-Identifier: MIT

#pragma once

#ifdef JPH_USE_DX11
#include <Jolt/Jolt.h>
#include <Jolt/Compute/ComputeSystem.h>
#include <Jolt/Compute/DX11/IncludeDX11.h>

JPH_NAMESPACE_BEGIN

// Embeds compute into an existing D3D11 device. Calls must be serialized on the immediate-context owning thread.
class JPH_EXPORT ComputeSystemDX11 final : public JPH::ComputeSystem
{
public:
    JPH_DECLARE_RTTI_VIRTUAL(JPH_EXPORT, ComputeSystemDX11)
    bool Initialize(ID3D11Device *device, ComputeSystemResult &outResult);
    void Shutdown();
    // Loads <name>.dxbc using ComputeSystem::mShaderLoader; bytecode must target cs_5_0.
    JPH::ComputeShaderResult CreateComputeShader(const char *name, JPH::uint32 x, JPH::uint32 y, JPH::uint32 z) override;
    JPH::ComputeBufferResult CreateComputeBuffer(
        JPH::ComputeBuffer::EType type, JPH::uint64 size, JPH::uint stride, const void *data = nullptr) override;
    JPH::ComputeQueueResult CreateComputeQueue() override;
    ID3D11Device *GetDevice() const;

private:
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
};

JPH_NAMESPACE_END
#endif // JPH_USE_DX11
