// =============================================================================
// AxleEngine - 3D Froxel Volumetric Fog & Atmospheric Participating Media
// Dual-compiled for Direct3D 12 (DXIL 6.0) & Vulkan 1.3 (SPIR-V)
// Static 3D Froxel Media Injection -> Front-to-Back Beer-Lambert Integration -> Apply
// =============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const float LOCAL_INTENSITY_SCALE = 0.05;

cbuffer VolumetricFogConstants : register(b0, space0) {
    float4x4 u_InvView;
    float4x4 u_InvProjection;
    float4   u_ScreenSize;              // (viewW, viewH, 1/viewW, 1/viewH)
    float4   u_SunDirectionIntensity;   // (xyz: unit dir toward sun, w: sun present 0 or 1)
    float4   u_SunRadiance;             // (rgb: scene-linear sun radiance, w: 0)
    float4   u_FogParams;               // (x: effective extinction [1/m], y: height falloff [1/m], z: g)
    float4   u_FogAlbedo;               // (rgb: single-scattering albedo)
    float4   u_FroxelGridParams;        // (x: gridW, y: gridH, z: gridD, w: orthographic flag)
    float4   u_AmbientRadiance;         // (rgb: authored scene ambient radiance)
    float4   u_FroxelDepthParams;       // (x: log(farZ / nearZ), y: nearZ, z: farZ, w: reserved)
};

// Matches Forward+ RHIMultiLightBufferData (register b2, space0).
struct FogDirectionalSunLight {
    float4 DirectionAndIlluminance;
    float4 ColorAndAmbient;
    float4 AmbientColor;
    float4 ShadowParams;
};

struct FogPointLightData {
    float4 PositionAndRadius;
    float4 ColorAndIntensity;
    float4 Params;
    float4 Reserved;
};

struct FogSpotLightData {
    float4 PositionAndRange;
    float4 DirectionAndIntensity;
    float4 ColorAndInnerAngle;
    float4 AngleParams;
};

struct FogRectLightData {
    float4 PositionAndWidth;
    float4 DirectionAndHeight;
    float4 UpAndRange;
    float4 ColorAndIntensity;
};

cbuffer MultiLightBuffer : register(b2, space0) {
    FogDirectionalSunLight u_SunLight;
    uint4                  u_LightCounts;        // x: NumPointLights, y: NumSpotLights, z: HasSunLight, w: LightingMode
    float4                 u_CameraPosExposure;
    float4                 u_LightReserved[2];   // [1].x: NumRectLights
    FogPointLightData      u_PointLights[16];
    FogSpotLightData       u_SpotLights[16];
    FogRectLightData       u_RectLights[2];
};

// Matches RHICascadeConstants (register b4, space0).
cbuffer CascadeReceiver : register(b4, space0) {
    float4x4 u_CascadeMatrices[4];
    float4   u_CascadeSplits;
    float4   u_CascadeAtlas; // (cascadeCount, 1/atlasSize, perTileResolution, nearClip)
    float4   u_CascadeBias;  // (base, slope, worldNormal, PCF tap spacing)
};

// Matches RHILocalShadowConstants (register b5, space0).
cbuffer LocalShadowReceiver : register(b5, space0) {
    float4x4 u_LocalShadowMatrices[16];
    float4   u_LocalShadowTiles[16];
    float4   u_LocalShadowBias[16];
    int4     u_LocalShadowViews[8];
    float4   u_LocalShadowAtlas; // (1/atlasSize, activeViewCount, 0, 0)
};

Texture2D<float4>   u_SceneColor       : register(t0, space1);
Texture2D<float>    u_SceneDepth       : register(t1, space1);
Texture3D<float4>   u_FroxelVolume     : register(t2, space1);
Texture3D<float4>   u_FroxelMedia      : register(t3, space1);
Texture2D<float>    u_CascadeDepth     : register(t4, space1);
Texture2D<float>    u_LocalShadowDepth : register(t5, space1);
SamplerState           u_LinearSampler : register(s0, space1);
SamplerComparisonState u_ShadowSampler : register(s1, space1);

