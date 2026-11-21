// =============================================================================
// TAA.hlsl - Temporal Anti-Aliasing Resolve (M15-U8-D)
// Closest-3x3 depth & velocity dilation, RG16F camera+object motion vectors,
// Catmull-Rom 5-tap history reconstruction, Karis HDR YCoCg variance clipping,
// and disocclusion/out-of-bounds history rejection.
// =============================================================================

cbuffer TAAConstants : register(b0, space0) {
    float4x4 u_InvViewProj;
    float4x4 u_PrevViewProj;
    float4   u_ScreenSize;    // x: width, y: height, z: 1/width, w: 1/height
    float4   u_TAAParams;     // x: jitterX, y: jitterY, z: feedbackMin, w: feedbackMax
    float4   u_TAAFlags;      // x: historyValid, y: hasVelocityTex, z: reactiveScale, w: disocclusionThreshold
    float4   u_PreviousJitter;
    float4   u_Reserved[4];
};

Texture2D         u_CurrentColor    : register(t0, space1);
Texture2D<float>  u_DepthTexture    : register(t1, space1);
Texture2D         u_HistoryColor    : register(t2, space1);
Texture2D<float2> u_VelocityTexture : register(t3, space1);
Texture2D<float>  u_ReactiveMask    : register(t4, space1);
Texture2D<float>  u_HistoryDepth    : register(t5, space1);
Texture2D<float>  u_PreviousSurfaceDepth : register(t6, space1);
SamplerState      u_LinearSampler   : register(s0, space1);
SamplerState      u_PointSampler    : register(s1, space1);

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

float3 SanitizeHDR(float3 c) {
    return any(isnan(c)) || any(isinf(c)) ? float3(0.0, 0.0, 0.0) : min(max(c, 0.0), 65000.0);
}

float3 TonemapWeight(float3 c) {
    return c * rcp(1.0 + max(c.r, max(c.g, c.b)));
}

float3 InvTonemapWeight(float3 c) {
    return c * rcp(max(1.0 - max(c.r, max(c.g, c.b)), 1e-6));
}

float3 RGBToYCoCg(float3 rgb) {
    float Y  = dot(rgb, float3(0.25, 0.50, 0.25));
    float Co = dot(rgb, float3(0.50, 0.00, -0.50));
    float Cg = dot(rgb, float3(-0.25, 0.50, -0.25));
    return float3(Y, Co, Cg);
}

float3 YCoCgToRGB(float3 ycocg) {
    float t = ycocg.x - ycocg.z;
    return max(float3(t + ycocg.y, ycocg.x + ycocg.z, t - ycocg.y), 0.0);
}

float3 SampleHistoryCatmullRom(float2 uv) {
    float2 texSize = u_ScreenSize.xy;
    float2 samplePos = uv * texSize;
    float2 tc1 = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - tc1;
    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float2 w0 = f2 - 0.5 * (f3 + f);
    float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    float2 w3 = 0.5 * (f3 - f2);
    float2 w2 = 1.0 - w0 - w1 - w3;

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / max(w12, 1e-5);
    float2 tc0  = clamp((tc1 - 1.0) * u_ScreenSize.zw, 0.0, 1.0);
    float2 tc3  = clamp((tc1 + 2.0) * u_ScreenSize.zw, 0.0, 1.0);
    float2 tc12 = clamp((tc1 + offset12) * u_ScreenSize.zw, 0.0, 1.0);

    float3 s0 = SanitizeHDR(u_HistoryColor.SampleLevel(u_LinearSampler, float2(tc12.x, tc0.y),  0).rgb);
    float3 s1 = SanitizeHDR(u_HistoryColor.SampleLevel(u_LinearSampler, float2(tc0.x,  tc12.y), 0).rgb);
    float3 s2 = SanitizeHDR(u_HistoryColor.SampleLevel(u_LinearSampler, float2(tc12.x, tc12.y), 0).rgb);
    float3 s3 = SanitizeHDR(u_HistoryColor.SampleLevel(u_LinearSampler, float2(tc3.x,  tc12.y), 0).rgb);
    float3 s4 = SanitizeHDR(u_HistoryColor.SampleLevel(u_LinearSampler, float2(tc12.x, tc3.y),  0).rgb);

    float wt0 = w12.x * w0.y;
    float wt1 = w0.x  * w12.y;
    float wt2 = w12.x * w12.y;
    float wt3 = w3.x  * w12.y;
    float wt4 = w12.x * w3.y;

    float wSum = max(wt0 + wt1 + wt2 + wt3 + wt4, 1e-5);
    return max((s0 * wt0 + s1 * wt1 + s2 * wt2 + s3 * wt3 + s4 * wt4) / wSum, 0.0);
}

