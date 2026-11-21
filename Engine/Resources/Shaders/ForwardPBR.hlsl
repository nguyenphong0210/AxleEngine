#define AXLE_FORWARD_LOCAL_SHADOWS 1
#define AXLE_FORWARD_ENVIRONMENT_IBL 1
#include "Occlusion.hlsli"
#define AXLE_FROXEL_CONSTANTS_REGISTER b7
#include "FroxelFogCommon.hlsli"
// =============================================================================
// ForwardPBR.hlsl - Unified D3D12 (DXIL) & Vulkan 1.3 (SPIR-V) PBR Forward+ Pipeline
// Standardized HLSL 2021 Codebase for AxleEngine Multi-Backend 3D Viewport Renderer
// =============================================================================

// HLSL 2021 Template Utilities
template<typename T>
T ClampZeroOne(T value) {
    return clamp(value, T(0.0), T(1.0));
}

// -----------------------------------------------------------------------------
// Physical Unit Conversion (M15-U1-01.1: Frozen)
// -----------------------------------------------------------------------------
// Directional light illuminance (Lux) to scene radiance scale.
// Display-referred calibration: direct sunlight (PhysicalIlluminance::DirectSunlight
// = 100,000 Lux) on a white Lambertian surface yields ~1.0 scene-linear radiance
// after the 1/PI diffuse normalization (100000 * 0.000035 / PI ~= 1.11), i.e. a
// near-white surface before tonemapping. This is an engine calibration constant,
// NOT a photometric-to-radiometric conversion; Lux is not converted to W/m^2 here.
// Frozen: any change requires M15 review, reference image update and test adjustment.
// Mirrored by the frozen-unit test in Test_LightingServicesUnit.cpp.
static const float DIRECT_LUX_TO_RADIANCE = 0.000035;

// Local light (point/spot/rect) intensity scale. Windowed inverse-square attenuation
// is applied first, then this factor produces scene-linear radiance. Historical
// calibration value with no recorded derivation; frozen as-is by M15-U1-01.1.
// Must be revised together with DIRECT_LUX_TO_RADIANCE to keep directional/local
// perceptual parity.
static const float LOCAL_INTENSITY_SCALE = 0.05;

// -----------------------------------------------------------------------------
// Solar Angular Radius Policy (M15-U1-01.2: Frozen)
// -----------------------------------------------------------------------------
// Physical angular radius of the solar disk (0.53 degree diameter = 0.00462512 rad).
// Authored bounds are clamped to [0.0, 0.25] radians on CPU and GPU.
// Convolving the GGX roughness with this finite radius prevents zero-roughness
// specular singularities and broadens the solar highlight physically.
static const float SUN_DEFAULT_ANGULAR_RADIUS = 0.00462512;
static const float SUN_MAX_ANGULAR_RADIUS = 0.25;

// -----------------------------------------------------------------------------
// BRDF Normalization Contract (M15-U1-01.3: Frozen)
// -----------------------------------------------------------------------------
// 1. Diffuse Normalization: Lambertian BRDF f_diffuse = albedo / PI (or albedo * INV_PI).
//    Hemispherical integral of cos(theta) dOmega is PI, ensuring 100% reflectance for white albedo.
// 2. Specular Denominator: Cook-Torrance microfacet normalization denominator 4 * (N.V) * (N.L).
// 3. Energy Conservation: kD = (1 - kS) * (1 - metallic) with kS = FresnelSchlick.
//    Metals (metallic=1) have zero diffuse (kD = 0); dielectrics partition energy such that kD + kS = 1.
static const float BRDF_DIFFUSE_NORMALIZATION = 0.31830988618379067154; // 1.0 / PI
static const float BRDF_SPECULAR_DENOMINATOR_SCALE = 4.0;

// -----------------------------------------------------------------------------
// Root Constants / Push Constants (Register b0, Space 0)
// Size: EXACTLY 128 bytes (32 DWORDs) - Guaranteed hardware limit across Vulkan & D3D12
// In Vulkan (SPIR-V), DXC compiles [[vk::push_constant]] to physical push constant range
// -----------------------------------------------------------------------------
#if defined(__spirv__)
struct ObjectConstants_t {
    float4x4 u_Model;               // 64 bytes (Offset 0) - Model matrix
    float4   u_ColorPrimary;        // 16 bytes (Offset 64) - Albedo/BaseColor
    float4   u_ColorSecondary;      // 16 bytes (Offset 80) - Secondary/Pattern tint & Emissive (rgb: tint, w: emission intensity)
    float4   u_Params;              // 16 bytes (Offset 96) - x: TileScale/UVScale, y: ShadingMode, z: Roughness, w: Metallic
    float4   MaterialExtra;         // 16 bytes (Offset 112) - xy: UVOffset, z: AlphaCutoff, w: SurfaceType (0: Opaque, 1: Cutout, 2: Transparent, 3: Additive)
};
[[vk::push_constant]] ObjectConstants_t ObjectConstants;
#define u_Model (ObjectConstants.u_Model)
#define u_ColorPrimary (ObjectConstants.u_ColorPrimary)
#define u_ColorSecondary (ObjectConstants.u_ColorSecondary)
#define u_Params (ObjectConstants.u_Params)
#define u_MaterialExtra (ObjectConstants.MaterialExtra)
#define u_SunDirAndIntensity (ObjectConstants.MaterialExtra)
#define u_UVOffset (ObjectConstants.MaterialExtra.xy)
#define u_AlphaCutoff (ObjectConstants.MaterialExtra.z)
#define u_SurfaceType (ObjectConstants.MaterialExtra.w)
#else
cbuffer ObjectConstants : register(b0, space0) {
    float4x4 u_Model;               // 64 bytes (Offset 0) - Model matrix
    float4   u_ColorPrimary;        // 16 bytes (Offset 64) - Albedo/BaseColor
    float4   u_ColorSecondary;      // 16 bytes (Offset 80) - Secondary/Pattern tint & Emissive
    float4   u_Params;              // 16 bytes (Offset 96) - x: TileScale/UVScale, y: ShadingMode, z: Roughness, w: Metallic
    float4   u_MaterialExtra;       // 16 bytes (Offset 112) - xy: UVOffset, z: AlphaCutoff, w: SurfaceType
};
#define u_SunDirAndIntensity (u_MaterialExtra)
#define u_UVOffset (u_MaterialExtra.xy)
#define u_AlphaCutoff (u_MaterialExtra.z)
#define u_SurfaceType (u_MaterialExtra.w)
#endif

// Dedicated material data, matching RHIMaterialConstants exactly (256 bytes).
cbuffer MaterialConstants : register(b3, space0) {
    float4 u_MaterialUV;       // scale.xy, offset.xy
    float4 u_MaterialSurface;  // normal strength, AO strength, direct specular, environment reflections
    float4 u_CoatAnisotropy;   // clearcoat, coat roughness, anisotropy strength, rotation in turns
    uint4 u_MaterialFlags;     // texture mask, authored material, reserved, reserved
    float4 u_DetailUV;
    float4 u_DetailParameters;
    float4 u_Refraction;
    float4 u_RefractionTint;
    float4 u_RefractionFrame;
    float4 u_Sheen;            // rgb: sheen color, w: sheen roughness (>= 0 enabled)
    float4 u_Subsurface;       // rgb: subsurface color, w: scatter radius / scale (> 0 enabled)
    float4 u_Absorption;       // rgb: Beer-Lambert absorption coeff, w: Cauchy dispersion
    float4 u_MaterialReserved[4];
};
bool HasMaterialMap(uint slot) { return (u_MaterialFlags.x & (1u << slot)) != 0; }

// -----------------------------------------------------------------------------
// Scene Uniform Buffer (Register b1, Space 0)
// Size: 256 bytes (Hardware 256-byte CBV alignment)
// -----------------------------------------------------------------------------
cbuffer SceneConstants : register(b1, space0) {
    float4x4 u_ViewProjection; // 64 bytes (Offset 0)
    float4x4 u_View;           // 64 bytes (Offset 64)
    float4x4 u_Projection;     // 64 bytes (Offset 128)
    float4   u_CameraPosition; // 16 bytes (Offset 192) - xyz: Camera World Pos, w: Exposure
    float4   u_AmbientLight;   // 16 bytes (Offset 208) - rgb: Ambient Light Color, w: Ambient Intensity
    float4   u_SunDirection;   // 16 bytes (Offset 224) - xyz: Sun Vector, w: Sun Illuminance
    float4   u_SunColor;       // 16 bytes (Offset 240) - rgb: Sun Light Color, w: Shadow Bias
};

