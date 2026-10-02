// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-License-Identifier: MIT

#include <Jolt/Jolt.h>
#ifdef JPH_USE_DX11
#include <Jolt/Compute/ComputeInterop.h>
#include <Jolt/Compute/DX11/ComputeSystemDX11.h>
#include <Jolt/Compute/DX11/ComputeQueueDX11.h>
#include <d3dcompiler.h>
#include "UnitTestFramework.h"

TEST_SUITE("ComputeDX11Tests")
{
    TEST_CASE("ExternalDeviceShaderLoaderAndReadback")
    {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        // WARP makes this integration test runnable on Windows CI without a physical GPU.
        CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context)));
        if (!device)
        {
            return;
        }
        ComputeSystemDX11 compute;
        ComputeSystemResult initialized;
        CHECK_FALSE(compute.Initialize(nullptr, initialized));
        CHECK(compute.Initialize(device.Get(), initialized));
        if (!compute.GetDevice())
        {
            return;
        }

        const char source[] = "RWStructuredBuffer<uint> output : register(u0); "
                              "[numthreads(4,1,1)] void main(uint3 id : SV_DispatchThreadID) { output[id.x] = id.x * 3 + 7; }";
        Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
        CHECK(SUCCEEDED(D3DCompile(source, sizeof(source) - 1, "DX11Test", nullptr, nullptr, "main", "cs_5_0", 0, 0, &code, &errors)));
        if (!code)
        {
            return;
        }
        compute.mShaderLoader = [&code](const char *name, Array<uint8> &data, String &error)
        {
            if (String(name) != "Test.dxbc")
            {
                error = "Unknown test shader";
                return false;
            }
            const auto *bytes = static_cast<const uint8 *>(code->GetBufferPointer());
            data.assign(bytes, bytes + code->GetBufferSize());
            return true;
        };
        CHECK(compute.CreateComputeShader("Missing", 4, 1, 1).HasError());
        CHECK(compute.CreateComputeShader("Test", 8, 1, 1).HasError());
        CHECK(compute.CreateComputeBuffer(ComputeBuffer::EType::RWBuffer, 4, 0).HasError());
        auto shaderResult = compute.CreateComputeShader("Test", 4, 1, 1);
        CHECK(shaderResult.IsValid());
        if (!shaderResult.IsValid())
        {
            return;
        }
        auto bufferResult = compute.CreateComputeBuffer(ComputeBuffer::EType::RWBuffer, 4, sizeof(uint32));
        CHECK(bufferResult.IsValid());
        if (!bufferResult.IsValid())
        {
            return;
        }
        auto queueResult = compute.CreateComputeQueue();
        CHECK(queueResult.IsValid());
        if (!queueResult.IsValid())
        {
            return;
        }
        auto &queue = static_cast<ComputeQueueDX11 &>(*queueResult.Get());

        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        queue.SetShader(shaderResult.Get());
        queue.SetRWBuffer("output", bufferResult.Get());
        queue.Dispatch(1);
        queue.Submit();
        D3D11_PRIMITIVE_TOPOLOGY topology;
        context->IAGetPrimitiveTopology(&topology);
        CHECK(topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        auto readbackResult = bufferResult.Get()->CreateReadBackBuffer();
        CHECK(readbackResult.IsValid());
        if (!readbackResult.IsValid())
        {
            return;
        }
        queue.ScheduleReadback(readbackResult.Get(), bufferResult.Get());
        queue.ExecuteAndWait();
        const uint32 *values = readbackResult.Get()->Map<uint32>(ComputeBuffer::EMode::Read);
        CHECK(values != nullptr);
        if (values == nullptr)
        {
            return;
        }
        for (uint32 i = 0; i < 4; ++i)
        {
            CHECK(values[i] == i * 3 + 7);
        }
        readbackResult.Get()->Unmap();
    }
    TEST_CASE("NativeInteropTextureTransfer")
    {
        CHECK(CreateComputeInterop(NativeComputeAPI::Vulkan, nullptr).HasError());
        CHECK(CreateComputeInterop(NativeComputeAPI::Direct3D11, nullptr).HasError());
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context)));
        if (!device)
        {
            return;
        }
        auto interopResult = CreateComputeInterop(NativeComputeAPI::Direct3D11, device.Get());
        CHECK(interopResult.IsValid());
        if (!interopResult.IsValid())
        {
            return;
        }
        auto &interop = *interopResult.Get();
        CHECK(interop.CreateFloat3Texture(0, 3).HasError());
        CHECK(interop.CreateFloat3Texture(5, 0).HasError());
        CHECK(interop.CreateFloat3Texture(5, 16385).HasError());
        const float positions[5][3] = {{1, 2, 3}, {-4, 5, 6}, {7, -8, 9}, {10, 11, -12}, {13, 14, 15}};
        auto buffer = interop.GetSystem().CreateComputeBuffer(ComputeBuffer::EType::Buffer, 5, sizeof(positions[0]), positions);
        auto texture = interop.CreateFloat3Texture(5, 3);
        CHECK(buffer.IsValid());
        CHECK(texture.IsValid());
        if (!buffer.IsValid() || !texture.IsValid())
        {
            return;
        }
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        interop.CopyToTexture(*texture.Get(), *buffer.Get());
        interop.Submit();
        D3D11_PRIMITIVE_TOPOLOGY topology;
        context->IAGetPrimitiveTopology(&topology);
        CHECK(topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        auto native = reinterpret_cast<ID3D11Texture2D *>(texture.Get()->GetNativeHandle());
        D3D11_TEXTURE2D_DESC descriptor;
        native->GetDesc(&descriptor);
        CHECK(descriptor.Width == 3);
        CHECK(descriptor.Height == 2);
        CHECK(descriptor.Format == DXGI_FORMAT_R32G32B32A32_FLOAT);
        descriptor.BindFlags = 0;
        descriptor.Usage = D3D11_USAGE_STAGING;
        descriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
        CHECK(SUCCEEDED(device->CreateTexture2D(&descriptor, nullptr, &readback)));
        if (!readback)
        {
            return;
        }
        context->CopyResource(readback.Get(), native);
        interop.GetQueue().ExecuteAndWait();
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        CHECK(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        if (!mapped.pData)
        {
            return;
        }
        for (uint32 index = 0; index < 5; ++index)
        {
            const auto *row = static_cast<const uint8 *>(mapped.pData) + (index / 3) * mapped.RowPitch;
            const auto *pixel = reinterpret_cast<const float *>(row) + (index % 3) * 4;
            CHECK(pixel[0] == positions[index][0]);
            CHECK(pixel[1] == positions[index][1]);
            CHECK(pixel[2] == positions[index][2]);
            CHECK(pixel[3] == 1.0f);
        }
        context->Unmap(readback.Get(), 0);
    }
}
#endif
