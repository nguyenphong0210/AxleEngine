// Physically Based 6-Level Pyramid Bloom Shader (M15 Unit 8 / S8.2)
// Threshold & Karis firefly suppression run ONCE at Down0 (mode == 0).
// Subsequent downsamples (mode == 1) use source-texel 13-tap filtering.
// Upsampling runs hardware additive blending (ONE + ONE) onto finer mips
// so every downsample octave contributes without overwriting finer detail,
// and PSApply applies BloomTint and BloomIntensity once with 1/6 normalization.

cbuffer BloomConstants : register(b0, space0) {
    float4 u_ScreenSize;   // x: dstWidth, y: dstHeight, z: 1/dstWidth, w: 1/dstHeight
    float4 u_BloomParams;  // x: threshold, y: intensity, z: knee, w: mode (0 = Down0 threshold+Karis, 1 = Down1..5)
    float4 u_BloomTint;    // x,y,z: tint color, w: tent scatter radius
    float4 u_SourceSize;   // x: srcWidth, y: srcHeight, z: 1/srcWidth, w: 1/srcHeight
    float4 u_Reserved[12];
};

Texture2D<float4> u_InputTexture : register(t0, space1);
Texture2D<float4> u_SceneTexture : register(t1, space1);
SamplerState u_LinearSampler : register(s0, space1);
#if defined(__spirv__)
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> u_OutputTexture : register(u0, space2);

float3 SanitizeHDR(float3 color) {
    return any(isnan(color)) ? float3(0.0, 0.0, 0.0) : min(max(color, 0.0), 65000.0);
}

float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 PrefilterThreshold(float3 color, float threshold, float knee) {
    color = SanitizeHDR(color);
    float brightness = max(color.r, max(color.g, color.b));
    float softKnee = max(threshold * max(knee, 0.0), 0.0);
    float rq = clamp(brightness - threshold + softKnee, 0.0, 2.0 * softKnee);
    rq = (rq * rq) / max(4.0 * softKnee, 1e-5);
    float contribution = max(rq, brightness - threshold) / max(brightness, 1e-5);
    return color * clamp(contribution, 0.0, 1.0);
}

float3 Downsample13TapKaris(float2 uv, float2 srcTexelSize, float threshold, float knee) {
    float3 A = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-2.0, -2.0) * srcTexelSize, 0).rgb, threshold, knee);
    float3 B = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 0.0, -2.0) * srcTexelSize, 0).rgb, threshold, knee);
    float3 C = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 2.0, -2.0) * srcTexelSize, 0).rgb, threshold, knee);

    float3 D = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-1.0, -1.0) * srcTexelSize, 0).rgb, threshold, knee);
    float3 E = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 1.0, -1.0) * srcTexelSize, 0).rgb, threshold, knee);

    float3 F = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-2.0,  0.0) * srcTexelSize, 0).rgb, threshold, knee);
    float3 G = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv,                                    0).rgb, threshold, knee);
    float3 H = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 2.0,  0.0) * srcTexelSize, 0).rgb, threshold, knee);

    float3 I = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-1.0,  1.0) * srcTexelSize, 0).rgb, threshold, knee);
    float3 J = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 1.0,  1.0) * srcTexelSize, 0).rgb, threshold, knee);

    float3 K = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-2.0,  2.0) * srcTexelSize, 0).rgb, threshold, knee);
    float3 L = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 0.0,  2.0) * srcTexelSize, 0).rgb, threshold, knee);
    float3 M = PrefilterThreshold(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 2.0,  2.0) * srcTexelSize, 0).rgb, threshold, knee);

    float3 box0 = (D + E + I + J) * 0.25;
    float3 box1 = (A + B + F + G) * 0.25;
    float3 box2 = (B + C + G + H) * 0.25;
    float3 box3 = (F + G + K + L) * 0.25;
    float3 box4 = (G + H + L + M) * 0.25;

    float w0 = 0.5 / (1.0 + Luminance(box0));
    float w1 = 0.125 / (1.0 + Luminance(box1));
    float w2 = 0.125 / (1.0 + Luminance(box2));
    float w3 = 0.125 / (1.0 + Luminance(box3));
    float w4 = 0.125 / (1.0 + Luminance(box4));
    float weightSum = max(w0 + w1 + w2 + w3 + w4, 1e-5);
    return (box0 * w0 + box1 * w1 + box2 * w2 + box3 * w3 + box4 * w4) / weightSum;
}