// -----------------------------------------------------------------------------
// Forward+ Multi-Light GPU Buffer (Register b2, Space 0)
// Size: 2304 bytes (256-byte aligned)
// Supports: 1 Primary Directional Sun Light (Physical Lux illuminance),
//           up to 16 Point Lights (Smooth windowed inverse-square falloff),
//           up to 16 Spot Lights (Inner/outer cone cutoff angles).
// -----------------------------------------------------------------------------
struct DirectionalSunLight {
    float4 DirectionAndIlluminance; // xyz: Normalized Sun Vector, w: Illuminance (Lux)
    float4 ColorAndAmbient;        // xyz: Sun Warm Color, w: Ambient Intensity
    float4 AmbientColor;           // xyz: Ambient Sky/Ground Tint, w: Ground bounce
    float4 ShadowParams;           // x: Shadow Bias, y: Filter Radius, z: solar angular radius (rad), w: Reserved
};

struct PointLightData {
    float4 PositionAndRadius; // xyz: World Pos, w: Radius (m)
    float4 ColorAndIntensity; // xyz: Linear RGB Color, w: Luminous Intensity (Lumens/Candela)
    float4 Params;            // x: Falloff exponent, y: MinRadius, z: Additional directional light, w: Reserved
    float4 Reserved;          // 64 bytes total
};

struct SpotLightData {
    float4 PositionAndRange;      // xyz: World Pos, w: Range (m)
    float4 DirectionAndIntensity; // xyz: Normalized Direction, w: Luminous Intensity
    float4 ColorAndInnerAngle;    // xyz: Linear RGB Color, w: cos(InnerConeAngle)
    float4 AngleParams;           // x: cos(OuterConeAngle), y: Falloff exponent, zw: Reserved
};

struct RectLightData {
    float4 PositionAndWidth;     // xyz: Pos, w: Width
    float4 DirectionAndHeight;   // xyz: Normal/Dir, w: Height
    float4 UpAndRange;           // xyz: Up, w: Range
    float4 ColorAndIntensity;    // xyz: Color, w: Intensity (sign: negative if two-sided)
};

cbuffer MultiLightBuffer : register(b2, space0) {
    DirectionalSunLight u_SunLight;
    uint4               u_LightCounts;        // x: NumPointLights (0..16), y: NumSpotLights (0..16), z: HasSunLight, w: LightingMode
    float4              u_CameraPosExposure;  // xyz: Camera Pos, w: Exposure
    float4              u_LightReserved[2];   // [0]: x disables studio lobes, y is env strength, z diffuse-only, w authored env rotation (rad); [1].x: NumRectLights
    PointLightData      u_PointLights[16];    // 16 * 64 = 1024 bytes
    SpotLightData       u_SpotLights[16];     // 16 * 64 = 1024 bytes
    RectLightData       u_RectLights[2];      // 2 * 64 = 128 bytes tail
};

// -----------------------------------------------------------------------------
// Texture & Sampler Bindings (Space 1)
// -----------------------------------------------------------------------------
Texture2D    u_AlbedoMap    : register(t0, space1);
Texture2D    u_NormalMap    : register(t1, space1);
Texture2D    u_RoughnessMap : register(t2, space1);
Texture2D    u_MetallicMap  : register(t3, space1);
Texture2D    u_AOMap        : register(t4, space1);
Texture2D    u_EmissiveMap  : register(t5, space1);
TextureCube  u_IrradianceMap: register(t6, space1);
TextureCube  u_PrefilterMap : register(t7, space1);
Texture2D    u_BRDFLUT      : register(t8, space1);
Texture2D u_ClearcoatMap : register(t9, space1);
Texture2D u_AnisotropyFlowMap : register(t10, space1);
Texture2D u_DetailNormalMap : register(t11, space1);
Texture2D u_DetailMaskMap : register(t12, space1);
Texture2D u_OpaqueSceneColor : register(t13, space1);
Texture2D<float> u_OpaqueSceneDepth : register(t14, space1);
// C13-01: matrices map world positions into right-handed ZO light clips.
cbuffer CascadeReceiver : register(b4,space0) {
    float4x4 u_CascadeMatrices[4];
    float4 u_CascadeSplits;
    float4 u_CascadeAtlas; // count, reciprocal atlas size, tile resolution, near
    float4 u_CascadeBias; // base, slope, world normal, PCF spacing
};
Texture2D<float> u_CascadeDepth : register(t15,space1);
SamplerComparisonState u_ShadowSampler : register(s2,space0);
float CascadeVisibility(uint cascade,float3 world,float3 normal,float3 sun) {
    float ndl=saturate(dot(normal,sun));
    float3 offset=normal*u_CascadeBias.z*(1-ndl);
    float4 clipPosition=mul(u_CascadeMatrices[cascade],float4(world+offset,1));
    float3 clip=clipPosition.xyz/clipPosition.w;
    if (any(abs(clip.xy)>1) || clip.z<0 || clip.z>1) return 1;
    float2 local=clip.xy*float2(.5,-.5)+.5;
    float2 tile=float2(cascade%2,cascade/2)*.5;
    float2 uv=tile+local*.5;
    float halfTexel=u_CascadeAtlas.y*.5;
    float reference=clip.z-u_CascadeBias.x-u_CascadeBias.y*(1-ndl);
    float visibility=0;
    [unroll] for(int y=-2;y<=2;++y) [unroll] for(int x=-2;x<=2;++x) {
        float2 tap=clamp(uv+float2(x,y)*u_CascadeAtlas.y*u_CascadeBias.w,tile+halfTexel,tile+.5-halfTexel);
        visibility+=u_CascadeDepth.SampleCmpLevelZero(u_ShadowSampler,tap,reference);
    }
    return visibility/25;
}
float SunShadowVisibility(float3 world,float3 normal,float3 sun,float depth) {
    uint count=(uint)u_CascadeAtlas.x;
    if(count==0) return 1;
    if(depth<u_CascadeAtlas.w || depth>u_CascadeSplits[count-1]) return 1;
    uint cascade=0;
    [unroll] for(uint c=0;c<3;++c) if(c+1<count && depth>u_CascadeSplits[c]) cascade=c+1;
    float value=CascadeVisibility(cascade,world,normal,sun);
    // Blend the final 10% of a split, and fade out at max shadow distance.
    float start=cascade==0?u_CascadeAtlas.w:u_CascadeSplits[cascade-1];
    float end=u_CascadeSplits[cascade];
    float blend=saturate((depth-lerp(start,end,.9))/max((end-start)*.1,1e-6));
    if(blend>0) value=lerp(value,cascade+1<count?CascadeVisibility(cascade+1,world,normal,sun):1,blend);
    return value;
}
#include "LocalShadows.hlsli"
SamplerState u_EnvironmentSampler : register(s3,space0);
SamplerState u_ScreenSampler : register(s1, space0);
SamplerState u_Sampler      : register(s0, space0);

#define AXLE_REFLECTION_PROBE_CBUFFER_REGISTER b6
TextureCube u_ReflectionProbeCube0 : register(t2, space4);
TextureCube u_ReflectionProbeCube1 : register(t3, space4);
TextureCube u_ReflectionProbeCube2 : register(t4, space4);
TextureCube u_ReflectionProbeCube3 : register(t5, space4);
#include "ReflectionProbeCommon.hlsli"

#define AXLE_DDGI_CBUFFER_REGISTER b8
Texture2D<float4> u_DDGIIrradiance : register(t7, space4);
Texture2D<float4> u_DDGIDistance : register(t8, space4);
#include "DDGICommon.hlsli"

Texture3D<float4> u_FroxelIntegrated : register(t0, space5);
Texture3D<float4> u_FroxelMedia      : register(t1, space5);

float4 ForwardFogAt(float2 uv, float viewDepth) {
    return EvaluateFroxelFogSample(u_FroxelIntegrated, u_FroxelMedia, u_ScreenSampler,
        uv, viewDepth, u_FogInvView, u_FogInvProjection, u_FogParams, u_FogGridParams, u_FogDepthParams);
}

float3 RefractedBackground(float2 uv, float viewDepth, float4 frontFog) {
    float3 background = u_OpaqueSceneColor.SampleLevel(u_ScreenSampler, uv, 0.0).rgb;
    if (u_FogParams.x <= 0.0) return background;
    // The copy contains the full fogged ray. Remove its near segment so the
    // material tints only transmitted light, then use the actual foreground ray.
    float4 sampledFront = ForwardFogAt(uv, viewDepth);
    if (sampledFront.a <= 1e-4) return float3(0.0, 0.0, 0.0);
    return max(background - sampledFront.rgb, 0.0) * (frontFog.a / sampledFront.a);
}

