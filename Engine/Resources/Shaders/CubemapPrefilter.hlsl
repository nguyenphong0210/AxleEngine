// =============================================================================
// AxleEngine Dynamic Reflection Cubemap Prefilter Pass (M15-U5-C)
// Operates strictly in 3D cubemap texel -> direction space (zero equirectangular
// projection) so cube face orientation, seams, and +-Y poles remain continuous.
// Dual-compiled for Direct3D 12 (DXIL 6.0) & Vulkan 1.3 (SPIR-V).
// =============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

cbuffer PrefilterConstants : register(b0, space0) {
    uint4  u_Dims;   // x: DstSize, y: SrcBaseSize, z: MipIndex, w: MipCount
    float4 u_Params; // x: Roughness [0..1], y: SampleCount, z: Reserved, w: Reserved
    float4 u_Reserved[14];
};

TextureCube<float4>    u_SourceCubemap : register(t0, space1);
SamplerState           u_CubeSampler   : register(s0, space1);
RWTexture2DArray<float4> u_DstMipFaces : register(u0, space2);

float3 CubeFaceTexelDirection(uint face, float2 uv) {
    float2 xy = uv * 2.0 - 1.0;
    switch (face) {
        case 0u: return normalize(float3( 1.0,  -xy.y, -xy.x)); // +X
        case 1u: return normalize(float3(-1.0,  -xy.y,  xy.x)); // -X
        case 2u: return normalize(float3( xy.x,  1.0,   xy.y)); // +Y
        case 3u: return normalize(float3( xy.x, -1.0,  -xy.y)); // -Y
        case 4u: return normalize(float3( xy.x, -xy.y,  1.0));  // +Z
        default: return normalize(float3(-xy.x, -xy.y, -1.0));  // -Z
    }
}

float3 SanitizeHDRRadiance(float3 c) {
    if (any(isnan(c)) || any(isinf(c))) {
        return float3(0.0, 0.0, 0.0);
    }
    return clamp(c, float3(0.0, 0.0, 0.0), float3(65000.0, 65000.0, 65000.0));
}

float RadicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(max(N, 1u)), RadicalInverseVdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * M_PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / max(1.0 + (a * a - 1.0) * Xi.y, 1e-6));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    float3 HLocal = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(tangent * HLocal.x + bitangent * HLocal.y + N * HLocal.z);
}

float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(M_PI * denom * denom, 1e-6);
}

[numthreads(8, 8, 1)]
void DownsampleCubeCS(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 dstPixel = dispatchThreadId.xy;
    uint face = dispatchThreadId.z;
    uint dstSize = max(u_Dims.x, 1u);
    if (face >= 6u || dstPixel.x >= dstSize || dstPixel.y >= dstSize) {
        return;
    }

    float invSize = 1.0 / float(dstSize);
    float2 uvCenter = (float2(dstPixel) + 0.5) * invSize;
    float delta = 0.25 * invSize;

    float3 d0 = CubeFaceTexelDirection(face, uvCenter + float2(-delta, -delta));
    float3 d1 = CubeFaceTexelDirection(face, uvCenter + float2( delta, -delta));
    float3 d2 = CubeFaceTexelDirection(face, uvCenter + float2(-delta,  delta));
    float3 d3 = CubeFaceTexelDirection(face, uvCenter + float2( delta,  delta));

    float3 c0 = SanitizeHDRRadiance(u_SourceCubemap.SampleLevel(u_CubeSampler, d0, 0.0).rgb);
    float3 c1 = SanitizeHDRRadiance(u_SourceCubemap.SampleLevel(u_CubeSampler, d1, 0.0).rgb);
    float3 c2 = SanitizeHDRRadiance(u_SourceCubemap.SampleLevel(u_CubeSampler, d2, 0.0).rgb);
    float3 c3 = SanitizeHDRRadiance(u_SourceCubemap.SampleLevel(u_CubeSampler, d3, 0.0).rgb);

    float3 filtered = 0.25 * (c0 + c1 + c2 + c3);
    u_DstMipFaces[uint3(dstPixel.x, dstPixel.y, face)] = float4(filtered, 1.0);
}

[numthreads(8, 8, 1)]
void PrefilterCubeCS(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 dstPixel = dispatchThreadId.xy;
    uint face = dispatchThreadId.z;
    uint dstSize = max(u_Dims.x, 1u);
    if (face >= 6u || dstPixel.x >= dstSize || dstPixel.y >= dstSize) {
        return;
    }

    float2 uv = (float2(dstPixel) + 0.5) / float(dstSize);
    float3 N = CubeFaceTexelDirection(face, uv);
    float roughness = saturate(u_Params.x);

    if (u_Dims.z == 0u || roughness <= 1e-4) {
        float3 sharp = SanitizeHDRRadiance(u_SourceCubemap.SampleLevel(u_CubeSampler, N, 0.0).rgb);
        u_DstMipFaces[uint3(dstPixel.x, dstPixel.y, face)] = float4(sharp, 1.0);
        return;
    }

    uint sampleCount = max((uint)u_Params.y, 1u);
    float srcBaseSize = float(max(u_Dims.y, 1u));
    float maxSrcLod = float(max(u_Dims.w, 1u) - 1u);
    float saTexel = 4.0 * M_PI / (6.0 * srcBaseSize * srcBaseSize);

    float3 prefilteredColor = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;

    [loop]
    for (uint i = 0u; i < sampleCount; ++i) {
        float2 Xi = Hammersley(i, sampleCount);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(N, H) * H - N);
        float NdotL = dot(N, L);
        if (NdotL > 0.0) {
            float NdotH = max(dot(N, H), 0.0);
            float D = DistributionGGX(NdotH, roughness);
            float pdf = (D * NdotH) / (4.0 * max(NdotH, 1e-4)) + 1e-4;
            float saSample = 1.0 / (float(sampleCount) * pdf + 1e-4);
            float mipLevel = clamp(0.5 * log2(saSample / max(saTexel, 1e-12)), 0.0, maxSrcLod);

            float3 radiance = SanitizeHDRRadiance(u_SourceCubemap.SampleLevel(u_CubeSampler, L, mipLevel).rgb);
            prefilteredColor += radiance * NdotL;
            totalWeight += NdotL;
        }
    }

    float3 finalColor = totalWeight > 1e-5
        ? (prefilteredColor / totalWeight)
        : SanitizeHDRRadiance(u_SourceCubemap.SampleLevel(u_CubeSampler, N, maxSrcLod).rgb);
    u_DstMipFaces[uint3(dstPixel.x, dstPixel.y, face)] = float4(SanitizeHDRRadiance(finalColor), 1.0);
}
