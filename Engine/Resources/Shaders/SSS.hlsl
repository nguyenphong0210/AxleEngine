// =============================================================================
// SSS.hlsl - Screen-Space Separable Subsurface Scattering (M15-U9-04, U9-05)
// Separable horizontal & vertical bilateral diffusion with depth, normal and
// material boundary rejection, followed by crisp specular recombination.
// Unified HLSL 2021 for D3D12 (DXIL) and Vulkan 1.3 (SPIR-V).
// =============================================================================

cbuffer SSSConstants : register(b0, space0) {
    float4x4 u_InvProjection;       // 64 bytes
    float4   u_ScreenSize;          // 16 bytes: (width, height, 1/width, 1/height)
    float4   u_DirectionAndProj;    // 16 bytes: (dirX, dirY, projScale, worldRadiusScale)
    float4   u_SSSParams;           // 16 bytes: (maxRadiusUV, depthRejectScale, normalMinDot, linearHDR)
    float4   u_Reserved[9];         // 144 bytes -> Total 256 bytes (CBV aligned)
};

Texture2D        u_InputDiffuse     : register(t0, space1);
Texture2D<float> u_DepthTex         : register(t1, space1);
Texture2D        u_NormalTex        : register(t2, space1);
Texture2D        u_ControlTex       : register(t3, space1);
Texture2D        u_SpecularSceneTex : register(t4, space1);
SamplerState     u_LinearSampler    : register(s0, space1);
SamplerState     u_PointSampler     : register(s1, space1);

