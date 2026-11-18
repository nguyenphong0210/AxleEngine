// =============================================================================
// AxleEngine - Ground Truth Ambient Occlusion (GTAO) & Screen-Space Contact Shadows
// Dual-compiled for Direct3D 12 (DXIL 6.0) & Vulkan 1.3 (SPIR-V)
// Horizon-based hemispherical integration with edge-preserving bilateral filter
// =============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "Occlusion.hlsli"

cbuffer GTAOConstants : register(b0, space0) {
    float4x4 u_InvProjection;
    float4x4 u_View;
    float4x4 u_Projection;
    float4   u_ScreenSize;      // (width, height, 1.0 / width, 1.0 / height)
    float4   u_GTAOParams;      // (radius pixels, strength, depth bias, power)
    float4   u_SunDirView;      // (xyz: view-space sun dir, w: sun enabled)
    float4   u_ContactParams;   // (max distance px, steps, strength, thickness)
    float4   u_Padding[16];     // Total 512 bytes aligned
};

// -----------------------------------------------------------------------------
// Common Math and Helpers
// -----------------------------------------------------------------------------
float3 ReconstructViewPos(float2 uv, float depth) {
    float4 clip = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(u_InvProjection, clip);
    return viewPos.xyz / max(abs(viewPos.w), 1e-6);
}

float3 ProjectViewToUVZ(float3 viewPos) {
    float4 clip = mul(u_Projection, float4(viewPos, 1.0));
    float invW = 1.0 / max(abs(clip.w), 1e-6);
    float3 ndc = clip.xyz * invW;
    return float3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, ndc.z);
}

// Stratified 4x4 Bayer dithering so neighboring half-res texels complement
// each other without low-frequency clumping when TAA is off.
float SampleBayer4x4(uint2 pixelPos) {
    static const float kBayer[16] = {
         0.0 / 16.0,  8.0 / 16.0,  2.0 / 16.0, 10.0 / 16.0,
        12.0 / 16.0,  4.0 / 16.0, 14.0 / 16.0,  6.0 / 16.0,
         3.0 / 16.0, 11.0 / 16.0,  1.0 / 16.0,  9.0 / 16.0,
        15.0 / 16.0,  7.0 / 16.0, 13.0 / 16.0,  5.0 / 16.0
    };
    return kBayer[(pixelPos.y & 3u) * 4u + (pixelPos.x & 3u)];
}

float SpatialHash(uint2 pixelPos) {
    return SampleBayer4x4(pixelPos);
}

float SpatialStepHash(uint2 pixelPos) {
    return SampleBayer4x4(uint2(pixelPos.y + 2u, pixelPos.x + 1u));
}

// =============================================================================
// Compute Shader (Horizon Search & Directional Contact Shadows)
// =============================================================================

Texture2D<float>    u_ComputeDepth   : register(t0, space1);
Texture2D<float4>   u_ComputeNormal  : register(t1, space1);
SamplerState        u_ComputeSampler : register(s0, space1);
RWTexture2D<float2> u_ComputeOutput  : register(u0, space2);

// Primitive of cos(theta - gamma) * sin(theta), anchored at theta=0.
// The two signed sides of a slice integrate with |sin(theta)|.
float HorizonIntegral(float theta, float gamma) {
    return 0.25 * (cos(gamma) - cos(2.0 * theta - gamma) + 2.0 * theta * sin(gamma));
}

float EvaluateGTAODistanceFalloff(float distance, float worldRadius, float falloffStartFrac) {
    float safeRadius = max(worldRadius, 0.05);
    if (distance <= 0.0 || distance >= safeRadius) return 0.0;
    float startDist = safeRadius * clamp(falloffStartFrac, 0.05, 0.95);
    float t = saturate((safeRadius - distance) / max(safeRadius - startDist, 1e-4));
    return t * t * (3.0 - 2.0 * t);
}

