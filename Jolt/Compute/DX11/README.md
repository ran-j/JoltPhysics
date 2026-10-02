# Embedded DirectX 11 compute backend

`JPH_USE_DX11` defaults to `ON` on Windows in this fork. Link `Jolt::Jolt`; no application shader-build rule is needed.
This backend uses an application-owned D3D11 device
(feature level 11.0+, D3D11.1 context-state interfaces). It does not create a window/device, select the renderer, or replace
the existing DX12/Vulkan/Metal implementations. The default `CreateComputeSystem()` factory remains unchanged.

Applications using shared graphics resources should call the platform-neutral
`CreateComputeInterop(NativeComputeAPI, void *device)` factory in `ComputeInterop.h`. It returns an error for unsupported
or disabled backends. The returned owner exposes `GetSystem()`, `GetQueue()`, `Submit()`, `CreateFloat3Texture()` and
`CopyToTexture()`. Textures expose a native handle for the graphics consumer. RGBA32F texels contain the source float3
and alpha 1, packed row-major with the requested width. Resources and calls must belong to the same interop instance.

The DX11 factory retains the external device, isolates graphics state, and submits to the same immediate stream.
No per-frame CPU readback, shader compilation or GPU wait is introduced. The application retains ownership of its graphics
handles and must release them while the native texture is still alive. Consumers that retain COM references can defer their
own release. Other native APIs require their own shared-device/resource/synchronization implementation; standalone compute
support does not imply graphics interop support.

For direct compute access, create `ComputeSystemDX11` and call `Initialize(device, result)`.
Jolt builds `JoltShaderCompilerDX11` and embeds its own hair and transfer shaders; initialization installs the embedded loader.
Override `ComputeSystem::mShaderLoader` to supply custom `<shader-name>.dxbc` bytecode targeting `cs_5_0` with reflection
retained. There is no dependency on an engine, bgfx, or an application-generated header.

`CreateComputeShader`, `CreateComputeBuffer`, `CreateComputeQueue` and initialization return Jolt `Result` errors.
The implementation works with C++17 and exceptions disabled. As with other Jolt compute backends, invalid queue/map
call sequences are programming errors checked by assertions; asynchronous driver failures are reported through `Trace`.

All calls must be serialized on the thread that owns the device's immediate context, including mapping/upload and native
interop. Each queue temporarily switches to its own `ID3DDeviceContextState`, then restores the application's state.
Do not interleave application graphics commands or another compute queue before ending the active submission.

- `Submit()` restores graphics state without waiting for GPU completion. It uses the same immediate command stream as rendering.
- `Execute()` ends an event query and restores state. `Wait()` waits for that event; `ExecuteAndWait()` is suitable for readback.
- Structured buffers and constant/upload/readback buffers implement the existing Jolt compute interface.
- `GetContext()` exposes the active context for native resource bindings after `SetShader`; use bindings declared in that shader.
- Owners must finish submission before destroying a queue. Device/COM resources are retained for their required lifetimes.

The standalone Jolt `UnitTests --test-suite=ComputeDX11Tests` test uses WARP and its own compiled shader. It covers external
device initialization, shader-loader errors, thread-group validation, generic compute, graphics-state restoration and readback.