float NearestRefractedDepth(float2 uv) {
    uint2 size;
    u_OpaqueSceneDepth.GetDimensions(size.x, size.y);
    float2 footprint = uv * float2(size) - 0.5;
    int2 first = int2(floor(footprint));
    // Test every contributing color tap. Filtering depth can hide a foreground silhouette.
    int2 last = first + int2(frac(footprint) > 0.0);
    int2 limit = int2(size) - 1;
    first = clamp(first, 0, limit);
    last = clamp(last, 0, limit);
    float top = min(u_OpaqueSceneDepth.Load(int3(first, 0)),
                    u_OpaqueSceneDepth.Load(int3(last.x, first.y, 0)));
    float bottom = min(u_OpaqueSceneDepth.Load(int3(first.x, last.y, 0)),
                       u_OpaqueSceneDepth.Load(int3(last, 0)));
    return min(top, bottom);
}

float3 RotateEnvironmentDirection(float3 direction) {
    float angle=u_LightReserved[0].w;
    float s=sin(angle), c=cos(angle);
    return float3(c*direction.x-s*direction.z,direction.y,s*direction.x+c*direction.z);
}

// -----------------------------------------------------------------------------
// Vertex Input Layout (Matches 64-byte RHIVertex3D)
// -----------------------------------------------------------------------------
struct VSInput {
    float3 Position   : POSITION;
    float3 Normal     : NORMAL;
    float4 Tangent    : TANGENT;
    float2 TexCoord   : TEXCOORD0;
    float4 Color      : COLOR0;
};

// -----------------------------------------------------------------------------
// Pixel / Interpolator Input
// -----------------------------------------------------------------------------
struct PSInput {
    float4 PositionCS  : SV_Position;
    float3 WorldPos    : POSITION;
    float3 NormalWS    : NORMAL;
    float4 TangentWS   : TANGENT;
    float2 TexCoord    : TEXCOORD0;
    float4 VertexColor : COLOR0;
    float3 LocalPos    : TEXCOORD1;
};

// =============================================================================
// VERTEX SHADER ENTRY POINT
// =============================================================================
PSInput VSMain(VSInput input) {
    PSInput output;
    float4 worldPos    = mul(u_Model, float4(input.Position, 1.0));
    output.PositionCS  = mul(u_ViewProjection, worldPos);
    output.WorldPos    = worldPos.xyz;
    // Cofactors implement inverse-transpose without requiring a second matrix.
    float3 a=mul((float3x3)u_Model,float3(1,0,0));
    float3 b=mul((float3x3)u_Model,float3(0,1,0));
    float3 c=mul((float3x3)u_Model,float3(0,0,1));
    float handedness=dot(a,cross(b,c))<0.0?-1.0:1.0;
    float3 transformedNormal=(cross(b,c)*input.Normal.x+cross(c,a)*input.Normal.y+cross(a,b)*input.Normal.z)*handedness;
    output.NormalWS=dot(transformedNormal,transformedNormal)>1e-12?normalize(transformedNormal):float3(0,1,0);
    float3 transformedTangent=mul((float3x3)u_Model,input.Tangent.xyz);
    output.TangentWS=float4(transformedTangent,input.Tangent.w*handedness);
    output.TexCoord    = input.TexCoord;
    output.VertexColor = input.Color;
    output.LocalPos    = input.Position;
    return output;
}

// =============================================================================
// PHYSICALLY BASED RENDERING (PBR) BRDF FUNCTIONS
// =============================================================================
#include "PBRCommon.hlsli"

// GLSL-compatible modulo utility: mod(x, y) = x - y * floor(x / y)
// Ensures 100% exact parity with OpenGL across positive and negative coordinate axes.
float glsl_mod(float x, float y) {
    return x - y * floor(x / y);
}

float2 AnisotropicAxes(float roughness, float anisotropy) {
    float aspect = sqrt(1.0 - 0.9 * abs(clamp(anisotropy, -1.0, 1.0)));
    float a = max(roughness * roughness, 0.001);
    return anisotropy >= 0 ? float2(a / aspect, a * aspect) : float2(a * aspect, a / aspect);
}
float MaterialDistribution(float3 N, float3 H, float3 T, float3 B, float roughness, float anisotropy) {
    float2 axes = AnisotropicAxes(roughness,anisotropy);
    float3 v = float3(dot(T,H) / axes.x, dot(B,H) / axes.y, dot(N,H));
    float d = dot(v, v);
    return 1.0 / max(PI * axes.x * axes.y * d * d, 1e-8);
}
float AnisotropicLambda(float3 N, float3 W, float3 T, float3 B, float2 axes) {
    float nw = max(abs(dot(N, W)), 0.001);
    float2 projected = float2(dot(T,W), dot(B,W)) * axes;
    return 0.5 * (sqrt(1.0 + dot(projected,projected) / (nw*nw)) - 1.0);
}
float MaterialGeometry(float3 N, float3 V, float3 L, float3 T, float3 B, float roughness, float anisotropy) {
    float2 axes = AnisotropicAxes(roughness,anisotropy);
    return 1.0 / (1.0 + AnisotropicLambda(N,V,T,B,axes) + AnisotropicLambda(N,L,T,B,axes));
}
// The fitted LTC is isotropic. Preserve authored anisotropic material response with
// the existing representative-point approximation until an anisotropic LTC fit is available.
float3 EvaluateAnisotropicAreaSpecular(RectLightData light, float3 P, float3 N, float3 V,
    float3 T, float3 B, float roughness, float anisotropy, float3 F0, float diffuseFormFactor) {
    float3 D = normalize(light.DirectionAndHeight.xyz);
    float3 U = normalize(light.UpAndRange.xyz);
    float3 R = normalize(cross(D, U));
    U = cross(R, D);
    float3 center = light.PositionAndWidth.xyz;
    float3 samplePosition = center;
    float denominator = dot(reflect(-V, N), D);
    if (abs(denominator) > 1e-5) {
        float distance = dot(center - P, D) / denominator;
        if (distance > 0.0) samplePosition = P + reflect(-V, N) * distance;
    }
    float2 halfSize = max(float2(light.PositionAndWidth.w, abs(light.DirectionAndHeight.w)) * 0.5, 0.005);
    float2 local = float2(dot(samplePosition - center, R), dot(samplePosition - center, U));
    if (light.DirectionAndHeight.w < 0.0) {
        float radius = length(local / halfSize);
        if (radius > 1.0) local /= radius;
    } else {
        local = clamp(local, -halfSize, halfSize);
    }
    samplePosition = center + R * local.x + U * local.y;
    float3 toPoint = samplePosition - P;
    float pointLength = length(toPoint);
    if (pointLength < 1e-5) return 0.0;
    float3 L = toPoint / pointLength;
    float nl = saturate(dot(N, L));
    float nv = max(dot(N, V), 0.001);
    float3 H = normalize(V + L);
    float3 brdf = MaterialDistribution(N, H, T, B, roughness, anisotropy) *
        MaterialGeometry(N, V, L, T, B, roughness, anisotropy) *
        FresnelSchlick(saturate(dot(V, H)), F0) / max(4.0 * nv * nl, 0.001);
    float centerLength = length(center - P);
    float centerNl = centerLength > 1e-5 ? saturate(dot(N, (center - P) / centerLength)) : 1.0;
    float solidAngle = min(2.0 * PI, PI * diffuseFormFactor / max(centerNl, 0.05));
    return brdf * nl * solidAngle;
}

float3 ApplyClearcoat(float3 baseLighting, float3 N, float3 V, float3 L, float nl, float coatStrength, float coatRoughness) {
    float coat = saturate(coatStrength) * u_MaterialSurface.z;
    float r = clamp(coatRoughness, 0.04, 1.0);
    float3 H = normalize(V+L);
    float f = 0.04 + 0.96 * pow(1.0-saturate(dot(V,H)), 5.0);
    float nv = max(dot(N,V), 0.001);
    float coatBRDF = DistributionGGX(N,H,r) * GeometrySmith(N,V,L,r) * f / max(4.0*nv*max(nl,0.001),0.0001);
    return baseLighting * (1.0-coat*f) * (1.0-coat*f) + coat * coatBRDF * nl;
}

void ApplyClearcoatSplit(
    inout float3 diffLighting,
    inout float3 specLighting,
    float3 N,
    float3 V,
    float3 L,
    float nl,
    float coatStrength,
    float coatRoughness) {
    float coat = saturate(coatStrength) * u_MaterialSurface.z;
    float r = clamp(coatRoughness, 0.04, 1.0);
    float3 H = normalize(V + L);
    float f = 0.04 + 0.96 * pow(1.0 - saturate(dot(V, H)), 5.0);
    float nv = max(dot(N, V), 0.001);
    float coatBRDF = DistributionGGX(N, H, r) * GeometrySmith(N, V, L, r) * f / max(4.0 * nv * max(nl, 0.001), 0.0001);
    float atten = (1.0 - coat * f) * (1.0 - coat * f);
    diffLighting *= atten;
    specLighting = specLighting * atten + coat * coatBRDF * nl;
}

