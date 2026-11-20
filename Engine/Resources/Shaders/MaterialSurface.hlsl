// Canonical opaque material surface template. Graphs supply AxleEvaluateMaterial.
cbuffer EngineObject : register(b0, space0) {
    column_major float4x4 u_Model;
};
cbuffer EngineScene : register(b1, space0) {
    column_major float4x4 u_ViewProjection;
    float4 u_CameraPosition;
    float4 u_SunDirection;
    float4 u_SunColor;
    column_major float4x4 u_Projection;
};

#include "Occlusion.hlsli"
#define AXLE_FROXEL_CONSTANTS_REGISTER b7
#include "FroxelFogCommon.hlsli"
Texture3D<float4> u_FroxelIntegrated : register(t0, space5);
Texture3D<float4> u_FroxelMedia : register(t1, space5);

// Shared lighting contract with ForwardPBR.  Graph materials use the same
// uploaded light buffer and shadow receiver data as the built-in path; they do
// not silently fall back to a hard-coded single-light approximation.
struct DirectionalSunLight {
    float4 DirectionAndIlluminance;
    float4 ColorAndAmbient;
    float4 AmbientColor;
    float4 ShadowParams;
};
struct PointLightData {
    float4 PositionAndRadius;
    float4 ColorAndIntensity;
    float4 Params;
    float4 Reserved;
};
struct SpotLightData {
    float4 PositionAndRange;
    float4 DirectionAndIntensity;
    float4 ColorAndInnerAngle;
    float4 AngleParams;
};
struct RectLightData {
    float4 PositionAndWidth;
    float4 DirectionAndHeight;
    float4 UpAndRange;
    float4 ColorAndIntensity;
};
// The graph parameter ABI owns b2/space0.  Keep the shared engine light
// contract in a reserved space so authored graph parameters cannot collide.
cbuffer MultiLightBuffer : register(b0, space3) {
    DirectionalSunLight u_SunLight;
    uint4 u_LightCounts;
    float4 u_CameraPosExposure;
    float4 u_LightReserved[2];
    PointLightData u_PointLights[16];
    SpotLightData u_SpotLights[16];
    RectLightData u_RectLights[2];
};
cbuffer CascadeReceiver : register(b4, space0) {
    float4x4 u_CascadeMatrices[4];
    float4 u_CascadeSplits;
    float4 u_CascadeAtlas;
    float4 u_CascadeBias;
};
cbuffer LocalShadowReceiver : register(b5, space0) {
    float4x4 u_LocalShadowMatrices[16];
    float4 u_LocalShadowTiles[16];
    float4 u_LocalShadowBias[16];
    int4 u_LocalShadowViews[8];
    float4 u_LocalShadowAtlas;
};

TextureCube u_IrradianceMap : register(t0, space2);
TextureCube u_PrefilterMap : register(t1, space2);
Texture2D u_BRDFLUT : register(t2, space2);
Texture2D<float> u_CascadeDepth : register(t3, space2);
Texture2D<float> u_LocalShadowDepth : register(t4, space2);
SamplerState u_EnvironmentSampler : register(s0, space2);
SamplerComparisonState u_ShadowSampler : register(s1, space2);

#define AXLE_REFLECTION_PROBE_CBUFFER_REGISTER b6
TextureCube u_ReflectionProbeCube0 : register(t2, space4);
TextureCube u_ReflectionProbeCube1 : register(t3, space4);
TextureCube u_ReflectionProbeCube2 : register(t4, space4);
TextureCube u_ReflectionProbeCube3 : register(t5, space4);
#include "ReflectionProbeCommon.hlsli"

// @AXLE_GRAPH_DECLARATIONS@

struct MaterialSurface {
    float3 BaseColor;
    float Metallic;
    float Roughness;
    float3 Normal; // Tangent-space direction; default (0, 0, 1).
    float3 Emission;
    float AO;
    float Clearcoat;
    float ClearcoatRoughness;
    float Anisotropy;
    float AnisotropyRotation;
    float3 SheenColor;
    float SheenRoughness;
    float3 SubsurfaceColor;
    float SubsurfaceRadius;
    float3 AbsorptionColor;
    float IOR;
    float Dispersion;
    uint FeatureFlags;
};

