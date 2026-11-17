#ifndef AXLE_REFLECTION_PROBE_COMMON_HLSLI
#define AXLE_REFLECTION_PROBE_COMMON_HLSLI

// Shared multi-probe influence, per-probe parallax correction, and
// energy-conserving reflection blend contract (M15-U5-E).
// Expects AXLE_REFLECTION_PROBE_CBUFFER_REGISTER and
// u_ReflectionProbeCube0..3 (TextureCube) to be declared before inclusion.

#ifndef AXLE_REFLECTION_PROBE_CBUFFER_REGISTER
#define AXLE_REFLECTION_PROBE_CBUFFER_REGISTER b6
#endif

struct ReflectionProbeGPUData {
    float4 PositionAndShape;      // xyz: center WS, w: 0 = Box, 1 = Sphere
    float4 HalfExtentsAndRadius;  // xyz: clamped half extents, w: clamped sphere radius
    float4 Params;                // x: intensity, y: parallaxCorrected (0/1), z: blendDistance (>0), w: stale (0/1)
    float4 AxisX;                 // xyz: orthonormal local +X axis WS
    float4 AxisY;                 // xyz: orthonormal local +Y axis WS
    float4 AxisZ;                 // xyz: orthonormal local +Z axis WS
    float4 PrefilterInfo;         // x: maxLod (>= 0), y: ready (0/1), zw: reserved
};

cbuffer ReflectionProbeBuffer : register(AXLE_REFLECTION_PROBE_CBUFFER_REGISTER, space0) {
    uint4 u_ReflectionHeader;                 // x: activeProbeCount (0..4), y: hasSkyIBL, z: diffuseOnly, w: ssrActive
    float4 u_ReflectionParams;                // x: ssrStrength, y: envIntensity, z: skyRotationRad, w: reserved
    float4 u_ReflectionSkyAmbient;            // rgb: analytical sky fallback color, w: reserved
    float4 u_ReflectionReserved;
    ReflectionProbeGPUData u_ReflectionProbes[4];
};

struct ProbeInfluenceResult {
    float Coverage;
    float BlendWeight;
};

struct MultiProbeBlendResult {
    float3 ProbeColor;
    float3 BlendedEnvColor;
    float ProbeCoverage;
    uint ContributingCount;
};

// The opaque forward draw publishes the exact indirect specular it added, plus
// its material response to incident radiance. SSR replaces only this term.
struct ReflectionSurfaceOutput {
    float4 Color : SV_Target0;
    float4 Environment : SV_Target1;
    float4 Response : SV_Target2;
};
ReflectionSurfaceOutput MakeReflectionSurfaceOutput(float4 color, float3 environment, float3 response) {
    ReflectionSurfaceOutput result;
    result.Color = color;
    result.Environment = float4(clamp(environment, 0.0, 65000.0), 0.04);
    result.Response = float4(clamp(response, 0.0, 65000.0), 1.0);
    return result;
}

ReflectionSurfaceOutput MakeMaterialReflectionSurfaceOutput(
    float4 color,
    float3 environment,
    float3 response,
    float roughness,
    float reflectionEnabled) {
    ReflectionSurfaceOutput result;
    result.Color = color;
    float packedRoughness = clamp(roughness, 0.04, 1.0);
    result.Environment = float4(
        clamp(environment, 0.0, 65000.0),
        reflectionEnabled > 0.001 ? packedRoughness : -packedRoughness);
    result.Response = float4(clamp(response, 0.0, 65000.0), 1.0);
    return result;
}

float3 SampleReflectionProbeSlot(uint slot, SamplerState envSampler, float3 dir, float lod) {
    switch (min(slot, 3u)) {
        case 0u: return u_ReflectionProbeCube0.SampleLevel(envSampler, dir, lod).rgb;
        case 1u: return u_ReflectionProbeCube1.SampleLevel(envSampler, dir, lod).rgb;
        case 2u: return u_ReflectionProbeCube2.SampleLevel(envSampler, dir, lod).rgb;
        default: return u_ReflectionProbeCube3.SampleLevel(envSampler, dir, lod).rgb;
    }
}

ProbeInfluenceResult EvaluateProbeInfluenceHLSL(ReflectionProbeGPUData probe, float3 worldPos) {
    ProbeInfluenceResult res;
    res.Coverage = 0.0;
    res.BlendWeight = 0.0;

    float3 delta = worldPos - probe.PositionAndShape.xyz;
    float3 local = float3(
        dot(delta, probe.AxisX.xyz),
        dot(delta, probe.AxisY.xyz),
        dot(delta, probe.AxisZ.xyz));

    float blendDist = max(probe.Params.z, 0.001);
    float innerRatio = 0.0;
    float distanceOutside = 0.0;

    if (probe.PositionAndShape.w > 0.5) {
        float radius = max(probe.HalfExtentsAndRadius.w, 0.001);
        float dist = length(local);
        innerRatio = clamp(dist / radius, 0.0, 1.0);
        distanceOutside = max(dist - radius, 0.0);
    } else {
        float3 halfExtents = max(probe.HalfExtentsAndRadius.xyz, float3(0.001, 0.001, 0.001));
        float3 absLocal = abs(local);
        float3 normAxes = absLocal / halfExtents;
        innerRatio = clamp(max(normAxes.x, max(normAxes.y, normAxes.z)), 0.0, 1.0);
        float3 outsideVec = max(absLocal - halfExtents, float3(0.0, 0.0, 0.0));
        distanceOutside = length(outsideVec);
    }

    float outerRatio = clamp(distanceOutside / blendDist, 0.0, 1.0);
    float coverage = 1.0 - smoothstep(0.0, 1.0, outerRatio);
    float interior = 1.0 - smoothstep(0.0, 1.0, innerRatio);

    res.Coverage = coverage;
    res.BlendWeight = coverage * (0.05 + 0.95 * interior);
    return res;
}

