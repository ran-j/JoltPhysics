// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-License-Identifier: MIT

#include "UnitTestFramework.h"
#include <Jolt/Compute/ComputeInterop.h>

TEST_SUITE("ComputeInteropTests")
{
    TEST_CASE("UnavailableInteropReturnsError")
    {
        CHECK(CreateComputeInterop(NativeComputeAPI::Unknown, nullptr).HasError());
#ifndef JPH_USE_DX11
        CHECK(CreateComputeInterop(NativeComputeAPI::Direct3D11, nullptr).HasError());
#endif
    }
}
