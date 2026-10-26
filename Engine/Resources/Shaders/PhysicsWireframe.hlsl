// =============================================================================
// AxleEngine - Modern Cross-API Physics Wireframe & Debug Shader (HLSL 2021)
// Line-Topology Vertex and Fragment Shader for Jolt Physics Debug Visualization
// =============================================================================

struct VSInput {
    float3 Position : POSITION;
    float4 Color    : COLOR;
};

struct PSInput {
    float4 Position : SV_Position;
    float4 Color    : COLOR;
};

// =============================================================================
// Constant Buffer (256-Byte Hardware Aligned)
// =============================================================================
cbuffer PhysicsDebugConstants : register(b0, space0) {
    float4x4 ViewProjection;     // 64 bytes (offset 0)
    float4   DepthBiasParams;    // 16 bytes (x = DepthBias, y = AlphaMultiplier, z = Reserved, w = Reserved) (offset 64)
    float4   Reserved[11];       // 176 bytes padding -> total 256 bytes (offset 80..256)
};

// =============================================================================
// Vertex Shader: Line-List Transform & Depth-Bias Correction
// =============================================================================
PSInput VSMain(VSInput input) {
    PSInput output;
    float4 clipPos = mul(ViewProjection, float4(input.Position, 1.0f));

    // Clip-space depth bias towards near plane prevents z-fighting with opaque scene geometry
    clipPos.z -= DepthBiasParams.x * clipPos.w;

    output.Position = clipPos;
    output.Color = input.Color;
    output.Color.a *= DepthBiasParams.y;

    return output;
}

// =============================================================================
// Pixel Shader: Direct Color Output with Wireframe Alpha
// =============================================================================
float4 PSMain(PSInput input) : SV_Target {
    return input.Color;
}