#if defined(__spirv__)
[[vk::image_format("rgba16f")]]
#endif
RWTexture3D<float4> u_FroxelOutput  : register(u0, space2);

float3 ReconstructViewPos(float2 uv, float depth) {
    float4 clip = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(u_InvProjection, clip);
    return viewPos.xyz / max(abs(viewPos.w), 1e-6);
}

void ReconstructWorldRay(float2 uv, out float3 originWS, out float3 directionPerUnitZ) {
    float3 viewPos = ReconstructViewPos(uv, 0.5);
    bool orthographic = u_FroxelGridParams.w > 0.5;
    float3 originView = orthographic ? float3(viewPos.xy, 0.0) : float3(0.0, 0.0, 0.0);
    float3 directionView = orthographic ? float3(0.0, 0.0, -1.0) : viewPos / max(-viewPos.z, 1e-6);
    originWS = mul(u_InvView, float4(originView, 1.0)).xyz;
    directionPerUnitZ = mul((float3x3)u_InvView, directionView);
}

// Henyey-Greenstein Mie phase function: normalized on S^2.
// cosTheta = dot(viewDirFromCamToSample, dirTowardSun).
float HenyeyGreenstein(float cosTheta, float g) {
    float cg = clamp(g, -0.95, 0.95);
    float c = clamp(cosTheta, -1.0, 1.0);
    float g2 = cg * cg;
    float denom = max(1.0 + g2 - 2.0 * cg * c, 1e-6);
    return (1.0 - g2) / (4.0 * float(M_PI) * denom * sqrt(denom));
}

float SliceDepth(float slicePosition, float sliceCount) {
    return u_FroxelDepthParams.y * exp(u_FroxelDepthParams.x * (slicePosition / sliceCount));
}

float BeerLambertSeries(float opticalDepth) {
    return 1.0 - 0.5 * opticalDepth + (1.0 / 6.0) * opticalDepth * opticalDepth;
}

float AbsorbedFraction(float opticalDepth) {
    return opticalDepth > 1e-3
        ? 1.0 - exp(-opticalDepth)
        : opticalDepth * BeerLambertSeries(opticalDepth);
}

float4 IntegrateSegment(float4 media, float distanceMeters) {
    float extinction = max(media.a, 0.0);
    float opticalDepth = extinction * distanceMeters;
    float transmittance = exp(-opticalDepth);
    // Taylor expansion of (1 - exp(-t)) / t avoids cancellation at small t.
    float weight = opticalDepth > 1e-3
        ? (1.0 - transmittance) / extinction
        : distanceMeters * BeerLambertSeries(opticalDepth);
    return float4(max(media.rgb, 0.0) * weight, transmittance);
}

// =============================================================================
// Shadow Atlas Sampling Helpers (CSM + Local Point/Spot Shadows)
// =============================================================================
float SampleAtlasTileVolumetricPCF(Texture2D<float> depthAtlas,
                                   float2 uv,
                                   float2 tileMin,
                                   float2 tileMax,
                                   float halfTexel,
                                   float filterRadius,
                                   float referenceDepth) {
    float2 offsets[4] = {
        float2(-0.5, -0.5), float2(0.5, -0.5),
        float2(-0.5,  0.5), float2(0.5,  0.5)
    };
    float visibility = 0.0;
    [unroll]
    for (int t = 0; t < 4; ++t) {
        float2 tap = clamp(uv + offsets[t] * filterRadius, tileMin + halfTexel, tileMax - halfTexel);
        visibility += depthAtlas.SampleCmpLevelZero(u_ShadowSampler, tap, referenceDepth);
    }
    return visibility * 0.25;
}

