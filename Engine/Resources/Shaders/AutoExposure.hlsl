// =============================================================================
// AutoExposure.hlsl - Unified D3D12 (DXIL) & Vulkan 1.3 (SPIR-V) Auto-Exposure Pass
// Full-Viewport 256-Bin Log-Luminance Histogram & Global Eye Adaptation Compute Passes
// =============================================================================

cbuffer AutoExposureConstants : register(b0, space0) {
    float4 u_ScreenSize;       // x: Width, y: Height, z: 1/Width, w: 1/Height
    float4 u_ExposureParams;   // x: MinLogLuma, y: MaxLogLuma, z: AdaptationSpeed, w: DeltaTime
    float4 u_ExposureLimits;   // x: MinExposure, y: MaxExposure, z: KeyValue (middle grey), w: ResetExposure (>= 0.5)
    float4 u_ExposureFallback; // x: ManualFallbackExposure, yzw: Reserved
    float4 u_Reserved[12];     // 192 bytes padding -> Total: 256 bytes (CBV aligned)
};

Texture2D<float4>   u_HDRTexture     : register(t0, space1);
Texture2D<uint>     u_HistogramSRV   : register(t1, space1);
SamplerState        u_LinearSampler  : register(s0, space1);
RWTexture2D<float4> u_ExposureOutput : register(u0, space2);
RWTexture2D<uint>   u_HistogramUAV   : register(u1, space2);

static const uint DDGI_AE_HISTOGRAM_BINS = 256u;
static const float AE_BLACK_PIXEL_EPSILON = 1e-5;

groupshared uint  s_LocalHistogram[256];
groupshared uint  s_GlobalCounts[256];
groupshared float s_GlobalWeights[256];
groupshared float s_GlobalWeightedLog[256];

[numthreads(256, 1, 1)]
void CSClearHistogram(uint3 dispatchThreadID : SV_DispatchThreadID) {
    if (dispatchThreadID.x < DDGI_AE_HISTOGRAM_BINS) {
        u_HistogramUAV[uint2(dispatchThreadID.x, 0)] = 0u;
    }
}

[numthreads(16, 16, 1)]
void CSBuildHistogram(uint3 dispatchThreadID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    s_LocalHistogram[groupIndex] = 0u;
    GroupMemoryBarrierWithGroupSync();

    uint width = (uint)u_ScreenSize.x;
    uint height = (uint)u_ScreenSize.y;
    if (dispatchThreadID.x < width && dispatchThreadID.y < height) {
        float3 color = u_HDRTexture.Load(int3(dispatchThreadID.xy, 0)).rgb;
        if (!any(isnan(color)) && !any(isinf(color))) {
            float luma = dot(max(color, 0.0.xxx), float3(0.2126, 0.7152, 0.0722));
            if (isfinite(luma) && luma > AE_BLACK_PIXEL_EPSILON) {
                float minLog = u_ExposureParams.x;
                float maxLog = u_ExposureParams.y;
                float logRange = max(maxLog - minLog, 0.01);
                float logLuma = clamp((log2(luma) - minLog) / logRange, 0.0, 1.0);
                uint binIndex = min((uint)(logLuma * 255.0 + 0.5), 255u);
                InterlockedAdd(s_LocalHistogram[binIndex], 1u);
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    uint localCount = s_LocalHistogram[groupIndex];
    if (localCount > 0u) {
        InterlockedAdd(u_HistogramUAV[uint2(groupIndex, 0)], localCount);
    }
}

[numthreads(256, 1, 1)]
void CSReduceExposure(uint groupIndex : SV_GroupIndex) {
    float minLog = u_ExposureParams.x;
    float maxLog = u_ExposureParams.y;
    float logRange = max(maxLog - minLog, 0.01);
    float totalScreenPixels = max(u_ScreenSize.x * u_ScreenSize.y, 1.0);

    uint binCount = u_HistogramSRV.Load(int3(groupIndex, 0, 0));
    float normWeight = float(binCount) / totalScreenPixels;
    float binLog = minLog + (float(groupIndex) / 255.0) * logRange;

    s_GlobalCounts[groupIndex] = binCount;
    s_GlobalWeights[groupIndex] = normWeight;
    s_GlobalWeightedLog[groupIndex] = normWeight * binLog;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 128u; stride > 0u; stride >>= 1u) {
        if (groupIndex < stride) {
            s_GlobalCounts[groupIndex] += s_GlobalCounts[groupIndex + stride];
            s_GlobalWeights[groupIndex] += s_GlobalWeights[groupIndex + stride];
            s_GlobalWeightedLog[groupIndex] += s_GlobalWeightedLog[groupIndex + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0u) {
        float minExp = min(u_ExposureLimits.x, u_ExposureLimits.y);
        float maxExp = max(u_ExposureLimits.x, u_ExposureLimits.y);
        float fallbackRaw = (isfinite(u_ExposureFallback.x) && u_ExposureFallback.x > 0.0) ? u_ExposureFallback.x : 1.0;
        float fallbackExp = clamp(fallbackRaw, minExp, maxExp);
        bool resetExposure = (u_ExposureLimits.w >= 0.5);

        float prevExposure = resetExposure ? fallbackExp : u_ExposureOutput[uint2(0, 0)].r;
        if (!isfinite(prevExposure) || prevExposure <= 0.0) {
            prevExposure = fallbackExp;
        } else {
            prevExposure = clamp(prevExposure, minExp, maxExp);
        }

        uint totalValid = s_GlobalCounts[0];
        if (totalValid == 0u || s_GlobalWeights[0] <= 0.0) {
            // Black or all-non-finite frame: stay finite; preserve previous valid exposure
            // across continuous frames and use clamped fallback on first frame or camera cut.
            float blackExposure = resetExposure ? fallbackExp : prevExposure;
            u_ExposureOutput[uint2(0, 0)] = float4(blackExposure, 0.0, blackExposure, 0.0);
        } else {
            float avgLog = s_GlobalWeightedLog[0] / s_GlobalWeights[0];
            float avgLuma = exp2(avgLog);
            float keyValue = (isfinite(u_ExposureLimits.z) && u_ExposureLimits.z > 0.0) ? u_ExposureLimits.z : 0.18;
            float targetExposure = clamp(keyValue / max(avgLuma, AE_BLACK_PIXEL_EPSILON), minExp, maxExp);

            float adaptedExposure = targetExposure;
            if (!resetExposure) {
                float speed = max(u_ExposureParams.z, 0.0);
                float dt = max(u_ExposureParams.w, 0.0);
                float blendFactor = 1.0 - exp(-dt * speed);
                adaptedExposure = clamp(lerp(prevExposure, targetExposure, blendFactor), minExp, maxExp);
            }

            u_ExposureOutput[uint2(0, 0)] = float4(adaptedExposure, avgLuma, targetExposure, 1.0);
        }
    }
}
