#ifndef AXLE_DDGI_COMMON_HLSLI
#define AXLE_DDGI_COMMON_HLSLI

#ifndef AXLE_DDGI_CBUFFER_REGISTER
#define AXLE_DDGI_CBUFFER_REGISTER b0
#endif

static const uint DDGI_IRRADIANCE_INTERIOR = 6u;
static const uint DDGI_IRRADIANCE_BORDER = 1u;
static const uint DDGI_IRRADIANCE_TILE = 8u;
static const uint DDGI_DISTANCE_INTERIOR = 14u;
static const uint DDGI_DISTANCE_BORDER = 1u;
static const uint DDGI_DISTANCE_TILE = 16u;
static const float DDGI_PI = 3.14159265359;
static const float DDGI_BACKFACE_REJECT_RATIO = 0.275;

cbuffer DDGIConstants : register(AXLE_DDGI_CBUFFER_REGISTER, space0) {
    float4 u_DDGIScreenSize;      // View width, height, 1/width, 1/height
    float4 u_DDGIAtlasSize;       // IrradW, IrradH, DistW, DistH
    float4 u_DDGIGridDimensions;  // x, y, z, total probeCount
    float4 u_DDGIGridOrigin;      // xyz origin, w = atlasCols
    float4 u_DDGIGridSpacing;     // xyz spacing, w = maxRayDistance
    float4 u_DDGIParams;          // x = energyMultiplier, y = hysteresis, z = viewBias, w = normalBias
    float4 u_DDGIUpdateParams;    // x = startProbeIndex, y = updateCount, z = captureCols, w = captureFaceRes
    float4 u_DDGIFlags;           // x = samplingEnabled, y = hasHistoryForMultiBounce, z = seedResetFrame, w = atlasRows
    float4 u_DDGIClipParams;      // x = captureNear, y = captureFar, z = 0, w = 0
    float4 u_DDGICaptureShadowDepth; // View-space depth row of the camera that populated the CSM atlas
    float4 u_DDGIReserved[6];
};

float2 DDGISignNotZero(float2 v) {
    return float2((v.x >= 0.0) ? 1.0 : -1.0, (v.y >= 0.0) ? 1.0 : -1.0);
}

// Maps unit direction -> [-1, 1]^2 octahedral coordinates
float2 DDGIOctEncode(float3 v) {
    float l1 = abs(v.x) + abs(v.y) + abs(v.z);
    float2 p = v.xy / max(l1, 1e-6);
    if (v.z <= 0.0) {
        p = (1.0 - abs(p.yx)) * DDGISignNotZero(p);
    }
    return p;
}

// Maps [-1, 1]^2 octahedral coordinates -> unit sphere direction
float3 DDGIOctDecode(float2 oct) {
    float3 v = float3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    if (v.z < 0.0) {
        v.xy = (1.0 - abs(v.yx)) * DDGISignNotZero(v.xy);
    }
    return normalize(v);
}

// Mirrors 1-texel border coordinates inside an octahedral tile [0 .. interior+1]
uint2 DDGIMirrorInteriorCoord(uint2 localTexel, uint interior) {
    uint last = interior + 1u;
    bool leftEdge = (localTexel.x == 0u);
    bool rightEdge = (localTexel.x == last);
    bool topEdge = (localTexel.y == 0u);
    bool bottomEdge = (localTexel.y == last);

    if ((leftEdge || rightEdge) && (topEdge || bottomEdge)) {
        return uint2(leftEdge ? interior : 1u, topEdge ? interior : 1u);
    }
    if (leftEdge) {
        return uint2(1u, last - localTexel.y);
    }
    if (rightEdge) {
        return uint2(interior, last - localTexel.y);
    }
    if (topEdge) {
        return uint2(last - localTexel.x, 1u);
    }
    if (bottomEdge) {
        return uint2(last - localTexel.x, interior);
    }
    return localTexel;
}

uint DDGIProbeCoordToIndex(int3 coord, int3 dims) {
    int3 clamped = clamp(coord, int3(0, 0, 0), max(dims - 1, int3(0, 0, 0)));
    return uint((clamped.y * dims.z + clamped.z) * dims.x + clamped.x);
}

int3 DDGIProbeIndexToCoord(uint probeIndex, int3 dims) {
    uint dx = max(uint(dims.x), 1u);
    uint dz = max(uint(dims.z), 1u);
    uint plane = dx * dz;
    uint iy = probeIndex / plane;
    uint rem = probeIndex % plane;
    uint iz = rem / dx;
    uint ix = rem % dx;
    return int3(int(ix), int(iy), int(iz));
}

float3 DDGIProbeCoordToWorldPos(int3 coord) {
    return u_DDGIGridOrigin.xyz + float3(coord) * u_DDGIGridSpacing.xyz;
}

uint2 DDGIProbeIndexToAtlasTile(uint probeIndex, uint atlasCols) {
    uint cols = max(atlasCols, 1u);
    return uint2(probeIndex % cols, probeIndex / cols);
}

