// AxleEngine Experimental DDGI Compute Kernels (M15 U7)
// Compiles to D3D12 DXIL 6.0 and Vulkan 1.3 SPIR-V.
//
// CSSeedClear:          Clears/initializes irradiance & distance moment atlases on reset.
// CSUpdateIrradiance:   Integrates 6x16x16 rasterized probe cube tiles -> 8x8 (6+1 border) octahedral irradiance.
// CSUpdateDistance:     Integrates radial hit distance Chebyshev moments -> 16x16 (14+1 border) distance moments.

#define AXLE_DDGI_CBUFFER_REGISTER b0
#include "DDGICommon.hlsli"

Texture2D<float4> u_CaptureColor   : register(t0, space1);
Texture2D<float>  u_CaptureDepth   : register(t1, space1);
SamplerState      u_CaptureSampler : register(s0, space1);

RWTexture2D<float4> u_IrradianceAtlas : register(u0, space2);
RWTexture2D<float4> u_DistanceAtlas   : register(u1, space2);

static const float3 g_FaceForward[6] = {
    float3( 1.0,  0.0,  0.0),
    float3(-1.0,  0.0,  0.0),
    float3( 0.0,  1.0,  0.0),
    float3( 0.0, -1.0,  0.0),
    float3( 0.0,  0.0,  1.0),
    float3( 0.0,  0.0, -1.0)
};

// Matches ReflectionCaptureContract::GetCubeFaceBasis + glm::lookAtRH (s = cross(F, U))
static const float3 g_FaceRightRH[6] = {
    float3( 0.0,  0.0,  1.0),
    float3( 0.0,  0.0, -1.0),
    float3(-1.0,  0.0,  0.0),
    float3(-1.0,  0.0,  0.0),
    float3(-1.0,  0.0,  0.0),
    float3( 1.0,  0.0,  0.0)
};

static const float3 g_FaceUp[6] = {
    float3(0.0, 1.0,  0.0),
    float3(0.0, 1.0,  0.0),
    float3(0.0, 0.0, -1.0),
    float3(0.0, 0.0,  1.0),
    float3(0.0, 1.0,  0.0),
    float3(0.0, 1.0,  0.0)
};

// Unprojects capture scratch pixel to world ray direction and clamped radial distance
float4 UnprojectCaptureRay(uint captureSlot, uint face, uint2 localPx, out float outFrontFlag) {
    uint cols = max(uint(u_DDGIUpdateParams.z + 0.5), 1u);
    uint faceRes = max(uint(u_DDGIUpdateParams.w + 0.5), 1u);
    uint linearFace = captureSlot * 6u + face;
    uint2 tile = uint2(linearFace % cols, linearFace / cols);
    uint2 px = tile * faceRes + localPx;

    float2 uv = (float2(localPx) + 0.5) / float(faceRes);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float3 unnormDir = g_FaceForward[face] + ndc.x * g_FaceRightRH[face] + ndc.y * g_FaceUp[face];
    float rayLenPerUnitZ = length(unnormDir);
    float3 rayDir = unnormDir / max(rayLenPerUnitZ, 1e-6);

    float depth = u_CaptureDepth.Load(int3(px, 0));
    float maxProbeDist = max(u_DDGIGridSpacing.w, 1.0);
    float n = max(u_DDGIClipParams.x, 1e-4);
    float f = max(u_DDGIClipParams.y, n + 1.0);

    float radialDist = maxProbeDist;
    outFrontFlag = 1.0;
    if (depth < 0.99999) {
        float viewZ = (f * n) / max(f - depth * (f - n), 1e-5);
        radialDist = min(viewZ * rayLenPerUnitZ, maxProbeDist);
        float alpha = u_CaptureColor.Load(int3(px, 0)).a;
        outFrontFlag = (alpha >= 0.5) ? 1.0 : 0.0;
    }

    return float4(rayDir, radialDist);
}

[numthreads(8, 8, 1)]
void CSSeedClear(uint3 dispatchID : SV_DispatchThreadID) {
    uint irradW = uint(u_DDGIAtlasSize.x + 0.5);
    uint irradH = uint(u_DDGIAtlasSize.y + 0.5);
    uint distW = uint(u_DDGIAtlasSize.z + 0.5);
    uint distH = uint(u_DDGIAtlasSize.w + 0.5);
    float maxDist = max(u_DDGIGridSpacing.w, 1.0);

    if (dispatchID.x < irradW && dispatchID.y < irradH) {
        u_IrradianceAtlas[dispatchID.xy] = float4(0.0, 0.0, 0.0, 0.0);
    }
    if (dispatchID.x < distW && dispatchID.y < distH) {
        u_DistanceAtlas[dispatchID.xy] = float4(maxDist, maxDist * maxDist, 0.0, 0.0);
    }
}

