// =============================================================================
// AxleEngine - 3D Froxel Volumetric Fog & Atmospheric Participating Media
// Dual-compiled for Direct3D 12 (DXIL 6.0) & Vulkan 1.3 (SPIR-V)
// Static 3D Froxel Media Injection -> Front-to-Back Beer-Lambert Integration -> Apply
// =============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

cbuffer VolumetricFogConstants : register(b0, space0) {
    float4x4 u_InvView;
    float4x4 u_InvProjection;
    float4   u_ScreenSize;              // (viewW, viewH, 1/viewW, 1/viewH)
    float4   u_SunDirectionIntensity;   // (xyz: unit dir toward sun, w: sun present 0 or 1)
    float4   u_SunRadiance;             // (rgb: scene-linear sun radiance, w: 0)
    float4   u_FogParams;               // (x: effective extinction [1/m], y: height falloff [1/m], z: g)
    float4   u_FogAlbedo;               // (rgb: single-scattering albedo)
    float4   u_FroxelGridParams;        // (x: gridW, y: gridH, z: gridD, w: orthographic flag)
    float4   u_AmbientRadiance;         // (rgb: authored scene ambient radiance)
    float4   u_FroxelDepthParams;       // (x: log(farZ / nearZ), y: nearZ, z: farZ, w: reserved)
};

Texture2D<float4>   u_SceneColor    : register(t0, space1);
Texture2D<float>    u_SceneDepth    : register(t1, space1);
Texture3D<float4>   u_FroxelVolume  : register(t2, space1);
Texture3D<float4>   u_FroxelMedia   : register(t3, space1);
SamplerState        u_LinearSampler : register(s0, space1);

#if defined(__spirv__)
[[vk::image_format("rgba16f")]]
#endif
RWTexture3D<float4> u_FroxelOutput  : register(u0, space2);

float3 ReconstructViewPos(float2 uv, float depth) {
    float4 clip = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(u_InvProjection, clip);
    return viewPos.xyz / max(abs(viewPos.w), 1e-6);
}

void ReconstructWorldRay(float2 uv, out float3 originWS, out float3 directionPerUnitZ) {
    float3 viewPos = ReconstructViewPos(uv, 0.5);
    bool orthographic = u_FroxelGridParams.w > 0.5;
    float3 originView = orthographic ? float3(viewPos.xy, 0.0) : float3(0.0, 0.0, 0.0);
    float3 directionView = orthographic ? float3(0.0, 0.0, -1.0) : viewPos / max(-viewPos.z, 1e-6);
    originWS = mul(u_InvView, float4(originView, 1.0)).xyz;
    directionPerUnitZ = mul((float3x3)u_InvView, directionView);
}

// Henyey-Greenstein Mie phase function: normalized on S^2.
// cosTheta = dot(viewDirFromCamToSample, dirTowardSun).
float HenyeyGreenstein(float cosTheta, float g) {
    float cg = clamp(g, -0.95, 0.95);
    float c = clamp(cosTheta, -1.0, 1.0);
    float g2 = cg * cg;
    float denom = max(1.0 + g2 - 2.0 * cg * c, 1e-6);
    return (1.0 - g2) / (4.0 * float(M_PI) * denom * sqrt(denom));
}

float SliceDepth(float slicePosition, float sliceCount) {
    return u_FroxelDepthParams.y * exp(u_FroxelDepthParams.x * (slicePosition / sliceCount));
}

float BeerLambertSeries(float opticalDepth) {
    return 1.0 - 0.5 * opticalDepth + (1.0 / 6.0) * opticalDepth * opticalDepth;
}

float AbsorbedFraction(float opticalDepth) {
    return opticalDepth > 1e-3
        ? 1.0 - exp(-opticalDepth)
        : opticalDepth * BeerLambertSeries(opticalDepth);
}