bool ReprojectCamera(float2 uv, float depth, out float2 velocity, out float previousDepth) {
    float2 sampleUV = uv - u_TAAParams.xy * float2(-1.0, 1.0);
    float4 clip = float4(sampleUV * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0);
    float4 world = mul(u_InvViewProj, clip);
    float4 previous = mul(u_PrevViewProj, world);
    velocity = float2(0.0, 0.0);
    previousDepth = -1.0;
    if (world.w <= 1e-7 || previous.w <= 1e-7) return false;
    float3 previousNDC = previous.xyz / previous.w;
    velocity = sampleUV - (previousNDC.xy * float2(0.5, -0.5) + 0.5);
    previousDepth = (depth >= 1.0) ? 1.0 : previousNDC.z;
    return all(isfinite(velocity)) && isfinite(previousDepth);
}

bool MatchesHistoryDepth(float2 uv, float2 velocity, float previousDepth) {
    if (!all(isfinite(velocity)) || !isfinite(previousDepth) || previousDepth < 0.0 || previousDepth > 1.0)
        return false;
    // Depth is still on the previous raster grid, whose jitter differs from
    // the stabilized color history coordinate system.
    float2 jitterDelta = (u_PreviousJitter.xy - u_TAAParams.xy) * float2(-1.0, 1.0);
    float2 previousUV = uv - velocity + jitterDelta;
    if (any(previousUV < 0.0) || any(previousUV > 1.0)) return false;
    float historyDepth = u_HistoryDepth.SampleLevel(u_PointSampler, previousUV, 0);
    if (!isfinite(historyDepth) || ((historyDepth >= 1.0) != (previousDepth >= 1.0))) return false;
    float tolerance = max(2e-5, u_TAAFlags.w * (1.0 - previousDepth));
    return abs(historyDepth - previousDepth) <= tolerance;
}