// @AXLE_GRAPH_FUNCTION@

struct VSInput {
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
    float2 TexCoord : TEXCOORD0;
};
struct PSInput {
    float4 Position : SV_Position;
    float3 WorldPos : POSITION0;
    float3 Normal : NORMAL0;
    float4 Tangent : TANGENT0;
    float2 TexCoord : TEXCOORD0;
};

float3 AxleSafeNormal(float3 value, float3 fallback) {
    return dot(value, value) > 1e-12 ? normalize(value) : fallback;
}

float3 AxleEvaluateWorldNormal(PSInput input, MaterialSurface surface) {
    float3 geometricNormal = AxleSafeNormal(input.Normal, float3(0,1,0));
    float3 fallbackTangent = abs(geometricNormal.z) < 0.99 ? cross(float3(0,0,1), geometricNormal) : cross(float3(0,1,0), geometricNormal);
    float3 tangent = AxleSafeNormal(input.Tangent.xyz - geometricNormal*dot(input.Tangent.xyz, geometricNormal), normalize(fallbackTangent));
    float3 bitangent = cross(geometricNormal, tangent)*(input.Tangent.w < 0.0 ? -1.0 : 1.0);
    return AxleSafeNormal(tangent*surface.Normal.x + bitangent*surface.Normal.y + geometricNormal*surface.Normal.z, geometricNormal);
}

float AxleSunShadowVisibility(float3 world, float3 normal, float3 sun) {
    uint count = (uint)u_CascadeAtlas.x;
    if (count == 0u) return 1.0;
    float4 cameraClip = mul(u_ViewProjection, float4(world, 1.0));
    float4 unproject = float4(-u_Projection[3][3], u_Projection[2][3], u_Projection[3][2], -u_Projection[2][2]);
    float depth = -AxleOcclusionViewDepth(cameraClip.z / cameraClip.w, unproject);
    if (depth < u_CascadeAtlas.w || depth > u_CascadeSplits[count - 1u]) return 1.0;
    uint cascade = 0u;
    [unroll] for (uint c = 0u; c < 3u; ++c)
        if (c + 1u < count && depth > u_CascadeSplits[c]) cascade = c + 1u;
    float4 clipPosition = mul(u_CascadeMatrices[cascade], float4(world, 1.0));
    if (clipPosition.w <= 0.0) return 1.0;
    float3 clip = clipPosition.xyz / clipPosition.w;
    if (any(abs(clip.xy) > 1.0) || clip.z < 0.0 || clip.z > 1.0) return 1.0;
    float2 local = clip.xy * float2(0.5, -0.5) + 0.5;
    float2 tile = float2(cascade % 2u, cascade / 2u) * 0.5;
    float2 uv = tile + local * 0.5;
    float ndl = saturate(dot(normal, sun));
    float reference = clip.z - u_CascadeBias.x - u_CascadeBias.y * (1.0 - ndl);
    float visibility = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y) [unroll] for (int x = -1; x <= 1; ++x) {
        float2 tap = clamp(uv + float2(x, y) * u_CascadeAtlas.y * u_CascadeBias.w,
                           tile + u_CascadeAtlas.y * 0.5, tile + 0.5 - u_CascadeAtlas.y * 0.5);
        visibility += u_CascadeDepth.SampleCmpLevelZero(u_ShadowSampler, tap, reference);
    }
    return visibility / 9.0;
}

PSInput VSMain(VSInput input) {
    PSInput output;
    float4 world = mul(u_Model, float4(input.Position, 1.0));
    output.Position = mul(u_ViewProjection, world);
    output.WorldPos = world.xyz;
    float3 a = mul((float3x3)u_Model, float3(1,0,0));
    float3 b = mul((float3x3)u_Model, float3(0,1,0));
    float3 c = mul((float3x3)u_Model, float3(0,0,1));
    float sign = dot(a, cross(b,c)) < 0 ? -1.0 : 1.0;
    float3 normal = (cross(b,c)*input.Normal.x + cross(c,a)*input.Normal.y + cross(a,b)*input.Normal.z)*sign;
    output.Normal = AxleSafeNormal(normal, float3(0,1,0));
    output.Tangent = float4(mul((float3x3)u_Model, input.Tangent.xyz), input.Tangent.w*sign);
    output.TexCoord = input.TexCoord;
    return output;
}

