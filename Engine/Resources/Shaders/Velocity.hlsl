// =============================================================================
// Velocity.hlsl - Screen-Space Camera & Object Motion Vector Pass (M15-U8-C)
// Unified HLSL 2021 for D3D12 (DXIL 6.0) & Vulkan 1.3 (SPIR-V)
//
// Outputs RG16_FLOAT UV velocity: velocity = currUV - prevUV
// so history sampling computes: prevUV = currUV - velocity.
// Jitter is included in SV_Position (matching Opaque depth buffer), while
// velocity uses UNJITTERED current & previous ViewProjection matrices so
// static geometry under sub-pixel jitter produces exact (0, 0) velocity.
// =============================================================================

#if defined(__spirv__)
struct ObjectVelocityConstants_t {
    float4x4 u_CurrModel;
    float4x4 u_PrevModel;
};
[[vk::push_constant]] ObjectVelocityConstants_t VelocityObjectConstants;
#define u_CurrModel (VelocityObjectConstants.u_CurrModel)
#define u_PrevModel (VelocityObjectConstants.u_PrevModel)
#else
cbuffer VelocityObjectConstants : register(b0, space0) {
    float4x4 u_CurrModel;
    float4x4 u_PrevModel;
};
#endif

cbuffer VelocitySceneConstants : register(b1, space0) {
    float4x4 u_CurrJitteredVP;
    float4x4 u_CurrUnjitteredVP;
    float4x4 u_PrevUnjitteredVP;
    float4x4 u_InvCurrUnjitteredVP;
    float4   u_VelocityParams;      // x: historyValid (1/0), y: width, z: height, w: reserved
    float4   u_VelocityReserved[15];
};

cbuffer MaterialConstants : register(b3, space0) {
    float4 u_MaterialUV;
    float4 u_MaterialSurface;
    float4 u_CoatAnisotropy;
    uint4  u_MaterialFlags;
    float4 u_DetailUV;
    float4 u_DetailParameters;
    float4 u_Refraction;
    float4 u_RefractionTint;
    float4 u_RefractionFrame;
    float4 u_Sheen;
    float4 u_Subsurface;
    float4 u_Absorption;
    float4 u_MaterialReserved[4]; // [2]: (uvOffsetX, uvOffsetY, 0, 0), [3]: (albedoAlpha, alphaCutoff, surfaceType, tileScale)
};

Texture2D u_AlbedoMap : register(t0, space1);
SamplerState u_Sampler : register(s0, space0);

float2 ComputeUVFromClip(float4 clipPos) {
    float2 ndc = clipPos.xy / max(abs(clipPos.w), 1e-6);
    return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

// -----------------------------------------------------------------------------
// Background / Camera-Only Velocity Pass (Far Plane)
// -----------------------------------------------------------------------------
struct BgVSOutput {
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

BgVSOutput VSBackground(uint vertexID : SV_VertexID) {
    BgVSOutput output;
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 1.0, 1.0);
    return output;
}

float2 PSBackground(BgVSOutput input) : SV_Target0 {
    if (u_VelocityParams.x < 0.5) {
        return float2(0.0, 0.0);
    }
    float2 ndc = float2(input.TexCoord.x * 2.0 - 1.0, 1.0 - input.TexCoord.y * 2.0);
    float4 worldH = mul(u_InvCurrUnjitteredVP, float4(ndc, 1.0, 1.0));
    if (abs(worldH.w) < 1e-7) {
        return float2(0.0, 0.0);
    }
    float3 worldPos = worldH.xyz / worldH.w;
    float4 prevClip = mul(u_PrevUnjitteredVP, float4(worldPos, 1.0));
    if (prevClip.w <= 1e-6) {
        return float2(0.0, 0.0);
    }
    float2 prevUV = ComputeUVFromClip(prevClip);
    float2 velocity = input.TexCoord - prevUV;
    return any(isnan(velocity)) || any(isinf(velocity)) ? float2(0.0, 0.0) : velocity;
}

// -----------------------------------------------------------------------------
// Per-Object Opaque & Cutout Velocity Pass
// -----------------------------------------------------------------------------
struct MeshVSInput {
    float3 Position   : POSITION;
    float3 Normal     : NORMAL;
    float4 Tangent    : TANGENT;
    float2 TexCoord   : TEXCOORD0;
    float4 Color      : COLOR0;
};

struct MeshVSOutput {
    float4 PositionCS  : SV_Position;
    float4 CurrClipPos : TEXCOORD0;
    float4 PrevClipPos : TEXCOORD1;
    float2 TexCoord    : TEXCOORD2;
};

MeshVSOutput VSMesh(MeshVSInput input) {
    MeshVSOutput output;
    float4 currWorldPos = mul(u_CurrModel, float4(input.Position, 1.0));
    float4 prevWorldPos = mul(u_PrevModel, float4(input.Position, 1.0));
    output.PositionCS  = mul(u_CurrJitteredVP, currWorldPos);
    output.CurrClipPos = mul(u_CurrUnjitteredVP, currWorldPos);
    output.PrevClipPos = mul(u_PrevUnjitteredVP, prevWorldPos);
    output.TexCoord    = input.TexCoord;
    return output;
}

float2 PSMesh(MeshVSOutput input) : SV_Target0 {
    int surfaceType = (int)(u_MaterialReserved[3].z + 0.5);
    if (surfaceType == 1) {
        float tileScale = max(0.01, u_MaterialReserved[3].w);
        float2 uv = (u_MaterialFlags.y != 0)
            ? (input.TexCoord * u_MaterialUV.xy + u_MaterialUV.zw)
            : (input.TexCoord * tileScale + u_MaterialReserved[2].xy);
        float texAlpha = ((u_MaterialFlags.x & 1u) != 0) ? u_AlbedoMap.Sample(u_Sampler, uv).a : 1.0;
        float alpha = texAlpha * u_MaterialReserved[3].x;
        if (alpha < saturate(u_MaterialReserved[3].y)) {
            discard;
        }
    }

    if (u_VelocityParams.x < 0.5) {
        return float2(0.0, 0.0);
    }
    if (input.CurrClipPos.w <= 1e-6 || input.PrevClipPos.w <= 1e-6) {
        return float2(0.0, 0.0);
    }
    float2 currUV = ComputeUVFromClip(input.CurrClipPos);
    float2 prevUV = ComputeUVFromClip(input.PrevClipPos);
    float2 velocity = currUV - prevUV;
    return any(isnan(velocity)) || any(isinf(velocity)) ? float2(0.0, 0.0) : velocity;
}