float SampleHorizonWeightedCos(float2 uv, uint2 sceneSize, float3 position, float3 normal,
                               float3 viewDir, float3 sliceAxis, float bias,
                               float worldRadius, float falloffStartFrac, float baseCos,
                               inout bool anyBlocker) {
    uint2 pixel = min(uint2(saturate(uv) * float2(sceneSize)), sceneSize - 1);
    float depth = u_ComputeDepth.Load(int3(pixel, 0));
    if (depth <= 0.0 || depth >= 0.9999) return baseCos;
    // Depth and reconstruction must describe the same surface sample.
    float3 delta = ReconstructViewPos((float2(pixel) + 0.5) / float2(sceneSize), depth) - position;
    float distance = length(delta);
    float viewDepth = abs(position.z);
    float minDist = max(bias, 0.001 * viewDepth);
    float selfBias = max(bias, 0.0015 * viewDepth) + 0.015 * distance;
    if (distance <= minDist || distance >= worldRadius || dot(delta, normal) <= selfBias) return baseCos;
    float falloff = EvaluateGTAODistanceFalloff(distance, worldRadius, falloffStartFrac);
    if (falloff <= 1e-4) return baseCos;
    float2 projected = float2(dot(delta, viewDir), dot(delta, sliceAxis));
    float sampleCos = projected.x / max(length(projected), 1e-6);
    if (sampleCos <= baseCos) return baseCos;
    anyBlocker = true;
    return lerp(baseCos, sampleCos, falloff);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 pixelPos = dispatchThreadId.xy;
    if (pixelPos.x >= (uint)u_ScreenSize.x || pixelPos.y >= (uint)u_ScreenSize.y) return;

    float2 rcpFrame = u_ScreenSize.zw;
    uint2 sceneSize;
    u_ComputeDepth.GetDimensions(sceneSize.x, sceneSize.y);
    uint2 sourcePixel = OcclusionSourcePixel(pixelPos, uint2(u_ScreenSize.xy), sceneSize);
    float2 uv = (float2(sourcePixel) + 0.5) / float2(sceneSize);

    // Do not average unrelated surface depths/normals before estimating AO.
    float depth = u_ComputeDepth.Load(int3(sourcePixel, 0));
    if (depth >= 0.9999 || depth <= 0.0) {
        u_ComputeOutput[pixelPos] = float2(1.0, 1.0);
        return;
    }

    float3 viewPos = ReconstructViewPos(uv, depth);

    // 1. Determine surface normal in view space
    uint2 normalSize;
    u_ComputeNormal.GetDimensions(normalSize.x, normalSize.y);
    float4 normalSample = u_ComputeNormal.Load(int3(min(sourcePixel, normalSize - 1), 0));
    float3 viewNormal;
    if (dot(normalSample.xyz, normalSample.xyz) > 0.01) {
        viewNormal = normalize(mul((float3x3)u_View, normalSample.xyz));
    } else {
        // High-quality screen derivative normal reconstruction
        float3 pR = ReconstructViewPos(uv + float2(rcpFrame.x, 0.0), u_ComputeDepth.SampleLevel(u_ComputeSampler, uv + float2(rcpFrame.x, 0.0), 0.0));
        float3 pL = ReconstructViewPos(uv - float2(rcpFrame.x, 0.0), u_ComputeDepth.SampleLevel(u_ComputeSampler, uv - float2(rcpFrame.x, 0.0), 0.0));
        float3 pD = ReconstructViewPos(uv + float2(0.0, rcpFrame.y), u_ComputeDepth.SampleLevel(u_ComputeSampler, uv + float2(0.0, rcpFrame.y), 0.0));
        float3 pU = ReconstructViewPos(uv - float2(0.0, rcpFrame.y), u_ComputeDepth.SampleLevel(u_ComputeSampler, uv - float2(0.0, rcpFrame.y), 0.0));
        float3 dx = (abs(pR.z - viewPos.z) < abs(pL.z - viewPos.z)) ? (pR - viewPos) : (viewPos - pL);
        float3 dy = (abs(pD.z - viewPos.z) < abs(pU.z - viewPos.z)) ? (pD - viewPos) : (viewPos - pU);
        viewNormal = normalize(cross(dy, dx));
        if (dot(viewNormal, -normalize(viewPos)) < 0.0) {
            viewNormal = -viewNormal;
        }
    }

    float3 viewDir = -normalize(viewPos);

    // 2. GTAO Horizon-Based Hemispherical Integration
    float ao = 1.0;
    float maxRadiusPx = max(u_GTAOParams.x, 2.0);
    float worldRadius = u_Padding[0].x > 1e-4 ? max(u_Padding[0].x, 0.05) : 2.5;
    float projScale = u_Padding[0].y;
    float falloffStartFrac = u_Padding[0].x > 1e-4 ? clamp(u_Padding[0].z, 0.05, 0.95) : 0.25;
    float radiusCap = u_Padding[0].w > 1.0 ? u_Padding[0].w : maxRadiusPx;
    float radiusPixels = (u_Padding[0].x > 1e-4 && projScale > 1e-4)
        ? clamp((worldRadius * projScale) / max(abs(viewPos.z), 0.1), 2.0, radiusCap)
        : maxRadiusPx;
    float strength = u_GTAOParams.y;
    float depthBias = u_GTAOParams.z;
    float power = max(u_GTAOParams.w, 0.1);

    if (strength > 0.001 && dot(viewNormal, viewDir) > 0.0) {
        float noise = SpatialHash(pixelPos);
        float stepNoise = SpatialStepHash(pixelPos);
        const int NUM_SLICES = 6;
        const int NUM_STEPS = 5;
        float totalOcclusion = 0.0;
        float totalWeight = 0.0;

        [unroll]
        for (int slice = 0; slice < NUM_SLICES; ++slice) {
            float phi = (float(slice) + noise) * (float(M_PI) / float(NUM_SLICES));
            float2 sliceDir = float2(cos(phi), sin(phi));

            // Project normal onto slice plane
            float3 screenAxis = float3(sliceDir.x, -sliceDir.y, 0.0);
            float3 sliceAxis = normalize(screenAxis - viewDir * dot(screenAxis, viewDir));
            float3 orthoAxis = cross(viewDir, sliceAxis);
            float3 projNormal = viewNormal - orthoAxis * dot(viewNormal, orthoAxis);
            float projLen = length(projNormal);
            if (projLen < 1e-4) continue;
            projNormal /= projLen;

            // Angle gamma between view direction and projected normal
            float gamma = atan2(dot(projNormal, sliceAxis), max(dot(projNormal, viewDir), 1e-6));
            float openNegative = gamma - float(M_PI) * 0.5;
            float openPositive = gamma + float(M_PI) * 0.5;

            // Search horizons h1 (positive) and h2 (negative) with smooth distance falloff.
            float baseCos1 = cos(openPositive);
            float baseCos2 = cos(openNegative);
            float horizonCos1 = baseCos1;
            float horizonCos2 = baseCos2;
            bool anyBlocker = false;

            for (int step = 0; step < NUM_STEPS; ++step) {
                float t = (float(step) + 0.15 + 0.85 * stepNoise) / float(NUM_STEPS);
                float stepDist = lerp(0.5, radiusPixels, t * t);

                horizonCos1 = max(horizonCos1, SampleHorizonWeightedCos(
                    uv + sliceDir * stepDist * rcpFrame, sceneSize, viewPos, viewNormal,
                    viewDir, sliceAxis, depthBias, worldRadius, falloffStartFrac, baseCos1, anyBlocker));
                horizonCos2 = max(horizonCos2, SampleHorizonWeightedCos(
                    uv - sliceDir * stepDist * rcpFrame, sceneSize, viewPos, viewNormal,
                    viewDir, sliceAxis, depthBias, worldRadius, falloffStartFrac, baseCos2, anyBlocker));
            }

            float h1 = clamp(acos(clamp(horizonCos1, -1.0, 1.0)), openNegative, openPositive);
            float h2 = clamp(-acos(clamp(horizonCos2, -1.0, 1.0)), openNegative, openPositive);
            // Normalize by the same finite-slice open hemisphere quadrature.
            // Unoccluded surfaces then stay exactly neutral at every tilt.
            float openWeight = (HorizonIntegral(openPositive, gamma) + HorizonIntegral(openNegative, gamma)) * projLen;
            float visibleWeight = (HorizonIntegral(h1, gamma) + HorizonIntegral(h2, gamma)) * projLen;
            totalOcclusion += anyBlocker ? max(openWeight - visibleWeight, 0.0) : 0.0;
            totalWeight += openWeight;
        }

        float visibility = totalWeight > 1e-4 ? 1.0 - totalOcclusion / totalWeight : 1.0;
        ao = saturate(1.0 - (1.0 - visibility) * strength);
        ao = pow(ao, power);
    }

    // 3. Screen-Space Directional Contact Shadows
    float contactShadow = 1.0;
    if (u_SunDirView.w > 0.5 && u_ContactParams.z > 0.001) {
        float3 lightDirView = normalize(u_SunDirView.xyz);
        float NdotL = dot(viewNormal, lightDirView);

        if (NdotL > 0.001) {
            float maxDistPx = max(u_ContactParams.x * 2.0, 8.0);
            int stepCount = max((int)u_ContactParams.y, 8);
            float shadowStrength = u_ContactParams.z;

            float viewDepth = abs(viewPos.z);
            float projX = max(abs(u_Projection[0][0]), 1e-4);
            float projY = max(abs(u_Projection[1][1]), 1e-4);
            float texelWorldSize = viewDepth * max(1.0 / (0.5 * float(sceneSize.x) * projX),
                                                   1.0 / (0.5 * float(sceneSize.y) * projY));

            float startBias = max(depthBias * 2.0, texelWorldSize + 0.0015 * viewDepth);
            float3 rayStart = viewPos + viewNormal * startBias;

            float targetWorldLength = maxDistPx * texelWorldSize * 1.5;
            float rayLength = clamp(targetWorldLength, 0.20, 0.04 * maxDistPx);

            bool isPerspective = abs(u_Projection[2][3]) > 1e-4;
            bool validRay = !isPerspective;
            if (isPerspective) {
                float nearViewZ = abs(ReconstructViewPos(float2(0.5, 0.5), 0.0).z);
                float safeNear = max(nearViewZ * 1.1, 0.02);
                if (rayStart.z < -safeNear) {
                    if (lightDirView.z > 1e-5) {
                        rayLength = min(rayLength, (-safeNear - rayStart.z) / lightDirView.z);
                    }
                    validRay = rayLength > 0.005;
                }
            }

            if (validRay) {
                float3 rayEnd = rayStart + lightDirView * rayLength;
                float thickness = max(max(u_ContactParams.w, 0.06), 0.12 + 0.025 * viewDepth);
                float zBias = max(0.002, texelWorldSize * 0.5 + 0.001 * viewDepth);
                float stepDither = SpatialStepHash(pixelPos);
                float blockerAccum = 0.0;

                for (int s = 1; s <= stepCount; ++s) {
                    float t = (float(s) - 0.75 + 0.75 * stepDither) / float(stepCount);
                    float3 rayPos = lerp(rayStart, rayEnd, t);

                    float3 rayUVZ = ProjectViewToUVZ(rayPos);
                    if (any(rayUVZ.xy < 0.0) || any(rayUVZ.xy > 1.0) || rayUVZ.z <= 0.0 || rayUVZ.z >= 0.9999) break;

                    uint2 tapPixel = min(uint2(rayUVZ.xy * float2(sceneSize)), sceneSize - 1);
                    if (all(tapPixel == sourcePixel)) continue;

                    float sampleDepth = u_ComputeDepth.Load(int3(tapPixel, 0));
                    if (sampleDepth <= 0.0 || sampleDepth >= 0.9999) continue;

                    float2 tapUV = (float2(tapPixel) + 0.5) / float2(sceneSize);
                    float3 samplePos = ReconstructViewPos(tapUV, sampleDepth);
                    float3 delta = samplePos - viewPos;
                    float planeBias = max(0.003, 0.0015 * viewDepth) + 0.01 * length(delta);
                    if (dot(delta, viewNormal) <= planeBias) continue;

                    float zDiff = samplePos.z - rayPos.z;
                    if (zDiff > zBias && zDiff < thickness) {
                        float depthFade = 1.0 - (zDiff / thickness);
                        float rayFade = 1.0 - smoothstep(0.70, 1.0, t);
                        blockerAccum = max(blockerAccum, depthFade * rayFade);
                    }
                }

                contactShadow = 1.0 - saturate(blockerAccum * shadowStrength * smoothstep(0.0, 0.08, NdotL));
            }
        }
    }

    u_ComputeOutput[pixelPos] = float2(ao, contactShadow);
}

