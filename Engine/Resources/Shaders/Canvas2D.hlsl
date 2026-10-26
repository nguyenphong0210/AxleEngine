// =============================================================================
// Canvas2D.hlsl - Unified D3D12 (DXIL) & Vulkan 1.3 (SPIR-V) 2D Batch Shader
// Procedural Signed Distance Field (SDF) Uber-Shader for In-Game UI Subsystem
// =============================================================================

// -----------------------------------------------------------------------------
// Constant Buffer (b0, space 0) - Hardware 256-byte CBV aligned
// -----------------------------------------------------------------------------
cbuffer Canvas2DConstants : register(b0, space0) {
    float4 ViewportParams; // x = ViewportWidth, y = ViewportHeight, z = DPIScale, w = Reserved
    float4 Reserved[15];   // 240 bytes padding -> total 256 bytes (CBV aligned)
};

// -----------------------------------------------------------------------------
// Texture Array (Space 1) & Sampler
// -----------------------------------------------------------------------------
Texture2D    u_Textures[32] : register(t0, space1);
SamplerState u_Sampler      : register(s0, space1);

// -----------------------------------------------------------------------------
// Vertex & Pixel Shader I/O
// -----------------------------------------------------------------------------
struct VSInput {
    float2 ScreenPos     : POSITION;
    float2 UV            : TEXCOORD0;
    float4 Color         : COLOR0;
    float4 BorderColor   : COLOR1;
    float2 QuadSize      : TEXCOORD1;
    float  CornerRadius  : RADIUS;
    float  BorderWidth   : BORDER;
    float2 AngleRange    : ANGLERANGE;
    uint   TextureIndex  : TEXINDEX;
    uint   PrimitiveType : PRIMTYPE;
};

struct PSInput {
    float4 PositionCS    : SV_Position;
    float2 UV            : TEXCOORD0;
    float4 Color         : COLOR0;
    float4 BorderColor   : COLOR1;
    float2 QuadSize      : TEXCOORD1;
    float  CornerRadius  : RADIUS;
    float  BorderWidth   : BORDER;
    float2 AngleRange    : ANGLERANGE;
    nointerpolation uint TextureIndex  : TEXINDEX;
    nointerpolation uint PrimitiveType : PRIMTYPE;
};

// =============================================================================
// VERTEX SHADER
// =============================================================================
PSInput VSMain(VSInput input) {
    PSInput output;
    float2 screenPos = input.ScreenPos;
    float2 vp = ViewportParams.xy;
    if (vp.x <= 0.0) vp.x = 1920.0;
    if (vp.y <= 0.0) vp.y = 1080.0;

    output.PositionCS.x = (screenPos.x / vp.x) * 2.0 - 1.0;
    output.PositionCS.y = 1.0 - (screenPos.y / vp.y) * 2.0;
    output.PositionCS.z = 0.0;
    output.PositionCS.w = 1.0;

    output.UV = input.UV;
    output.Color = input.Color;
    output.BorderColor = input.BorderColor;
    output.QuadSize = input.QuadSize;
    output.CornerRadius = input.CornerRadius;
    output.BorderWidth = input.BorderWidth;
    output.AngleRange = input.AngleRange;
    output.TextureIndex = input.TextureIndex;
    output.PrimitiveType = input.PrimitiveType;

    return output;
}

// =============================================================================
// PIXEL SHADER - Procedural SDF Evaluation
// =============================================================================
float4 PSMain(PSInput input) : SV_Target {
    // 0: SDF_Rect (Rounded Box with Anti-Aliased Border)
    if (input.PrimitiveType == 0) {
        float2 p = (input.UV - 0.5) * input.QuadSize;
        float2 b = 0.5 * input.QuadSize;
        float r = min(input.CornerRadius, min(b.x, b.y));
        float2 q = abs(p) - b + r;
        float d_outer = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
        float alpha_outer = saturate(0.5 - d_outer);

        float4 finalColor = input.Color;
        if (input.BorderWidth > 0.0) {
            float d_inner = d_outer + input.BorderWidth;
            float t_border = saturate(0.5 - d_inner);
            finalColor = lerp(input.BorderColor, input.Color, t_border);
        }
        return finalColor * float4(1.0, 1.0, 1.0, alpha_outer);
    }

    // 1: Sprite (Bindless Texture Blit)
    if (input.PrimitiveType == 1) {
        uint texIdx = min(input.TextureIndex, 31u);
        float4 texColor = u_Textures[NonUniformResourceIndex(texIdx)].Sample(u_Sampler, input.UV);
        return texColor * input.Color;
    }

    // 2: MSDF_Text (Multi-Channel Signed Distance Field Text)
    if (input.PrimitiveType == 2) {
        uint texIdx = min(input.TextureIndex, 31u);
        float3 msd = u_Textures[NonUniformResourceIndex(texIdx)].Sample(u_Sampler, input.UV).rgb;
        float sd = max(min(msd.r, msd.g), min(max(msd.r, msd.g), msd.b)) - 0.5;
        float2 unitRange = float2(4.0, 4.0) / max(input.QuadSize, float2(1.0, 1.0));
        float2 screenTexSize = float2(1.0, 1.0) / max(fwidth(input.UV), float2(1e-4, 1e-4));
        float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
        float opacity = saturate(sd * screenPxRange + 0.5);
        return input.Color * float4(1.0, 1.0, 1.0, opacity);
    }

    // 3: SDF_RadialArc (Automotive Tachometer & Circular Gauge)
    if (input.PrimitiveType == 3) {
        float2 p = (input.UV - 0.5) * input.QuadSize;
        float rho = length(p);
        float outerR = min(input.QuadSize.x, input.QuadSize.y) * 0.5;
        float thickness = max(input.CornerRadius, 1.0);
        float ringDist = abs(rho - (outerR - 0.5 * thickness)) - 0.5 * thickness;
        float ringAlpha = saturate(0.5 - ringDist);

        float angleRad = atan2(p.y, p.x);
        float startRad = input.AngleRange.x;
        float endRad = input.AngleRange.y;
        float activeRad = input.BorderWidth; // Active Angle (rad)

        const float TWO_PI = 6.28318530718;
        float relAngle = fmod(angleRad - startRad + TWO_PI * 2.0, TWO_PI);
        float totalSpan = fmod(endRad - startRad + TWO_PI * 2.0, TWO_PI);
        if (totalSpan <= 0.001) totalSpan = TWO_PI;
        float activeSpan = fmod(activeRad - startRad + TWO_PI * 2.0, TWO_PI);

        float4 arcColor = float4(0.0, 0.0, 0.0, 0.0);
        if (relAngle <= activeSpan) {
            arcColor = input.Color;
        } else if (relAngle <= totalSpan) {
            arcColor = input.BorderColor;
        }
        return arcColor * float4(1.0, 1.0, 1.0, ringAlpha);
    }

    return input.Color;
}
