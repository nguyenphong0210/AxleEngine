// =============================================================================
// ObjectID.hlsl - GPU Object ID Picking Shader (HLSL 2021)
// Standardized single-source cross-API shader for AxleEngine
// Compiles to Direct3D 12 DXIL and Vulkan 1.3 SPIR-V via RHIShaderCompiler
// Renders Entity ID (uint) to R32_UINT offscreen render target
// =============================================================================

// -----------------------------------------------------------------------------
// Root Constants (Register b0, Space 0)
// Size: EXACTLY 128 bytes (32 DWORDs)
// -----------------------------------------------------------------------------
cbuffer ObjectConstants : register(b0, space0) {
    float4x4 u_ModelViewProjection; // 64 bytes (Offset 0)
    uint     u_EntityID;            // 4 bytes  (Offset 64)
    uint3    u_Padding;             // 12 bytes (Offset 68)
    float4   u_Reserved[3];         // 48 bytes (Offset 80..128)
};

// -----------------------------------------------------------------------------
// Vertex Input: Standard 64-byte RHIVertex3D compatible layout
// -----------------------------------------------------------------------------
struct VSInput {
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR;
};

struct VSOutput {
    float4 Position : SV_Position;
};

// -----------------------------------------------------------------------------
// Vertex Shader: Transform to Clip-Space
// -----------------------------------------------------------------------------
VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.Position = mul(u_ModelViewProjection, float4(input.Position, 1.0f));
    return output;
}

// -----------------------------------------------------------------------------
// Pixel Shader: Return integer Entity ID directly to SV_Target0
// -----------------------------------------------------------------------------
uint PSMain(VSOutput input) : SV_Target0 {
    return u_EntityID;
}
