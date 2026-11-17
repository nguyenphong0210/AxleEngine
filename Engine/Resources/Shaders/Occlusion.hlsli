#ifndef AXLE_OCCLUSION_HLSLI
#define AXLE_OCCLUSION_HLSLI

// Shared by GTAO generation, its bounded fallback composite, and forward
// shading. A visibility texel always belongs to one real scene depth texel.
uint2 OcclusionSourcePixel(uint2 pixel, uint2 occlusionSize, uint2 sceneSize) {
    return min(((pixel * 2 + 1) * sceneSize) / (occlusionSize * 2), sceneSize - 1);
}

float AxleOcclusionViewDepth(float depth, float4 unproject) {
    return (unproject.x * depth + unproject.y) / (unproject.z * depth + unproject.w);
}

float2 AxleReconstructOcclusion(Texture2D<float> sceneDepth, Texture2D<float2> visibility,
                                uint2 centerPixel, float4 unproject) {
    uint2 sceneSize, occlusionSize;
    sceneDepth.GetDimensions(sceneSize.x, sceneSize.y);
    visibility.GetDimensions(occlusionSize.x, occlusionSize.y);
    centerPixel = min(centerPixel, sceneSize - 1);
    float centerDepth = sceneDepth.Load(int3(centerPixel, 0));
    if (centerDepth <= 0.0 || centerDepth >= 0.9999) return float2(1.0, 1.0);
    float centerZ = AxleOcclusionViewDepth(centerDepth, unproject);
    float2 centerUV = (float2(centerPixel) + 0.5) / float2(sceneSize);
    int2 occlusionMax = int2(occlusionSize) - int2(1, 1);
    int2 occlusionCenter = min(int2(centerUV * float2(occlusionSize)), occlusionMax);
    float2 occlusionSum = 0.0;
    float weightSum = 0.0;
    const float spatialWeights[5] = {0.06136, 0.24477, 0.38774, 0.24477, 0.06136};
    [unroll] for (int y = -2; y <= 2; ++y) {
        [unroll] for (int x = -2; x <= 2; ++x) {
            int2 aoPixel = clamp(occlusionCenter + int2(x, y), int2(0, 0), occlusionMax);
            uint2 tapPixel = OcclusionSourcePixel(uint2(aoPixel), occlusionSize, sceneSize);
            float tapDepth = sceneDepth.Load(int3(tapPixel, 0));
            if (tapDepth <= 0.0 || tapDepth >= 0.9999) continue;
            float tapZ = AxleOcclusionViewDepth(tapDepth, unproject);
            float depthDiff = abs(centerZ - tapZ);
            float depthWeight = exp(-depthDiff / (0.05 * max(abs(centerZ), 0.1) + 1e-4));
            if (depthWeight < 1e-3) continue;
            float totalWeight = depthWeight * spatialWeights[x + 2] * spatialWeights[y + 2];
            occlusionSum += visibility.Load(int3(aoPixel, 0)) * totalWeight;
            weightSum += totalWeight;
        }
    }
    // Thin surfaces absent from the half-resolution representation stay neutral.
    return weightSum > 1e-4 ? (occlusionSum / weightSum) : float2(1.0, 1.0);
}

// A separate descriptor set keeps visibility out of normal/depth prepass
// material bindings, avoiding a read-before-write cycle in the render graph.
Texture2D<float2> u_ForwardOcclusion : register(t0, space4);
Texture2D<float> u_ForwardOcclusionDepth : register(t1, space4);

float2 AxleForwardVisibility(float3 position, float4x4 projection, float2 enabled) {
    if (all(enabled <= 0.0)) return float2(1.0, 1.0);
    // Invert the projection's depth equation, including orthographic matrices.
    float4 unproject = float4(-projection[3][3], projection[2][3], projection[3][2], -projection[2][2]);
    uint2 sceneSize;
    u_ForwardOcclusionDepth.GetDimensions(sceneSize.x, sceneSize.y);
    uint2 pixel = min(uint2(position.xy), sceneSize - 1);
    float guideDepth = u_ForwardOcclusionDepth.Load(int3(pixel, 0));
    if (guideDepth <= 0.0 || guideDepth >= 0.9999) return float2(1.0, 1.0);
    float fragmentZ = AxleOcclusionViewDepth(position.z, unproject);
    float guideZ = AxleOcclusionViewDepth(guideDepth, unproject);
    // Non-depth-writing or otherwise unrepresented fragments cannot borrow
    // visibility from the surface behind them.
    if (abs(fragmentZ - guideZ) > 0.001 * max(abs(fragmentZ), 0.1)) return float2(1.0, 1.0);
    float2 value = saturate(AxleReconstructOcclusion(u_ForwardOcclusionDepth, u_ForwardOcclusion, pixel, unproject));
    // Separated lighting consumes true visibility. Soft floors belong only to
    // the retained bounded post-composite fallback in GTAO.hlsl.
    return float2(enabled.x > 0.0 ? value.x : 1.0,
                  enabled.y > 0.0 ? value.y : 1.0);
}

float AxleDirectionalVisibility(float csm, float contact) {
    return min(saturate(csm), saturate(contact));
}
#endif
