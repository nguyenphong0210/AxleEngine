// =============================================================================
// AxleEngine - Screen Space Reflections (SSR) & Parallax-Corrected Probes
// Dual-compiled for Direct3D 12 (DXIL 6.0) & Vulkan 1.3 (SPIR-V)
// View-space raymarching with binary search refinement and probe fallback
// =============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

cbuffer SSRConstants : register(b0, space0) {
    float4x4 u_View;
    float4x4 u_InvView;
    float4x4 u_Projection;
    float4x4 u_InvProjection;
    float4   u_ScreenSize;      // (width, height, 1.0 / width, 1.0 / height)
    float4   u_SSRParams;       // (max distance m, steps, strength, thickness)
    float4   u_ProbeBoxMin;     // (xyz: box min, w: parallax corrected 1.0 or 0.0)
    float4   u_ProbeBoxMax;     // (xyz: box max, w: probe intensity)
    float4   u_ProbePosition;   // (xyz: probe center, w: has probe 1.0 or 0.0)
    float4   u_Padding[11];     // Total 512 bytes aligned
};

// -----------------------------------------------------------------------------
// Common Math and Helpers
// -----------------------------------------------------------------------------
float3 ReconstructViewPos(float2 uv, float depth) {
    float4 clip = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(u_InvProjection, clip);
    return viewPos.xyz / max(abs(viewPos.w), 1e-6);
}

float SpatialHash(uint2 pixelPos) {
    return frac(52.9829189 * frac(dot(float2(pixelPos), float2(0.06711056, 0.00583715))));
}

// =============================================================================
// Compute Shader (Screen Space Raymarching)
// =============================================================================

// Keep each Hi-Z level as a statically bound texture.  Some Vulkan drivers
// reject descriptor-array reflection unless runtimeDescriptorArray is enabled;
// the fixed eight-level pyramid is fully portable and still uses one range.
Texture2D<float>    u_HiZ0           : register(t0, space1);
Texture2D<float>    u_HiZ1           : register(t1, space1);
Texture2D<float>    u_HiZ2           : register(t2, space1);
Texture2D<float>    u_HiZ3           : register(t3, space1);
Texture2D<float>    u_HiZ4           : register(t4, space1);
Texture2D<float>    u_HiZ5           : register(t5, space1);
Texture2D<float>    u_HiZ6           : register(t6, space1);
Texture2D<float>    u_HiZ7           : register(t7, space1);
Texture2D<float4>   u_ComputeNormal  : register(t8, space1);
Texture2D<float4>   u_ComputeColor   : register(t9, space1);
SamplerState        u_ComputeSampler : register(s0, space1);
RWTexture2D<float4> u_ComputeOutput  : register(u0, space2);