float3 ApplyProbeParallaxHLSL(ReflectionProbeGPUData probe, float3 worldPos, float3 reflDir) {
    float dirLenSq = dot(reflDir, reflDir);
    float3 unitDir = dirLenSq > 1e-12 ? (reflDir * rsqrt(dirLenSq)) : float3(0.0, 1.0, 0.0);
    if (probe.Params.y < 0.5) {
        return unitDir;
    }

    float3 delta = worldPos - probe.PositionAndShape.xyz;
    float3 localPos = float3(
        dot(delta, probe.AxisX.xyz),
        dot(delta, probe.AxisY.xyz),
        dot(delta, probe.AxisZ.xyz));
    float3 localDirRaw = float3(
        dot(unitDir, probe.AxisX.xyz),
        dot(unitDir, probe.AxisY.xyz),
        dot(unitDir, probe.AxisZ.xyz));
    float localDirLenSq = dot(localDirRaw, localDirRaw);
    float3 localDir = localDirLenSq > 1e-12 ? (localDirRaw * rsqrt(localDirLenSq)) : float3(0.0, 1.0, 0.0);

    float3 correctedLocal = localDir;
    if (probe.PositionAndShape.w > 0.5) {
        float radius = max(probe.HalfExtentsAndRadius.w, 0.001);
        float b = dot(localPos, localDir);
        float c = dot(localPos, localPos) - radius * radius;
        float disc = b * b - c;
        if (disc >= 0.0) {
            float tExit = -b + sqrt(disc);
            if (tExit > 1e-4) {
                float3 hitLocal = localPos + localDir * tExit;
                float hitLenSq = dot(hitLocal, hitLocal);
                if (hitLenSq > 1e-10) {
                    correctedLocal = hitLocal * rsqrt(hitLenSq);
                }
            }
        }
    } else {
        float3 halfExtents = max(probe.HalfExtentsAndRadius.xyz, float3(0.001, 0.001, 0.001));
        float tEnter = -3.402823466e+38;
        float tExit = 3.402823466e+38;
        [unroll] for (uint axis = 0; axis < 3; ++axis) {
            if (abs(localDir[axis]) < 1e-6) {
                if (abs(localPos[axis]) > halfExtents[axis]) return unitDir;
            } else {
                float t0 = (-halfExtents[axis] - localPos[axis]) / localDir[axis];
                float t1 = (halfExtents[axis] - localPos[axis]) / localDir[axis];
                tEnter = max(tEnter, min(t0, t1));
                tExit = min(tExit, max(t0, t1));
            }
        }
        if (tExit > 1e-4 && tExit >= tEnter) {
            float3 hitLocal = localPos + localDir * tExit;
            float hitLenSq = dot(hitLocal, hitLocal);
            if (hitLenSq > 1e-10) {
                correctedLocal = hitLocal * rsqrt(hitLenSq);
            }
        }
    }

    float3 correctedWorld =
        probe.AxisX.xyz * correctedLocal.x +
        probe.AxisY.xyz * correctedLocal.y +
        probe.AxisZ.xyz * correctedLocal.z;
    float outLenSq = dot(correctedWorld, correctedWorld);
    return outLenSq > 1e-12 ? (correctedWorld * rsqrt(outLenSq)) : unitDir;
}

MultiProbeBlendResult EvaluateMultiProbeBlendHLSL(
    float3 worldPos,
    float3 reflDir,
    float roughness,
    SamplerState envSampler,
    float3 skyFallbackColor) {
    MultiProbeBlendResult result;
    result.ProbeColor = float3(0.0, 0.0, 0.0);
    result.BlendedEnvColor = max(skyFallbackColor, float3(0.0, 0.0, 0.0));
    result.ProbeCoverage = 0.0;
    result.ContributingCount = 0u;

    uint count = min(u_ReflectionHeader.x, 4u);
    if (count == 0u) {
        return result;
    }

    float weightSum = 0.0;
    float skyComplement = 1.0;
    float3 weightedColor = float3(0.0, 0.0, 0.0);
    float clampedRoughness = clamp(roughness, 0.0, 1.0);

    [unroll]
    for (uint i = 0u; i < 4u; ++i) {
        if (i >= count) break;
        ReflectionProbeGPUData probe = u_ReflectionProbes[i];
        if (probe.PrefilterInfo.y < 0.5) continue;

        ProbeInfluenceResult inf = EvaluateProbeInfluenceHLSL(probe, worldPos);
        if (inf.Coverage <= 0.0 || inf.BlendWeight <= 0.0) continue;

        float3 correctedDir = ApplyProbeParallaxHLSL(probe, worldPos, reflDir);
        float lod = clampedRoughness * max(probe.PrefilterInfo.x, 0.0);
        float3 sampleColor = max(SampleReflectionProbeSlot(i, envSampler, correctedDir, lod), float3(0.0, 0.0, 0.0));
        float intensity = max(probe.Params.x, 0.0);

        weightedColor += (sampleColor * intensity) * inf.BlendWeight;
        weightSum += inf.BlendWeight;
        skyComplement *= (1.0 - saturate(inf.Coverage));
        result.ContributingCount += 1u;
    }

    if (weightSum > 1e-6 && result.ContributingCount > 0u) {
        result.ProbeColor = weightedColor / weightSum;
        result.ProbeCoverage = saturate(1.0 - skyComplement);
        result.BlendedEnvColor = lerp(max(skyFallbackColor, float3(0.0, 0.0, 0.0)), result.ProbeColor, result.ProbeCoverage);
    }
    return result;
}

#endif
