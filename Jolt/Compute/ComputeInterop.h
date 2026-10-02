// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-License-Identifier: MIT

#pragma once

#include <Jolt/Compute/ComputeSystem.h>

JPH_NAMESPACE_BEGIN

enum class NativeComputeAPI
{
    Unknown,
    Direct3D11,
    Direct3D12,
    Vulkan,
    Metal
};

// Owns a native RGBA32F texture populated from a float3 compute buffer.
// The graphics consumer must release its reference before destroying this object.
class JPH_EXPORT ComputeTexture : public RefTarget<ComputeTexture>, public NonCopyable
{
public:
    JPH_OVERRIDE_NEW_DELETE
    virtual ~ComputeTexture() = default;
    virtual uintptr_t GetNativeHandle() const = 0;
};

using ComputeTextureResult = Result<Ref<ComputeTexture>>;

// Embeds compute into the graphics device's command stream. All calls belong on the graphics owner thread.
// Resources passed to this interface must belong to this interop instance and remain alive until submission finishes.
class JPH_EXPORT ComputeInterop : public RefTarget<ComputeInterop>, public NonCopyable
{
public:
    JPH_OVERRIDE_NEW_DELETE
    virtual ~ComputeInterop() = default;
    virtual ComputeSystem &GetSystem() = 0;
    virtual ComputeQueue &GetQueue() = 0;
    virtual ComputeTextureResult CreateFloat3Texture(uint32 elementCount, uint32 width) = 0;
    virtual void CopyToTexture(ComputeTexture &destination, const ComputeBuffer &source) = 0;
    // Restore graphics state and order compute before subsequent graphics commands, without a CPU/GPU wait.
    virtual void Submit() = 0;
};

using ComputeInteropResult = Result<Ref<ComputeInterop>>;

// device is the API-native device. Unsupported or unbuilt backends return an explicit error; never fall back to CPU.
JPH_EXPORT ComputeInteropResult CreateComputeInterop(NativeComputeAPI api, void *device);

JPH_NAMESPACE_END
