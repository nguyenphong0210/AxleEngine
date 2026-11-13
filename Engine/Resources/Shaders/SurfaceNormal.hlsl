// Opaque surface-normal preparation pass for screen-space lighting features.
// The vertex shader is shared with ForwardPBR; this file supplies its pixel
// stage so ScreenNormal contains the interpolated shading normal, not a depth
// derivative or an uninitialized attachment.

struct PSInput {
    float4 PositionCS  : SV_Position;
    float3 WorldPos    : POSITION;
    float3 NormalWS    : NORMAL;
    float4 TangentWS   : TANGENT;
    float2 TexCoord    : TEXCOORD0;
    float4 VertexColor : COLOR0;
    float3 LocalPos    : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_Target0 {
    float3 normal = normalize(input.NormalWS);
    return float4(normal, 1.0);
}