// =============================================================================
// PIXEL SHADER ENTRY POINT
// =============================================================================
// RG normal maps use the +Y convention; reconstruct Z before applying strength.
float3 DecodeMaterialNormal(float2 rg,float strength) {
    float2 xy=rg*2.0-1.0;
    float z=sqrt(saturate(1.0-dot(xy,xy)));
    float3 n=float3(xy*max(0.0,strength),max(z,1e-4));
    return normalize(n);
}

struct ForwardSurfaceEval {
    float4 Color;
    float3 Environment;
    float3 Response;
    float Roughness;
    float ReflectionEnabled;
    float3 DiffuseSSS;
    float4 ControlSSS;
    float3 CrispSpecularAndEmissive;
};

ForwardSurfaceEval MakeForwardSurfaceEval(
    float4 color,
    float3 environment,
    float3 response,
    float roughness,
    float reflectionEnabled,
    float3 diffuseSSS,
    float4 controlSSS,
    float3 crispSpecularAndEmissive) {
    ForwardSurfaceEval result;
    result.Color = color;
    result.Environment = environment;
    result.Response = response;
    result.Roughness = roughness;
    result.ReflectionEnabled = reflectionEnabled;
    result.DiffuseSSS = diffuseSSS;
    result.ControlSSS = controlSSS;
    result.CrispSpecularAndEmissive = crispSpecularAndEmissive;
    return result;
}

