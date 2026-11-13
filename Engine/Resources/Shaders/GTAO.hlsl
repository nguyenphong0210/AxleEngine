// =============================================================================
// AxleEngine - Ground Truth Ambient Occlusion (GTAO) & Screen-Space Contact Shadows
// Dual-compiled for Direct3D 12 (DXIL 6.0) & Vulkan 1.3 (SPIR-V)
// Horizon-based hemispherical integration with edge-preserving bilateral filter
// =============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

cbuffer GTAOConstants : register(b0, space0) {
    float4x4 u_InvProjection;
    float4x4 u_View;
    float4   u_ScreenSize;      // (width, height, 1.0 / width, 1.0 / height)
    float4   u_GTAOParams;      // (radius pixels, strength, depth bias, power)
    float4   u_SunDirView;      // (xyz: view-space sun dir, w: sun enabled)
    float4   u_ContactParams;   // (max distance px, steps, strength, thickness)
    float4   u_Padding[4];      // Total 256 bytes aligned
};

// -----------------------------------------------------------------------------
// Common Math and Helpers
// -----------------------------------------------------------------------------
float3 ReconstructViewPos(float2 uv, float depth) {
    float4 clip = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(u_InvProjection, clip);
    return viewPos.xyz / max(abs(viewPos.w), 1e-6);
}

// Interleaved spatial noise hash for spatial jittering
float SpatialHash(uint2 pixelPos) {
    return frac(52.9829189 * frac(dot(float2(pixelPos), float2(0.06711056, 0.00583715))));
}

// =============================================================================
// Compute Shader (Horizon Search & Directional Contact Shadows)
// =============================================================================

