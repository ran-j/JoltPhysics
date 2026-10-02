// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-License-Identifier: MIT

#include <Jolt/Jolt.h>
#include <Jolt/Compute/ComputeInterop.h>

JPH_NAMESPACE_BEGIN

#ifdef JPH_USE_DX11
ComputeInteropResult CreateComputeInteropDX11(void *device);
#endif

ComputeInteropResult CreateComputeInterop(NativeComputeAPI api, void *device)
{
#ifdef JPH_USE_DX11
    if (api == NativeComputeAPI::Direct3D11)
    {
        return CreateComputeInteropDX11(device);
    }
#endif
    JPH_UNUSED(api);
    JPH_UNUSED(device);
    ComputeInteropResult result;
    result.SetError("Jolt: native compute interop is unavailable for the requested graphics API in this build");
    return result;
}

JPH_NAMESPACE_END
