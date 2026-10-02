StructuredBuffer<float3> gPositions : register(t0);
RWTexture2D<float4> gTexture : register(u0);
cbuffer Params : register(b0) { uint cVertexCount; uint cTextureWidth; uint2 cPadding; };
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x < cVertexCount)
    {
        gTexture[uint2(tid.x % cTextureWidth, tid.x / cTextureWidth)] = float4(gPositions[tid.x], 1);
    }
}
