// Lightweight DDGI probe atlas update. The atlas is intentionally bounded and
// uses the current HDR scene as a stable fallback when ray tracing is absent.
cbuffer DDGIConstants : register(b0, space0) {
    float4 u_ScreenSize;
    float4 u_AtlasSize;
    float4 u_GridDimensions;
    float4 u_GridOrigin;
    float4 u_GridSpacing;
    float4 u_Params;
    float4 u_Reserved[10];
};

Texture2D<float> u_Depth : register(t0, space1);
Texture2D<float4> u_Scene : register(t1, space1);
SamplerState u_Sampler : register(s0, space1);
RWTexture2D<float4> u_Atlas : register(u0, space2);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint2 atlasPixel = id.xy;
    if (atlasPixel.x >= (uint)u_AtlasSize.x || atlasPixel.y >= (uint)u_AtlasSize.y) return;
    float2 uv = (float2(atlasPixel) + 0.5) / u_AtlasSize.xy;
    float3 radiance = u_Scene.SampleLevel(u_Sampler, uv, 0.0).rgb;
    float depth = u_Depth.SampleLevel(u_Sampler, uv, 0.0);
    float validity = depth < 0.9999 ? 1.0 : 0.25;
    u_Atlas[atlasPixel] = float4(max(radiance, 0.0) * u_Params.x * validity, validity);
}