float SampleCascadeTileVolumetric(uint cascade, float3 worldPos) {
    float4 clipPosition = mul(u_CascadeMatrices[cascade], float4(worldPos, 1.0));
    if (abs(clipPosition.w) <= 1e-6) return 1.0;
    float3 clip = clipPosition.xyz / clipPosition.w;
    if (any(abs(clip.xy) > 1.0) || clip.z < 0.0 || clip.z > 1.0) return 1.0;
    float2 local = clip.xy * float2(0.5, -0.5) + 0.5;
    float2 tileMin = float2(cascade % 2u, cascade / 2u) * 0.5;
    float2 uv = tileMin + local * 0.5;
    float texel = u_CascadeAtlas.y;
    float reference = saturate(clip.z - u_CascadeBias.x);
    return SampleAtlasTileVolumetricPCF(
        u_CascadeDepth, uv, tileMin, tileMin + 0.5, texel * 0.5, texel * max(u_CascadeBias.w, 0.0), reference);
}

float EvaluateCascadeVolumetricShadow(float3 worldPos, float viewDepth) {
    uint count = (uint)u_CascadeAtlas.x;
    if (count == 0u) return 1.0;
    if (viewDepth < u_CascadeAtlas.w || viewDepth > u_CascadeSplits[count - 1u]) return 1.0;
    uint cascade = 0u;
    [unroll]
    for (uint c = 0u; c < 3u; ++c) {
        if (c + 1u < count && viewDepth > u_CascadeSplits[c]) cascade = c + 1u;
    }
    float value = SampleCascadeTileVolumetric(cascade, worldPos);
    float start = (cascade == 0u) ? u_CascadeAtlas.w : u_CascadeSplits[cascade - 1u];
    float end = u_CascadeSplits[cascade];
    float blend = saturate((viewDepth - lerp(start, end, 0.9)) / max((end - start) * 0.1, 1e-6));
    if (blend > 0.0) {
        float nextVal = (cascade + 1u < count) ? SampleCascadeTileVolumetric(cascade + 1u, worldPos) : 1.0;
        value = lerp(value, nextVal, blend);
    }
    return value;
}

float LocalShadowVisibilityVolumetric(int view, float3 worldPos, float3 dirTowardLight) {
    if (view < 0 || view >= (int)u_LocalShadowAtlas.y) return 1.0;
    float4 bias = u_LocalShadowBias[view];
    float3 samplePos = worldPos + dirTowardLight * bias.x;
    float4 clip = mul(u_LocalShadowMatrices[view], float4(samplePos, 1.0));
    if (clip.w <= 1e-6) return 1.0;
    float3 projected = clip.xyz / clip.w;
    if (any(abs(projected.xy) > 1.0) || projected.z < 0.0 || projected.z > 1.0) return 1.0;
    float4 tile = u_LocalShadowTiles[view];
    float2 uv = tile.xy + (projected.xy * float2(0.5, -0.5) + 0.5) * tile.z;
    float texel = u_LocalShadowAtlas.x;
    return SampleAtlasTileVolumetricPCF(
        u_LocalShadowDepth, uv, tile.xy, tile.xy + tile.z, texel * 0.5, texel * max(bias.w, 0.0), projected.z);
}

float PointShadowVisibilityVolumetric(uint index, float3 lightPos, float3 worldPos, float3 dirTowardLight) {
    int first = u_LocalShadowViews[index / 4u][index % 4u];
    if (first < 0 || first >= (int)u_LocalShadowAtlas.y) return 1.0;
    float3 direction = (worldPos + dirTowardLight * u_LocalShadowBias[first].x) - lightPos;
    float3 axis = abs(direction);
    int face;
    if (axis.x >= axis.y && axis.x >= axis.z) face = (direction.x >= 0.0) ? 0 : 1;
    else if (axis.y >= axis.z)                face = (direction.y >= 0.0) ? 2 : 3;
    else                                      face = (direction.z >= 0.0) ? 4 : 5;
    return LocalShadowVisibilityVolumetric(first + face, worldPos, dirTowardLight);
}

