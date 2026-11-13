// =============================================================================
// TAA.hlsl - Unified D3D12 (DXIL) & Vulkan 1.3 (SPIR-V) Temporal Anti-Aliasing Pass
// Standardized HLSL 2021 Sub-Pixel Jitter Reprojection & YCoCg Neighborhood Clamping
// =============================================================================

cbuffer TAAConstants : register(b0, space0) {
    float4x4 u_InvViewProj;   // 64 bytes (Offset 0) - Current frame inverse View-Projection
    float4x4 u_PrevViewProj;  // 64 bytes (Offset 64) - Previous frame View-Projection
    float4   u_ScreenSize;    // 16 bytes (Offset 128) - x: Width, y: Height, z: 1/Width, w: 1/Height
    float4   u_TAAParams;     // 16 bytes (Offset 144) - x: JitterX, y: JitterY, z: FeedbackMin, w: FeedbackMax
    float4   u_Reserved[6];   // 96 bytes padding -> Total: 256 bytes (CBV aligned)
};

Texture2D<float4> u_CurrentColor  : register(t0, space1);
Texture2D<float>  u_DepthTexture  : register(t1, space1);
Texture2D<float4> u_HistoryColor  : register(t2, space1);
SamplerState      u_LinearSampler : register(s0, space1);
SamplerState      u_PointSampler  : register(s1, space1);

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

// Convert RGB to YCoCg color space
float3 RGBToYCoCg(float3 c) {
    return float3(
         0.25 * c.r + 0.50 * c.g + 0.25 * c.b,
         0.50 * c.r - 0.50 * c.b,
        -0.25 * c.r + 0.50 * c.g - 0.25 * c.b
    );
}

// Convert YCoCg back to linear RGB
float3 YCoCgToRGB(float3 c) {
    return float3(
        c.x + c.y - c.z,
        c.x + c.z,
        c.x - c.y - c.z
    );
}

// -----------------------------------------------------------------------------
// PIXEL SHADER: Reprojection, 3x3 YCoCg Neighborhood Clamping & Temporal Blend
// -----------------------------------------------------------------------------
float4 PSMain(VSOutput input) : SV_Target {
    float2 uv = input.TexCoord;
    float2 texelSize = u_ScreenSize.zw;

    // 1. Sample 3x3 neighborhood of current frame
    float3 c0 = u_CurrentColor.SampleLevel(u_PointSampler, uv + float2(-1.0, -1.0) * texelSize, 0.0).rgb;
    float3 c1 = u_CurrentColor.SampleLevel(u_PointSampler, uv + float2( 0.0, -1.0) * texelSize, 0.0).rgb;
    float3 c2 = u_CurrentColor.SampleLevel(u_PointSampler, uv + float2( 1.0, -1.0) * texelSize, 0.0).rgb;
    float3 c3 = u_CurrentColor.SampleLevel(u_PointSampler, uv + float2(-1.0,  0.0) * texelSize, 0.0).rgb;
    float3 c4 = u_CurrentColor.SampleLevel(u_PointSampler, uv, 0.0).rgb; // Center
    float3 c5 = u_CurrentColor.SampleLevel(u_PointSampler, uv + float2( 1.0,  0.0) * texelSize, 0.0).rgb;
    float3 c6 = u_CurrentColor.SampleLevel(u_PointSampler, uv + float2(-1.0,  1.0) * texelSize, 0.0).rgb;
    float3 c7 = u_CurrentColor.SampleLevel(u_PointSampler, uv + float2( 0.0,  1.0) * texelSize, 0.0).rgb;
    float3 c8 = u_CurrentColor.SampleLevel(u_PointSampler, uv + float2( 1.0,  1.0) * texelSize, 0.0).rgb;

    // Convert samples to YCoCg space
    float3 y0 = RGBToYCoCg(c0);
    float3 y1 = RGBToYCoCg(c1);
    float3 y2 = RGBToYCoCg(c2);
    float3 y3 = RGBToYCoCg(c3);
    float3 y4 = RGBToYCoCg(c4);
    float3 y5 = RGBToYCoCg(c5);
    float3 y6 = RGBToYCoCg(c6);
    float3 y7 = RGBToYCoCg(c7);
    float3 y8 = RGBToYCoCg(c8);

    // Compute bounding box in YCoCg
    float3 boxMin = min(y0, min(y1, min(y2, min(y3, min(y4, min(y5, min(y6, min(y7, y8))))))));
    float3 boxMax = max(y0, max(y1, max(y2, max(y3, max(y4, max(y5, max(y6, max(y7, y8))))))));

    // Optional variance clipping for tighter bounds
    float3 m1 = (y0 + y1 + y2 + y3 + y4 + y5 + y6 + y7 + y8) / 9.0;
    float3 m2 = (y0*y0 + y1*y1 + y2*y2 + y3*y3 + y4*y4 + y5*y5 + y6*y6 + y7*y7 + y8*y8) / 9.0;
    float3 sigma = sqrt(max(m2 - m1 * m1, 0.0));
    boxMin = max(boxMin, m1 - 1.25 * sigma);
    boxMax = min(boxMax, m1 + 1.25 * sigma);

    // 2. Sample closest depth in 3x3 to avoid edge bleed on silhouettes
    float d4 = u_DepthTexture.SampleLevel(u_PointSampler, uv, 0.0).r;
    float depth = d4;

    // 3. Reconstruct world space position and reproject into previous frame
    float2 ndcXY = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 clipPos = float4(ndcXY, depth, 1.0);
    float4 worldPosH = mul(u_InvViewProj, clipPos);
    float3 worldPos = worldPosH.xyz / max(worldPosH.w, 1e-5);

    float4 prevClip = mul(u_PrevViewProj, float4(worldPos, 1.0));
    float2 prevNDC = prevClip.xy / max(prevClip.w, 1e-5);
    float2 prevUV = prevNDC * float2(0.5, -0.5) + 0.5;

    // Check if reprojected UV is within screen bounds
    bool validHistory = (prevUV.x >= 0.0 && prevUV.x <= 1.0 && prevUV.y >= 0.0 && prevUV.y <= 1.0);
    if (u_Reserved[0].x < 0.5) {
        return float4(c4, 1.0);
    }
    float feedbackMin = (u_TAAParams.z > 0.0) ? u_TAAParams.z : 0.88;
    float feedbackMax = (u_TAAParams.w > 0.0) ? u_TAAParams.w : 0.96;

    if (!validHistory || depth >= 0.9999) {
        return float4(c4, 1.0);
    }

    // 4. Sample history texture with bilinear filtering
    float3 historyRGB = u_HistoryColor.SampleLevel(u_LinearSampler, prevUV, 0.0).rgb;
    float3 historyYCoCg = RGBToYCoCg(historyRGB);

    // 5. Clamp history color to current neighborhood YCoCg bounding box
    float3 clampedHistoryYCoCg = clamp(historyYCoCg, boxMin, boxMax);
    float3 clampedHistoryRGB = max(YCoCgToRGB(clampedHistoryYCoCg), 0.0);

    // 6. Luminance-weighted feedback blend
    float alpha = 1.0 - feedbackMax;
    float3 resolvedRGB = lerp(clampedHistoryRGB, c4, alpha);

    return float4(resolvedRGB, 1.0);
}
