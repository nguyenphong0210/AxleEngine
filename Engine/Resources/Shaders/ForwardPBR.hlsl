#define AXLE_FORWARD_LOCAL_SHADOWS 1
#define AXLE_FORWARD_ENVIRONMENT_IBL 1
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
    float4 u_MaterialReserved[7];
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
    float4              u_LightReserved[2];   // [0]: x disables studio lobes, y is env strength, z diffuse-only; [1].x: NumRectLights
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
float SunShadowVisibility(float3 world,float3 normal,float3 sun) {
    uint count=(uint)u_CascadeAtlas.x;
    if(count==0) return 1;
    float depth=-mul(u_View,float4(world,1)).z;
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
float3 ApplyClearcoat(float3 baseLighting, float3 N, float3 V, float3 L, float nl, float coatStrength, float coatRoughness) {
    float coat = saturate(coatStrength) * u_MaterialSurface.z;
    float r = clamp(coatRoughness, 0.04, 1.0);
    float3 H = normalize(V+L);
    float f = 0.04 + 0.96 * pow(1.0-saturate(dot(V,H)), 5.0);
    float nv = max(dot(N,V), 0.001);
    float coatBRDF = DistributionGGX(N,H,r) * GeometrySmith(N,V,L,r) * f / max(4.0*nv*max(nl,0.001),0.0001);
    return baseLighting * (1.0-coat*f) * (1.0-coat*f) + coat * coatBRDF * nl;
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

float3 LTC_IntegrateEdge(float3 va, float3 vb) {
    float3 cr = cross(va, vb);
    float len = length(cr);
    if (len < 1e-6) return float3(0.0, 0.0, 0.0);
    float dt = clamp(dot(va, vb), -1.0, 1.0);
    return (cr / len) * acos(dt);
}

float4 PSMain(PSInput input) : SV_Target0 {
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
    if (shadingMode == 0 || shadingMode == 1) {
        // Geometric screen derivative flat normal for procedural caro & low-poly meshes (Exact OpenGL Parity)
        float3 dX = ddx(input.WorldPos);
        float3 dY = ddy(input.WorldPos);
        float3 crossN = cross(dX, dY);
        if (length(crossN) > 1e-6) {
            N = normalize(crossN);
        }
    } else if ((HasMaterialMap(1)||HasMaterialMap(8)) && abs(input.TangentWS.w)>0.1) {
        float3 T=input.TangentWS.xyz-N*dot(N,input.TangentWS.xyz);
        T=normalize(T);
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
    float ggxSingleScatterEnergy = max(ggxDFG.x + ggxDFG.y, 0.05);
    float3 ggxEnergyCompensation = min(1.0 + F0 * (1.0 / ggxSingleScatterEnergy - 1.0), 8.0);
    
    // Direct Radiance Accumulation
    float3 Lo = float3(0.0, 0.0, 0.0);

    // 6. Primary Directional Sun Light (Physical Illuminance in Lux)
    bool hasMultiLightSun = (u_LightCounts.z > 0);
    float3 sunDir = hasMultiLightSun ? normalize(u_SunLight.DirectionAndIlluminance.xyz) 
                                     : (length(u_SunDirection.xyz) > 0.01 ? normalize(u_SunDirection.xyz) : normalize(float3(0.45, 0.85, 0.25)));

    float sunIntensity = hasMultiLightSun ? max(u_SunLight.DirectionAndIlluminance.w,0.0) * 0.000035 : max(u_SunDirection.w,0.0);
    float3 sunColor = hasMultiLightSun ? u_SunLight.ColorAndAmbient.rgb : u_SunColor.rgb;
    float3 sunRadiance = sunColor * sunIntensity * SunShadowVisibility(input.WorldPos,N,sunDir);

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
        float sunAngularRadius = max(u_SunLight.ShadowParams.z, 0.0);
        float sunConvolvedRoughness = sqrt(saturate(roughness * roughness + sunAngularRadius * sunAngularRadius));
        float  NDF = MaterialDistribution(N, H, tangent, bitangent, sunConvolvedRoughness, coatAnisotropy.z);
        float  G   = MaterialGeometry(N, V, sunDir, tangent, bitangent, sunConvolvedRoughness, coatAnisotropy.z);
        float3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 numerator   = NDF * G * F;
        float  denominator = 4.0 * NdotV * max(NdotL_Sun, 0.001) + 0.0001;
        float3 specular    = numerator / denominator * u_MaterialSurface.z * ggxEnergyCompensation;

        float3 kS = F;
        float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);
        float3 diffuse = kD * albedo.rgb / PI;

        Lo += ApplyClearcoat(diffuse * NdotL_Sun_Diff + specular * NdotL_Sun, N, V, sunDir, NdotL_Sun, coatAnisotropy.x, coatAnisotropy.y) * sunRadiance;
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
            radiance = pLight.ColorAndIntensity.rgb * (pLight.ColorAndIntensity.w * attenuation * 0.05) * PointShadowVisibility(p,pLight.PositionAndRadius.xyz,input.WorldPos,N,L);
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
        float  denominator = 4.0 * NdotV * max(NdotL, 0.001) + 0.0001;
        float3 specular    = numerator / denominator * u_MaterialSurface.z * ggxEnergyCompensation;

        float3 kS = F;
        float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);
        float3 diffuse = kD * albedo.rgb / PI;

        Lo += ApplyClearcoat(diffuse * NdotL_Diff + specular * NdotL, N, V, L, NdotL, coatAnisotropy.x, coatAnisotropy.y) * radiance;
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
        float3 radiance = sLight.ColorAndInnerAngle.rgb * (sLight.DirectionAndIntensity.w * totalAtten * 0.05) * LocalShadowVisibility(u_LocalShadowViews[4+s/4][s%4],input.WorldPos,N,L);

        float3 H = normalize(V + L);
        float  NDF = MaterialDistribution(N, H, tangent, bitangent, roughness, coatAnisotropy.z);
        float  G   = MaterialGeometry(N, V, L, tangent, bitangent, roughness, coatAnisotropy.z);
        float3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 numerator   = NDF * G * F;
        float  denominator = 4.0 * NdotV * NdotL + 0.0001;
        float3 specular    = numerator / denominator * u_MaterialSurface.z * ggxEnergyCompensation;

        float3 kS = F;
        float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);
        float3 diffuse = kD * albedo.rgb / PI;

        Lo += ApplyClearcoat((diffuse + specular) * NdotL, N, V, L, NdotL, coatAnisotropy.x, coatAnisotropy.y) * radiance;
    }

    // 8b. Rect Area Lights (LTC Polygonal Diffuse & Representative Point Specular)
    uint numRectLights = min((uint)u_LightReserved[1].x, 2u);
    for (uint r = 0; r < numRectLights; ++r) {
        RectLightData rLight = u_RectLights[r];
        float3 lightPos = rLight.PositionAndWidth.xyz;
        float  halfWidth = rLight.PositionAndWidth.w * 0.5;
        float3 lightDir = normalize(rLight.DirectionAndHeight.xyz);
        float  halfHeight = rLight.DirectionAndHeight.w * 0.5;
        float3 lightUp = normalize(rLight.UpAndRange.xyz);
        float  range = rLight.UpAndRange.w;
        float3 lightColor = rLight.ColorAndIntensity.xyz;
        float  rawIntensity = rLight.ColorAndIntensity.w;
        bool   twoSided = (rawIntensity < 0.0);
        float  intensity = abs(rawIntensity);

        float3 toLight = lightPos - input.WorldPos;
        float dist = length(toLight);
        if (dist > range || range <= 0.001) continue;

        // Front-facing test for one-sided rect light
        float dirDot = dot(-toLight, lightDir);
        if (!twoSided && dirDot <= 0.0) continue;

        // Tangent space for rect quad
        float3 lightRight = normalize(cross(lightDir, lightUp));
        lightUp = cross(lightRight, lightDir);

        // Quad 4 vertices in world space
        float3 p0 = lightPos - lightRight * halfWidth - lightUp * halfHeight;
        float3 p1 = lightPos + lightRight * halfWidth - lightUp * halfHeight;
        float3 p2 = lightPos + lightRight * halfWidth + lightUp * halfHeight;
        float3 p3 = lightPos - lightRight * halfWidth + lightUp * halfHeight;

        // Spherical polygon irradiance integration (Heitz 2016 LTC diffuse)
        float3 v0 = normalize(p0 - input.WorldPos);
        float3 v1 = normalize(p1 - input.WorldPos);
        float3 v2 = normalize(p2 - input.WorldPos);
        float3 v3 = normalize(p3 - input.WorldPos);

        float3 F_diff = LTC_IntegrateEdge(v0, v1) + LTC_IntegrateEdge(v1, v2) + LTC_IntegrateEdge(v2, v3) + LTC_IntegrateEdge(v3, v0);
        float diffuseIrradiance = max(0.0, dot(F_diff, N)) / (2.0 * PI);

        // Specular: Karis representative point on rectangle
        float3 R_vec = reflect(-V, N);
        float denom = dot(R_vec, lightDir);
        float3 specPos = lightPos;
        if (abs(denom) > 1e-5) {
            float t = dot(lightPos - input.WorldPos, lightDir) / denom;
            if (t > 0.0) {
                float3 hitPoint = input.WorldPos + R_vec * t;
                float3 diff = hitPoint - lightPos;
                float px = clamp(dot(diff, lightRight), -halfWidth, halfWidth);
                float py = clamp(dot(diff, lightUp), -halfHeight, halfHeight);
                specPos = lightPos + lightRight * px + lightUp * py;
            }
        }
        float3 L_spec = normalize(specPos - input.WorldPos);
        float3 H_spec = normalize(V + L_spec);
        float NdotL_spec = max(dot(N, L_spec), 0.0);
        float NdotH_spec = max(dot(N, H_spec), 0.0);
        float VdotH_spec = max(dot(V, H_spec), 0.0);

        float NDF_spec = MaterialDistribution(N, H_spec, tangent, bitangent, roughness, coatAnisotropy.z);
        float G_spec = MaterialGeometry(N, V, L_spec, tangent, bitangent, roughness, coatAnisotropy.z);
        float3 F_spec = FresnelSchlick(VdotH_spec, F0);
        float3 specBRDF = (NDF_spec * G_spec * F_spec) / (4.0 * NdotV * max(NdotL_spec, 0.001) + 0.001) * ggxEnergyCompensation;

        float atten = saturate(1.0 - (dist / range)) * saturate(1.0 - (dist / range)) / (dist * dist + 1.0);
        float3 radiance = lightColor * (intensity * atten * 0.05);

        float3 kS_rect = F_spec;
        float3 kD_rect = (float3(1.0, 1.0, 1.0) - kS_rect) * (1.0 - metallic);
        float3 diffContrib = kD_rect * albedo.rgb * diffuseIrradiance;
        float3 specContrib = specBRDF * NdotL_spec * u_MaterialSurface.z;

        Lo += ApplyClearcoat(diffContrib + specContrib, N, V, L_spec, max(NdotL_spec, 0.001), coatAnisotropy.x, coatAnisotropy.y) * radiance;
    }

    // 9. Ambient Lighting: Image-Based Lighting (IBL) or Analytical Approximation
    float3 ambientF = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 ambientKD = (float3(1.0, 1.0, 1.0) - ambientF) * (1.0 - metallic);
    float3 ambient;

    if (u_LightCounts.w == 1u) {
        // Image-Based Lighting (IBL) via Split-Sum Approximation
        // A. Diffuse Irradiance: Cosine-weighted hemispherical integration
        float3 irradiance = u_IrradianceMap.Sample(u_EnvironmentSampler, N).rgb;
        float3 diffuseAmbient = irradiance * albedo.rgb * ambientKD;

        // B. Specular Radiance: Pre-filtered environment radiance (roughness-dependent mip level)
        float3 R = reflect(-V, N);
        uint envWidth,envHeight,envLevels;
        u_PrefilterMap.GetDimensions(0,envWidth,envHeight,envLevels);
        float MAX_REFLECTION_LOD=max(0,int(envLevels)-1);
        float3 prefilteredColor = u_PrefilterMap.SampleLevel(u_EnvironmentSampler, R, roughness * MAX_REFLECTION_LOD).rgb;

        // C. Split-sum BRDF integration 2D LUT: scale (rg.x) and bias (rg.y) for Fresnel reflection
        float2 envBRDF = u_BRDFLUT.Sample(u_EnvironmentSampler, float2(max(NdotV, 0.0), roughness)).rg;
        float3 specularAmbient = prefilteredColor * (ambientF * envBRDF.x + envBRDF.y) * ggxEnergyCompensation;

        float coatF = saturate(coatAnisotropy.x) * (0.04 + 0.96 * pow(1.0-NdotV, 5.0));
        float3 coatEnvironment = u_PrefilterMap.SampleLevel(u_EnvironmentSampler, R, clamp(coatAnisotropy.y,0.04,1.0) * MAX_REFLECTION_LOD).rgb;
        diffuseAmbient *= (1.0-coatF) * (1.0-coatF);
        specularAmbient = (specularAmbient * (1.0-coatF) * (1.0-coatF) + coatEnvironment * coatF) * u_MaterialSurface.w;
        float envIntensity = max(u_LightReserved[0].y,0.0);
        ambient = (diffuseAmbient + specularAmbient) * envIntensity * ao;
    } else {
        // Shared analytical environment approximation: only authored ambient
        // strength contributes; no studio softboxes are synthesized by a view.
        float3 ambientColor=u_SunLight.AmbientColor.rgb;
        float3 R=reflect(-V,N);
        float3 diffuseAmbient=ambientColor*albedo.rgb*ambientKD;
        float3 specularAmbient=ambientColor*ambientF*(1.0-roughness*0.5)*u_MaterialSurface.w*ggxEnergyCompensation;
        ambient=(diffuseAmbient+specularAmbient*(1-saturate(u_LightReserved[0].z)))*max(u_LightReserved[0].y,0.0)*ao;
    }
    
    // 10. Total Radiance Accumulation (Linear HDR Radiance)
    float3 color = ambient + Lo + emissive;
    // Preserve finite radiance when an intense, narrow lobe is stored in FP16.
    color = clamp(color, 0.0, 65000.0);
    
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
    
    if (surfaceType==2 && u_Refraction.w>0.5 && u_RefractionFrame.z>0.5) {
        float2 screenUV=input.PositionCS.xy/u_RefractionFrame.xy;
        float3 refracted=refract(-V,N,1.0/u_Refraction.y);
        float4 endClip=mul(u_ViewProjection,float4(input.WorldPos+refracted*u_Refraction.z,1));
        float4 startClip=mul(u_ViewProjection,float4(input.WorldPos,1));
        float2 offset=(endClip.xy/max(endClip.w,1e-5)-startClip.xy/max(startClip.w,1e-5))*float2(0.5,-0.5)*u_Refraction.x;
        float2 halfPixel=0.5/u_RefractionFrame.xy;
        float2 sampleUV=clamp(screenUV+offset,halfPixel,1.0-halfPixel);
        // Do not pull an opaque foreground object across the glass silhouette.
        if (u_OpaqueSceneDepth.SampleLevel(u_ScreenSampler,sampleUV,0)<input.PositionCS.z-1e-5) sampleUV=screenUV;
        float4 background=u_OpaqueSceneColor.SampleLevel(u_ScreenSampler,sampleUV,0);
        // Transparent editor clear has no rendered checkerboard to refract.
        if (background.a>0.001) {
            float f0=(u_Refraction.y-1.0)/(u_Refraction.y+1.0);
            float fresnel=f0*f0+(1.0-f0*f0)*pow(1.0-NdotV,5.0);
            float3 transmitted=background.rgb;
            if (u_CameraPosition.w<=0.0) {
                transmitted=pow(max(transmitted,0.0),2.2);
                color=pow(max(color,0.0),2.2);
            }
            color=transmitted*u_RefractionTint.rgb*(1.0-fresnel)+color*fresnel;
            if (u_CameraPosition.w<=0.0) color=pow(max(color,0.0),1.0/2.2);
            return float4(color,1.0);
        }
    }
    return float4(color, albedo.a);
}
