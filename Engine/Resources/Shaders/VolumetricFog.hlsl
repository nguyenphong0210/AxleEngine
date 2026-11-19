// =============================================================================
// AxleEngine - 3D Froxel Volumetric Fog & Atmospheric Participating Media
// Dual-compiled for Direct3D 12 (DXIL 6.0) & Vulkan 1.3 (SPIR-V)
// Static 3D Froxel Media Injection -> Front-to-Back Beer-Lambert Integration -> Apply
// =============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

cbuffer VolumetricFogConstants : register(b0, space0) {
    float4x4 u_View;
    float4x4 u_InvView;
    float4x4 u_Projection;
    float4x4 u_InvProjection;
    float4   u_ScreenSize;              // (viewW, viewH, 1/viewW, 1/viewH)
    float4   u_SunDirectionIntensity;   // (xyz: unit dir toward sun, w: sun present 0 or 1)
    float4   u_SunRadiance;             // (rgb: scene-linear sun radiance, w: 0)
    float4   u_FogParams;               // (x: density [1/m], y: height falloff [1/m], z: g, w: farZ)
    float4   u_FogColorIntensity;       // (rgb: single-scatter albedo, w: intensity multiplier)
    float4   u_FroxelGridParams;        // (x: gridW, y: gridH, z: gridD, w: nearZ)
    float4   u_AmbientRadiance;         // (rgb: authored scene ambient radiance, w: temporalBlendAlpha)
    float4   u_FroxelDepthParams;       // (x: log(farZ / nearZ), y: nearZ, z: farZ, w: reserved)
    float4   u_Reserved[8];             // Total 512 bytes aligned
};

Texture2D<float4>   u_SceneColor    : register(t0, space1);
Texture2D<float>    u_SceneDepth    : register(t1, space1);
Texture3D<float4>   u_FroxelVolume  : register(t2, space1);
SamplerState        u_LinearSampler : register(s0, space1);

RWTexture3D<float4> u_FroxelOutput  : register(u0, space2);

float3 ReconstructViewPos(float2 uv, float depth) {
    float4 clip = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(u_InvProjection, clip);
    return viewPos.xyz / max(abs(viewPos.w), 1e-6);
}

