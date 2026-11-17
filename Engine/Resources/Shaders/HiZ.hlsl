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
    uint2 maxPixel = max(uint2(u_SourceSize.xy), uint2(1u, 1u)) - 1u;
    uint2 clamped = min(pixel, maxPixel);
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

    uint2 srcSize = max(uint2(u_SourceSize.xy), uint2(1u, 1u));
    uint2 dstSize = max(uint2(u_DestinationSize.xy), uint2(1u, 1u));
    if (srcSize.x == 1u && srcSize.y == 1u) {
        u_Destination[pixel] = LoadSource(uint2(0u, 0u));
        return;
    }

    uint2 base = pixel * 2u;
    float a = LoadSource(base);
    float b = LoadSource(base + uint2(1u, 0u));
    float c = LoadSource(base + uint2(0u, 1u));
    float d = LoadSource(base + uint2(1u, 1u));
    // Engine depth is [0, 1] (0=near, 1=far). Nearest blocker uses min().
    float minDepth = min(min(a, b), min(c, d));

    // Conservative odd-edge reduction: fold the trailing odd border column/row
    // into the last destination texel so no closer blocker is dropped at odd views.
    bool includeExtraX = (srcSize.x > 1u) && ((srcSize.x & 1u) != 0u) && (pixel.x + 1u == dstSize.x);
    bool includeExtraY = (srcSize.y > 1u) && ((srcSize.y & 1u) != 0u) && (pixel.y + 1u == dstSize.y);
    if (includeExtraX) {
        minDepth = min(minDepth, min(LoadSource(base + uint2(2u, 0u)), LoadSource(base + uint2(2u, 1u))));
    }
    if (includeExtraY) {
        minDepth = min(minDepth, min(LoadSource(base + uint2(0u, 2u)), LoadSource(base + uint2(1u, 2u))));
    }
    if (includeExtraX && includeExtraY) {
        minDepth = min(minDepth, LoadSource(base + uint2(2u, 2u)));
    }
    u_Destination[pixel] = minDepth;
}

// =============================================================================
// Per-Level Hi-Z Debug Visualization (M15-U5-07.1)
// =============================================================================

struct VisualizeVSOutput {
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

VisualizeVSOutput VisualizeVSMain(uint vertexID : SV_VertexID) {
    VisualizeVSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    output.TexCoord = float2(uv.x, 1.0f - uv.y);
    return output;
}

float4 VisualizePSMain(VisualizeVSOutput input) : SV_Target {
    uint2 levelSize = max(uint2(u_SourceSize.xy), uint2(1u, 1u));
    uint2 texel = min(uint2(saturate(input.TexCoord) * float2(levelSize)), levelSize - 1u);
    float depth = u_SourceDepth.Load(int3(texel, 0));
    if (!isfinite(depth)) return float4(1.0, 0.0, 1.0, 1.0);
    if (depth >= 0.9999) return float4(0.0, 0.0, 0.0, 1.0);
    float nearTone = pow(saturate(1.0 - saturate(depth)), 0.25);
    return float4(nearTone, nearTone, nearTone, 1.0);
}