void AccumulatePointLights(float3 sampleWS, float3 unitRayWS, float g, inout float3 incomingRadiance) {
    uint numPointLights = min(u_LightCounts.x, 16u);
    [loop]
    for (uint p = 0u; p < numPointLights; ++p) {
        FogPointLightData pLight = u_PointLights[p];
        if (pLight.Params.z > 0.5) {
            float lLenSq = dot(pLight.PositionAndRadius.xyz, pLight.PositionAndRadius.xyz);
            if (lLenSq <= 1e-12) continue;
            float3 L = pLight.PositionAndRadius.xyz * rsqrt(lLenSq);
            float phase = HenyeyGreenstein(dot(unitRayWS, L), g);
            incomingRadiance += max(pLight.ColorAndIntensity.rgb, float3(0.0, 0.0, 0.0)) *
                (max(pLight.ColorAndIntensity.w, 0.0) * phase);
        } else {
            float radius = pLight.PositionAndRadius.w;
            if (radius <= 0.0) continue;
            float3 lightVec = pLight.PositionAndRadius.xyz - sampleWS;
            float dist = length(lightVec);
            if (dist >= radius || dist <= 1e-5) continue;
            float3 L = lightVec / dist;

            float minRadius = max(0.0025, pLight.Params.y);
            float distSq = max(dist * dist, minRadius);
            float ratio = dist / radius;
            float ratio4 = ratio * ratio * ratio * ratio;
            float window = saturate(1.0 - ratio4);
            float attenuation = (window * window) / distSq;
            float falloffExp = pLight.Params.x;
            if (falloffExp > 0.1 && abs(falloffExp - 1.0) > 0.01) {
                attenuation = pow(max(attenuation, 0.0), falloffExp);
            }
            float vis = PointShadowVisibilityVolumetric(p, pLight.PositionAndRadius.xyz, sampleWS, L);
            if (vis <= 0.0) continue;
            float phase = HenyeyGreenstein(dot(unitRayWS, L), g);
            incomingRadiance += max(pLight.ColorAndIntensity.rgb, float3(0.0, 0.0, 0.0)) *
                (max(pLight.ColorAndIntensity.w, 0.0) * attenuation * LOCAL_INTENSITY_SCALE * phase * vis);
        }
    }
}

void AccumulateSpotLights(float3 sampleWS, float3 unitRayWS, float g, inout float3 incomingRadiance) {
    uint numSpotLights = min(u_LightCounts.y, 16u);
    [loop]
    for (uint s = 0u; s < numSpotLights; ++s) {
        FogSpotLightData sLight = u_SpotLights[s];
        float range = sLight.PositionAndRange.w;
        if (range <= 0.0) continue;
        float3 lightVec = sLight.PositionAndRange.xyz - sampleWS;
        float dist = length(lightVec);
        if (dist >= range || dist <= 1e-5) continue;
        float3 L = lightVec / dist;

        float dirLenSq = dot(sLight.DirectionAndIntensity.xyz, sLight.DirectionAndIntensity.xyz);
        if (dirLenSq <= 1e-10) continue;
        float3 spotDir = sLight.DirectionAndIntensity.xyz * rsqrt(dirLenSq);
        float cosTheta = dot(-L, spotDir);
        float cosInner = sLight.ColorAndInnerAngle.w;
        float cosOuter = sLight.AngleParams.x;
        if (cosTheta <= cosOuter) continue;

        float spotAtten = saturate((cosTheta - cosOuter) / max(1e-5, cosInner - cosOuter));
        spotAtten = spotAtten * spotAtten;
        float distSq = max(dist * dist, 0.0025);
        float ratio = dist / range;
        float ratio4 = ratio * ratio * ratio * ratio;
        float window = saturate(1.0 - ratio4);
        float distAtten = (window * window) / distSq;

        int shadowView = u_LocalShadowViews[4u + (s / 4u)][s % 4u];
        float vis = LocalShadowVisibilityVolumetric(shadowView, sampleWS, L);
        if (vis <= 0.0) continue;

        float phase = HenyeyGreenstein(dot(unitRayWS, L), g);
        incomingRadiance += max(sLight.ColorAndInnerAngle.rgb, float3(0.0, 0.0, 0.0)) *
            (max(sLight.DirectionAndIntensity.w, 0.0) * distAtten * spotAtten * LOCAL_INTENSITY_SCALE * phase * vis);
    }
}