float4 IntegrateSegment(float4 media, float distanceMeters) {
    float extinction = max(media.a, 0.0);
    float opticalDepth = extinction * distanceMeters;
    float transmittance = exp(-opticalDepth);
    // Taylor expansion of (1 - exp(-t)) / t avoids cancellation at small t.
    float weight = opticalDepth > 1e-3
        ? (1.0 - transmittance) / extinction
        : distanceMeters * BeerLambertSeries(opticalDepth);
    return float4(max(media.rgb, 0.0) * weight, transmittance);
}

// =============================================================================
// Pass 1: Inject Media & Scattering per Froxel (CSInjectMedia -> FroxelScattering)
// Stores float4(inScatterPerMeter.rgb, extinctionSigmaT)
// =============================================================================
[numthreads(4, 4, 4)]
void CSInjectMedia(uint3 dtid : SV_DispatchThreadID) {
    uint3 gridDims = uint3(
        max((uint)round(u_FroxelGridParams.x), 1u),
        max((uint)round(u_FroxelGridParams.y), 1u),
        max((uint)round(u_FroxelGridParams.z), 1u));
    if (dtid.x >= gridDims.x || dtid.y >= gridDims.y || dtid.z >= gridDims.z) return;

    float effectiveBaseDensity = u_FogParams.x;
    if (effectiveBaseDensity <= 0.0) {
        u_FroxelOutput[dtid] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float heightFalloff = u_FogParams.y;
    float g = u_FogParams.z;
    float3 fogAlbedo = u_FogAlbedo.rgb;

    float2 uv = (float2(dtid.xy) + 0.5) / float2(gridDims.xy);
    float3 originWS, rayDirWorldPerUnitZ;
    ReconstructWorldRay(uv, originWS, rayDirWorldPerUnitZ);
    float3 unitRayWS = normalize(rayDirWorldPerUnitZ);

    float zCenter = SliceDepth(float(dtid.z) + 0.5, float(gridDims.z));
    float3 sampleWS = originWS + rayDirWorldPerUnitZ * zCenter;
    float heightAboveGround = max(sampleWS.y, 0.0);
    float localDensity = effectiveBaseDensity * exp(-heightAboveGround * heightFalloff);

    float3 incomingRadiance = max(u_AmbientRadiance.rgb, float3(0.0, 0.0, 0.0));
    if (u_SunDirectionIntensity.w > 0.5) {
        float cosTheta = dot(unitRayWS, normalize(u_SunDirectionIntensity.xyz));
        float phase = HenyeyGreenstein(cosTheta, g);
        incomingRadiance += max(u_SunRadiance.rgb, float3(0.0, 0.0, 0.0)) * phase;
    }

    float3 inScatterPerMeter = (fogAlbedo * localDensity) * incomingRadiance;
    u_FroxelOutput[dtid] = float4(min(inScatterPerMeter, 65504.0), localDensity);
}

// =============================================================================
// Pass 2: Front-to-Back Beer-Lambert Slice Integration (CSIntegrate -> FroxelIntegrated)
// FroxelIntegrated[..., iz] stores cumulative [nearZ, z_{iz+1}] (inScatter.rgb, transmittance)
// =============================================================================
[numthreads(8, 8, 1)]
void CSIntegrate(uint3 dtid : SV_DispatchThreadID) {
    uint3 gridDims = uint3(
        max((uint)round(u_FroxelGridParams.x), 1u),
        max((uint)round(u_FroxelGridParams.y), 1u),
        max((uint)round(u_FroxelGridParams.z), 1u));
    if (dtid.x >= gridDims.x || dtid.y >= gridDims.y) return;

    float2 uv = (float2(dtid.xy) + 0.5) / float2(gridDims.xy);
    float3 originWS, rayDirWorldPerUnitZ;
    ReconstructWorldRay(uv, originWS, rayDirWorldPerUnitZ);
    float rayLen = length(rayDirWorldPerUnitZ);

    float3 accumInScatter = float3(0.0, 0.0, 0.0);
    float accumTransmittance = 1.0;

    [loop]
    for (uint iz = 0u; iz < gridDims.z; ++iz) {
        float4 media = u_FroxelVolume.Load(int4(int(dtid.x), int(dtid.y), int(iz), 0));
        float z0 = SliceDepth(float(iz), float(gridDims.z));
        float z1 = SliceDepth(float(iz + 1u), float(gridDims.z));
        float ds = max(z1 - z0, 0.0) * rayLen;

        float4 step = IntegrateSegment(media, ds);
        accumInScatter += accumTransmittance * step.rgb;
        accumTransmittance *= step.a;

        u_FroxelOutput[uint3(dtid.x, dtid.y, iz)] = float4(
            min(accumInScatter, 65504.0),
            clamp(accumTransmittance, 0.0, 1.0));
    }
}

// =============================================================================
// Pass 3: Fullscreen Composite Pass (VSMain & PSMain)
// =============================================================================
struct VSOutput {
    float4 Position : SV_Position;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target {
    int2 maxCoord = max(int2(u_ScreenSize.xy) - int2(1, 1), int2(0, 0));
    int2 pixelCoord = clamp(int2(input.Position.xy), int2(0, 0), maxCoord);
    float4 sceneColor = u_SceneColor.Load(int3(pixelCoord, 0));

    if (u_FogParams.x <= 0.0) {
        return sceneColor;
    }

    float2 uv = (float2(pixelCoord) + 0.5) * u_ScreenSize.zw;
    float depth = u_SceneDepth.Load(int3(pixelCoord, 0));
    float nearZ = u_FroxelDepthParams.y;
    float farZ = u_FroxelDepthParams.z;
    float logRatio = u_FroxelDepthParams.x;
    float gridD = u_FroxelGridParams.z;

    float pixelViewZ = depth >= 1.0 ? farZ : -ReconstructViewPos(uv, depth).z;
    if (pixelViewZ <= nearZ) {
        return sceneColor;
    }

    float4 fogSample;
    if (pixelViewZ >= farZ) {
        fogSample = u_FroxelVolume.SampleLevel(u_LinearSampler, float3(uv, 1.0 - 0.5 / gridD), 0.0);
    } else {
        float slice = min(floor(saturate(log(pixelViewZ / nearZ) / logRatio) * gridD), gridD - 1.0);
        fogSample = float4(0.0, 0.0, 0.0, 1.0);
        if (slice > 0.0) {
            fogSample = u_FroxelVolume.SampleLevel(u_LinearSampler, float3(uv, (slice - 0.5) / gridD), 0.0);
        }
        // Keep both XY-filtered endpoints continuous across slice boundaries.
        // The Beer-Lambert weight evaluates the partial distance inside the slice.
        float3 sampleUV = float3(uv, (slice + 0.5) / gridD);
        float extinction = max(u_FroxelMedia.SampleLevel(u_LinearSampler, sampleUV, 0.0).a, 0.0);
        float4 sliceEnd = u_FroxelVolume.SampleLevel(u_LinearSampler, sampleUV, 0.0);
        float3 originWS, directionPerUnitZ;
        ReconstructWorldRay(uv, originWS, directionPerUnitZ);
        float z0 = SliceDepth(slice, gridD);
        float span = SliceDepth(slice + 1.0, gridD) - z0;
        float fraction = span > 0.0 ? saturate((pixelViewZ - z0) / span) : 0.0;
        float tau = extinction * span * length(directionPerUnitZ);
        float weight = tau > 0.0 ? AbsorbedFraction(tau * fraction) / AbsorbedFraction(tau) : fraction;
        fogSample = lerp(fogSample, sliceEnd, saturate(weight));
    }

    float3 inScattering = max(fogSample.rgb, float3(0.0, 0.0, 0.0));
    float transmittance = saturate(fogSample.a);
    float3 finalColor = min(sceneColor.rgb * transmittance + inScattering, 65504.0);
    return float4(finalColor, sceneColor.a);
}
