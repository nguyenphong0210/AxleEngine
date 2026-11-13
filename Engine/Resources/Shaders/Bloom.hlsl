// =============================================================================
// Bloom.hlsl - Unified D3D12 (DXIL) & Vulkan 1.3 (SPIR-V) Physically Based Bloom Pass
// Standardized HLSL 2021 Dual Kawase / 13-Tap Downsample, 9-Tap Tent Filter & Apply
// =============================================================================

cbuffer BloomConstants : register(b0, space0) {
    float4 u_ScreenSize;   // x: Width, y: Height, z: 1/Width, w: 1/Height
    float4 u_BloomParams;  // x: Threshold, y: Intensity, z: Knee, w: Mode (0: Downsample+Threshold, 1: Upsample/Blur)
    float4 u_BloomTint;    // rgb: Bloom Tint, w: Blur Radius
    float4 u_Reserved[13]; // 208 bytes padding -> Total: 256 bytes (CBV aligned)
};

Texture2D<float4>   u_InputTexture  : register(t0, space1);
SamplerState        u_LinearSampler : register(s0, space1);
RWTexture2D<float4> u_OutputStorage : register(u0, space2);
Texture2D<float4>   u_SceneTexture  : register(t1, space1);

// Karis average weighting to eliminate fireflies / single-pixel HDR spikes
float KarisWeight(float3 color) {
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

// Quadratic soft-knee thresholding
float3 PrefilterThreshold(float3 color, float threshold, float knee) {
    float brightness = max(color.r, max(color.g, color.b));
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * max(knee, 1e-4) + 1e-4);
    float contrib = max(soft, brightness - threshold);
    contrib /= max(brightness, 1e-4);
    return color * saturate(contrib);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint width = (uint)u_ScreenSize.x;
    uint height = (uint)u_ScreenSize.y;
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height) return;

    float2 texelSize = u_ScreenSize.zw;
    float2 uv = (float2(dispatchThreadID.xy) + 0.5) * texelSize;

    int mode = (int)u_BloomParams.w;
    float threshold = u_BloomParams.x;
    float intensity = u_BloomParams.y;
    float knee = max(u_BloomParams.z, 0.01);
    float radius = max(u_BloomTint.w, 1.0);

    if (mode == 0) {
        // --- PASS 0: 13-Tap Downsampling with Karis Partial Averaging & Thresholding ---
        float2 d = texelSize * radius;

        float3 a = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-2.0,  2.0) * d, 0.0).rgb;
        float3 b = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 0.0,  2.0) * d, 0.0).rgb;
        float3 c = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 2.0,  2.0) * d, 0.0).rgb;
        float3 d_samp = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-2.0,  0.0) * d, 0.0).rgb;
        float3 e = u_InputTexture.SampleLevel(u_LinearSampler, uv, 0.0).rgb;
        float3 f = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 2.0,  0.0) * d, 0.0).rgb;
        float3 g = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-2.0, -2.0) * d, 0.0).rgb;
        float3 h = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 0.0, -2.0) * d, 0.0).rgb;
        float3 i = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 2.0, -2.0) * d, 0.0).rgb;

        float3 j = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-1.0,  1.0) * d, 0.0).rgb;
        float3 k = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 1.0,  1.0) * d, 0.0).rgb;
        float3 l = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-1.0, -1.0) * d, 0.0).rgb;
        float3 m = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 1.0, -1.0) * d, 0.0).rgb;

        // Apply thresholding to source samples
        a = PrefilterThreshold(a, threshold, knee);
        b = PrefilterThreshold(b, threshold, knee);
        c = PrefilterThreshold(c, threshold, knee);
        d_samp = PrefilterThreshold(d_samp, threshold, knee);
        e = PrefilterThreshold(e, threshold, knee);
        f = PrefilterThreshold(f, threshold, knee);
        g = PrefilterThreshold(g, threshold, knee);
        h = PrefilterThreshold(h, threshold, knee);
        i = PrefilterThreshold(i, threshold, knee);
        j = PrefilterThreshold(j, threshold, knee);
        k = PrefilterThreshold(k, threshold, knee);
        l = PrefilterThreshold(l, threshold, knee);
        m = PrefilterThreshold(m, threshold, knee);

        // 4 sub-box averages with Karis weighting
        float3 box1 = (a + b + d_samp + e) * 0.25;
        float3 box2 = (b + c + e + f) * 0.25;
        float3 box3 = (d_samp + e + g + h) * 0.25;
        float3 box4 = (e + f + h + i) * 0.25;
        float3 box5 = (j + k + l + m) * 0.25;

        float w1 = KarisWeight(box1);
        float w2 = KarisWeight(box2);
        float w3 = KarisWeight(box3);
        float w4 = KarisWeight(box4);
        float w5 = KarisWeight(box5);

        float3 bloom = (box1 * w1 + box2 * w2 + box3 * w3 + box4 * w4 + box5 * w5) /
                       max(w1 + w2 + w3 + w4 + w5, 1e-4);

        u_OutputStorage[dispatchThreadID.xy] = float4(bloom, 1.0);
    } else {
        // --- PASS 1: 9-Tap Tent Filter Upsampling & Blur Expansion ---
        float2 d = texelSize * radius;

        float3 s0 = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-1.0,  1.0) * d, 0.0).rgb;
        float3 s1 = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 0.0,  1.0) * d, 0.0).rgb;
        float3 s2 = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 1.0,  1.0) * d, 0.0).rgb;
        float3 s3 = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-1.0,  0.0) * d, 0.0).rgb;
        float3 s4 = u_InputTexture.SampleLevel(u_LinearSampler, uv, 0.0).rgb;
        float3 s5 = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 1.0,  0.0) * d, 0.0).rgb;
        float3 s6 = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-1.0, -1.0) * d, 0.0).rgb;
        float3 s7 = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 0.0, -1.0) * d, 0.0).rgb;
        float3 s8 = u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 1.0, -1.0) * d, 0.0).rgb;

        float3 blurred = (s0 + s2 + s6 + s8) * 0.0625 +
                         (s1 + s3 + s5 + s7) * 0.125 +
                         s4 * 0.25;

        float3 tinted = blurred * u_BloomTint.rgb * intensity;
        u_OutputStorage[dispatchThreadID.xy] = float4(tinted, 1.0);
    }
}

// -----------------------------------------------------------------------------
// Fullscreen Vertex & Pixel Shader for Additive Composite
// -----------------------------------------------------------------------------
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

float4 PSApply(VSOutput input) : SV_Target {
    float3 sceneColor = u_SceneTexture.Sample(u_LinearSampler, input.TexCoord).rgb;
    float3 bloomColor = u_InputTexture.Sample(u_LinearSampler, input.TexCoord).rgb;
    float intensity = u_BloomParams.y;
    float3 finalColor = sceneColor + bloomColor * intensity;
    return float4(finalColor, 1.0);
}
