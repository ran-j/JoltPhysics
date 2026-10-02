// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-License-Identifier: MIT

#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Compute/ComputeQueue.h>
#ifdef JPH_USE_DX11
#include <Jolt/Compute/DX11/IncludeDX11.h>
#include <memory>

JPH_NAMESPACE_BEGIN

class JPH_EXPORT ComputeQueueDX11 final : public JPH::ComputeQueue
{
public:
    ComputeQueueDX11();
    bool Initialize(ID3D11Device *device, ComputeQueueResult &outResult);
    ~ComputeQueueDX11();
    void SetShader(const JPH::ComputeShader *shader) override;
    void SetConstantBuffer(const char *name, const JPH::ComputeBuffer *buffer) override;
    void SetBuffer(const char *name, const JPH::ComputeBuffer *buffer) override;
    void SetRWBuffer(const char *name, JPH::ComputeBuffer *buffer, EBarrier barrier = EBarrier::Yes) override;
    void Dispatch(JPH::uint x, JPH::uint y = 1, JPH::uint z = 1) override;
    void ScheduleReadback(JPH::ComputeBuffer *destination, const JPH::ComputeBuffer *source) override;
    void Execute() override;
    void Wait() override;
    // Streaming submission: preserves the immediate context's graphics state, does not wait for GPU completion.
    // Unlike Execute/Wait, restores the graphics context immediately. Readbacks require Execute followed by Wait.
    void Submit();
    ID3D11DeviceContext *GetContext() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};
JPH_NAMESPACE_END
#endif // JPH_USE_DX11