void AccumulateRectLights(float3 sampleWS, float3 unitRayWS, float g, inout float3 incomingRadiance) {
    uint numRectLights = min((uint)u_LightReserved[1].x, 2u);
    [loop]
    for (uint r = 0u; r < numRectLights; ++r) {
        FogRectLightData rLight = u_RectLights[r];
        float range = rLight.UpAndRange.w;
        float rawIntensity = rLight.ColorAndIntensity.w;
        float intensity = abs(rawIntensity);
        if (range <= 1e-3 || intensity <= 0.0) continue;
        float3 lightVec = rLight.PositionAndWidth.xyz - sampleWS;
        float dist = length(lightVec);
        if (dist >= range || dist <= 1e-5) continue;
        float3 L = lightVec / dist;

        float dirLenSq = dot(rLight.DirectionAndHeight.xyz, rLight.DirectionAndHeight.xyz);
        float3 lightDir = (dirLenSq > 1e-12) ? (rLight.DirectionAndHeight.xyz * rsqrt(dirLenSq)) : float3(0.0, 0.0, -1.0);
        bool twoSided = (rawIntensity < 0.0);
        float signedForward = dot(-L, lightDir);
        if (!twoSided && signedForward <= 0.0) continue;
        float emitCos = twoSided ? abs(signedForward) : max(signedForward, 0.0);

        bool isDisk = (rLight.DirectionAndHeight.w < 0.0);
        float w = max(rLight.PositionAndWidth.w, 0.01);
        float h = max(abs(rLight.DirectionAndHeight.w), 0.01);
        float area = w * h * (isDisk ? (float(M_PI) * 0.25) : 1.0);
        float eqRadiusSq = area / float(M_PI);
        float solidAngleFactor = 2.0 * (1.0 - dist * rsqrt(dist * dist + eqRadiusSq)) * emitCos;

        float ratio = saturate(dist / range);
        float ratio4 = ratio * ratio * ratio * ratio;
        float win = saturate(1.0 - ratio4);
        float rangeAtten = win * win;
        float phase = HenyeyGreenstein(dot(unitRayWS, L), g);
        incomingRadiance += max(rLight.ColorAndIntensity.rgb, float3(0.0, 0.0, 0.0)) *
            (intensity * rangeAtten * LOCAL_INTENSITY_SCALE * solidAngleFactor * phase);
    }
}

void EvaluateVolumetricScatteringAtSample(float3 sampleWS,
                                          float viewDepth,
                                          float3 unitRayWS,
                                          float effectiveBaseDensity,
                                          float heightFalloff,
                                          float g,
                                          float3 fogAlbedo,
                                          out float outLocalDensity,
                                          out float3 outInScatterPerMeter) {
    float heightAboveGround = max(sampleWS.y, 0.0);
    outLocalDensity = effectiveBaseDensity * exp(-heightAboveGround * heightFalloff);
    if (outLocalDensity <= 0.0) {
        outInScatterPerMeter = float3(0.0, 0.0, 0.0);
        return;
    }

    float3 incomingRadiance = max(u_AmbientRadiance.rgb, float3(0.0, 0.0, 0.0));
    if (u_SunDirectionIntensity.w > 0.5) {
        float sunVis = EvaluateCascadeVolumetricShadow(sampleWS, viewDepth);
        if (sunVis > 0.0) {
            float cosTheta = dot(unitRayWS, normalize(u_SunDirectionIntensity.xyz));
            float phase = HenyeyGreenstein(cosTheta, g);
            incomingRadiance += max(u_SunRadiance.rgb, float3(0.0, 0.0, 0.0)) * (phase * sunVis);
        }
    }

    AccumulatePointLights(sampleWS, unitRayWS, g, incomingRadiance);
    AccumulateSpotLights(sampleWS, unitRayWS, g, incomingRadiance);
    AccumulateRectLights(sampleWS, unitRayWS, g, incomingRadiance);

    outInScatterPerMeter = (fogAlbedo * outLocalDensity) * incomingRadiance;
}