ForwardSurfaceEval EvaluateForwardSurface(PSInput input, bool ddgiCapture = false) {
    // 1. Unpack Shading Controls from Object Push Constants
    float tileScale = max(0.01, u_Params.x);
    int shadingMode = (int)u_Params.y;
    float roughness = clamp(u_Params.z, 0.04, 1.0);
    float metallic  = clamp(u_Params.w, 0.0, 1.0);
    
    // 2. BaseColor / Albedo & Material Property Evaluation
    float4 albedo = u_ColorPrimary;
    float ao = 1.0;
    float3 emissive = float3(0.0, 0.0, 0.0);
    
    if (shadingMode == 0) {
        // Mode 0: Procedural World XZ Caro / Checkerboard Pattern (Matches OpenGL Floor)
        float2 coord = floor(input.WorldPos.xz * tileScale);
        float pattern = glsl_mod(coord.x + coord.y, 2.0);
        albedo = lerp(u_ColorPrimary, u_ColorSecondary, pattern);
    } else if (shadingMode == 1) {
        // Mode 1: Local 3D Caro / Checkerboard Pattern (Matches OpenGL 3D Mesh default)
        float3 coord = floor((input.LocalPos + float3(0.5, 0.5, 0.5)) * tileScale + float3(0.0001, 0.0001, 0.0001));
        float pattern = glsl_mod(coord.x + coord.y + coord.z, 2.0);
        albedo = lerp(u_ColorPrimary, u_ColorSecondary, pattern);
    } else if (shadingMode == 2) {
        // Mode 2: Spherical Caro Pattern (Matches OpenGL Spheres)
        float3 nLocal = normalize(input.LocalPos);
        float uSph = atan2(nLocal.z, nLocal.x) / (2.0 * PI) + 0.5;
        float vSph = asin(clamp(nLocal.y, -1.0, 1.0)) / PI + 0.5;
        float2 coord = floor(float2(uSph * tileScale, vSph * (tileScale * 0.5)));
        float pattern = glsl_mod(coord.x + coord.y, 2.0);
        albedo = lerp(u_ColorPrimary, u_ColorSecondary, pattern);
    } else if (shadingMode == 3) {
        // Mode 3: Solid Color / Solid PBR Material (Matches OpenGL PatternMode 3)
        albedo = u_ColorPrimary;
        if (u_ColorSecondary.w > 0.0) {
            emissive = u_ColorSecondary.rgb * u_ColorSecondary.w;
        }
    } else if (shadingMode == 4) {
        // Mode 4: Splatmap Terrain Heightfield (Blended with Vertex Color)
        albedo = u_ColorPrimary * input.VertexColor;
    } else if (shadingMode == 5) {
        // Mode 5: Textured PBR Material (Separate Maps: Albedo, Roughness, Metallic, AO)
        float2 uv = (u_MaterialFlags.y != 0 ? input.TexCoord * u_MaterialUV.xy + u_MaterialUV.zw : input.TexCoord * tileScale + u_UVOffset);
        float4 albedoSample = (HasMaterialMap(0) ? u_AlbedoMap.Sample(u_Sampler, uv) : float4(1, 1, 1, 1));
        albedo = albedoSample * u_ColorPrimary;
        
        roughness = (HasMaterialMap(2) ? u_RoughnessMap.Sample(u_Sampler, uv).r : 1.0) * roughness;
        roughness = clamp(roughness, 0.04, 1.0);
        
        metallic = (HasMaterialMap(3) ? u_MetallicMap.Sample(u_Sampler, uv).r : 1.0) * metallic;
        metallic = clamp(metallic, 0.0, 1.0);
        
        ao = (HasMaterialMap(4) ? u_AOMap.Sample(u_Sampler, uv).r : 1.0);
        float3 emissiveSample = (HasMaterialMap(5) ? u_EmissiveMap.Sample(u_Sampler, uv).rgb : float3(1, 1, 1));
        emissive = emissiveSample * u_ColorSecondary.rgb * max(0.0, u_ColorSecondary.w);
    } else if (shadingMode == 6) {
        // Mode 6: Packed ORM Textured PBR (R: Occlusion, G: Roughness, B: Metallic)
        float2 uv = (u_MaterialFlags.y != 0 ? input.TexCoord * u_MaterialUV.xy + u_MaterialUV.zw : input.TexCoord * tileScale + u_UVOffset);
        float4 albedoSample = (HasMaterialMap(0) ? u_AlbedoMap.Sample(u_Sampler, uv) : float4(1, 1, 1, 1));
        albedo = albedoSample * u_ColorPrimary;
        
        // Sample Packed ORM from slot t2 (u_RoughnessMap)
        float3 orm = HasMaterialMap(2) ? u_RoughnessMap.Sample(u_Sampler, uv).rgb : float3(1, 1, 1);
        ao = orm.r;
        roughness = clamp(orm.g * roughness, 0.04, 1.0);
        metallic = clamp(orm.b * metallic, 0.0, 1.0);
        
        float3 emissiveSample = (HasMaterialMap(5) ? u_EmissiveMap.Sample(u_Sampler, uv).rgb : float3(1, 1, 1));
        emissive = emissiveSample * u_ColorSecondary.rgb * max(0.0, u_ColorSecondary.w);
    }
    
    float4 coatAnisotropy=u_CoatAnisotropy;
    if (HasMaterialMap(6)) {
        float2 coatUV=input.TexCoord*u_MaterialUV.xy+u_MaterialUV.zw;
        float2 coatMap=u_ClearcoatMap.Sample(u_Sampler,coatUV).rg;
        coatAnisotropy.x*=saturate(coatMap.r);
        coatAnisotropy.y*=1.0-saturate(coatMap.g); // Godot-compatible green glossiness.
    }

    // Cutout alpha test (SurfaceType == 1): discard fragment if alpha < cutoff
    int surfaceType = (int)(u_SurfaceType + 0.5);
    float alphaCutoff = saturate(u_AlphaCutoff);
    ao = lerp(1.0, ao, saturate(u_MaterialSurface.y));
    if (surfaceType == 1 && albedo.a < alphaCutoff) {
        discard;
    }
    
    // 3. Normal Vector & Tangent Space Perturbation
    float3 N = normalize(input.NormalWS);
    if ((HasMaterialMap(1)||HasMaterialMap(8)) && abs(input.TangentWS.w)>0.1) {
        float3 T=input.TangentWS.xyz-N*dot(N,input.TangentWS.xyz);
        float3 fallbackTangent = abs(N.z) < 0.99 ? cross(float3(0,0,1), N) : cross(float3(0,1,0), N);
        T=dot(T,T)>1e-12?normalize(T):normalize(fallbackTangent);
        float3 B=cross(N,T)*(input.TangentWS.w<0.0?-1.0:1.0);
        float2 uv=input.TexCoord*u_MaterialUV.xy+u_MaterialUV.zw;
        float3 baseNormal=float3(0,0,1);
        if (HasMaterialMap(1)) baseNormal=DecodeMaterialNormal(u_NormalMap.Sample(u_Sampler,uv).rg,u_MaterialSurface.x);
        if (HasMaterialMap(8)) {
            float2 detailUV=input.TexCoord*u_DetailUV.xy+u_DetailUV.zw;
            float mask=HasMaterialMap(9)?u_DetailMaskMap.Sample(u_Sampler,detailUV).r:1.0;
            float3 detail=DecodeMaterialNormal(u_DetailNormalMap.Sample(u_Sampler,detailUV).rg,u_DetailParameters.x*saturate(mask));
            // Reoriented normal mapping: either flat input preserves the other layer.
            float3 t=baseNormal+float3(0,0,1);
            float3 u=detail*float3(-1,-1,1);
            baseNormal=normalize(t*dot(t,u)/max(t.z,1e-5)-u);
        }
        N=normalize(mul(baseNormal,float3x3(T,B,N)));
    }

    // 4. View Vector
    float3 camPos = (length(u_CameraPosition.xyz) > 0.001) ? u_CameraPosition.xyz : float3(0.0, 5.0, 10.0);
    float3 V = normalize(camPos - input.WorldPos);
    if (dot(N, V) < 0.0) {
        N = -N;
    }
    float NdotV = max(dot(N, V), 0.001);
    
    float3 tangent = input.TangentWS.xyz - N * dot(N, input.TangentWS.xyz);
    if (dot(tangent, tangent) < 1e-8) tangent = cross(abs(N.y) < 0.99 ? float3(0,1,0) : float3(1,0,0), N);
    tangent = normalize(tangent);
    float3 bitangent = normalize(cross(N, tangent)) * (input.TangentWS.w < 0.0 ? -1.0 : 1.0);
    float sn, cs;
    sincos(coatAnisotropy.w * 2.0 * PI, sn, cs);
    if (HasMaterialMap(7)) {
        float2 flowUV=input.TexCoord*u_MaterialUV.xy+u_MaterialUV.zw;
        float4 flowMap=u_AnisotropyFlowMap.Sample(u_Sampler,flowUV);
        float2 flow=flowMap.rg*2.0-1.0;
        if (dot(flow,flow)>1e-6) {
            flow=normalize(flow);
            float rotatedSin=sn*flow.x+cs*flow.y;
            cs=cs*flow.x-sn*flow.y;
            sn=rotatedSin;
        }
        coatAnisotropy.z*=saturate(flowMap.a);
    }
    float3 rotatedT = tangent * cs + bitangent * sn;
    bitangent = bitangent * cs - tangent * sn;
    tangent = rotatedT;

    // 5. Base Reflectivity F0 (Dielectric = 0.04, Metal = Albedo)
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo.rgb, metallic);

    // M14-U1.1: Kulla-Conty/Filament GGX energy compensation.
    // The existing DFG LUT stores the Fresnel split terms A and B. Their sum
    // is the directional single-scatter GGX energy r for this view and
    // roughness. Scaling the single-scatter lobe by
    //     1 + F0 * (1/r - 1)
    // restores the energy lost to subsequent microfacet bounces while
    // preserving the existing LUT, descriptor layout, and fallback paths.
    // Keep a finite floor for grazing/high-roughness samples and malformed
    // fallback LUTs; the final HDR clamp remains the last safety boundary.
    float2 ggxDFG = u_BRDFLUT.SampleLevel(u_EnvironmentSampler, float2(NdotV, roughness), 0.0).rg;
    float ggxSingleScatterEnergy = ggxDFG.x + ggxDFG.y;
    // The resident neutral fallback LUT has r=1. A missing/corrupt bake must
    // also stay neutral instead of magnifying every direct and indirect lobe.
    if (!isfinite(ggxSingleScatterEnergy) || ggxSingleScatterEnergy <= 0.0)
        ggxSingleScatterEnergy = 1.0;
    ggxSingleScatterEnergy = clamp(ggxSingleScatterEnergy, 0.05, 1.0);
    float3 ggxEnergyCompensation = min(1.0 + F0 * (1.0 / ggxSingleScatterEnergy - 1.0), 8.0);
    
    // Direct Radiance Accumulation (split into diffuse + crisp specular for SSS)
    float2 screenVisibility = surfaceType < 2 ? AxleForwardVisibility(input.PositionCS.xyz, u_Projection, u_LightReserved[1].yz) : float2(1.0, 1.0);
    ao *= screenVisibility.x;
    float3 diffLo = float3(0.0, 0.0, 0.0);
    float3 specLo = float3(0.0, 0.0, 0.0);
    const bool hasSSS = (surfaceType < 2 && u_Subsurface.w > 0.001);
    const float sssGain = hasSSS ? min(u_Subsurface.w, 1.0) * 0.25 : 0.0;

    // 6. Primary Directional Sun Light (Physical Illuminance in Lux)
    bool hasMultiLightSun = (u_LightCounts.z > 0);
    float3 sunDir = hasMultiLightSun ? normalize(u_SunLight.DirectionAndIlluminance.xyz) 
                                     : (length(u_SunDirection.xyz) > 0.01 ? normalize(u_SunDirection.xyz) : normalize(float3(0.45, 0.85, 0.25)));

    float sunIntensity = hasMultiLightSun ? max(u_SunLight.DirectionAndIlluminance.w,0.0) * DIRECT_LUX_TO_RADIANCE : max(u_SunDirection.w,0.0);
    float3 sunColor = hasMultiLightSun ? u_SunLight.ColorAndAmbient.rgb : u_SunColor.rgb;
    float shadowDepth = ddgiCapture ? dot(u_DDGICaptureShadowDepth, float4(input.WorldPos, 1.0))
                                    : -mul(u_View, float4(input.WorldPos, 1.0)).z;
    float3 sunRadiance = sunColor * sunIntensity *
        AxleDirectionalVisibility(SunShadowVisibility(input.WorldPos, N, sunDir, shadowDepth), screenVisibility.y);

    float NdotL_Sun = max(dot(N, sunDir), 0.0);
    // Photographic Studio Softbox Diffuse Wrap (Area Light Approximation)
    // Smooths the terminator so white dielectric materials form silky, continuous gradients without harsh dark crescent smudges
    float sunWrap = 0.0;
    float NdotL_Sun_Diff = saturate((dot(N, sunDir) + sunWrap) / ((1.0 + sunWrap) * (1.0 + sunWrap)));

    if (NdotL_Sun > 0.0 || NdotL_Sun_Diff > 0.0) {
        float3 H = normalize(V + sunDir);
        // Convolve the microfacet lobe with the finite solar disk. The default
        // radius is the measured 0.53 degree solar diameter; wider authored
        // radii soften the highlight while remaining energy bounded.
        float sunAngularRadius = hasMultiLightSun ? clamp(u_SunLight.ShadowParams.z, 0.0, SUN_MAX_ANGULAR_RADIUS) : SUN_DEFAULT_ANGULAR_RADIUS;
        float sunConvolvedRoughness = sqrt(saturate(roughness * roughness + sunAngularRadius * sunAngularRadius));
        float  NDF = MaterialDistribution(N, H, tangent, bitangent, sunConvolvedRoughness, coatAnisotropy.z);
        float  G   = MaterialGeometry(N, V, sunDir, tangent, bitangent, sunConvolvedRoughness, coatAnisotropy.z);
        float3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 numerator   = NDF * G * F;
        float  denominator = BRDF_SPECULAR_DENOMINATOR_SCALE * NdotV * max(NdotL_Sun, 0.001) + 0.0001;
        float3 specular    = numerator / denominator * u_MaterialSurface.z * ggxEnergyCompensation;

        float3 kS = F;
        float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);
        float3 diffuse = kD * albedo.rgb * BRDF_DIFFUSE_NORMALIZATION;

        float3 sunDiff = diffuse * NdotL_Sun_Diff;
        float3 sunSpec = specular * NdotL_Sun;
        ApplyClearcoatSplit(sunDiff, sunSpec, N, V, sunDir, NdotL_Sun, coatAnisotropy.x, coatAnisotropy.y);
        ApplyCharlieSheenDirectSplit(sunDiff, sunSpec, N, H, NdotL_Sun, NdotV, u_Sheen.rgb, u_Sheen.w, u_MaterialSurface.z);
        diffLo += sunDiff * sunRadiance;
        specLo += sunSpec * sunRadiance;
        if (hasSSS) {
            float wrap = 0.5;
            float NdotL_wrap = saturate((dot(N, sunDir) + wrap) / ((1.0 + wrap) * (1.0 + wrap)));
            float3 scatterDir = -(sunDir + N * 0.3);
            float VdotS = max(dot(V, normalize(scatterDir)), 0.0);
            float sssTransmittance = pow(VdotS, 4.0) * 0.5;
            float3 sssLighting = (NdotL_wrap * (1.0 - metallic) + sssTransmittance) * u_Subsurface.rgb * sssGain;
            diffLo += sssLighting * sunRadiance * (1.0 - metallic);
        }
    }

    // 7. Local lights and additional directional lights (up to 16 combined)
    uint numPointLights = min(u_LightCounts.x, 16u);
    for (uint p = 0; p < numPointLights; ++p) {
        PointLightData pLight = u_PointLights[p];
        float3 L;
        float3 radiance;
        if (pLight.Params.z > 0.5) {
            L = normalize(pLight.PositionAndRadius.xyz);
            radiance = pLight.ColorAndIntensity.rgb * pLight.ColorAndIntensity.w;
        } else {
            float radius = pLight.PositionAndRadius.w;
            if (radius <= 0.0) continue;
            float3 lightVec = pLight.PositionAndRadius.xyz - input.WorldPos;
            float dist = length(lightVec);
            if (dist >= radius || dist <= 0.0001) continue;
            L = lightVec / dist;

            // Smooth windowed inverse-square attenuation.
            float minRadius = max(0.0025, pLight.Params.y);
            float distSq = max(dist * dist, minRadius);
            float ratio = dist / radius;
            float ratio4 = ratio * ratio * ratio * ratio;
            float window = ClampZeroOne(1.0 - ratio4);
            float attenuation = (window * window) / distSq;
            float falloffExp = pLight.Params.x;
            if (falloffExp > 0.1 && abs(falloffExp - 1.0) > 0.01) {
                attenuation = pow(max(attenuation, 0.0), falloffExp);
            }
            radiance = pLight.ColorAndIntensity.rgb * (pLight.ColorAndIntensity.w * attenuation * LOCAL_INTENSITY_SCALE) * PointShadowVisibility(p,pLight.PositionAndRadius.xyz,input.WorldPos,N,L);
        }
        float NdotL = max(dot(N, L), 0.0);
        float pWrap = 0.0;
        float NdotL_Diff = saturate((dot(N, L) + pWrap) / ((1.0 + pWrap) * (1.0 + pWrap)));
        if (NdotL <= 0.0 && NdotL_Diff <= 0.0) continue;

        float3 H = normalize(V + L);
        float  NDF = MaterialDistribution(N, H, tangent, bitangent, roughness, coatAnisotropy.z);
        float  G   = MaterialGeometry(N, V, L, tangent, bitangent, roughness, coatAnisotropy.z);
        float3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 numerator   = NDF * G * F;
        float  denominator = BRDF_SPECULAR_DENOMINATOR_SCALE * NdotV * max(NdotL, 0.001) + 0.0001;
        float3 specular    = numerator / denominator * u_MaterialSurface.z * ggxEnergyCompensation;

        float3 kS = F;
        float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);
        float3 diffuse = kD * albedo.rgb * BRDF_DIFFUSE_NORMALIZATION;

        float3 pointDiff = diffuse * NdotL_Diff;
        float3 pointSpec = specular * NdotL;
        ApplyClearcoatSplit(pointDiff, pointSpec, N, V, L, NdotL, coatAnisotropy.x, coatAnisotropy.y);
        ApplyCharlieSheenDirectSplit(pointDiff, pointSpec, N, H, NdotL, NdotV, u_Sheen.rgb, u_Sheen.w, u_MaterialSurface.z);
        diffLo += pointDiff * radiance;
        specLo += pointSpec * radiance;
        if (hasSSS) {
            float wrap = 0.5;
            float NdotL_wrap = saturate((dot(N, L) + wrap) / ((1.0 + wrap) * (1.0 + wrap)));
            float3 scatterDir = -(L + N * 0.3);
            float VdotS = max(dot(V, normalize(scatterDir)), 0.0);
            float sssTransmittance = pow(VdotS, 4.0) * 0.5;
            float3 sssLighting = (NdotL_wrap * (1.0 - metallic) + sssTransmittance) * u_Subsurface.rgb * sssGain;
            diffLo += sssLighting * radiance * (1.0 - metallic);
        }
    }

    // 8. Spot Lights (up to 16 with inner/outer cone cutoff angles)
    uint numSpotLights = min(u_LightCounts.y, 16u);
    for (uint s = 0; s < numSpotLights; ++s) {
        SpotLightData sLight = u_SpotLights[s];
        float range = sLight.PositionAndRange.w;
        if (range <= 0.0) continue;

        float3 lightVec = sLight.PositionAndRange.xyz - input.WorldPos;
        float dist = length(lightVec);
        if (dist >= range || dist <= 0.0001) continue;

        float3 L = lightVec / dist;
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        // Direction cone angle evaluation
        float3 spotDir = normalize(sLight.DirectionAndIntensity.xyz);
        float cosTheta = dot(-L, spotDir);
        float cosInner = sLight.ColorAndInnerAngle.w;
        float cosOuter = sLight.AngleParams.x;

        if (cosTheta <= cosOuter) continue; // Outside outer cone cutoff

        // Smooth quadratic cutoff between inner and outer angles
        float spotAtten = ClampZeroOne((cosTheta - cosOuter) / max(0.0001, cosInner - cosOuter));
        spotAtten = spotAtten * spotAtten;

        // Smooth windowed distance attenuation
        float distSq = max(dist * dist, 0.0025);
        float ratio = dist / range;
        float ratio4 = ratio * ratio * ratio * ratio;
        float window = ClampZeroOne(1.0 - ratio4);
        float distAtten = (1.0 / distSq) * (window * window);

        float totalAtten = distAtten * spotAtten;
        float3 radiance = sLight.ColorAndInnerAngle.rgb * (sLight.DirectionAndIntensity.w * totalAtten * LOCAL_INTENSITY_SCALE) * LocalShadowVisibility(u_LocalShadowViews[4+s/4][s%4],input.WorldPos,N,L);

        float3 H = normalize(V + L);
        float  NDF = MaterialDistribution(N, H, tangent, bitangent, roughness, coatAnisotropy.z);
        float  G   = MaterialGeometry(N, V, L, tangent, bitangent, roughness, coatAnisotropy.z);
        float3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 numerator   = NDF * G * F;
        float  denominator = BRDF_SPECULAR_DENOMINATOR_SCALE * NdotV * max(NdotL, 0.001) + 0.0001;
        float3 specular    = numerator / denominator * u_MaterialSurface.z * ggxEnergyCompensation;

        float3 kS = F;
        float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);
        float3 diffuse = kD * albedo.rgb * BRDF_DIFFUSE_NORMALIZATION;

        float3 spotDiff = diffuse * NdotL;
        float3 spotSpec = specular * NdotL;
        ApplyClearcoatSplit(spotDiff, spotSpec, N, V, L, NdotL, coatAnisotropy.x, coatAnisotropy.y);
        ApplyCharlieSheenDirectSplit(spotDiff, spotSpec, N, H, NdotL, NdotV, u_Sheen.rgb, u_Sheen.w, u_MaterialSurface.z);
        diffLo += spotDiff * radiance;
        specLo += spotSpec * radiance;
        if (hasSSS) {
            float wrap = 0.5;
            float NdotL_wrap = saturate((dot(N, L) + wrap) / ((1.0 + wrap) * (1.0 + wrap)));
            float3 scatterDir = -(L + N * 0.3);
            float VdotS = max(dot(V, normalize(scatterDir)), 0.0);
            float sssTransmittance = pow(VdotS, 4.0) * 0.5;
            float3 sssLighting = (NdotL_wrap * (1.0 - metallic) + sssTransmittance) * u_Subsurface.rgb * sssGain;
            diffLo += sssLighting * radiance * (1.0 - metallic);
        }
    }

    // 8b. Rect / Disk Area Lights (Validated LTC Diffuse & Specular, M15-U5-40..45)
    uint numRectLights = min((uint)u_LightReserved[1].x, 2u);
    for (uint r = 0; r < numRectLights; ++r) {
        RectLightData rLight = u_RectLights[r];
        LTCAreaLightResult ltc = EvaluateLTCAreaLightTerms(
            rLight.PositionAndWidth,
            rLight.DirectionAndHeight,
            rLight.UpAndRange,
            rLight.ColorAndIntensity,
            input.WorldPos,
            N,
            V,
            albedo.rgb,
            metallic,
            roughness,
            F0,
            LOCAL_INTENSITY_SCALE);
        if (ltc.RangeAttenuation <= 0.0) continue;

        float3 diffContrib = ltc.DiffuseLighting;
        float3 areaSpecular = ltc.SpecularLighting;
        if (abs(coatAnisotropy.z) > 0.00001) {
            float3 anisotropicSpecular = EvaluateAnisotropicAreaSpecular(rLight, input.WorldPos, N, V,
                tangent, bitangent, roughness, coatAnisotropy.z, F0, ltc.DiffuseFormFactor);
            float anisotropyBlend = smoothstep(0.0, 0.1, abs(coatAnisotropy.z));
            areaSpecular = lerp(areaSpecular, anisotropicSpecular, anisotropyBlend * anisotropyBlend);
        }
        float3 areaDiff = diffContrib;
        float3 areaSpec = areaSpecular * u_MaterialSurface.z * ggxEnergyCompensation;
        float coat = saturate(coatAnisotropy.x) * u_MaterialSurface.z;
        if (coat > 0.0) {
            LTCAreaLightResult coatLTC = EvaluateLTCAreaLightTerms(
                rLight.PositionAndWidth, rLight.DirectionAndHeight, rLight.UpAndRange, rLight.ColorAndIntensity,
                input.WorldPos, N, V, 1.0, 1.0, clamp(coatAnisotropy.y, 0.04, 1.0), 0.04, LOCAL_INTENSITY_SCALE);
            float3 attenuation = 1.0 - coat * coatLTC.SpecularAlbedo;
            float3 atten2 = attenuation * attenuation;
            areaDiff *= atten2;
            areaSpec = areaSpec * atten2 + coat * coatLTC.SpecularLighting;
        }
        ApplyCharlieSheenAreaSplit(
            areaDiff,
            areaSpec,
            rLight.PositionAndWidth,
            rLight.DirectionAndHeight,
            rLight.UpAndRange,
            input.WorldPos,
            N,
            V,
            ltc.DiffuseFormFactor,
            u_Sheen.rgb,
            u_Sheen.w,
            u_MaterialSurface.z);
        diffLo += areaDiff * ltc.Radiance;
        specLo += areaSpec * ltc.Radiance;
        if (hasSSS) {
            diffLo += ltc.DiffuseFormFactor * u_Subsurface.rgb * sssGain * (1.0 - metallic) * ltc.Radiance;
        }
    }

    // 9. Ambient Lighting: Image-Based Lighting (IBL), Multi-Probe Parallax & Sky Fallback
    float3 ambientF = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 ambientKD = (float3(1.0, 1.0, 1.0) - ambientF) * (1.0 - metallic);
    float3 diffuseAmbient = float3(0.0, 0.0, 0.0);
    float3 specularAmbient = float3(0.0, 0.0, 0.0);
    float3 reflectionEnvironment = 0.0;
    float3 reflectionResponse = 0.0;

    bool hasSplitSumIBL = (u_LightCounts.w == 1u);
    uint envWidth = 0, envHeight = 0, envLevels = 1;
    if (hasSplitSumIBL) {
        u_PrefilterMap.GetDimensions(0, envWidth, envHeight, envLevels);
    }
    float MAX_REFLECTION_LOD = max(0.0, float(envLevels) - 1.0);
    float3 R = reflect(-V, N);
    float2 envBRDF = u_BRDFLUT.Sample(u_EnvironmentSampler, float2(max(NdotV, 0.0), roughness)).rg;
    float envIntensity = max(u_LightReserved[0].y, 0.0);
    float3 diffuseAO = ComputeMultiBounceAO(ao, albedo.rgb, saturate(u_LightReserved[1].w));
    float specAO = ComputeSpecularOcclusion(NdotV, ao, roughness);
    float4 ddgiSample = DDGISampleVolumeIrradiance(u_DDGIIrradiance, u_DDGIDistance, u_EnvironmentSampler, input.WorldPos, N, V);
    float3 ddgiDiffuse = ddgiSample.rgb * albedo.rgb * ambientKD;

    if (hasSplitSumIBL) {
        // Image-Based Lighting (IBL) via Split-Sum Approximation + Multi-Probe Blend
        // A. Diffuse Irradiance: Cosine-weighted hemispherical integration (replaced smoothly by world-space DDGI when enabled)
        float3 irradiance = u_IrradianceMap.Sample(u_EnvironmentSampler, RotateEnvironmentDirection(N)).rgb;
        diffuseAmbient = lerp(irradiance * albedo.rgb * ambientKD * envIntensity, ddgiDiffuse, ddgiSample.a);

        // B. Specular Radiance: Pre-filtered local probes (scaled by probe Intensity) -> sky fallback (scaled by envIntensity)
        float3 skyPrefilter = u_PrefilterMap.SampleLevel(u_EnvironmentSampler, RotateEnvironmentDirection(R), roughness * MAX_REFLECTION_LOD).rgb * envIntensity;
        MultiProbeBlendResult envBlend = EvaluateMultiProbeBlendHLSL(input.WorldPos, R, roughness, u_EnvironmentSampler, skyPrefilter);
        float3 prefilteredColor = envBlend.BlendedEnvColor;

        // C. Split-sum BRDF integration 2D LUT: scale (rg.x) and bias (rg.y) for Fresnel reflection
        specularAmbient = prefilteredColor * (ambientF * envBRDF.x + envBRDF.y) * ggxEnergyCompensation;

        float coatF = saturate(coatAnisotropy.x) * (0.04 + 0.96 * pow(1.0-NdotV, 5.0));
        float coatRough = clamp(coatAnisotropy.y, 0.04, 1.0);
        float coatSpecAO = coatAnisotropy.x > 0.0 ? ComputeSpecularOcclusion(NdotV, ao, coatRough) : specAO;
        float3 skyCoatEnv = u_PrefilterMap.SampleLevel(u_EnvironmentSampler, RotateEnvironmentDirection(R), coatRough * MAX_REFLECTION_LOD).rgb * envIntensity;
        float3 coatEnvironment = EvaluateMultiProbeBlendHLSL(input.WorldPos, R, coatRough, u_EnvironmentSampler, skyCoatEnv).BlendedEnvColor;
        diffuseAmbient *= (1.0-coatF) * (1.0-coatF);
        specularAmbient = (specularAmbient * (1.0-coatF) * (1.0-coatF) * specAO + coatEnvironment * coatF * coatSpecAO) * u_MaterialSurface.w;
        reflectionEnvironment = specularAmbient;
        reflectionResponse = ((ambientF * envBRDF.x + envBRDF.y) * ggxEnergyCompensation *
            (1.0-coatF) * (1.0-coatF) * specAO + coatF * coatSpecAO) * u_MaterialSurface.w;
    } else {
        // Shared analytical environment approximation with seamless local-probe split-sum blend:
        // local probes carry their own authored Intensity even when world sky/ambient is zero.
        float3 ambientColor = u_SunLight.AmbientColor.rgb;
        diffuseAmbient = lerp(ambientColor * albedo.rgb * ambientKD * envIntensity, ddgiDiffuse, ddgiSample.a);
        float3 analyticalSpec = ambientColor * ambientF * (1.0 - roughness * 0.5) * u_MaterialSurface.w * ggxEnergyCompensation * (1.0 - saturate(u_LightReserved[0].z)) * envIntensity;
        MultiProbeBlendResult envBlend = EvaluateMultiProbeBlendHLSL(input.WorldPos, R, roughness, u_EnvironmentSampler, float3(0.0, 0.0, 0.0));
        float3 probeSpec = envBlend.ProbeColor * (ambientF * envBRDF.x + envBRDF.y) * ggxEnergyCompensation * u_MaterialSurface.w;
        specularAmbient = lerp(analyticalSpec, probeSpec, envBlend.ProbeCoverage) * specAO;
        // True split-sum BRDF reflectance drives dynamic SSR hits even in unlit dark rooms without sky IBL.
        reflectionResponse = (ambientF * envBRDF.x + envBRDF.y) * ggxEnergyCompensation * u_MaterialSurface.w * specAO;
        if (coatAnisotropy.x > 0.0 && envBlend.ProbeCoverage > 0.0) {
            float coatF = saturate(coatAnisotropy.x) * (0.04 + 0.96 * pow(1.0 - NdotV, 5.0));
            float coatRough = clamp(coatAnisotropy.y, 0.04, 1.0);
            float coatSpecAO = ComputeSpecularOcclusion(NdotV, ao, coatRough);
            float3 coatProbeEnv = EvaluateMultiProbeBlendHLSL(input.WorldPos, R, coatRough, u_EnvironmentSampler, float3(0.0, 0.0, 0.0)).ProbeColor;
            float coatBlend = coatF * envBlend.ProbeCoverage;
            diffuseAmbient *= (1.0 - coatBlend) * (1.0 - coatBlend);
            specularAmbient = specularAmbient * (1.0 - coatBlend) * (1.0 - coatBlend) + coatProbeEnv * coatBlend * u_MaterialSurface.w * coatSpecAO;
            reflectionResponse = reflectionResponse * (1.0 - coatBlend) * (1.0 - coatBlend) + coatBlend * u_MaterialSurface.w * coatSpecAO;
        }
        reflectionEnvironment = specularAmbient;
    }
    diffuseAmbient *= diffuseAO;

    if (u_Sheen.w >= 0.0) {
        float sheenRough = clamp(u_Sheen.w, 0.04, 1.0);
        float3 sheenSky = (hasSplitSumIBL ? u_PrefilterMap.SampleLevel(u_EnvironmentSampler, RotateEnvironmentDirection(R), sheenRough * MAX_REFLECTION_LOD).rgb : u_SunLight.AmbientColor.rgb) * envIntensity;
        float3 sheenEnv = EvaluateMultiProbeBlendHLSL(input.WorldPos, R, sheenRough, u_EnvironmentSampler, sheenSky).BlendedEnvColor;
        ApplyCharlieSheenIBLSplit(diffuseAmbient, specularAmbient, reflectionEnvironment, reflectionResponse, sheenEnv, NdotV, ao, u_Sheen.rgb, u_Sheen.w, u_MaterialSurface.w);
    }
    if (hasSSS) {
        float3 sssEnv = (hasSplitSumIBL ? u_IrradianceMap.Sample(u_EnvironmentSampler, RotateEnvironmentDirection(N)).rgb : u_SunLight.AmbientColor.rgb);
        float3 sssAmbient = sssEnv * albedo.rgb * u_Subsurface.rgb * sssGain * 0.5 * (1.0 - metallic);
        diffuseAmbient += sssAmbient * max(u_LightReserved[0].y, 0.0) * diffuseAO;
    }

    // 10. Total Radiance Accumulation (Linear HDR Radiance)
    float3 ambient = diffuseAmbient + specularAmbient;
    float3 Lo = diffLo + specLo;
    float3 diffuseTotal = clamp(diffuseAmbient + diffLo, 0.0, 65000.0);
    float3 crispSpecEmissive = clamp(specularAmbient + specLo + emissive, 0.0, 65000.0);
    float3 color = ambient + Lo + emissive;
    float4 controlSSS = hasSSS ? float4(saturate(u_Subsurface.rgb), clamp(u_Subsurface.w, 0.01, 16.0)) : float4(0.0, 0.0, 0.0, 0.0);
    // Preserve finite radiance when an intense, narrow lobe is stored in FP16.
    color = clamp(color, 0.0, 65000.0);

    float4 fogSample = float4(0.0, 0.0, 0.0, 1.0);
    float viewDepth = -mul(u_View, float4(input.WorldPos, 1.0)).z;
    if ((surfaceType == 2 || surfaceType == 3) && u_FogParams.x > 0.0) {
        float2 fogUV = input.PositionCS.xy * u_FogScreenSize.zw;
        fogSample = ForwardFogAt(fogUV, viewDepth);
    }
    const bool hasRefractionBackground = (surfaceType == 2 && u_Refraction.w > 0.5 && u_RefractionFrame.z > 0.5);
    if (!hasRefractionBackground) {
        if (surfaceType == 2) {
            color = clamp(color * fogSample.a + fogSample.rgb, 0.0, 65000.0);
        } else if (surfaceType == 3) {
            color = clamp(color * fogSample.a, 0.0, 65000.0);
        }
    }
    
    // 11. Tone Mapping & Color Polish (ACES Filmic Operator & Gamma Correction)
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
    
    if (surfaceType == 2 && u_Refraction.w > 0.5 && u_RefractionFrame.z > 0.5) {
        float2 screenUV = input.PositionCS.xy / u_RefractionFrame.xy;
        float thickness = clamp(u_Refraction.z, 0.0, 100.0);
        float4 startClip = mul(u_ViewProjection, float4(input.WorldPos, 1.0));
        float2 halfPixel = 0.5 / u_RefractionFrame.xy;
        float3 cauchyIOR = EvaluateCauchyIORs(u_Refraction.y, u_Absorption.w);

        float2 sampleUV = ProjectSnellRefractedUV(V, N, cauchyIOR.y, thickness, u_Refraction.x, input.WorldPos, u_ViewProjection, startClip, screenUV, halfPixel);
        // Do not pull an opaque foreground object across the glass silhouette.
        const float foregroundDepth = input.PositionCS.z - 1e-5;
        if (NearestRefractedDepth(sampleUV) < foregroundDepth) sampleUV = screenUV;

        float3 transmitted;
        if (u_Absorption.w > 0.001 && cauchyIOR.y > 1.001) {
            float2 uvR = ProjectSnellRefractedUV(V, N, cauchyIOR.x, thickness, u_Refraction.x, input.WorldPos, u_ViewProjection, startClip, screenUV, halfPixel);
            float2 uvB = ProjectSnellRefractedUV(V, N, cauchyIOR.z, thickness, u_Refraction.x, input.WorldPos, u_ViewProjection, startClip, screenUV, halfPixel);
            if (NearestRefractedDepth(uvR) < foregroundDepth) uvR = sampleUV;
            if (NearestRefractedDepth(uvB) < foregroundDepth) uvB = sampleUV;
            transmitted = float3(
                RefractedBackground(uvR, viewDepth, fogSample).r,
                RefractedBackground(sampleUV, viewDepth, fogSample).g,
                RefractedBackground(uvB, viewDepth, fogSample).b);
        } else {
            transmitted = RefractedBackground(sampleUV, viewDepth, fogSample);
        }

        float4 background = u_OpaqueSceneColor.SampleLevel(u_ScreenSampler, sampleUV, 0);
        // Transparent editor clear has no rendered backdrop to refract.
        if (background.a > 0.001) {
            float f0 = (cauchyIOR.y - 1.0) / (cauchyIOR.y + 1.0);
            float fresnel = f0 * f0 + (1.0 - f0 * f0) * pow(1.0 - NdotV, 5.0);
            if (u_CameraPosition.w <= 0.0) {
                transmitted = pow(max(transmitted, 0.0), 2.2);
                color = pow(max(color, 0.0), 2.2);
            }
            // Apply linear Beer-Lambert absorption using Snell optical path policy.
            float3 transmittance = EvaluateBeerLambertTransmittance(u_Absorption.rgb, thickness, NdotV, cauchyIOR.y);
            transmitted *= transmittance;
            color = clamp(fogSample.rgb + transmitted * u_RefractionTint.rgb * (1.0 - fresnel) +
                color * fogSample.a * fresnel, 0.0, 65000.0);
            if (u_CameraPosition.w <= 0.0) color = pow(max(color, 0.0), 1.0 / 2.2);
            return MakeForwardSurfaceEval(float4(color, 1.0), 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, color);
        }
        if (u_FogParams.x > 0.0) {
            color = clamp(color * fogSample.a + fogSample.rgb, 0.0, 65000.0);
        }
    }
    return MakeForwardSurfaceEval(float4(color, albedo.a), reflectionEnvironment, reflectionResponse, roughness, u_MaterialSurface.w, hasSSS ? diffuseTotal : 0.0, controlSSS, crispSpecEmissive);
}

