// =============================================================================
// ToneMapping.hlsl - Unified D3D12 (DXIL) & Vulkan 1.3 (SPIR-V) Post-Processing Pass
// Standardized HLSL 2021 Codebase for AxleEngine Film-Grade Tone Mapping & Exposure
// =============================================================================

// -----------------------------------------------------------------------------
// Constant Buffer (b0, space 0) - Hardware 256-byte CBV aligned
// -----------------------------------------------------------------------------
cbuffer ToneMappingConstants : register(b0, space0) {
    float4 u_Params;       // x: Exposure, y: Gamma, z: ToneMappingMode, w: WhitePoint
    float4 u_ColorGrading; // x: Contrast, y: Saturation, z: VignetteIntensity, w: Reserved
    float4 u_Reserved[14]; // 224 bytes padding -> Total: 256 bytes (CBV aligned)
};

// -----------------------------------------------------------------------------
// Descriptor Set (Space 1)
// Texture t0, Sampler s0
// -----------------------------------------------------------------------------
Texture2D    u_HDRTexture    : register(t0, space1);
SamplerState u_LinearSampler : register(s0, space1);

// -----------------------------------------------------------------------------
// Vertex & Pixel Shader I/O
// -----------------------------------------------------------------------------
struct VSOutput {
    float4 PositionCS : SV_Position;
    float2 TexCoord   : TEXCOORD0;
};

// =============================================================================
// VERTEX SHADER - Fullscreen Triangle via SV_VertexID (Zero Vertex Buffers)
// =============================================================================
VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    // Generate fullscreen triangle covering [-1, 1] x [-1, 1] in clip space
    // vertexID 0: uv (0, 0) -> ndc (-1,  1) [Top-Left]
    // vertexID 1: uv (2, 0) -> ndc ( 3,  1) [Top-Right]
    // vertexID 2: uv (0, 2) -> ndc (-1, -3) [Bottom-Left]
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.TexCoord = uv;
    output.PositionCS = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

// =============================================================================
// FILM-GRADE TONE MAPPING CURVES
// =============================================================================

// 1. ACES Filmic Tone Mapping (Krzysztof Narkowicz analytical approximation)
// Widely adopted in AAA games & Academy Color Encoding System
float3 ToneMapACES(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// 2. Extended Reinhard Operator with Configurable White Point W
// f(x) = (x * (1 + x / (W * W))) / (1 + x)
float3 ToneMapReinhardExtended(float3 x, float whitePoint) {
    float w2 = max(whitePoint * whitePoint, 0.01);
    return saturate((x * (1.0 + x / w2)) / (1.0 + x));
}

// 3. Uncharted 2 Filmic Curve (John Hable / Naughty Dog)
// Provides toe, linear segment, and shoulder with custom white point tuning
float3 Uncharted2Tonemap(float3 x) {
    const float A = 0.15; // Shoulder Strength
    const float B = 0.50; // Linear Strength
    const float C = 0.10; // Linear Angle
    const float D = 0.20; // Toe Strength
    const float E = 0.02; // Toe Numerator
    const float F = 0.30; // Toe Denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - (E / F);
}

float3 ToneMapUncharted2(float3 x, float whitePoint) {
    const float exposureBias = 2.0;
    float3 curr = Uncharted2Tonemap(x * exposureBias);
    float W = (whitePoint > 0.01) ? whitePoint : 11.2;
    float3 whiteScale = 1.0 / Uncharted2Tonemap(W.xxx);
    return saturate(curr * whiteScale);
}

// 4. Color Grading (Contrast, Saturation & Subtle Vignette)
float3 ApplyColorGrading(float3 color, float2 uv, float contrast, float saturation, float vignette) {
    // Contrast (around midpoint 0.5)
    if (abs(contrast - 1.0) > 0.001) {
        color = saturate((color - 0.5) * contrast + 0.5);
    }
    // Saturation (Rec.709 Luma: 0.2126 R + 0.7152 G + 0.0722 B)
    if (abs(saturation - 1.0) > 0.001) {
        float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
        color = saturate(lerp(luma.xxx, color, saturation));
    }
    // Vignette
    if (vignette > 0.001) {
        float2 coord = (uv - 0.5) * 2.0;
        float dist = length(coord);
        float vig = smoothstep(0.8, 0.8 - vignette * 0.45, dist);
        color *= vig;
    }
    return color;
}

// =============================================================================
// PIXEL SHADER - Photometric Exposure, Tone Mapping & sRGB Display EOTF
// =============================================================================
float4 PSMain(VSOutput input) : SV_Target {
    // Sample raw floating-point HDR scene radiance
    float4 hdrSample = u_HDRTexture.Sample(u_LinearSampler, input.TexCoord);
    float3 hdrColor = hdrSample.rgb;

    // 1. Photometric Exposure Compensation
    float exposure = (u_Params.x > 0.0) ? u_Params.x : 1.0;
    float3 exposed = hdrColor * exposure;

    // 2. Film-Grade Tone Mapping Operator Selection
    int mode = (int)u_Params.z;
    float whitePoint = (u_Params.w > 0.0) ? u_Params.w : 4.0;
    float3 mappedColor;

    if (mode == 0) {
        // Mode 0: ACES Filmic (Narkowicz fit)
        mappedColor = ToneMapACES(exposed);
    } else if (mode == 1) {
        // Mode 1: Extended Reinhard
        mappedColor = ToneMapReinhardExtended(exposed, whitePoint);
    } else if (mode == 2) {
        // Mode 2: Uncharted 2 (John Hable)
        mappedColor = ToneMapUncharted2(exposed, whitePoint);
    } else {
        // Mode 3: Linear Bypass (clamped to [0, 1])
        mappedColor = saturate(exposed);
    }

    // 3. Optional Color Grading Polish
    float contrast = (u_ColorGrading.x > 0.0) ? u_ColorGrading.x : 1.0;
    float saturation = (u_ColorGrading.y > 0.0) ? u_ColorGrading.y : 1.0;
    float vignette = u_ColorGrading.z;
    mappedColor = ApplyColorGrading(mappedColor, input.TexCoord, contrast, saturation, vignette);

    // 4. Gamma Correction / Display EOTF Transfer Function (sRGB Gamma 2.2)
    float gamma = (u_Params.y > 0.0) ? u_Params.y : 2.2;
    float3 srgbColor = pow(max(mappedColor, float3(0.0, 0.0, 0.0)), 1.0 / gamma);

    return float4(srgbColor, hdrSample.a);
}
