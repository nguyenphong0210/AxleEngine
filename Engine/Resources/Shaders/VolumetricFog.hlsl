// =============================================================================
// AxleEngine - Volumetric Fog & Atmospheric Participating Media
// Dual-compiled for Direct3D 12 (DXIL 6.0) & Vulkan 1.3 (SPIR-V)
// Raymarched participating media with Mie phase scattering & height falloff
// =============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

cbuffer VolumetricFogConstants : register(b0, space0) {
    float4x4 u_View;
    float4x4 u_InvView;
    float4x4 u_Projection;
    float4x4 u_InvProjection;
    float4   u_ScreenSize;              // (width, height, 1.0 / width, 1.0 / height)
    float4   u_SunDirectionIntensity;   // (xyz: dir towards sun, w: intensity)
    float4   u_SunColor;                // (rgb: sun color, w: 1.0)
    float4   u_FogParams;               // (x: density, y: height falloff, z: asymmetry g, w: max dist)
    float4   u_FogColorIntensity;       // (rgb: fog color, w: intensity)
    float4   u_DDGIParams;              // (x: ddgi multiplier, yzw: reserved)
    float4   u_Padding[10];             // Total 512 bytes aligned
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

// Henyey-Greenstein Mie phase function for atmospheric forward scattering
float HenyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * float(M_PI) * max(denom * sqrt(max(denom, 1e-5)), 1e-5));
}

// =============================================================================
// Compute Shader (Raymarched Participating Media In-Scattering)
// =============================================================================

Texture2D<float>    u_ComputeDepth   : register(t0, space1);
SamplerState        u_ComputeSampler : register(s0, space1);
RWTexture2D<float4> u_ComputeOutput  : register(u0, space2);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 pixelPos = dispatchThreadId.xy;
    if (pixelPos.x >= (uint)u_ScreenSize.x || pixelPos.y >= (uint)u_ScreenSize.y) return;

    float2 rcpFrame = u_ScreenSize.zw;
    float2 uv = (float2(pixelPos) + 0.5) * rcpFrame;

    float depth = u_ComputeDepth.SampleLevel(u_ComputeSampler, uv, 0.0);
    float3 viewPos = ReconstructViewPos(uv, depth);

    float baseDensity = max(u_FogParams.x, 0.0001);
    float heightFalloff = max(u_FogParams.y, 0.001);
    float g = clamp(u_FogParams.z, -0.9, 0.9);
    float maxDist = max(u_FogParams.w, 10.0);
    float fogIntensity = max(u_FogColorIntensity.w, 0.0);

    float sceneDist = (depth >= 0.9999) ? maxDist : min(length(viewPos), maxDist);
    if (sceneDist <= 0.001) {
        u_ComputeOutput[pixelPos] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float3 viewDirWS = normalize(mul((float3x3)u_InvView, normalize(viewPos)));
    float3 camPosWS = mul(u_InvView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float3 sunDirWS = normalize(u_SunDirectionIntensity.xyz);
    float sunIntensity = u_SunDirectionIntensity.w;

    float cosTheta = dot(viewDirWS, sunDirWS);
    float phase = HenyeyGreenstein(cosTheta, g);

    const uint NUM_STEPS = 16u;
    float stepSize = sceneDist / float(NUM_STEPS);
    float jitter = SpatialHash(pixelPos);
    float rayDist = stepSize * (0.5 + jitter * 0.5);

    float3 inScattering = float3(0.0, 0.0, 0.0);
    float transmittance = 1.0;

    [loop]
    for (uint i = 0u; i < NUM_STEPS; ++i) {
        float3 sampleWS = camPosWS + viewDirWS * rayDist;

        // Height-based exponential fog density falloff
        float height = max(sampleWS.y, 0.0);
        float localDensity = baseDensity * exp(-height * heightFalloff);

        if (localDensity > 1e-6) {
            float stepOpticalDepth = localDensity * stepSize;
            float stepTransmittance = exp(-stepOpticalDepth);

            // Direct sun scattering + ambient atmospheric sky tint
            float3 sunLight = u_SunColor.rgb * (sunIntensity * phase);
            float3 ambientLight = u_FogColorIntensity.rgb * 0.25;
            float3 stepScattering = (sunLight + ambientLight) * localDensity * fogIntensity;

            // Integration over step
            inScattering += transmittance * (stepScattering * ((1.0 - stepTransmittance) / max(localDensity, 1e-6)));
            transmittance *= stepTransmittance;
        }

        rayDist += stepSize;
        if (transmittance < 0.001) break;
    }

    u_ComputeOutput[pixelPos] = float4(inScattering, transmittance);
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

Texture2D<float4>   u_SceneColor    : register(t0, space1);
Texture2D<float4>   u_FogTexture    : register(t1, space1);
SamplerState        u_LinearSampler : register(s0, space1);

float4 PSMain(VSOutput input) : SV_Target {
    float2 uv = input.TexCoord;
    float4 sceneColor = u_SceneColor.SampleLevel(u_LinearSampler, uv, 0.0);
    float4 fogSample = u_FogTexture.SampleLevel(u_LinearSampler, uv, 0.0);

    float3 inScattering = fogSample.rgb;
    float transmittance = saturate(fogSample.a);

    float3 finalColor = sceneColor.rgb * transmittance + inScattering;
    return float4(finalColor, sceneColor.a);
}