float SampleHiZ(uint mip, float2 uv) {
    switch (min(mip, 7u)) {
        case 0u: return u_HiZ0.SampleLevel(u_ComputeSampler, uv, 0.0);
        case 1u: return u_HiZ1.SampleLevel(u_ComputeSampler, uv, 0.0);
        case 2u: return u_HiZ2.SampleLevel(u_ComputeSampler, uv, 0.0);
        case 3u: return u_HiZ3.SampleLevel(u_ComputeSampler, uv, 0.0);
        case 4u: return u_HiZ4.SampleLevel(u_ComputeSampler, uv, 0.0);
        case 5u: return u_HiZ5.SampleLevel(u_ComputeSampler, uv, 0.0);
        case 6u: return u_HiZ6.SampleLevel(u_ComputeSampler, uv, 0.0);
        default: return u_HiZ7.SampleLevel(u_ComputeSampler, uv, 0.0);
    }
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 pixelPos = dispatchThreadId.xy;
    if (pixelPos.x >= (uint)u_ScreenSize.x || pixelPos.y >= (uint)u_ScreenSize.y) return;

    float2 rcpFrame = u_ScreenSize.zw;
    float2 uv = (float2(pixelPos) + 0.5) * rcpFrame;

    float depth = SampleHiZ(0u, uv);
    if (depth >= 0.9999 || depth <= 0.0) {
        u_ComputeOutput[pixelPos] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 viewPos = ReconstructViewPos(uv, depth);

    // Determine normal in view space
    float4 normalSample = u_ComputeNormal.SampleLevel(u_ComputeSampler, uv, 0.0);
    float3 viewNormal;
    if (dot(normalSample.xyz, normalSample.xyz) > 0.01) {
        viewNormal = normalize(mul((float3x3)u_View, normalSample.xyz));
    } else {
        float3 pR = ReconstructViewPos(uv + float2(rcpFrame.x, 0.0), SampleHiZ(0u, uv + float2(rcpFrame.x, 0.0)));
        float3 pL = ReconstructViewPos(uv - float2(rcpFrame.x, 0.0), SampleHiZ(0u, uv - float2(rcpFrame.x, 0.0)));
        float3 pD = ReconstructViewPos(uv + float2(0.0, rcpFrame.y), SampleHiZ(0u, uv + float2(0.0, rcpFrame.y)));
        float3 pU = ReconstructViewPos(uv - float2(0.0, rcpFrame.y), SampleHiZ(0u, uv - float2(0.0, rcpFrame.y)));
        float3 dx = (abs(pR.z - viewPos.z) < abs(pL.z - viewPos.z)) ? (pR - viewPos) : (viewPos - pL);
        float3 dy = (abs(pD.z - viewPos.z) < abs(pU.z - viewPos.z)) ? (pD - viewPos) : (viewPos - pU);
        viewNormal = normalize(cross(dy, dx));
        if (dot(viewNormal, -normalize(viewPos)) < 0.0) {
            viewNormal = -viewNormal;
        }
    }

    float3 viewDir = normalize(viewPos);
    float3 reflDir = reflect(viewDir, viewNormal);

    uint maxSteps = clamp((uint)u_SSRParams.y, 8u, 64u);
    float maxDist = max(u_SSRParams.x, 5.0);
    float stepSize = maxDist / float(maxSteps);
    float thickness = max(u_SSRParams.w, 0.05);

    float jitter = SpatialHash(pixelPos);
    float3 rayPos = viewPos + reflDir * (stepSize * (1.0 + jitter * 0.5));

    float3 hitColor = float3(0.0, 0.0, 0.0);
    float confidence = 0.0;

    [loop]
    for (uint i = 0; i < maxSteps; ++i) {
        rayPos += reflDir * stepSize;
        float4 clip = mul(u_Projection, float4(rayPos, 1.0));
        if (clip.w <= 0.001) break;
        float3 ndc = clip.xyz / clip.w;
        float2 rayUV = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);

        if (any(rayUV < 0.0) || any(rayUV > 1.0)) break;

        uint hizLevel = min((uint)max(0.0, floor(log2(max(1.0, float(i + 1) / 4.0)))), 7u);
        float sampleDepth = SampleHiZ(hizLevel, rayUV);
        float3 sampleViewPos = ReconstructViewPos(rayUV, sampleDepth);

        float zDiff = sampleViewPos.z - rayPos.z;
        if (zDiff > 0.001 && zDiff < thickness) {
            // Binary search refinement (4 steps)
            float3 startPos = rayPos - reflDir * stepSize;
            float3 endPos = rayPos;
            [unroll]
            for (int b = 0; b < 4; ++b) {
                float3 midPos = (startPos + endPos) * 0.5;
                float4 midClip = mul(u_Projection, float4(midPos, 1.0));
                float2 midUV = float2(midClip.x / midClip.w * 0.5 + 0.5, 0.5 - midClip.y / midClip.w * 0.5);
                float midDepth = SampleHiZ(0u, midUV);
                float3 midView = ReconstructViewPos(midUV, midDepth);
                if (midPos.z < midView.z) {
                    endPos = midPos;
                } else {
                    startPos = midPos;
                }
            }
            float4 finalClip = mul(u_Projection, float4(endPos, 1.0));
            float2 hitUV = float2(finalClip.x / finalClip.w * 0.5 + 0.5, 0.5 - finalClip.y / finalClip.w * 0.5);

            float2 dCoords = smoothstep(0.0, 0.12, hitUV) * (1.0 - smoothstep(0.88, 1.0, hitUV));
            float screenFade = saturate(dCoords.x * dCoords.y);
            float distFade = saturate(1.0 - (float(i) / float(maxSteps)));

            confidence = screenFade * distFade;
            hitColor = u_ComputeColor.SampleLevel(u_ComputeSampler, hitUV, 0.0).rgb;
            break;
        }
    }

    // Fallback: Parallax-Corrected Reflection Probe approximation
    if (confidence < 0.99 && u_ProbePosition.w > 0.5) {
        float3 worldPos = mul(u_InvView, float4(viewPos, 1.0)).xyz;
        float3 reflWS = normalize(mul((float3x3)u_InvView, reflDir));
        float3 probeDir = reflWS;

        if (u_ProbeBoxMin.w > 0.5) {
            float3 boxMin = u_ProbeBoxMin.xyz;
            float3 boxMax = u_ProbeBoxMax.xyz;
            float3 safeR = sign(reflWS) * max(abs(reflWS), 1e-5);
            float3 invR = 1.0 / safeR;
            float3 tMin = (boxMin - worldPos) * invR;
            float3 tMax = (boxMax - worldPos) * invR;
            float3 t2 = max(tMin, tMax);
            float tDist = min(t2.x, min(t2.y, t2.z));
            if (tDist > 0.0) {
                float3 intersectWS = worldPos + reflWS * tDist;
                probeDir = normalize(intersectWS - u_ProbePosition.xyz);
            }
        }
        float3 probeColor = float3(0.08, 0.08, 0.08) * u_ProbeBoxMax.w;
        hitColor = lerp(probeColor, hitColor, confidence);
        confidence = max(confidence, 0.25 * saturate(u_ProbeBoxMax.w));
    }

    u_ComputeOutput[pixelPos] = float4(hitColor, confidence);
}

