// =============================================================================
// SurfaceNormal.hlsl - Opaque and Cutout Surface Normal Producer
// Standardized HLSL 2021 for AxleEngine Multi-Backend (DXIL & SPIR-V)
// Matches ForwardPBR normal/detail decoding, strength, tangent frame and cutout.
// =============================================================================

#if defined(__spirv__)
struct ObjectConstants_t {
    float4x4 u_Model;
    float4   u_ColorPrimary;
    float4   u_ColorSecondary;
    float4   u_Params;
    float4   MaterialExtra;
};
[[vk::push_constant]] ObjectConstants_t ObjectConstants;
#define u_Model (ObjectConstants.u_Model)
#define u_ColorPrimary (ObjectConstants.u_ColorPrimary)
#define u_ColorSecondary (ObjectConstants.u_ColorSecondary)
#define u_Params (ObjectConstants.u_Params)
#define u_MaterialExtra (ObjectConstants.MaterialExtra)
#define u_UVOffset (ObjectConstants.MaterialExtra.xy)
#define u_AlphaCutoff (ObjectConstants.MaterialExtra.z)
#define u_SurfaceType (ObjectConstants.MaterialExtra.w)
#else
cbuffer ObjectConstants : register(b0, space0) {
    float4x4 u_Model;
    float4   u_ColorPrimary;
    float4   u_ColorSecondary;
    float4   u_Params;
    float4   u_MaterialExtra;
};
#define u_UVOffset (u_MaterialExtra.xy)
#define u_AlphaCutoff (u_MaterialExtra.z)
#define u_SurfaceType (u_MaterialExtra.w)
#endif

// Dedicated material data, matching RHIMaterialConstants exactly (256 bytes).
cbuffer MaterialConstants : register(b3, space0) {
    float4 u_MaterialUV;       // scale.xy, offset.xy
    float4 u_MaterialSurface;  // normal strength, AO strength, direct specular, environment reflections
    float4 u_CoatAnisotropy;   // clearcoat, coat roughness, anisotropy strength, rotation in turns
    uint4  u_MaterialFlags;    // texture mask, authored material, reserved, reserved
    float4 u_DetailUV;
    float4 u_DetailParameters;
    float4 u_Refraction;
    float4 u_RefractionTint;
    float4 u_RefractionFrame;
    float4 u_Sheen;
    float4 u_Subsurface;
    float4 u_Absorption;
    float4 u_MaterialReserved[4];
};
bool HasMaterialMap(uint slot) { return (u_MaterialFlags.x & (1u << slot)) != 0; }

Texture2D u_AlbedoMap       : register(t0, space1);
Texture2D u_NormalMap       : register(t1, space1);
Texture2D u_DetailNormalMap : register(t11, space1);
Texture2D u_DetailMaskMap   : register(t12, space1);
SamplerState u_Sampler      : register(s0);

struct PSInput {
    float4 PositionCS  : SV_Position;
    float3 WorldPos    : POSITION;
    float3 NormalWS    : NORMAL;
    float4 TangentWS   : TANGENT;
    float2 TexCoord    : TEXCOORD0;
    float4 VertexColor : COLOR0;
    float3 LocalPos    : TEXCOORD1;
};

// RG normal maps use the +Y convention; reconstruct Z before applying strength.
float3 DecodeMaterialNormal(float2 rg, float strength) {
    float2 xy = rg * 2.0 - 1.0;
    float z = sqrt(saturate(1.0 - dot(xy, xy)));
    float3 n = float3(xy * max(0.0, strength), max(z, 1e-4));
    return normalize(n);
}

float4 PSMain(PSInput input) : SV_Target0 {
    // 1. Cutout alpha test (SurfaceType == 1): discard fragment if alpha < cutoff
    int surfaceType = (int)(u_SurfaceType + 0.5);
    float alphaCutoff = saturate(u_AlphaCutoff);
    if (surfaceType == 1) {
        float tileScale = max(0.01, u_Params.x);
        float2 uv = (u_MaterialFlags.y != 0 ? input.TexCoord * u_MaterialUV.xy + u_MaterialUV.zw : input.TexCoord * tileScale + u_UVOffset);
        float4 albedoSample = (HasMaterialMap(0) ? u_AlbedoMap.Sample(u_Sampler, uv) : float4(1, 1, 1, 1));
        float4 albedo = albedoSample * u_ColorPrimary;
        if (albedo.a < alphaCutoff) {
            discard;
        }
    }

    // 2. Normal Vector & Tangent Space Perturbation (exact parity with ForwardPBR)
    float3 N = normalize(input.NormalWS);
    if ((HasMaterialMap(1) || HasMaterialMap(8)) && abs(input.TangentWS.w) > 0.1) {
        float3 T = input.TangentWS.xyz - N * dot(N, input.TangentWS.xyz);
        float3 fallbackTangent = abs(N.z) < 0.99 ? cross(float3(0,0,1), N) : cross(float3(0,1,0), N);
        T = dot(T,T)>1e-12 ? normalize(T) : normalize(fallbackTangent);
        float3 B = cross(N, T) * (input.TangentWS.w < 0.0 ? -1.0 : 1.0);
        float2 uv = input.TexCoord * u_MaterialUV.xy + u_MaterialUV.zw;
        float3 baseNormal = float3(0, 0, 1);
        if (HasMaterialMap(1)) baseNormal = DecodeMaterialNormal(u_NormalMap.Sample(u_Sampler, uv).rg, u_MaterialSurface.x);
        if (HasMaterialMap(8)) {
            float2 detailUV = input.TexCoord * u_DetailUV.xy + u_DetailUV.zw;
            float mask = HasMaterialMap(9) ? u_DetailMaskMap.Sample(u_Sampler, detailUV).r : 1.0;
            float3 detail = DecodeMaterialNormal(u_DetailNormalMap.Sample(u_Sampler, detailUV).rg, u_DetailParameters.x * saturate(mask));
            // Reoriented normal mapping: either flat input preserves the other layer.
            float3 t = baseNormal + float3(0, 0, 1);
            float3 u = detail * float3(-1, -1, 1);
            baseNormal = normalize(t * dot(t, u) / max(t.z, 1e-5) - u);
        }
        N = normalize(mul(baseNormal, float3x3(T, B, N)));
    }

    // 3. Return normalized world normal with validity marker 1.0 in w (background clear is 0.0)
    return float4(N, 1.0);
}