float3 ReconstructViewRayPerUnitZ(float2 uv) {
    float3 viewPos = ReconstructViewPos(uv, 0.5);
    return viewPos / max(-viewPos.z, 1e-6);
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

float SliceBoundaryDepth(float boundaryIdx, float sliceCount, float nearZ, float logRatio) {
    return nearZ * exp(logRatio * (boundaryIdx / max(sliceCount, 1.0)));
}

float SliceCenterDepth(uint sliceIdx, float sliceCount, float nearZ, float logRatio) {
    return nearZ * exp(logRatio * ((float(sliceIdx) + 0.5) / max(sliceCount, 1.0)));
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

    float baseDensity = max(u_FogParams.x, 0.0);
    float fogIntensity = max(u_FogColorIntensity.w, 0.0);
    float effectiveBaseDensity = baseDensity * fogIntensity;
    if (effectiveBaseDensity <= 0.0) {
        u_FroxelOutput[dtid] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float heightFalloff = max(u_FogParams.y, 0.0);
    float g = clamp(u_FogParams.z, -0.95, 0.95);
    float3 fogAlbedo = max(u_FogColorIntensity.rgb, float3(0.0, 0.0, 0.0));
    float nearZ = max(u_FroxelDepthParams.y, 1e-4);
    float logRatio = max(u_FroxelDepthParams.x, 1e-6);

    float2 uv = (float2(dtid.xy) + 0.5) / float2(gridDims.xy);
    float3 rayView = ReconstructViewRayPerUnitZ(uv);
    float3 rayDirWorldPerUnitZ = mul((float3x3)u_InvView, rayView);
    float rayLen = length(rayDirWorldPerUnitZ);
    float3 unitRayWS = rayDirWorldPerUnitZ / max(rayLen, 1e-6);
    float3 camPosWS = mul(u_InvView, float4(0.0, 0.0, 0.0, 1.0)).xyz;

    float zCenter = SliceCenterDepth(dtid.z, float(gridDims.z), nearZ, logRatio);
    float3 sampleWS = camPosWS + rayDirWorldPerUnitZ * zCenter;
    float heightAboveGround = max(sampleWS.y, 0.0);
    float localDensity = effectiveBaseDensity * exp(-heightAboveGround * heightFalloff);

    float3 incomingRadiance = max(u_AmbientRadiance.rgb, float3(0.0, 0.0, 0.0));
    if (u_SunDirectionIntensity.w > 0.5) {
        float cosTheta = dot(unitRayWS, normalize(u_SunDirectionIntensity.xyz));
        float phase = HenyeyGreenstein(cosTheta, g);
        incomingRadiance += max(u_SunRadiance.rgb, float3(0.0, 0.0, 0.0)) * phase;
    }

    float3 inScatterPerMeter = (fogAlbedo * localDensity) * incomingRadiance;
    u_FroxelOutput[dtid] = float4(
        min(inScatterPerMeter, float3(65000.0, 65000.0, 65000.0)),
        min(localDensity, 65000.0));
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
    float3 rayView = ReconstructViewRayPerUnitZ(uv);
    float3 rayDirWorldPerUnitZ = mul((float3x3)u_InvView, rayView);
    float rayLen = length(rayDirWorldPerUnitZ);

    float nearZ = max(u_FroxelDepthParams.y, 1e-4);
    float logRatio = max(u_FroxelDepthParams.x, 1e-6);

    float3 accumInScatter = float3(0.0, 0.0, 0.0);
    float accumTransmittance = 1.0;

    [loop]
    for (uint iz = 0u; iz < gridDims.z; ++iz) {
        float4 media = u_FroxelVolume.Load(int4(int(dtid.x), int(dtid.y), int(iz), 0));
        float3 inScatterPerMeter = max(media.rgb, float3(0.0, 0.0, 0.0));
        float sigmaT = max(media.a, 0.0);

        float z0 = SliceBoundaryDepth(float(iz), float(gridDims.z), nearZ, logRatio);
        float z1 = SliceBoundaryDepth(float(iz + 1u), float(gridDims.z), nearZ, logRatio);
        float ds = max(z1 - z0, 0.0) * rayLen;

        float stepTransmittance = 1.0;
        float3 stepInScatter = float3(0.0, 0.0, 0.0);
        if (sigmaT > 0.0 && ds > 0.0) {
            float opticalDepth = sigmaT * ds;
            stepTransmittance = exp(-opticalDepth);
            float sliceIntegFactor = (opticalDepth > 1e-4)
                ? ((1.0 - stepTransmittance) / sigmaT)
                : (ds * (1.0 - 0.5 * opticalDepth + (1.0 / 6.0) * opticalDepth * opticalDepth));
            stepInScatter = inScatterPerMeter * sliceIntegFactor;
        } else if (ds > 0.0) {
            stepInScatter = inScatterPerMeter * ds;
        }

        accumInScatter += accumTransmittance * stepInScatter;
        accumTransmittance *= stepTransmittance;

        u_FroxelOutput[uint3(dtid.x, dtid.y, iz)] = float4(
            min(accumInScatter, float3(65000.0, 65000.0, 65000.0)),
            clamp(accumTransmittance, 0.0, 1.0));
    }
}

// =============================================================================
// Pass 3: Fullscreen Composite Pass (VSMain & PSMain)
// =============================================================================
struct VSOutput {
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    output.TexCoord = float2(uv.x, 1.0f - uv.y);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target {
    int2 maxCoord = max(int2(u_ScreenSize.xy) - int2(1, 1), int2(0, 0));
    int2 pixelCoord = clamp(int2(input.Position.xy), int2(0, 0), maxCoord);
    float4 sceneColor = u_SceneColor.Load(int3(pixelCoord, 0));

    float effectiveDensity = max(u_FogParams.x, 0.0) * max(u_FogColorIntensity.w, 0.0);
    if (effectiveDensity <= 0.0) {
        return sceneColor;
    }

    float2 uv = (float2(pixelCoord) + 0.5) * u_ScreenSize.zw;
    float depth = u_SceneDepth.Load(int3(pixelCoord, 0));
    float nearZ = max(u_FroxelDepthParams.y, 1e-4);
    float farZ = max(u_FroxelDepthParams.z, nearZ * 1.001);
    float logRatio = max(u_FroxelDepthParams.x, 1e-6);
    float gridD = max(u_FroxelGridParams.z, 1.0);

    float pixelViewZ = (depth >= 0.999999) ? farZ : max(-ReconstructViewPos(uv, depth).z, 0.0);
    if (pixelViewZ <= nearZ) {
        return sceneColor;
    }

    float clampedZ = clamp(pixelViewZ, nearZ, farZ);
    float sliceRatio = saturate(log(clampedZ / nearZ) / logRatio);
    float slicePos = sliceRatio * gridD;

    float4 fogSample;
    if (slicePos < 1.0) {
        float4 slice1 = u_FroxelVolume.SampleLevel(u_LinearSampler, float3(uv, 0.5 / gridD), 0.0);
        fogSample = lerp(float4(0.0, 0.0, 0.0, 1.0), slice1, slicePos);
    } else {
        float wCoord = clamp((slicePos - 0.5) / gridD, 0.5 / gridD, (gridD - 0.5) / gridD);
        fogSample = u_FroxelVolume.SampleLevel(u_LinearSampler, float3(uv, wCoord), 0.0);
    }

    float3 inScattering = max(fogSample.rgb, float3(0.0, 0.0, 0.0));
    float transmittance = saturate(fogSample.a);
    float3 finalColor = sceneColor.rgb * transmittance + inScattering;
    return float4(finalColor, sceneColor.a);
}