float2 DDGISampleTileUV(uint probeIndex, uint atlasCols, uint interior, uint tileSize, float2 atlasDims, float3 dir) {
    uint2 tile = DDGIProbeIndexToAtlasTile(probeIndex, atlasCols);
    float2 tileOrigin = float2(tile) * float(tileSize);
    float2 oct = DDGIOctEncode(normalize(dir));
    float2 px = tileOrigin + 1.0 + (oct * 0.5 + 0.5) * float(interior);
    return px / max(atlasDims, float2(1.0, 1.0));
}

float DDGIEvaluateChebyshev(float2 moments, float dist) {
    if (dist <= moments.x) {
        return 1.0;
    }
    float variance = max(moments.y - moments.x * moments.x, 1e-4);
    float d = dist - moments.x;
    float p = variance / (variance + d * d);
    return saturate(p * p * p);
}

float4 DDGISampleVolumeIrradiance(Texture2D<float4> irradAtlas, Texture2D<float4> distAtlas,
                                    SamplerState linearSampler, float3 worldPos,
                                    float3 worldNormal, float3 viewDir) {
    if (u_DDGIFlags.x < 0.5) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    int3 dims = int3(u_DDGIGridDimensions.xyz + 0.5);
    if (any(dims <= 0)) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    float3 spacing = max(u_DDGIGridSpacing.xyz, float3(1e-4, 1e-4, 1e-4));
    float3 biasedPos = worldPos + worldNormal * u_DDGIParams.w + viewDir * u_DDGIParams.z;
    float3 gridCoordF = (biasedPos - u_DDGIGridOrigin.xyz) / spacing;
    float3 maxGrid = float3(max(dims - 1, int3(0, 0, 0)));
    float3 clampedF = clamp(gridCoordF, float3(0.0, 0.0, 0.0), maxGrid);
    float3 outside = abs(gridCoordF - clampedF);
    float cageWeight = saturate(1.0 - max(outside.x, max(outside.y, outside.z)) * 2.0);
    if (cageWeight <= 1e-4) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    int3 baseCell = clamp(int3(floor(clampedF)), int3(0, 0, 0), max(dims - 2, int3(0, 0, 0)));
    float3 alpha = saturate(clampedF - float3(baseCell));
    uint atlasCols = max(uint(u_DDGIGridOrigin.w + 0.5), 1u);
    float2 irradAtlasDims = u_DDGIAtlasSize.xy;
    float2 distAtlasDims = u_DDGIAtlasSize.zw;

    float3 irradSum = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;

    [unroll]
    for (int i = 0; i < 8; ++i) {
        int3 offset = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        int3 probeCoord = min(baseCell + offset, max(dims - 1, int3(0, 0, 0)));
        uint probeIdx = DDGIProbeCoordToIndex(probeCoord, dims);
        float3 probePos = DDGIProbeCoordToWorldPos(probeCoord);

        float3 tri = lerp(1.0 - alpha, alpha, float3(offset));
        float trilinearWeight = max(tri.x * tri.y * tri.z, 1e-4);

        float2 irradUV = DDGISampleTileUV(probeIdx, atlasCols, DDGI_IRRADIANCE_INTERIOR,
                                          DDGI_IRRADIANCE_TILE, irradAtlasDims, worldNormal);
        float4 irradSample = irradAtlas.SampleLevel(linearSampler, irradUV, 0.0);

        float3 probeToBiased = biasedPos - probePos;
        float distToBiased = length(probeToBiased);
        float3 probeRayDir = (distToBiased > 1e-4) ? (probeToBiased / distToBiased) : -worldNormal;
        float2 distUV = DDGISampleTileUV(probeIdx, atlasCols, DDGI_DISTANCE_INTERIOR,
                                         DDGI_DISTANCE_TILE, distAtlasDims, probeRayDir);
        float4 distSample = distAtlas.SampleLevel(linearSampler, distUV, 0.0);

        // Suppress uninitialized probes and probes enclosed inside backfacing solid geometry
        float validProbe = step(0.01, irradSample.a) * step(0.01, distSample.z);
        float backfaceRatio = saturate(distSample.w);
        float airWeight = saturate((DDGI_BACKFACE_REJECT_RATIO - backfaceRatio) / (DDGI_BACKFACE_REJECT_RATIO - 0.15));

        float3 probeToSurface = probePos - worldPos;
        float surfDist = length(probeToSurface);
        float3 toProbeDir = (surfDist > 1e-4) ? (probeToSurface / surfDist) : worldNormal;
        float wrapCos = saturate(dot(toProbeDir, worldNormal) * 0.5 + 0.5);
        float dirWeight = wrapCos * wrapCos + 0.05;

        float chebyshev = DDGIEvaluateChebyshev(distSample.xy, distToBiased);

        float weight = trilinearWeight * validProbe * airWeight * dirWeight * chebyshev;
        weight = max(weight, 0.0);
        if (weight > 1e-5) {
            irradSum += max(irradSample.rgb, float3(0.0, 0.0, 0.0)) * weight;
            weightSum += weight;
        }
    }

    if (weightSum <= 1e-5) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    float3 irradiance = (irradSum / weightSum) * max(u_DDGIParams.x, 0.0);
    float confidence = cageWeight * saturate(weightSum * 4.0);
    return float4(irradiance, confidence);
}

#endif // AXLE_DDGI_COMMON_HLSLI