Texture2D<float>    u_ComputeDepth   : register(t0, space1);
Texture2D<float4>   u_ComputeNormal  : register(t1, space1);
SamplerState        u_ComputeSampler : register(s0, space1);
RWTexture2D<float2> u_ComputeOutput  : register(u0, space2);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 pixelPos = dispatchThreadId.xy;
    if (pixelPos.x >= (uint)u_ScreenSize.x || pixelPos.y >= (uint)u_ScreenSize.y) return;

    float2 rcpFrame = u_ScreenSize.zw;
    float2 uv = (float2(pixelPos) + 0.5) * rcpFrame;

    float depth = u_ComputeDepth.SampleLevel(u_ComputeSampler, uv, 0.0);
    if (depth >= 0.9999 || depth <= 0.0) {
        u_ComputeOutput[pixelPos] = float2(1.0, 1.0);
        return;
    }

    float3 viewPos = ReconstructViewPos(uv, depth);

    // 1. Determine surface normal in view space
    float4 normalSample = u_ComputeNormal.SampleLevel(u_ComputeSampler, uv, 0.0);
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
    float radiusPixels = max(u_GTAOParams.x, 2.0);
    float strength = u_GTAOParams.y;
    float depthBias = u_GTAOParams.z;
    float power = max(u_GTAOParams.w, 0.1);

    if (strength > 0.001) {
        float noise = SpatialHash(pixelPos);
        const int NUM_SLICES = 4;
        const int NUM_STEPS = 4;
        float totalVisibility = 0.0;
        float totalWeight = 0.0;

        [unroll]
        for (int slice = 0; slice < NUM_SLICES; ++slice) {
            float phi = (float(slice) + noise) * (float(M_PI) / float(NUM_SLICES));
            float2 sliceDir = float2(cos(phi), sin(phi));

            // Project normal onto slice plane
            float3 sliceAxis = normalize(float3(sliceDir.x, sliceDir.y, 0.0));
            float3 orthoAxis = cross(viewDir, sliceAxis);
            float3 projNormal = viewNormal - orthoAxis * dot(viewNormal, orthoAxis);
            float projLen = length(projNormal);
            if (projLen < 1e-4) continue;
            projNormal /= projLen;

            // Angle gamma between view direction and projected normal
            float cosGamma = clamp(dot(projNormal, viewDir), -1.0, 1.0);
            float gamma = acos(cosGamma);
            if (dot(projNormal, sliceAxis) < 0.0) gamma = -gamma;

            // Search horizons h1 (positive) and h2 (negative)
            float h1 = -float(M_PI) * 0.5;
            float h2 = -float(M_PI) * 0.5;

            for (int step = 1; step <= NUM_STEPS; ++step) {
                float stepFraction = float(step) / float(NUM_STEPS);
                float stepDist = stepFraction * radiusPixels;

                // Positive direction sample
                float2 uvSample1 = saturate(uv + sliceDir * stepDist * rcpFrame);
                float depthSample1 = u_ComputeDepth.SampleLevel(u_ComputeSampler, uvSample1, 0.0);
                if (depthSample1 < 0.9999) {
                    float3 posSample1 = ReconstructViewPos(uvSample1, depthSample1);
                    float3 delta1 = posSample1 - viewPos;
                    float dist1 = length(delta1);
                    if (dist1 > depthBias && dist1 < 4.0) {
                        float cosAngle1 = dot(delta1 / dist1, viewDir);
                        float angle1 = acos(clamp(cosAngle1, -1.0, 1.0));
                        h1 = max(h1, angle1);
                    }
                }

                // Negative direction sample
                float2 uvSample2 = saturate(uv - sliceDir * stepDist * rcpFrame);
                float depthSample2 = u_ComputeDepth.SampleLevel(u_ComputeSampler, uvSample2, 0.0);
                if (depthSample2 < 0.9999) {
                    float3 posSample2 = ReconstructViewPos(uvSample2, depthSample2);
                    float3 delta2 = posSample2 - viewPos;
                    float dist2 = length(delta2);
                    if (dist2 > depthBias && dist2 < 4.0) {
                        float cosAngle2 = dot(delta2 / dist2, viewDir);
                        float angle2 = acos(clamp(cosAngle2, -1.0, 1.0));
                        h2 = max(h2, angle2);
                    }
                }
            }

            // Analytical GTAO integration Jimenez 2016
            h1 = clamp(h1, -float(M_PI) * 0.5, float(M_PI) * 0.5);
            h2 = clamp(h2, -float(M_PI) * 0.5, float(M_PI) * 0.5);
            float sliceVis = 0.25 * (sin(2.0 * h1 - gamma) + sin(2.0 * h2 - gamma) + 2.0 * sin(gamma));
            sliceVis = saturate(sliceVis);

            totalVisibility += sliceVis * projLen;
            totalWeight += projLen;
        }

        float visibility = totalWeight > 1e-4 ? (totalVisibility / totalWeight) : 1.0;
        ao = saturate(1.0 - (1.0 - visibility) * strength);
        ao = pow(ao, power);
    }

    // 3. Screen-Space Directional Contact Shadows
    float contactShadow = 1.0;
    if (u_SunDirView.w > 0.5 && u_ContactParams.z > 0.001) {
        float3 lightDirView = normalize(u_SunDirView.xyz);
        float NdotL = dot(viewNormal, lightDirView);

        if (NdotL > 0.0) {
            float maxDistPx = max(u_ContactParams.x, 4.0);
            int stepCount = max((int)u_ContactParams.y, 8);
            float shadowStrength = u_ContactParams.z;
            float thickness = max(u_ContactParams.w, 0.02);

            float3 rayStart = viewPos + viewNormal * (depthBias * 2.0);
            float blockerAccum = 0.0;

            for (int s = 1; s <= stepCount; ++s) {
                float t = float(s) / float(stepCount);
                float stepDist = t * (maxDistPx * 0.05); // world-space scale
                float3 rayPos = rayStart + lightDirView * stepDist;

                // Project rayPos back to screen UV (Y is inverted between view space and screen UV)
                float2 rayUV = uv + float2(lightDirView.x, -lightDirView.y) * (t * maxDistPx * rcpFrame);
                if (any(rayUV < 0.0) || any(rayUV > 1.0)) break;

                float sampleDepth = u_ComputeDepth.SampleLevel(u_ComputeSampler, rayUV, 0.0);
                if (sampleDepth >= 0.9999) continue;

                float3 samplePos = ReconstructViewPos(rayUV, sampleDepth);
                float zDiff = samplePos.z - rayPos.z;

                if (zDiff > 0.002 && zDiff < thickness) {
                    blockerAccum = max(blockerAccum, 1.0 - (zDiff / thickness));
                }
            }

            contactShadow = 1.0 - saturate(blockerAccum * shadowStrength);
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

    float centerDepth = u_SceneDepth.SampleLevel(u_LinearSampler, uv, 0.0);
    float4 sceneColor = u_SceneColor.SampleLevel(u_LinearSampler, uv, 0.0);

    if (centerDepth >= 0.9999) {
        return sceneColor;
    }

    float centerZ = ReconstructViewPos(uv, centerDepth).z;

    // Cross-bilateral 5x5 reconstruction to eliminate depth edge-bleed
    float2 occlusionSum = 0.0;
    float weightSum = 0.0;

    const float spatialWeights[5] = { 0.06136, 0.24477, 0.38774, 0.24477, 0.06136 };

    [unroll]
    for (int y = -2; y <= 2; ++y) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            float2 offsetUV = uv + float2(x, y) * rcpFrame;
            float tapDepth = u_SceneDepth.SampleLevel(u_LinearSampler, offsetUV, 0.0);
            float tapZ = ReconstructViewPos(offsetUV, tapDepth).z;

            float depthDiff = abs(centerZ - tapZ);
            float depthWeight = exp(-depthDiff / (0.05 * max(abs(centerZ), 0.1) + 1e-4));
            float spatialWeight = spatialWeights[x + 2] * spatialWeights[y + 2];
            float totalWeight = depthWeight * spatialWeight;

            float2 tapOcclusion = u_OcclusionTexture.SampleLevel(u_LinearSampler, offsetUV, 0.0);
            occlusionSum += tapOcclusion * totalWeight;
            weightSum += totalWeight;
        }
    }

    float2 filteredOcclusion = weightSum > 1e-4 ? (occlusionSum / weightSum) : u_OcclusionTexture.SampleLevel(u_LinearSampler, uv, 0.0);
    float finalAO = saturate(filteredOcclusion.r);
    float finalContact = saturate(filteredOcclusion.g);

    // Apply soft minimum floor so ambient and contact shadows never crush geometry to solid black
    finalAO = lerp(0.35, 1.0, finalAO);
    finalContact = lerp(0.5, 1.0, finalContact);

    // GTAO is recorded after the forward color pass in the current renderer.
    // Keep the screen-space composite conservative: ambient visibility gets a
    // bounded weight and contact visibility gets its own bounded weight. This
    // prevents the old full-color multiplication from crushing direct light,
    // specular highlights and emissive materials. A future pre-lighting AO
    // path can replace these weights with a true separated indirect term.
    float ambientWeight = 0.25 * saturate(u_GTAOParams.y);
    float contactWeight = 0.20 * saturate(u_ContactParams.z);
    float ambientFactor = lerp(1.0, finalAO, ambientWeight);
    float contactFactor = lerp(1.0, finalContact, contactWeight);
    return float4(sceneColor.rgb * ambientFactor * contactFactor, sceneColor.a);
}
