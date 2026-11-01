// =============================================================================
// DefaultMaterial.hlsl - AxleEngine Standard PBR Forward Shader
// Direct3D 12 (DXIL) & Vulkan 1.3 (SPIR-V) Standard HLSL 2021 Codebase
// =============================================================================

// Per-Object Constants (Register b0, Space 0)
cbuffer ObjectConstants : register(b0, space0) {
    float4x4 u_Model;
    float4   u_BaseColor;
    float4   u_Params; // x: Roughness, y: Metallic, z: AO, w: Tiling
};

// Global Scene Constants (Register b1, Space 0)
cbuffer SceneConstants : register(b1, space0) {
    float4x4 u_ViewProjection;
    float4   u_CameraPosition;
    float4   u_SunDirection;
    float4   u_SunColor;
};

// Texture & Sampler Bindings
Texture2D    u_BaseMap : register(t0, space0);
SamplerState u_Sampler : register(s0, space0);

// Vertex Input & Pixel Input Structs
struct VSInput {
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
};

struct PSInput {
    float4 Position : SV_Position;
    float3 WorldPos : POSITION0;
    float3 Normal   : NORMAL0;
    float2 TexCoord : TEXCOORD0;
};

// -----------------------------------------------------------------------------
// Vertex Shader Entry Point
// -----------------------------------------------------------------------------
PSInput VSMain(VSInput input) {
    PSInput output;
    float4 worldPos = mul(u_Model, float4(input.Position, 1.0));
    output.Position = mul(u_ViewProjection, worldPos);
    output.WorldPos = worldPos.xyz;
    output.Normal   = normalize(mul((float3x3)u_Model, input.Normal));
    output.TexCoord = input.TexCoord * u_Params.w;
    return output;
}

// -----------------------------------------------------------------------------
// Pixel Shader Entry Point
// -----------------------------------------------------------------------------
float4 PSMain(PSInput input) : SV_Target0 {
    float4 texColor = u_BaseMap.Sample(u_Sampler, input.TexCoord);
    float3 albedo   = u_BaseColor.rgb * texColor.rgb;

    // Lambertian Diffuse + Directional Sun Lighting
    float3 N = normalize(input.Normal);
    float3 L = normalize(-u_SunDirection.xyz);
    float NdotL = max(dot(N, L), 0.0);

    float3 diffuse  = albedo * u_SunColor.rgb * NdotL;
    float3 ambient  = albedo * 0.15;
    float3 finalRgb = diffuse + ambient;

    return float4(finalRgb, u_BaseColor.a * texColor.a);
}