// =============================================================================
// Pass 1: Inject Media & Scattering per Froxel (CSInjectMedia -> FroxelScattering)
// Stores float4(inScatterPerMeter.rgb, extinctionSigmaT)
// =============================================================================
[numthreads(4, 4, 4)]
void CSInjectMedia(uint3 dtid : SV_DispatchThreadID) {
    uint3 gridDims = uint3(
        max((uint)round(u_FroxelGridParams.x), 1u),
        max((uint)round(u_FroxelGridParams.y), 1u),
        max((uint)round(u_FroxelGridParams.z), 1u));
    if (dtid.x >= gridDims.x || dtid.y >= gridDims.y || dtid.z >= gridDims.z) return;

    float effectiveBaseDensity = u_FogParams.x;
    if (effectiveBaseDensity <= 0.0) {
        u_FroxelOutput[dtid] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float heightFalloff = u_FogParams.y;
    float g = u_FogParams.z;
    float3 fogAlbedo = u_FogAlbedo.rgb;

    float2 uv = (float2(dtid.xy) + 0.5) / float2(gridDims.xy);
    float3 originWS, rayDirWorldPerUnitZ;
    ReconstructWorldRay(uv, originWS, rayDirWorldPerUnitZ);
    float3 unitRayWS = normalize(rayDirWorldPerUnitZ);

    bool hasShadowMaps = ((u_SunDirectionIntensity.w > 0.5) && ((uint)u_CascadeAtlas.x > 0u)) ||
                         (((uint)u_LocalShadowAtlas.y > 0u) && ((u_LightCounts.x > 0u) || (u_LightCounts.y > 0u)));

    float3 accumScatter = float3(0.0, 0.0, 0.0);
    float accumDensity = 0.0;
    if (hasShadowMaps) {
        float z0 = SliceDepth(float(dtid.z), float(gridDims.z));
        float z1 = SliceDepth(float(dtid.z + 1u), float(gridDims.z));
        [unroll]
        for (uint k = 0u; k < 4u; ++k) {
            float zSample = lerp(z0, z1, (float(k) + 0.5) * 0.25);
            float3 sampleWS = originWS + rayDirWorldPerUnitZ * zSample;
            float sampleDensity = 0.0;
            float3 sampleScatter = float3(0.0, 0.0, 0.0);
            EvaluateVolumetricScatteringAtSample(
                sampleWS, zSample, unitRayWS, effectiveBaseDensity, heightFalloff, g, fogAlbedo,
                sampleDensity, sampleScatter);
            accumDensity += sampleDensity;
            accumScatter += sampleScatter;
        }
        accumDensity *= 0.25;
        accumScatter *= 0.25;
    } else {
        float zCenter = SliceDepth(float(dtid.z) + 0.5, float(gridDims.z));
        float3 sampleWS = originWS + rayDirWorldPerUnitZ * zCenter;
        EvaluateVolumetricScatteringAtSample(
            sampleWS, zCenter, unitRayWS, effectiveBaseDensity, heightFalloff, g, fogAlbedo,
            accumDensity, accumScatter);
    }

    u_FroxelOutput[dtid] = float4(min(accumScatter, 65504.0), accumDensity);
}

// =============================================================================
// Pass 2: Front-to-Back Beer-Lambert Slice Integration (CSIntegrate -> FroxelIntegrated)
// FroxelIntegrated[..., iz] stores cumulative [nearZ, z_{iz+1}] (inScatter.rgb, transmittance)
// =============================================================================
[numthreads(8, 8, 1)]
void CSIntegrate(uint3 dtid : SV_DispatchThreadID) {
    uint3 gridDims = uint3(
        max((uint)round(u_FroxelGridParams.x), 1u),
        max((uint)round(u_FroxelGridParams.y), 1u),
        max((uint)round(u_FroxelGridParams.z), 1u));
    if (dtid.x >= gridDims.x || dtid.y >= gridDims.y) return;

    float2 uv = (float2(dtid.xy) + 0.5) / float2(gridDims.xy);
    float3 originWS, rayDirWorldPerUnitZ;
    ReconstructWorldRay(uv, originWS, rayDirWorldPerUnitZ);
    float rayLen = length(rayDirWorldPerUnitZ);

    float3 accumInScatter = float3(0.0, 0.0, 0.0);
    float accumTransmittance = 1.0;

    [loop]
    for (uint iz = 0u; iz < gridDims.z; ++iz) {
        float4 media = u_FroxelVolume.Load(int4(int(dtid.x), int(dtid.y), int(iz), 0));
        float z0 = SliceDepth(float(iz), float(gridDims.z));
        float z1 = SliceDepth(float(iz + 1u), float(gridDims.z));
        float ds = max(z1 - z0, 0.0) * rayLen;

        float4 step = IntegrateSegment(media, ds);
        accumInScatter += accumTransmittance * step.rgb;
        accumTransmittance *= step.a;

        u_FroxelOutput[uint3(dtid.x, dtid.y, iz)] = float4(
            min(accumInScatter, 65504.0),
            clamp(accumTransmittance, 0.0, 1.0));
    }
}

// =============================================================================
// Pass 3: Fullscreen Composite Pass (VSMain & PSMain)
// =============================================================================
struct VSOutput {
    float4 Position : SV_Position;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target {
    int2 maxCoord = max(int2(u_ScreenSize.xy) - int2(1, 1), int2(0, 0));
    int2 pixelCoord = clamp(int2(input.Position.xy), int2(0, 0), maxCoord);
    float4 sceneColor = u_SceneColor.Load(int3(pixelCoord, 0));

    if (u_FogParams.x <= 0.0) {
        return sceneColor;
    }

    float2 uv = (float2(pixelCoord) + 0.5) * u_ScreenSize.zw;
    float depth = u_SceneDepth.Load(int3(pixelCoord, 0));
    float nearZ = u_FroxelDepthParams.y;
    float farZ = u_FroxelDepthParams.z;
    float logRatio = u_FroxelDepthParams.x;
    float gridD = u_FroxelGridParams.z;

    float pixelViewZ = depth >= 1.0 ? farZ : -ReconstructViewPos(uv, depth).z;
    if (pixelViewZ <= nearZ) {
        return sceneColor;
    }

    float4 fogSample;
    if (pixelViewZ >= farZ) {
        fogSample = u_FroxelVolume.SampleLevel(u_LinearSampler, float3(uv, 1.0 - 0.5 / gridD), 0.0);
    } else {
        float slice = min(floor(saturate(log(pixelViewZ / nearZ) / logRatio) * gridD), gridD - 1.0);
        fogSample = float4(0.0, 0.0, 0.0, 1.0);
        if (slice > 0.0) {
            fogSample = u_FroxelVolume.SampleLevel(u_LinearSampler, float3(uv, (slice - 0.5) / gridD), 0.0);
        }
        // Keep both XY-filtered endpoints continuous across slice boundaries.
        // The Beer-Lambert weight evaluates the partial distance inside the slice.
        float3 sampleUV = float3(uv, (slice + 0.5) / gridD);
        float extinction = max(u_FroxelMedia.SampleLevel(u_LinearSampler, sampleUV, 0.0).a, 0.0);
        float4 sliceEnd = u_FroxelVolume.SampleLevel(u_LinearSampler, sampleUV, 0.0);
        float3 originWS, directionPerUnitZ;
        ReconstructWorldRay(uv, originWS, directionPerUnitZ);
        float z0 = SliceDepth(slice, gridD);
        float span = SliceDepth(slice + 1.0, gridD) - z0;
        float fraction = span > 0.0 ? saturate((pixelViewZ - z0) / span) : 0.0;
        float tau = extinction * span * length(directionPerUnitZ);
        float weight = tau > 0.0 ? AbsorbedFraction(tau * fraction) / AbsorbedFraction(tau) : fraction;
        fogSample = lerp(fogSample, sliceEnd, saturate(weight));
    }

    float3 inScattering = max(fogSample.rgb, float3(0.0, 0.0, 0.0));
    float transmittance = saturate(fogSample.a);
    float3 finalColor = min(sceneColor.rgb * transmittance + inScattering, 65504.0);
    return float4(finalColor, sceneColor.a);
}