// =============================================================================
// Fullscreen Bilateral Filter & Application Pass (VSMain & PSMain)
// =============================================================================

struct VSOutput {
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    output.TexCoord = float2(uv.x, 1.0f - uv.y);
    return output;
}

Texture2D<float>    u_SceneDepth        : register(t0, space1);
Texture2D<float2>   u_OcclusionTexture  : register(t1, space1);
Texture2D<float4>   u_SceneColor        : register(t2, space1);
SamplerState        u_LinearSampler     : register(s0, space1);

float4 PSMain(VSOutput input) : SV_Target {
    float2 uv = input.TexCoord;
    float2 rcpFrame = u_ScreenSize.zw;

    // The apply pass is full-resolution even though the AO source is half-
    // resolution. Fetch scene depth at exact texels so bilinear depth filtering
    // cannot manufacture values across silhouettes before edge rejection runs.
    uint2 sceneSize = uint2(max(u_ScreenSize.x, 1.0), max(u_ScreenSize.y, 1.0));
    int2 sceneMax = int2(sceneSize) - int2(1, 1);
    int2 centerPixel = clamp(int2(uv * u_ScreenSize.xy), int2(0, 0), sceneMax);
    float2 centerUV = (float2(centerPixel) + 0.5) * rcpFrame;

    float centerDepth = u_SceneDepth.Load(int3(centerPixel, 0));
    float4 sceneColor = u_SceneColor.SampleLevel(u_LinearSampler, uv, 0.0);

    if (centerDepth >= 0.9999) {
        return sceneColor;
    }

    float4 unproject = float4(u_InvProjection[2][2], u_InvProjection[2][3], u_InvProjection[3][2], u_InvProjection[3][3]);
    float2 filteredOcclusion = AxleReconstructOcclusion(u_SceneDepth, u_OcclusionTexture, uint2(centerPixel), unproject);
    float finalAO = saturate(filteredOcclusion.r);
    float finalContact = saturate(filteredOcclusion.g);

    // Apply soft minimum floor so ambient and contact shadows never crush geometry to solid black
    finalAO = lerp(0.35, 1.0, finalAO);
    finalContact = lerp(0.5, 1.0, finalContact);

    // Retained bounded post-composite fallback. The native pre-lighting path
    // consumes visibility in forward shading instead of executing this draw.
    // Keep this explicit fallback conservative when used by older callers.
    float ambientWeight = 0.25 * saturate(u_GTAOParams.y);
    float contactWeight = 0.20 * saturate(u_ContactParams.z);
    float ambientFactor = lerp(1.0, finalAO, ambientWeight);
    float contactFactor = lerp(1.0, finalContact, contactWeight);
    return float4(sceneColor.rgb * ambientFactor * contactFactor, sceneColor.a);
}

