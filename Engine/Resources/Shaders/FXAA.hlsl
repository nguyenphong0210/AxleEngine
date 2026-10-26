// =============================================================================
// AxleEngine - High-Performance Screen-Space Anti-Aliasing (FXAA 3.11 Quality)
// Dual-compiled for Direct3D 12 (DXIL 6.0) & Vulkan 1.3 (SPIR-V)
// =============================================================================

cbuffer FXAAConstants : register(b0, space0) {
    float4 u_ScreenSize;      // (width, height, 1.0 / width, 1.0 / height)
    float4 u_FXAAParams;      // (subpixQuality, edgeThreshold, edgeThresholdMin, qualityPreset)
    float4 u_Padding[14];     // Pad to exactly 256 bytes for hardware CBV alignment
};

Texture2D u_InputTexture : register(t0, space1);
SamplerState u_LinearSampler : register(s0, space1);

struct VSOutput {
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

// Fullscreen triangle via SV_VertexID unprojection (zero vertex buffer overhead)
VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    output.TexCoord = float2(uv.x, 1.0f - uv.y);
    return output;
}

// Perceptual Rec. 601 / Rec. 709 luma calculation
float FxaaLuma(float3 rgb) {
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

// FXAA 3.11 Quality Implementation
float4 PSMain(VSOutput input) : SV_Target {
    float2 posM = input.TexCoord;
    float2 rcpFrame = u_ScreenSize.zw;

    float subpixQuality = u_FXAAParams.x > 0.0 ? u_FXAAParams.x : 0.75;
    float edgeThreshold = u_FXAAParams.y > 0.0 ? u_FXAAParams.y : 0.166;
    float edgeThresholdMin = u_FXAAParams.z > 0.0 ? u_FXAAParams.z : 0.0833;

    // 1. Center and cardinal samples
    float4 colorM = u_InputTexture.SampleLevel(u_LinearSampler, posM, 0.0);
    float lumaM = FxaaLuma(colorM.rgb);

    float lumaN = FxaaLuma(u_InputTexture.SampleLevel(u_LinearSampler, posM + float2(0.0, -rcpFrame.y), 0.0).rgb);
    float lumaS = FxaaLuma(u_InputTexture.SampleLevel(u_LinearSampler, posM + float2(0.0,  rcpFrame.y), 0.0).rgb);
    float lumaW = FxaaLuma(u_InputTexture.SampleLevel(u_LinearSampler, posM + float2(-rcpFrame.x, 0.0), 0.0).rgb);
    float lumaE = FxaaLuma(u_InputTexture.SampleLevel(u_LinearSampler, posM + float2( rcpFrame.x, 0.0), 0.0).rgb);

    // 2. Local contrast range
    float rangeMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float rangeMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float range = rangeMax - rangeMin;

    // Early exit if contrast is below edge detection threshold
    if (range < max(edgeThresholdMin, rangeMax * edgeThreshold)) {
        return colorM;
    }

    // 3. Diagonal corner samples for sub-pixel aliasing and edge direction
    float lumaNW = FxaaLuma(u_InputTexture.SampleLevel(u_LinearSampler, posM + float2(-rcpFrame.x, -rcpFrame.y), 0.0).rgb);
    float lumaNE = FxaaLuma(u_InputTexture.SampleLevel(u_LinearSampler, posM + float2( rcpFrame.x, -rcpFrame.y), 0.0).rgb);
    float lumaSW = FxaaLuma(u_InputTexture.SampleLevel(u_LinearSampler, posM + float2(-rcpFrame.x,  rcpFrame.y), 0.0).rgb);
    float lumaSE = FxaaLuma(u_InputTexture.SampleLevel(u_LinearSampler, posM + float2( rcpFrame.x,  rcpFrame.y), 0.0).rgb);

    // 4. Sub-pixel filter calculation (lowpass filter difference)
    float lumaL = (lumaN + lumaS + lumaW + lumaE) * 2.0 + (lumaNW + lumaNE + lumaSW + lumaSE);
    float subpixBlend = clamp(abs(lumaL / 12.0 - lumaM) / range, 0.0, 1.0);
    subpixBlend = smoothstep(0.0, 1.0, subpixBlend);
    subpixBlend = subpixBlend * subpixBlend * subpixQuality;

    // 5. Edge orientation (horizontal vs vertical gradient)
    float edgeHorz = abs((lumaNW + lumaNE) - 2.0 * lumaN) +
                     2.0 * abs((lumaW + lumaE) - 2.0 * lumaM) +
                     abs((lumaSW + lumaSE) - 2.0 * lumaS);

    float edgeVert = abs((lumaNW + lumaSW) - 2.0 * lumaW) +
                     2.0 * abs((lumaN + lumaS) - 2.0 * lumaM) +
                     abs((lumaNE + lumaSE) - 2.0 * lumaE);

    bool isHorizontal = (edgeHorz >= edgeVert);

    // 6. Select perpendicular step and gradient
    float luma1 = isHorizontal ? lumaN : lumaW;
    float luma2 = isHorizontal ? lumaS : lumaE;
    float gradient1 = abs(luma1 - lumaM);
    float gradient2 = abs(luma2 - lumaM);

    float stepLength = isHorizontal ? rcpFrame.y : rcpFrame.x;
    float lumaGrad = 0.0;
    if (gradient1 >= gradient2) {
        stepLength = -stepLength;
        lumaGrad = gradient1;
    } else {
        lumaGrad = gradient2;
    }

    // 7. Edge search along tangent
    float2 posEdge = posM;
    if (isHorizontal) {
        posEdge.y += stepLength * 0.5;
    } else {
        posEdge.x += stepLength * 0.5;
    }

    float2 stepDir = isHorizontal ? float2(rcpFrame.x, 0.0) : float2(0.0, rcpFrame.y);

    float2 posNeg = posEdge - stepDir;
    float2 posPos = posEdge + stepDir;

    float lumaNeg = FxaaLuma(u_InputTexture.SampleLevel(u_LinearSampler, posNeg, 0.0).rgb);
    float lumaPos = FxaaLuma(u_InputTexture.SampleLevel(u_LinearSampler, posPos, 0.0).rgb);

    float gradientThreshold = lumaGrad * 0.5;
    bool doneNeg = abs(lumaNeg - lumaM) >= gradientThreshold;
    bool donePos = abs(lumaPos - lumaM) >= gradientThreshold;

    // Fixed search iterations (Quality 3.11 standard)
    [unroll]
    for (int i = 0; i < 8; i++) {
        if (!doneNeg) {
            posNeg -= stepDir * 1.5;
            lumaNeg = FxaaLuma(u_InputTexture.SampleLevel(u_LinearSampler, posNeg, 0.0).rgb);
            doneNeg = abs(lumaNeg - lumaM) >= gradientThreshold;
        }
        if (!donePos) {
            posPos += stepDir * 1.5;
            lumaPos = FxaaLuma(u_InputTexture.SampleLevel(u_LinearSampler, posPos, 0.0).rgb);
            donePos = abs(lumaPos - lumaM) >= gradientThreshold;
        }
        if (doneNeg && donePos) break;
    }

    // 8. Distances to endpoints
    float distNeg = isHorizontal ? (posM.x - posNeg.x) : (posM.y - posNeg.y);
    float distPos = isHorizontal ? (posPos.x - posM.x) : (posPos.y - posM.y);

    float edgeBlend = 0.0;
    if (distNeg < distPos) {
        edgeBlend = 0.5 - (distNeg / (distNeg + distPos));
    } else {
        edgeBlend = 0.5 - (distPos / (distNeg + distPos));
    }

    // 9. Final blend factor: maximum of sub-pixel blend and edge blend
    float finalBlend = max(subpixBlend, edgeBlend);

    float2 finalUV = posM;
    if (isHorizontal) {
        finalUV.y += stepLength * finalBlend;
    } else {
        finalUV.x += stepLength * finalBlend;
    }

    return u_InputTexture.SampleLevel(u_LinearSampler, finalUV, 0.0);
}