float4 PSMain(VSOutput input) : SV_Target {
    float2 uv = input.TexCoord;
    float2 texelSize = u_ScreenSize.zw;
    float4 currentSample = u_CurrentColor.SampleLevel(u_PointSampler, uv, 0);
    float3 currentCenter = SanitizeHDR(currentSample.rgb);
    float4 current = float4(currentCenter, currentSample.a);

    if (u_TAAFlags.x < 0.5) return current;

    float depth = u_DepthTexture.SampleLevel(u_PointSampler, uv, 0);
    float2 centerVelocity;
    float previousDepth;
    if (u_TAAFlags.y > 0.5) {
        centerVelocity = u_VelocityTexture.SampleLevel(u_PointSampler, uv, 0);
        previousDepth = u_PreviousSurfaceDepth.SampleLevel(u_PointSampler, uv, 0);
    } else if (!ReprojectCamera(uv, depth, centerVelocity, previousDepth)) {
        return current;
    }
    if ((depth >= 1.0) != (previousDepth >= 1.0) ||
        !MatchesHistoryDepth(uv, centerVelocity, previousDepth)) return current;

    // 1. Closest 3x3 depth tap (forward-Z smallest depth) + YCoCg neighborhood statistics
    float closestDepth = 1.0;
    float2 closestOffset = float2(0.0, 0.0);
    float3 m1 = float3(0.0, 0.0, 0.0);
    float3 m2 = float3(0.0, 0.0, 0.0);
    float3 boxMin = float3(1e5, 1e5, 1e5);
    float3 boxMax = float3(-1e5, -1e5, -1e5);
    float reactiveMax = 0.0;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            float2 off = float2((float)dx, (float)dy);
            float2 tapUV = uv + off * texelSize;
            float d = u_DepthTexture.SampleLevel(u_PointSampler, tapUV, 0);
            if (d < closestDepth) {
                closestDepth = d;
                closestOffset = off;
            }
            float3 sampleRGB = SanitizeHDR(u_CurrentColor.SampleLevel(u_PointSampler, tapUV, 0).rgb);
            float3 ycocg = RGBToYCoCg(TonemapWeight(sampleRGB));
            m1 += ycocg;
            m2 += ycocg * ycocg;
            boxMin = min(boxMin, ycocg);
            boxMax = max(boxMax, ycocg);
            reactiveMax = max(reactiveMax, u_ReactiveMask.SampleLevel(u_PointSampler, tapUV, 0));
        }
    }

    // 2. Dilated velocity lookup at closest 3x3 depth texel (camera + per-object motion)
    float2 velocity = float2(0.0, 0.0);
    if (u_TAAFlags.y > 0.5) {
        velocity = u_VelocityTexture.SampleLevel(u_PointSampler, uv + closestOffset * texelSize, 0).xy;
    } else {
        float unusedDepth;
        if (!ReprojectCamera(uv + closestOffset * texelSize, closestDepth, velocity, unusedDepth))
            return current;
    }

    // A nearest foreground tap can belong to a different surface. Keep its
    // dilated motion only when it points to history at the center surface depth.
    if (any(closestOffset != 0.0) && !MatchesHistoryDepth(uv, velocity, previousDepth))
        velocity = centerVelocity;

    float2 historyUV = uv - velocity;
    if (any(historyUV < 0.0) || any(historyUV > 1.0)) return current;

    // 3. Variance-clipped YCoCg rejection & disocclusion gating
    float3 mu = m1 / 9.0;
    float3 sigma = sqrt(max(m2 / 9.0 - mu * mu, 0.0));
    float gamma = 1.25;
    float3 varianceMin = max(boxMin, mu - gamma * sigma);
    float3 varianceMax = min(boxMax, mu + gamma * sigma);

    float3 historyRGB = SampleHistoryCatmullRom(historyUV);
    float3 historyYCoCg = RGBToYCoCg(TonemapWeight(historyRGB));
    float3 clampedHistoryYCoCg = clamp(historyYCoCg, varianceMin, varianceMax);

    float3 outOfBox = max(float3(0.0, 0.0, 0.0), max(boxMin - historyYCoCg, historyYCoCg - boxMax));
    float boxSpread = max(length(sigma), 0.02);
    float disocclusionRatio = length(outOfBox) / boxSpread;
    float disocclusionGate = exp(-disocclusionRatio * 2.5);

    float speedPx = length(velocity * u_ScreenSize.xy);
    float motionFactor = saturate(speedPx * 0.25);
    float reactive = saturate(reactiveMax * saturate(u_TAAFlags.z));
    float effectiveMax = lerp(u_TAAParams.w, 0.08, reactive);
    float effectiveMin = lerp(u_TAAParams.z, 0.04, reactive);
    float baseFeedback = min(lerp(effectiveMax, effectiveMin, motionFactor), effectiveMax);
    float historyWeight = clamp(baseFeedback * disocclusionGate, 0.0, 0.96);

    float3 currentYCoCg = RGBToYCoCg(TonemapWeight(currentCenter));
    float3 blendedYCoCg = lerp(currentYCoCg, clampedHistoryYCoCg, historyWeight);
    float3 resolved = InvTonemapWeight(YCoCgToRGB(blendedYCoCg));
    return float4(SanitizeHDR(resolved), current.a);
}
