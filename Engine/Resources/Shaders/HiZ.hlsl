// AxleEngine Hi-Z depth reduction. Each dispatch writes one independently
// allocated reduction level so the RHI can expose legal storage views on both
// D3D12 and Vulkan without relying on multi-mip UAV aliasing.

cbuffer HiZConstants : register(b0, space0) {
    float4 u_SourceSize;       // width, height, 1/width, 1/height
    float4 u_DestinationSize;  // width, height, 1/width, 1/height
    float4 u_Params;           // x: source is hardware depth, y: level
    float4 u_Reserved[13];
};

Texture2D<float> u_SourceDepth : register(t0, space1);
SamplerState u_PointSampler : register(s0, space1);
RWTexture2D<float> u_Destination : register(u0, space2);

float LoadSource(uint2 pixel) {
    uint2 clamped = min(pixel, uint2(u_SourceSize.xy) - 1u);
    return u_SourceDepth.Load(int3(clamped, 0));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= (uint)u_DestinationSize.x || pixel.y >= (uint)u_DestinationSize.y) return;

    if (u_Params.x > 0.5) {
        u_Destination[pixel] = LoadSource(pixel);
        return;
    }

    uint2 base = pixel * 2u;
    float a = LoadSource(base);
    float b = LoadSource(base + uint2(1, 0));
    float c = LoadSource(base + uint2(0, 1));
    float d = LoadSource(base + uint2(1, 1));
    // With the engine's zero-to-one depth convention, the nearest blocker is
    // the minimum depth and is the conservative choice for SSR traversal.
    u_Destination[pixel] = min(min(a, b), min(c, d));
}
