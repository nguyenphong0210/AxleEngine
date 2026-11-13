// =============================================================================
// AutoExposure.hlsl - Unified D3D12 (DXIL) & Vulkan 1.3 (SPIR-V) Auto-Exposure Pass
// Log-Luminance Histogram & Smooth Eye Adaptation Compute Pass
// =============================================================================

cbuffer AutoExposureConstants : register(b0, space0) {
    float4 u_ScreenSize;      // x: Width, y: Height, z: 1/Width, w: 1/Height
    float4 u_ExposureParams;  // x: MinLogLuma, y: MaxLogLuma, z: AdaptationSpeed, w: DeltaTime
    float4 u_ExposureLimits;  // x: MinExposure, y: MaxExposure, z: KeyValue (middle grey), w: Mode
    float4 u_Reserved[13];    // 208 bytes padding -> Total: 256 bytes (CBV aligned)
};

Texture2D<float4>   u_HDRTexture     : register(t0, space1);
SamplerState        u_LinearSampler  : register(s0, space1);
RWTexture2D<float4> u_ExposureOutput : register(u0, space2);

groupshared uint s_Histogram[64];

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    // Clear shared histogram
    if (groupIndex < 64) {
        s_Histogram[groupIndex] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint width = (uint)u_ScreenSize.x;
    uint height = (uint)u_ScreenSize.y;
    float minLog = u_ExposureParams.x;
    float maxLog = u_ExposureParams.y;
    float logRange = max(maxLog - minLog, 0.01);

    if (dispatchThreadID.x < width && dispatchThreadID.y < height) {
        float3 color = u_HDRTexture.Load(int3(dispatchThreadID.xy, 0)).rgb;
        float luma = dot(color, float3(0.2126, 0.7152, 0.0722));

        if (luma > 0.0001) {
            float logLuma = clamp((log2(luma) - minLog) / logRange, 0.0, 1.0);
            uint binIndex = min((uint)(logLuma * 63.0), 63u);
            InterlockedAdd(s_Histogram[binIndex], 1);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Group thread 0 calculates average luminance and writes adapted exposure
    if (groupIndex == 0) {
        float totalWeight = 0.0;
        float weightedLogSum = 0.0;

        for (uint i = 2; i < 62; ++i) { // Exclude extreme outliers
            float count = (float)s_Histogram[i];
            float binLog = minLog + (float(i) / 63.0) * logRange;
            weightedLogSum += count * binLog;
            totalWeight += count;
        }

        float avgLogLuma = (totalWeight > 0.0) ? (weightedLogSum / totalWeight) : 0.0;
        float avgLuma = exp2(avgLogLuma);

        float keyValue = (u_ExposureLimits.z > 0.0) ? u_ExposureLimits.z : 0.18; // 18% middle grey
        float targetExposure = keyValue / max(avgLuma, 0.001);
        targetExposure = clamp(targetExposure, u_ExposureLimits.x, u_ExposureLimits.y);

        // Read previous exposure from output storage
        float prevExposure = u_ExposureOutput[uint2(0, 0)].r;
        if (prevExposure <= 0.001) prevExposure = 1.0;

        float speed = u_ExposureParams.z;
        float dt = u_ExposureParams.w;
        float blendFactor = 1.0 - exp(-dt * max(speed, 0.1));
        float adaptedExposure = lerp(prevExposure, targetExposure, blendFactor);

        u_ExposureOutput[uint2(0, 0)] = float4(adaptedExposure, avgLuma, targetExposure, 1.0);
    }
}