// =============================================================================
// Full-Resolution Normal- & Plane-Depth-Aware Bilateral Upsample Pass
// =============================================================================

Texture2D<float>    u_UpsampleDepth     : register(t0, space1);
Texture2D<float4>   u_UpsampleNormal    : register(t1, space1);
Texture2D<float2>   u_UpsampleOcclusion : register(t2, space1);
SamplerState        u_UpsampleSampler   : register(s0, space1);
RWTexture2D<float2> u_UpsampleOutput    : register(u0, space2);

[numthreads(8, 8, 1)]
void CSUpsample(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 pixelPos = dispatchThreadId.xy;
    uint2 sceneSize;
    u_UpsampleDepth.GetDimensions(sceneSize.x, sceneSize.y);
    if (pixelPos.x >= sceneSize.x || pixelPos.y >= sceneSize.y) return;

    float centerDepth = u_UpsampleDepth.Load(int3(pixelPos, 0));
    if (centerDepth <= 0.0 || centerDepth >= 0.9999) {
        u_UpsampleOutput[pixelPos] = float2(1.0, 1.0);
        return;
    }

    float2 centerUV = (float2(pixelPos) + 0.5) / float2(sceneSize);
    float3 centerPos = ReconstructViewPos(centerUV, centerDepth);
    float centerZ = abs(centerPos.z);

    uint2 normalSize;
    u_UpsampleNormal.GetDimensions(normalSize.x, normalSize.y);
    float4 centerNormalSample = u_UpsampleNormal.Load(int3(min(pixelPos, normalSize - 1), 0));
    bool hasCenterNormal = dot(centerNormalSample.xyz, centerNormalSample.xyz) > 0.01 && centerNormalSample.w > 0.5;
    float3 centerNormal = hasCenterNormal ? normalize(mul((float3x3)u_View, centerNormalSample.xyz)) : float3(0.0, 0.0, 1.0);

    uint2 halfSize;
    u_UpsampleOcclusion.GetDimensions(halfSize.x, halfSize.y);
    halfSize = max(halfSize, uint2(1, 1));
    int2 halfMax = int2(halfSize) - int2(1, 1);

    float2 halfCoord = centerUV * float2(halfSize) - 0.5;
    int2 baseHalf = int2(floor(halfCoord));

    float2 occlusionSum = float2(0.0, 0.0);
    float weightSum = 0.0;
    // Span the full 4x4 half-res Bayer cycle (~8 full-res px) inside continuous surfaces
    // so stratified AO noise averages smooth while normal/depth edges stay crisp.
    const float invTwoSigmaSq = 1.0 / (2.0 * 2.8 * 2.8);

    [unroll] for (int dy = -1; dy <= 2; ++dy) {
        [unroll] for (int dx = -1; dx <= 2; ++dx) {
            int2 aoPixel = clamp(baseHalf + int2(dx, dy), int2(0, 0), halfMax);
            uint2 tapPixel = OcclusionSourcePixel(uint2(aoPixel), halfSize, sceneSize);
            float tapDepth = u_UpsampleDepth.Load(int3(tapPixel, 0));
            if (tapDepth <= 0.0 || tapDepth >= 0.9999) continue;

            float3 tapPos = ReconstructViewPos((float2(tapPixel) + 0.5) / float2(sceneSize), tapDepth);
            float tapZ = abs(tapPos.z);

            float normalWeight = 1.0;
            if (hasCenterNormal) {
                float4 tapNormalSample = u_UpsampleNormal.Load(int3(min(tapPixel, normalSize - 1), 0));
                if (dot(tapNormalSample.xyz, tapNormalSample.xyz) > 0.01 && tapNormalSample.w > 0.5) {
                    float3 tapNormal = normalize(mul((float3x3)u_View, tapNormalSample.xyz));
                    float ndot = dot(centerNormal, tapNormal);
                    if (ndot < 0.25) continue;
                    normalWeight = pow(saturate(ndot), 12.0);
                }
            }

            float depthDiff = abs(centerZ - tapZ);
            float planeDist = hasCenterNormal ? abs(dot(tapPos - centerPos, centerNormal)) : depthDiff;
            float effectiveDist = hasCenterNormal ? min(planeDist, depthDiff) : depthDiff;
            float depthSigma = 0.02 * max(centerZ, 0.1) + 0.005;
            float depthWeight = exp(-effectiveDist / depthSigma);
            if (depthWeight < 1e-3) continue;

            float2 deltaPx = (float2(tapPixel) + 0.5) - (float2(pixelPos) + 0.5);
            float spatialWeight = exp(-dot(deltaPx, deltaPx) * invTwoSigmaSq);
            float totalWeight = spatialWeight * depthWeight * normalWeight;
            occlusionSum += saturate(u_UpsampleOcclusion.Load(int3(aoPixel, 0))) * totalWeight;
            weightSum += totalWeight;
        }
    }

    u_UpsampleOutput[pixelPos] = weightSum > 1e-4 ? saturate(occlusionSum / weightSum) : float2(1.0, 1.0);
}