ReflectionSurfaceOutput EvaluateReflectionSurface(PSInput input) {
    MaterialSurface surface = AxleEvaluateMaterial(input.TexCoord);
    float3 base = max(surface.BaseColor, float3(0,0,0));
    float metallic = saturate(surface.Metallic);
    float roughness = clamp(surface.Roughness, 0.04, 1.0);
    // Keep the color path's geometric TBN construction stable. The normal
    // helper is shared with PSNormalMain, but must not feed back into the
    // tangent basis used by the clear-coat lobe.
    float3 geometricNormal = AxleSafeNormal(input.Normal, float3(0,1,0));
    float3 fallbackTangent = abs(geometricNormal.z) < 0.99 ? cross(float3(0,0,1), geometricNormal) : cross(float3(0,1,0), geometricNormal);
    float3 tangent = AxleSafeNormal(input.Tangent.xyz - geometricNormal*dot(input.Tangent.xyz, geometricNormal), normalize(fallbackTangent));
    float3 bitangent = cross(geometricNormal, tangent)*(input.Tangent.w < 0.0 ? -1.0 : 1.0);
    float3 normal = AxleSafeNormal(tangent*surface.Normal.x + bitangent*surface.Normal.y + geometricNormal*surface.Normal.z, geometricNormal);
    float3 view = AxleSafeNormal(u_CameraPosition.xyz - input.WorldPos, normal);
    bool hasMultiLightSun = u_LightCounts.z > 0u;
    float3 light = AxleSafeNormal(hasMultiLightSun ? u_SunLight.DirectionAndIlluminance.xyz : u_SunDirection.xyz, normal);
    float3 halfVector = AxleSafeNormal(view + light, normal);
    float nv = max(dot(normal, view), 0.001);
    float nl = saturate(dot(normal, light));
    float3 f0 = lerp(float3(0.04,0.04,0.04), base, metallic);
    float3 fresnel = FresnelSchlick(saturate(dot(halfVector, view)), f0);
    float3 specular = DistributionGGX(normal, halfVector, roughness)*GeometrySmith(normal, view, light, roughness)*fresnel / max(4.0*nv*nl, 0.0001);
    float3 diffuse = (1.0 - fresnel)*(1.0 - metallic)*base * INV_PI;

    // Direct sun lighting using unified physical calibration scale
    static const float DIRECT_LUX_TO_RADIANCE = 0.000035;
    float sunIntensity = hasMultiLightSun ? max(u_SunLight.DirectionAndIlluminance.w, 0.0) : max(u_SunDirection.w, 0.0);
    float sunRadianceScale = hasMultiLightSun || sunIntensity > 10.0 ? DIRECT_LUX_TO_RADIANCE : 1.0;
    float3 sunColor = hasMultiLightSun ? u_SunLight.ColorAndAmbient.rgb : u_SunColor.rgb;
    float2 screenVisibility = AxleForwardVisibility(input.Position.xyz, u_Projection, u_LightReserved[1].yz);
    float3 directLight = max(sunColor, float3(0,0,0)) * (sunIntensity * sunRadianceScale) * nl * AxleDirectionalVisibility(AxleSunShadowVisibility(input.WorldPos, normal, light), screenVisibility.y);
    float3 direct = (diffuse + specular) * directLight;

    // Keep the graph path on the same Forward+ light data contract.  The
    // authored graph still controls the surface value, while the renderer
    // supplies the complete local-light list through MultiLightBuffer.
    [loop] for (uint p = 0u; p < min(u_LightCounts.x, 16u); ++p) {
        PointLightData pointLight = u_PointLights[p];
        float radius = pointLight.PositionAndRadius.w;
        float3 toLight = pointLight.PositionAndRadius.xyz - input.WorldPos;
        float distanceToLight = length(toLight);
        if (radius <= 0.0 || distanceToLight <= 1e-4 || distanceToLight >= radius) continue;
        float3 localLight = toLight / distanceToLight;
        float ratio = distanceToLight / radius;
        float window = saturate(1.0 - ratio * ratio * ratio * ratio);
        float attenuation = (window * window) / max(distanceToLight * distanceToLight, max(pointLight.Params.y, 0.0025));
        if (pointLight.Params.x > 0.1 && abs(pointLight.Params.x - 1.0) > 0.01)
            attenuation = pow(max(attenuation, 0.0), pointLight.Params.x);
        float localNl = saturate(dot(normal, localLight));
        if (localNl <= 0.0) continue;
        float3 localHalf = AxleSafeNormal(view + localLight, normal);
        float3 localF = FresnelSchlick(saturate(dot(localHalf, view)), f0);
        float3 localSpec = DistributionGGX(normal, localHalf, roughness) *
            GeometrySmith(normal, view, localLight, roughness) * localF /
            max(4.0 * nv * localNl, 0.0001);
        float3 localDiffuse = (1.0 - localF) * (1.0 - metallic) * base * INV_PI;
        float3 radiance = max(pointLight.ColorAndIntensity.rgb, 0.0) *
            (pointLight.ColorAndIntensity.w * attenuation * 0.05);
        direct += (localDiffuse + localSpec) * radiance * localNl;
    }
    [loop] for (uint s = 0u; s < min(u_LightCounts.y, 16u); ++s) {
        SpotLightData spot = u_SpotLights[s];
        float range = spot.PositionAndRange.w;
        float3 toLight = spot.PositionAndRange.xyz - input.WorldPos;
        float distanceToLight = length(toLight);
        if (range <= 0.0 || distanceToLight <= 1e-4 || distanceToLight >= range) continue;
        float3 localLight = toLight / distanceToLight;
        float cosTheta = dot(-localLight, normalize(spot.DirectionAndIntensity.xyz));
        float cone = saturate((cosTheta - spot.AngleParams.x) /
                              max(0.0001, spot.ColorAndInnerAngle.w - spot.AngleParams.x));
        if (cone <= 0.0) continue;
        float ratio = distanceToLight / range;
        float window = saturate(1.0 - ratio * ratio * ratio * ratio);
        float attenuation = (window * window) / max(distanceToLight * distanceToLight, 0.0025) * cone * cone;
        float localNl = saturate(dot(normal, localLight));
        if (localNl <= 0.0) continue;
        float3 localHalf = AxleSafeNormal(view + localLight, normal);
        float3 localF = FresnelSchlick(saturate(dot(localHalf, view)), f0);
        float3 localSpec = DistributionGGX(normal, localHalf, roughness) *
            GeometrySmith(normal, view, localLight, roughness) * localF /
            max(4.0 * nv * localNl, 0.0001);
        float3 localDiffuse = (1.0 - localF) * (1.0 - metallic) * base * INV_PI;
        float3 radiance = max(spot.ColorAndInnerAngle.rgb, 0.0) *
            (spot.DirectionAndIntensity.w * attenuation * 0.05);
        direct += (localDiffuse + localSpec) * radiance * localNl;
    }
    float3 areaDirect = 0.0;
    [loop] for (uint r = 0u; r < min((uint)u_LightReserved[1].x, 2u); ++r) {
        RectLightData rLight = u_RectLights[r];
        LTCAreaLightResult ltc = EvaluateLTCAreaLightTerms(
            rLight.PositionAndWidth,
            rLight.DirectionAndHeight,
            rLight.UpAndRange,
            rLight.ColorAndIntensity,
            input.WorldPos,
            normal,
            view,
            base,
            metallic,
            roughness,
            f0,
            0.05);
        if (ltc.RangeAttenuation > 0.0) {
            float3 areaLighting = ltc.DiffuseLighting + ltc.SpecularLighting;
            float areaCoat = saturate(surface.Clearcoat);
            if (areaCoat > 0.0) {
                LTCAreaLightResult coatLTC = EvaluateLTCAreaLightTerms(
                    rLight.PositionAndWidth, rLight.DirectionAndHeight, rLight.UpAndRange, rLight.ColorAndIntensity,
                    input.WorldPos, normal, view, 1.0, 1.0, clamp(surface.ClearcoatRoughness, 0.04, 1.0), 0.04, 0.05);
                float3 attenuation = 1.0 - areaCoat * coatLTC.SpecularAlbedo;
                areaLighting = areaLighting * attenuation * attenuation + areaCoat * coatLTC.SpecularLighting;
            }
            areaDirect += areaLighting * ltc.Radiance;
        }
    }

    // Clearcoat layer evaluation
    float coat = saturate(surface.Clearcoat);
    if (coat > 0.0) {
        float coatRoughness = clamp(surface.ClearcoatRoughness, 0.04, 1.0);
        float coatF = 0.04 + 0.96 * pow(1.0 - saturate(dot(view, halfVector)), 5.0);
        float coatBRDF = DistributionGGX(normal, halfVector, coatRoughness) * GeometrySmith(normal, view, light, coatRoughness) * coatF / max(4.0 * nv * max(nl, 0.001), 0.0001);
        direct = direct * (1.0 - coat * coatF) * (1.0 - coat * coatF) + coat * coatBRDF * directLight;
    }

    direct += areaDirect; // Area clearcoat was integrated with each emitter, independent of sun direction.

    // Ambient / indirect term scaled by AO; direct and emissive are preserved
    float3 ambientRef = FresnelSchlickRoughness(nv, f0, roughness);
    float3 ambient;
    float3 reflectionEnvironment = 0.0;
    float3 reflectionResponse = 0.0;
    float3 reflDir = reflect(-view, normal);
    float combinedAO = saturate(surface.AO) * screenVisibility.x;
    float3 diffuseAO = ComputeMultiBounceAO(combinedAO, base, saturate(u_LightReserved[1].w));
    float specAO = ComputeSpecularOcclusion(nv, combinedAO, roughness);
    float3 diffuseRatio = (screenVisibility.x > 1e-4) ? (diffuseAO / screenVisibility.x) : float3(0.0, 0.0, 0.0);
    float specRatio = (screenVisibility.x > 1e-4) ? (specAO / screenVisibility.x) : 0.0;
    if (u_LightCounts.w == 1u) {
        uint envWidth, envHeight, envLevels;
        u_PrefilterMap.GetDimensions(0, envWidth, envHeight, envLevels);
        float maxLod = max(0.0, float(envLevels) - 1.0);
        float3 ambientF = ambientRef;
        float3 ambientKD = (1.0 - ambientF) * (1.0 - metallic);
        float envIntensity = max(u_LightReserved[0].y, 0.0);
        float rotation = u_ReflectionParams.z;
        float sinRot = sin(rotation), cosRot = cos(rotation);
        float3 skyNormal = float3(cosRot*normal.x-sinRot*normal.z, normal.y, sinRot*normal.x+cosRot*normal.z);
        float3 skyReflection = float3(cosRot*reflDir.x-sinRot*reflDir.z, reflDir.y, sinRot*reflDir.x+cosRot*reflDir.z);
        float3 irradiance = u_IrradianceMap.Sample(u_EnvironmentSampler, skyNormal).rgb * envIntensity;
        float3 skyPrefiltered = u_PrefilterMap.SampleLevel(u_EnvironmentSampler, skyReflection, roughness * maxLod).rgb * envIntensity;
        float3 prefiltered = EvaluateMultiProbeBlendHLSL(input.WorldPos, reflDir, roughness, u_EnvironmentSampler, skyPrefiltered).BlendedEnvColor;
        float2 envBRDF = u_BRDFLUT.Sample(u_EnvironmentSampler, float2(nv, roughness)).rg;
        ambient = irradiance * base * ambientKD * diffuseRatio + prefiltered * (ambientF * envBRDF.x + envBRDF.y) * specRatio;
        reflectionResponse = (ambientF * envBRDF.x + envBRDF.y) * specAO;
        reflectionEnvironment = prefiltered * reflectionResponse;
    } else {
        float3 ambientColor = hasMultiLightSun ? u_SunLight.AmbientColor.rgb : max(u_SunColor.rgb, float3(0,0,0));
        float ambientStrength = hasMultiLightSun ? max(u_LightReserved[0].y, 0.0) : max(u_SunColor.w, 0.0);
        MultiProbeBlendResult probeBlend = EvaluateMultiProbeBlendHLSL(input.WorldPos, reflDir, roughness, u_EnvironmentSampler, float3(0,0,0));
        float2 envBRDF = u_BRDFLUT.Sample(u_EnvironmentSampler, float2(nv, roughness)).rg;
        float3 probeSpec = probeBlend.ProbeColor * (ambientRef * envBRDF.x + envBRDF.y);
        float3 skySpec = ambientRef * 0.02 * (1.0 - saturate(u_LightReserved[0].z)) * ambientStrength;
        ambient = base * 0.03 * (1.0 - metallic) * ambientStrength * diffuseRatio + lerp(skySpec, probeSpec, probeBlend.ProbeCoverage) * specRatio;
        reflectionResponse = (ambientRef * envBRDF.x + envBRDF.y) * specAO;
        reflectionEnvironment = lerp(skySpec, probeSpec, probeBlend.ProbeCoverage) * specAO;
    }
    float3 color = direct + ambient * screenVisibility.x + max(surface.Emission, float3(0,0,0));
    color = clamp(color, 0.0, 65000.0);
    if (u_FogParams.x > 0.0) {
        float3 cameraForward = mul((float3x3)u_FogInvView, float3(0.0, 0.0, -1.0));
        float viewDepth = dot(input.WorldPos - u_CameraPosition.xyz, cameraForward);
        float4 fog = EvaluateFroxelFogSample(u_FroxelIntegrated, u_FroxelMedia, u_EnvironmentSampler,
            input.Position.xy * u_FogScreenSize.zw, viewDepth, u_FogInvView, u_FogInvProjection,
            u_FogParams, u_FogGridParams, u_FogDepthParams);
        color = clamp(color * fog.a + (u_FogParams.w > 0.5 ? 0.0 : fog.rgb), 0.0, 65000.0);
    }

    // 11. Tone Mapping & Color Polish (ACES Filmic Operator & Gamma Correction)
    // Parity with ForwardPBR.hlsl output policy (M15-U3-23.4):
    // If u_CameraPosition.w > 0.0 (HDR Post-Processing Mode): Output pure linear HDR radiance
    // If u_CameraPosition.w <= 0.0 (Legacy SDR Fallback): Apply ACES Filmic & 2.2 Gamma
    if (u_CameraPosition.w <= 0.0) {
        // ACES Filmic (Narkowicz fit)
        const float a = 2.51;
        const float b = 0.03;
        const float c = 2.43;
        const float d = 0.59;
        const float e = 0.14;
        color = saturate((color * (a * color + b)) / (color * (c * color + d) + e));
        color = pow(color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    }
    return MakeMaterialReflectionSurfaceOutput(float4(color, 1.0), reflectionEnvironment, reflectionResponse, roughness, 1.0);
}

float4 PSMain(PSInput input) : SV_Target0 { return EvaluateReflectionSurface(input).Color; }
ReflectionSurfaceOutput PSReflectionMain(PSInput input) { return EvaluateReflectionSurface(input); }

// Material Graphs use this companion entry point for the screen-space normal
// producer. It evaluates the same authored graph function and TBN path as the
// color pass, so GTAO/SSR never silently fall back to the default descriptor.
float4 PSNormalMain(PSInput input) : SV_Target0 {
    MaterialSurface surface = AxleEvaluateMaterial(input.TexCoord);
    return float4(AxleEvaluateWorldNormal(input, surface), 1.0);
}
