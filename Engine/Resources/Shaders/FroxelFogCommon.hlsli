#ifndef AXLE_FROXEL_FOG_COMMON_HLSLI
#define AXLE_FROXEL_FOG_COMMON_HLSLI

#ifdef AXLE_FROXEL_CONSTANTS_REGISTER
cbuffer VolumetricFogConstants : register(AXLE_FROXEL_CONSTANTS_REGISTER, space0) {
    float4x4 u_FogInvView;
    float4x4 u_FogInvProjection;
    float4   u_FogScreenSize;
    float4   u_FogSunDirectionIntensity;
    float4   u_FogSunRadiance;
    float4   u_FogParams;
    float4   u_FogAlbedo;
    float4   u_FogGridParams;
    float4   u_FogAmbientRadiance;
    float4   u_FogDepthParams;
    float4x4 u_FogPrevViewProjection;
    float4   u_FogPrevViewDepthRow;
    float4   u_FogTemporalParams;
};
#endif

float FroxelSliceDepth(float sliceIndex, float gridD, float nearZ, float logRatio) {
    return nearZ * exp(logRatio * (sliceIndex / max(gridD, 1.0)));
}

float FroxelAbsorbedFraction(float tau) {
    return (tau > 1e-4)
        ? (1.0 - exp(-tau))
        : (tau - 0.5 * tau * tau + (1.0 / 6.0) * tau * tau * tau);
}

void FroxelReconstructWorldRay(float2 uv,
                               float4x4 invProjection,
                               float4x4 invView,
                               float isOrtho,
                               out float3 originWS,
                               out float3 rayDirWorldPerUnitZ) {
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 vpNear4 = mul(invProjection, float4(ndc, 0.0, 1.0));
    float4 vpFar4  = mul(invProjection, float4(ndc, 1.0, 1.0));
    float3 vpNear  = vpNear4.xyz / (abs(vpNear4.w) > 1e-6 ? vpNear4.w : 1.0);
    float3 vpFar   = vpFar4.xyz  / (abs(vpFar4.w)  > 1e-6 ? vpFar4.w  : 1.0);

    if (isOrtho > 0.5) {
        originWS = mul(invView, float4(vpNear.xy, 0.0, 1.0)).xyz;
        rayDirWorldPerUnitZ = mul((float3x3)invView, float3(0.0, 0.0, -1.0));
    } else {
        originWS = mul(invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
        float3 delta = vpFar - vpNear;
        float lenDelta = length(delta);
        float3 rayDirVP = (lenDelta > 1e-7) ? (delta / lenDelta) : float3(0.0, 0.0, -1.0);
        float3 rayPerUnitZ_VP = rayDirVP / max(-rayDirVP.z, 1e-5);
        rayDirWorldPerUnitZ = mul((float3x3)invView, rayPerUnitZ_VP);
    }
}

// Evaluates cumulative Beer-Lambert in-scattering (.rgb) and transmittance (.a)
// from nearZ to pixelViewZ using piecewise Beer-Lambert partial-slice integration.
float4 EvaluateFroxelFogSample(Texture3D<float4> integratedVol,
                               Texture3D<float4> mediaVol,
                               SamplerState linearSampler,
                               float2 screenUV,
                               float pixelViewZ,
                               float4x4 invView,
                               float4x4 invProjection,
                               float4 fogParams,
                               float4 froxelGridParams,
                               float4 froxelDepthParams) {
    if (fogParams.x <= 0.0) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float nearZ = froxelDepthParams.y;
    float farZ = froxelDepthParams.z;
    float logRatio = froxelDepthParams.x;
    float gridD = froxelGridParams.z;
    if (gridD <= 0.0 || logRatio <= 0.0 || nearZ <= 0.0 || pixelViewZ <= nearZ) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float2 uv = clamp(screenUV, float2(0.0, 0.0), float2(1.0, 1.0));
    float4 fogSample;
    if (pixelViewZ >= farZ) {
        fogSample = integratedVol.SampleLevel(linearSampler, float3(uv, 1.0 - 0.5 / gridD), 0.0);
    } else {
        float slice = min(floor(saturate(log(pixelViewZ / nearZ) / logRatio) * gridD), gridD - 1.0);
        fogSample = float4(0.0, 0.0, 0.0, 1.0);
        if (slice > 0.0) {
            fogSample = integratedVol.SampleLevel(linearSampler, float3(uv, (slice - 0.5) / gridD), 0.0);
        }
        float3 sampleUV = float3(uv, (slice + 0.5) / gridD);
        float extinction = max(mediaVol.SampleLevel(linearSampler, sampleUV, 0.0).a, 0.0);
        float4 sliceEnd = integratedVol.SampleLevel(linearSampler, sampleUV, 0.0);
        float3 originWS, directionPerUnitZ;
        FroxelReconstructWorldRay(uv, invProjection, invView, froxelGridParams.w, originWS, directionPerUnitZ);
        float z0 = FroxelSliceDepth(slice, gridD, nearZ, logRatio);
        float span = FroxelSliceDepth(slice + 1.0, gridD, nearZ, logRatio) - z0;
        float fraction = span > 0.0 ? saturate((pixelViewZ - z0) / span) : 0.0;
        float tau = extinction * span * length(directionPerUnitZ);
        float weight = tau > 0.0 ? FroxelAbsorbedFraction(tau * fraction) / FroxelAbsorbedFraction(tau) : fraction;
        fogSample = lerp(fogSample, sliceEnd, saturate(weight));
    }

    return float4(max(fogSample.rgb, float3(0.0, 0.0, 0.0)), saturate(fogSample.a));
}

#endif