groupshared float4 gs_FaceColor[256];
groupshared float4 gs_FaceRayDist[256];
groupshared float4 gs_IrradTile[64];

[numthreads(8, 8, 1)]
void CSUpdateIrradiance(uint3 groupID : SV_GroupID, uint3 localID : SV_GroupThreadID) {
    uint captureSlot = groupID.x;
    uint updateCount = uint(u_DDGIUpdateParams.y + 0.5);
    uint probeCount = max(uint(u_DDGIGridDimensions.w + 0.5), 1u);
    if (captureSlot >= updateCount) {
        return;
    }

    uint startProbe = uint(u_DDGIUpdateParams.x + 0.5);
    uint probeIdx = (startProbe + captureSlot) % probeCount;
    uint atlasCols = max(uint(u_DDGIGridOrigin.w + 0.5), 1u);
    uint2 probeTile = DDGIProbeIndexToAtlasTile(probeIdx, atlasCols);
    uint2 tileOrigin = probeTile * DDGI_IRRADIANCE_TILE;

    uint localLinear = localID.y * 8u + localID.x;
    uint faceRes = max(uint(u_DDGIUpdateParams.w + 0.5), 1u);
    uint cols = max(uint(u_DDGIUpdateParams.z + 0.5), 1u);
    float dA = (2.0 / float(faceRes)) * (2.0 / float(faceRes));

    bool isInterior = (localID.x >= 1u && localID.x <= DDGI_IRRADIANCE_INTERIOR &&
                       localID.y >= 1u && localID.y <= DDGI_IRRADIANCE_INTERIOR);
    float3 normalDir = float3(0.0, 1.0, 0.0);
    if (isInterior) {
        float2 oct = ((float2(localID.xy - 1u) + 0.5) / float(DDGI_IRRADIANCE_INTERIOR)) * 2.0 - 1.0;
        normalDir = DDGIOctDecode(oct);
    }

    float3 irradSum = float3(0.0, 0.0, 0.0);
    float cosWeightSum = 0.0;
    float frontSamples = 0.0;

    for (uint face = 0u; face < 6u; ++face) {
        uint linearFace = captureSlot * 6u + face;
        uint2 capTile = uint2(linearFace % cols, linearFace / cols);

        [unroll]
        for (uint stepIdx = 0u; stepIdx < 4u; ++stepIdx) {
            uint idx = localLinear + stepIdx * 64u;
            uint2 localPx = uint2(idx % 16u, idx / 16u);
            uint2 px = capTile * faceRes + localPx;

            float frontFlag = 1.0;
            float4 rayDist = UnprojectCaptureRay(captureSlot, face, localPx, frontFlag);
            float3 rad = max(u_CaptureColor.Load(int3(px, 0)).rgb, float3(0.0, 0.0, 0.0));
            if (any(isnan(rad)) || any(isinf(rad))) {
                rad = float3(0.0, 0.0, 0.0);
            }
            float2 uv = (float2(localPx) + 0.5) / float(faceRes);
            float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
            float r2 = 1.0 + ndc.x * ndc.x + ndc.y * ndc.y;
            float dOmega = dA / (r2 * sqrt(r2));

            gs_FaceColor[idx] = float4(rad, frontFlag);
            gs_FaceRayDist[idx] = float4(rayDist.xyz, dOmega);
        }
        GroupMemoryBarrierWithGroupSync();

        if (isInterior) {
            for (uint k = 0u; k < 256u; ++k) {
                float4 colFront = gs_FaceColor[k];
                float4 dirOmega = gs_FaceRayDist[k];
                frontSamples += colFront.a;
                float cosTheta = max(dot(dirOmega.xyz, normalDir), 0.0);
                float w = cosTheta * dirOmega.w;
                irradSum += colFront.rgb * (w * colFront.a);
                cosWeightSum += w;
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (isInterior) {
        // Normalized to match ForwardPBR pre-convolved irradiance map scale (1 / PI * integral L*cos dw)
        float3 newIrrad = (cosWeightSum > 1e-6) ? (irradSum / cosWeightSum) : float3(0.0, 0.0, 0.0);
        uint2 writeCoord = tileOrigin + localID.xy;
        float4 prev = u_IrradianceAtlas[writeCoord];
        bool valid = frontSamples / 1536.0 > 1.0 - DDGI_BACKFACE_REJECT_RATIO;
        bool resetSeed = (u_DDGIFlags.z > 0.5) || (prev.a <= 0.001);
        float hysteresis = resetSeed ? 0.0 : clamp(u_DDGIParams.y, 0.0, 0.99);
        float3 blended = lerp(newIrrad, max(prev.rgb, float3(0.0, 0.0, 0.0)), hysteresis);
        gs_IrradTile[localLinear] = valid ? float4(blended, 1.0) : float4(0.0, 0.0, 0.0, 0.0);
    } else {
        gs_IrradTile[localLinear] = float4(0.0, 0.0, 0.0, 0.0);
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 srcCoord = DDGIMirrorInteriorCoord(localID.xy, DDGI_IRRADIANCE_INTERIOR);
    u_IrradianceAtlas[tileOrigin + localID.xy] = gs_IrradTile[srcCoord.y * 8u + srcCoord.x];
}

groupshared float4 gs_DistTile[256];

[numthreads(16, 16, 1)]
void CSUpdateDistanceMoments(uint3 groupID : SV_GroupID, uint3 localID : SV_GroupThreadID) {
    uint captureSlot = groupID.x;
    uint updateCount = uint(u_DDGIUpdateParams.y + 0.5);
    uint probeCount = max(uint(u_DDGIGridDimensions.w + 0.5), 1u);
    if (captureSlot >= updateCount) {
        return;
    }

    uint startProbe = uint(u_DDGIUpdateParams.x + 0.5);
    uint probeIdx = (startProbe + captureSlot) % probeCount;
    uint atlasCols = max(uint(u_DDGIGridOrigin.w + 0.5), 1u);
    uint2 probeTile = DDGIProbeIndexToAtlasTile(probeIdx, atlasCols);
    uint2 tileOrigin = probeTile * DDGI_DISTANCE_TILE;

    uint localLinear = localID.y * 16u + localID.x;
    bool isInterior = (localID.x >= 1u && localID.x <= DDGI_DISTANCE_INTERIOR &&
                       localID.y >= 1u && localID.y <= DDGI_DISTANCE_INTERIOR);
    float3 texelDir = float3(0.0, 1.0, 0.0);
    if (isInterior) {
        float2 oct = ((float2(localID.xy - 1u) + 0.5) / float(DDGI_DISTANCE_INTERIOR)) * 2.0 - 1.0;
        texelDir = DDGIOctDecode(oct);
    }

    float rSum = 0.0;
    float rSqSum = 0.0;
    float weightSum = 0.0;
    float backfaceCount = 0.0;

    for (uint face = 0u; face < 6u; ++face) {
        float frontFlag = 1.0;
        float4 rayDist = UnprojectCaptureRay(captureSlot, face, localID.xy, frontFlag);
        // Reuse gs_FaceRayDist: xyz = rayDir, w = packed sign (+dist for front, -dist for backface)
        gs_FaceRayDist[localLinear] = float4(rayDist.xyz, (frontFlag >= 0.5) ? rayDist.w : -rayDist.w);
        GroupMemoryBarrierWithGroupSync();

        if (isInterior) {
            for (uint k = 0u; k < 256u; ++k) {
                float4 rd = gs_FaceRayDist[k];
                bool isBack = (rd.w < 0.0);
                if (isBack) {
                    backfaceCount += 1.0;
                }
                float cosAlpha = max(dot(rd.xyz, texelDir), 0.0);
                if (cosAlpha > 0.0) {
                    float w = pow(cosAlpha, 24.0);
                    if (w > 1e-5) {
                        float dist = abs(rd.w);
                        rSum += dist * w;
                        rSqSum += (dist * dist) * w;
                        weightSum += w;
                    }
                }
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (isInterior) {
        float maxDist = max(u_DDGIGridSpacing.w, 1.0);
        float newMean = (weightSum > 1e-6) ? clamp(rSum / weightSum, 0.0, maxDist) : maxDist;
        float newMeanSq = (weightSum > 1e-6) ? clamp(rSqSum / weightSum, newMean * newMean, maxDist * maxDist) : (maxDist * maxDist);
        float backfaceRatio = saturate(backfaceCount / 1536.0);

        uint2 writeCoord = tileOrigin + localID.xy;
        float4 prev = u_DistanceAtlas[writeCoord];
        bool resetSeed = (u_DDGIFlags.z > 0.5) || (prev.z <= 0.001) || prev.w >= DDGI_BACKFACE_REJECT_RATIO;
        float hysteresis = resetSeed ? 0.0 : clamp(u_DDGIParams.y, 0.0, 0.99);

        float mean = lerp(newMean, prev.x, hysteresis);
        float meanSq = lerp(newMeanSq, prev.y, hysteresis);
        // Geometric validity must reflect the current capture even when moments blend.
        gs_DistTile[localLinear] = float4(mean, meanSq, 1.0, backfaceRatio);
    } else {
        gs_DistTile[localLinear] = float4(0.0, 0.0, 0.0, 0.0);
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 srcCoord = DDGIMirrorInteriorCoord(localID.xy, DDGI_DISTANCE_INTERIOR);
    u_DistanceAtlas[tileOrigin + localID.xy] = gs_DistTile[srcCoord.y * 16u + srcCoord.x];
}