struct VSOutput {
    float4 PositionCS : SV_Position;
    float2 TexCoord   : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.TexCoord = uv;
    output.PositionCS = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float LinearizeViewDepth(float depth) {
    float4 viewH = mul(u_InvProjection, float4(0.0, 0.0, clamp(depth, 0.0, 0.999999), 1.0));
    return abs(viewH.z / max(abs(viewH.w), 1e-6));
}

float3 SafeNormalizeNormal(float3 n) {
    float lenSq = dot(n, n);
    return lenSq > 1e-8 ? (n * rsqrt(lenSq)) : float3(0.0, 0.0, 1.0);
}

float ComputeBoundaryWeight(
    float centerDepth,
    float3 centerNormal,
    float4 centerControl,
    float sampleDepth,
    float3 sampleNormal,
    float4 sampleControl) {
    // Active SSS check & material profile match (rejects stone/metal/different SSS neighbors).
    if (sampleControl.a <= 0.001) {
        return 0.0;
    }
    float colorDiff = length(centerControl.rgb - sampleControl.rgb);
    float radiusDiff = abs(centerControl.a - sampleControl.a) / max(max(centerControl.a, sampleControl.a), 0.1);
    if (colorDiff > 0.08 || radiusDiff > 0.15) {
        return 0.0;
    }

    // Silhouette / depth-jump rejection in view-space world units.
    float depthDelta = abs(centerDepth - sampleDepth);
    float maxAllowedDepthStep = max(u_SSSParams.y * max(centerControl.a, 0.05), 0.05);
    if (depthDelta > maxAllowedDepthStep) {
        return 0.0;
    }
    float depthWeight = exp(-depthDelta * (3.0 / maxAllowedDepthStep));

    // Normal discontinuity rejection to stop cross-face crease/edge bleed.
    float ndot = dot(centerNormal, sampleNormal);
    float normalMinDot = u_SSSParams.z;
    if (ndot < normalMinDot) {
        return 0.0;
    }
    float normalWeight = pow(saturate((ndot - normalMinDot) / max(1.0 - normalMinDot, 1e-4)), 2.0);

    return depthWeight * normalWeight;
}

float3 EvaluateSeparableSSS(float2 uv, float4 centerControl) {
    float3 centerDiffuse = u_InputDiffuse.SampleLevel(u_PointSampler, uv, 0.0).rgb;
    float radiusWorld = clamp(centerControl.a, 0.0, 16.0);
    if (radiusWorld <= 0.001) {
        return centerDiffuse;
    }

    float centerRawDepth = u_DepthTex.SampleLevel(u_PointSampler, uv, 0.0);
    if (centerRawDepth >= 0.99999) {
        return centerDiffuse;
    }
    float centerDepth = LinearizeViewDepth(centerRawDepth);
    float3 centerNormal = SafeNormalizeNormal(u_NormalTex.SampleLevel(u_PointSampler, uv, 0.0).xyz);

    // Project SSS world radius into UV blur footprint, bounded cleanly on screen.
    float uvRadius = min(radiusWorld * u_DirectionAndProj.w * u_DirectionAndProj.z / max(centerDepth, 0.1), u_SSSParams.x);
    if (uvRadius <= u_ScreenSize.w * 0.25) {
        return centerDiffuse;
    }

    // 11-tap symmetric separable Gaussian kernel with per-channel scatter profile.
    float2 dirStep = u_DirectionAndProj.xy * uvRadius * 0.2; // * (1/5) across [-5..+5]
    float3 scatterStrength = saturate(centerControl.rgb);

    float3 accumColor = centerDiffuse;
    float3 accumWeight = float3(1.0, 1.0, 1.0);

    [unroll]
    for (int i = -5; i <= 5; ++i) {
        if (i == 0) continue;
        float t = float(i) * 0.2; // Normalized [-1..1]
        float gaussW = exp(-3.0 * t * t);
        float2 sampleUV = clamp(uv + dirStep * float(i), u_ScreenSize.zw * 0.5, 1.0 - u_ScreenSize.zw * 0.5);

        float sampleRawDepth = u_DepthTex.SampleLevel(u_PointSampler, sampleUV, 0.0);
        if (sampleRawDepth >= 0.99999) continue;
        float sampleDepth = LinearizeViewDepth(sampleRawDepth);
        float3 sampleNormal = SafeNormalizeNormal(u_NormalTex.SampleLevel(u_PointSampler, sampleUV, 0.0).xyz);
        float4 sampleControl = u_ControlTex.SampleLevel(u_PointSampler, sampleUV, 0.0);

        float bilateral = ComputeBoundaryWeight(centerDepth, centerNormal, centerControl,
                                                sampleDepth, sampleNormal, sampleControl);
        if (bilateral <= 1e-5) continue;

        float3 tapW = (gaussW * bilateral) * scatterStrength;
        float3 sampleDiffuse = u_InputDiffuse.SampleLevel(u_PointSampler, sampleUV, 0.0).rgb;
        accumColor += sampleDiffuse * tapW;
        accumWeight += tapW;
    }

    return accumColor / max(accumWeight, float3(1e-4, 1e-4, 1e-4));
}

float3 ToneMapACESSSS(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PSBlurH(VSOutput input) : SV_Target0 {
    float4 centerControl = u_ControlTex.SampleLevel(u_PointSampler, input.TexCoord, 0.0);
    if (centerControl.a <= 0.001) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    float3 blurred = EvaluateSeparableSSS(input.TexCoord, centerControl);
    return float4(clamp(blurred, 0.0, 65000.0), centerControl.a);
}

float4 PSBlurVComposite(VSOutput input) : SV_Target0 {
    float4 sceneSpecOrFull = u_SpecularSceneTex.SampleLevel(u_PointSampler, input.TexCoord, 0.0);
    float4 centerControl = u_ControlTex.SampleLevel(u_PointSampler, input.TexCoord, 0.0);
    if (centerControl.a <= 0.001) {
        // Non-SSS opaque geometry & background sky remain completely untouched.
        return sceneSpecOrFull;
    }

    float3 diffusedSSS = EvaluateSeparableSSS(input.TexCoord, centerControl);
    // Recombine diffused subsurface scatter with crisp undiffused specular, clearcoat, sheen & emissive.
    float3 combined = clamp(sceneSpecOrFull.rgb + diffusedSSS, 0.0, 65000.0);
    if (u_SSSParams.w <= 0.0) {
        combined = pow(max(ToneMapACESSSS(combined), 0.0), 1.0 / 2.2);
    }
    return float4(combined, sceneSpecOrFull.a);
}