float4 PSMain(PSInput input) : SV_Target0 { return EvaluateForwardSurface(input).Color; }
ReflectionSurfaceOutput PSReflectionMain(PSInput input) {
    ForwardSurfaceEval eval = EvaluateForwardSurface(input);
    return MakeMaterialReflectionSurfaceOutput(eval.Color, eval.Environment, eval.Response, eval.Roughness, eval.ReflectionEnabled);
}
SubsurfaceSurfaceOutput PSSSSMain(PSInput input) {
    ForwardSurfaceEval eval = EvaluateForwardSurface(input);
    float4 outColor = (eval.ControlSSS.a > 0.001)
        ? float4(clamp(eval.CrispSpecularAndEmissive, 0.0, 65000.0), eval.Color.a)
        : eval.Color;
    return MakeSubsurfaceSurfaceOutput(outColor, eval.DiffuseSSS, eval.ControlSSS);
}
ReflectionSubsurfaceSurfaceOutput PSReflectionSSSMain(PSInput input) {
    ForwardSurfaceEval eval = EvaluateForwardSurface(input);
    float4 outColor = (eval.ControlSSS.a > 0.001)
        ? float4(clamp(eval.CrispSpecularAndEmissive, 0.0, 65000.0), eval.Color.a)
        : eval.Color;
    return MakeReflectionSubsurfaceSurfaceOutput(outColor, eval.Environment, eval.Response, eval.Roughness, eval.ReflectionEnabled, eval.DiffuseSSS, eval.ControlSSS);
}
float4 PSDDGICapture(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target0 {
    const bool cutout = (int)(u_SurfaceType + 0.5) == 1;
    if (!isFrontFace && !cutout) return 0.0;
    // Cutout discard must run for both sides before a fragment can write probe depth.
    const float3 radiance = EvaluateForwardSurface(input, true).Color.rgb;
    return isFrontFace ? float4(radiance, 1.0) : 0.0;
}
