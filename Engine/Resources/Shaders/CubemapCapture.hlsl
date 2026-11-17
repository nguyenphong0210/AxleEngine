// AxleEngine Dynamic Reflection Cubemap Orientation Pass
// Converts a right-handed captured cube face into left-handed D3D12/Vulkan
// TextureCube face space by horizontally mirroring texel X (u -> 1 - u) and
// sanitizing non-finite / negative HDR radiance values prior to face copy.

cbuffer CaptureOrientConstants : register(b0, space0) {
    uint4 u_Params; // x: Resolution, y: FlipHorizontal (1/0), z: FaceIndex, w: Reserved
    float4 u_Reserved[15];
};

Texture2D<float4> u_SourceFace : register(t0, space1);
RWTexture2D<float4> u_OrientedFace : register(u0, space2);

[numthreads(8, 8, 1)]
void OrientFaceCS(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 dstPixel = dispatchThreadId.xy;
    uint size = max(u_Params.x, 1u);
    if (dstPixel.x >= size || dstPixel.y >= size) {
        return;
    }

    uint srcX = (u_Params.y != 0u) ? (size - 1u - dstPixel.x) : dstPixel.x;
    float4 sampleVal = u_SourceFace.Load(int3(srcX, dstPixel.y, 0));

    float3 rgb = sampleVal.rgb;
    if (any(isnan(rgb)) || any(isinf(rgb))) {
        rgb = float3(0.0, 0.0, 0.0);
    } else {
        rgb = max(rgb, float3(0.0, 0.0, 0.0));
    }
    float alpha = (isnan(sampleVal.a) || isinf(sampleVal.a)) ? 1.0 : saturate(sampleVal.a);
    u_OrientedFace[dstPixel] = float4(rgb, alpha);
}