float3 Downsample13TapPlain(float2 uv, float2 srcTexelSize) {
    float3 A = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-2.0, -2.0) * srcTexelSize, 0).rgb);
    float3 B = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 0.0, -2.0) * srcTexelSize, 0).rgb);
    float3 C = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 2.0, -2.0) * srcTexelSize, 0).rgb);

    float3 D = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-1.0, -1.0) * srcTexelSize, 0).rgb);
    float3 E = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 1.0, -1.0) * srcTexelSize, 0).rgb);

    float3 F = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-2.0,  0.0) * srcTexelSize, 0).rgb);
    float3 G = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv,                                    0).rgb);
    float3 H = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 2.0,  0.0) * srcTexelSize, 0).rgb);

    float3 I = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-1.0,  1.0) * srcTexelSize, 0).rgb);
    float3 J = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 1.0,  1.0) * srcTexelSize, 0).rgb);

    float3 K = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2(-2.0,  2.0) * srcTexelSize, 0).rgb);
    float3 L = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 0.0,  2.0) * srcTexelSize, 0).rgb);
    float3 M = SanitizeHDR(u_InputTexture.SampleLevel(u_LinearSampler, uv + float2( 2.0,  2.0) * srcTexelSize, 0).rgb);

    return G * 0.125 +
           (A + C + K + M) * 0.03125 +
           (B + F + H + L) * 0.0625 +
           (D + E + I + J) * 0.125;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    uint dstW = max((uint)u_ScreenSize.x, 1u);
    uint dstH = max((uint)u_ScreenSize.y, 1u);
    if (DTid.x >= dstW || DTid.y >= dstH) return;

    float2 uv = (float2(DTid.xy) + 0.5) * u_ScreenSize.zw;
    float2 srcTexelSize = u_SourceSize.zw;
    float3 result = float3(0.0, 0.0, 0.0);
    if (u_BloomParams.w < 0.5) {
        // Normalize the input octave once so additive reconstruction stays in FP16 range.
        result = Downsample13TapKaris(uv, srcTexelSize, u_BloomParams.x, u_BloomParams.z) * (1.0 / 6.0);
    } else {
        result = Downsample13TapPlain(uv, srcTexelSize);
    }
    u_OutputTexture[DTid.xy] = float4(SanitizeHDR(result), 1.0);
}

struct VSOutput {
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

// Rendered with additive blending (ONE + ONE) onto finer Bloom[level] so prior
// downsample energy in Bloom[level] is strictly preserved and summed.
float4 PSUpsample(VSOutput input) : SV_Target {
    float2 texel = u_SourceSize.zw * max(u_BloomTint.w, 0.25);
    float2 uv = input.TexCoord;
    float3 sum = float3(0.0, 0.0, 0.0);
    sum += SanitizeHDR(u_InputTexture.Sample(u_LinearSampler, uv + float2(-texel.x, -texel.y)).rgb) * 1.0;
    sum += SanitizeHDR(u_InputTexture.Sample(u_LinearSampler, uv + float2(     0.0, -texel.y)).rgb) * 2.0;
    sum += SanitizeHDR(u_InputTexture.Sample(u_LinearSampler, uv + float2( texel.x, -texel.y)).rgb) * 1.0;
    sum += SanitizeHDR(u_InputTexture.Sample(u_LinearSampler, uv + float2(-texel.x,      0.0)).rgb) * 2.0;
    sum += SanitizeHDR(u_InputTexture.Sample(u_LinearSampler, uv).rgb) * 4.0;
    sum += SanitizeHDR(u_InputTexture.Sample(u_LinearSampler, uv + float2( texel.x,      0.0)).rgb) * 2.0;
    sum += SanitizeHDR(u_InputTexture.Sample(u_LinearSampler, uv + float2(-texel.x,  texel.y)).rgb) * 1.0;
    sum += SanitizeHDR(u_InputTexture.Sample(u_LinearSampler, uv + float2(     0.0,  texel.y)).rgb) * 2.0;
    sum += SanitizeHDR(u_InputTexture.Sample(u_LinearSampler, uv + float2( texel.x,  texel.y)).rgb) * 1.0;
    return float4(SanitizeHDR(sum * (1.0 / 16.0)), 1.0);
}

// Composite the normalized pyramid with one tint and one linear intensity.
float4 PSApply(VSOutput input) : SV_Target {
    float4 sceneSample = u_SceneTexture.Sample(u_LinearSampler, input.TexCoord);
    float3 sceneColor = SanitizeHDR(sceneSample.rgb);
    float3 bloomColor = SanitizeHDR(u_InputTexture.Sample(u_LinearSampler, input.TexCoord).rgb);
    float3 bloomScale = max(u_BloomTint.rgb, 0.0) * max(u_BloomParams.y, 0.0);
    return float4(SanitizeHDR(sceneColor + bloomColor * bloomScale), sceneSample.a);
}