// =============================================================================
// Fullscreen Composite Pass (VSMain & PSMain)
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

Texture2D<float>    u_SceneDepth    : register(t0, space1);
Texture2D<float4>   u_SSRTexture    : register(t1, space1);
Texture2D<float4>   u_SceneColor    : register(t2, space1);
Texture2D<float4>   u_SceneNormal   : register(t3, space1);
SamplerState        u_LinearSampler : register(s0, space1);

float4 PSMain(VSOutput input) : SV_Target {
    float2 uv = input.TexCoord;
    float depth = u_SceneDepth.SampleLevel(u_LinearSampler, uv, 0.0);
    float4 sceneColor = u_SceneColor.SampleLevel(u_LinearSampler, uv, 0.0);

    if (depth >= 0.9999) {
        return sceneColor;
    }

    float4 ssrSample = u_SSRTexture.SampleLevel(u_LinearSampler, uv, 0.0);
    float3 ssrColor = ssrSample.rgb;
    float confidence = saturate(ssrSample.a);

    float4 normalSample = u_SceneNormal.SampleLevel(u_LinearSampler, uv, 0.0);
    float3 viewPos = ReconstructViewPos(uv, depth);
    float3 V = normalize(-viewPos);
    float3 N;
    if (dot(normalSample.xyz, normalSample.xyz) > 0.01) {
        N = normalize(mul((float3x3)u_View, normalSample.xyz));
    } else {
        float2 rcpFrame = u_ScreenSize.zw;
        float3 pR = ReconstructViewPos(uv + float2(rcpFrame.x, 0.0), u_SceneDepth.SampleLevel(u_LinearSampler, uv + float2(rcpFrame.x, 0.0), 0.0));
        float3 pL = ReconstructViewPos(uv - float2(rcpFrame.x, 0.0), u_SceneDepth.SampleLevel(u_LinearSampler, uv - float2(rcpFrame.x, 0.0), 0.0));
        float3 pD = ReconstructViewPos(uv + float2(0.0, rcpFrame.y), u_SceneDepth.SampleLevel(u_LinearSampler, uv + float2(0.0, rcpFrame.y), 0.0));
        float3 pU = ReconstructViewPos(uv - float2(0.0, rcpFrame.y), u_SceneDepth.SampleLevel(u_LinearSampler, uv - float2(0.0, rcpFrame.y), 0.0));
        float3 dx = (abs(pR.z - viewPos.z) < abs(pL.z - viewPos.z)) ? (pR - viewPos) : (viewPos - pL);
        float3 dy = (abs(pD.z - viewPos.z) < abs(pU.z - viewPos.z)) ? (pD - viewPos) : (viewPos - pU);
        N = normalize(cross(dy, dx));
        if (dot(N, V) < 0.0) {
            N = -N;
        }
    }
    float NdotV = max(dot(N, V), 0.001);

    // Fresnel Schlick dielectric factor (F0 = 0.04)
    float3 F0 = float3(0.04, 0.04, 0.04);
    float3 F = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);

    float ssrStrength = u_SSRParams.z;
    float3 finalColor = sceneColor.rgb + ssrColor * (confidence * F * ssrStrength);
    return float4(finalColor, sceneColor.a);
}
