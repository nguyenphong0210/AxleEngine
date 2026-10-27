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
// -----------------------------------------------------------------------------
cbuffer ObjectConstants : register(b0, space0) {
    float4x4 u_Model;               // 64 bytes (Offset 0) - Model matrix
    float4   u_ColorPrimary;        // 16 bytes (Offset 64) - Albedo/BaseColor
    float4   u_ColorSecondary;      // 16 bytes (Offset 80) - Secondary/Pattern tint
    float4   u_Params;              // 16 bytes (Offset 96) - x: TileScale/UVScale, y: ShadingMode, z: Roughness, w: Metallic
    float4   u_SunDirAndIntensity;  // 16 bytes (Offset 112) - xyz: Normalized Sun Direction, w: Sun Intensity
};

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
    float4 ShadowParams;           // x: Shadow Bias, y: Filter Radius, zw: Reserved
};

struct PointLightData {
    float4 PositionAndRadius; // xyz: World Pos, w: Radius (m)
    float4 ColorAndIntensity; // xyz: Linear RGB Color, w: Luminous Intensity (Lumens/Candela)
    float4 Params;            // x: Falloff exponent (1.0 = inverse-square), y: MinRadius (0.05m), zw: Reserved
    float4 Reserved;          // 64 bytes total
};

struct SpotLightData {
    float4 PositionAndRange;      // xyz: World Pos, w: Range (m)
    float4 DirectionAndIntensity; // xyz: Normalized Direction, w: Luminous Intensity
    float4 ColorAndInnerAngle;    // xyz: Linear RGB Color, w: cos(InnerConeAngle)
    float4 AngleParams;           // x: cos(OuterConeAngle), y: Falloff exponent, zw: Reserved
};

cbuffer MultiLightBuffer : register(b2, space0) {
    DirectionalSunLight u_SunLight;
    uint4               u_LightCounts;        // x: NumPointLights (0..16), y: NumSpotLights (0..16), z: HasSunLight, w: LightingMode
    float4              u_CameraPosExposure;  // xyz: Camera Pos, w: Exposure
    float4              u_LightReserved[2];   // 32 bytes header padding to 128 bytes
    PointLightData      u_PointLights[16];    // 16 * 64 = 1024 bytes
    SpotLightData       u_SpotLights[16];     // 16 * 64 = 1024 bytes
    float4              u_LightTailReserved[8]; // 128 bytes tail padding to 2304 bytes
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
SamplerState u_Sampler      : register(s0, space1);

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
    output.NormalWS    = normalize(mul((float3x3)u_Model, input.Normal));
    output.TangentWS   = float4(normalize(mul((float3x3)u_Model, input.Tangent.xyz)), input.Tangent.w);
    output.TexCoord    = input.TexCoord;
    output.VertexColor = input.Color;
    output.LocalPos    = input.Position;
    return output;
}

// =============================================================================
// PHYSICALLY BASED RENDERING (PBR) BRDF FUNCTIONS
// =============================================================================
static const float PI = 3.14159265358979323846;

// 1. Trowbridge-Reitz GGX Normal Distribution Function (NDF)
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0000001);
}

// 2. Schlick-GGX Geometry Function
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// 3. Smith's Dual Shadowing-Masking Geometry Attenuation
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// 4. Fresnel-Schlick Approximation
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(ClampZeroOne(1.0 - cosTheta), 5.0);
}

// 5. Fresnel-Schlick with Roughness for Ambient Specular Reflection
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float3 maxRef = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
    return F0 + (maxRef - F0) * pow(ClampZeroOne(1.0 - cosTheta), 5.0);
}

// GLSL-compatible modulo utility: mod(x, y) = x - y * floor(x / y)
// Ensures 100% exact parity with OpenGL across positive and negative coordinate axes.
float glsl_mod(float x, float y) {
    return x - y * floor(x / y);
}

// =============================================================================
// PIXEL SHADER ENTRY POINT
// =============================================================================
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
    } else if (shadingMode == 4) {
        // Mode 4: Splatmap Terrain Heightfield (Blended with Vertex Color)
        albedo = u_ColorPrimary * input.VertexColor;
    } else if (shadingMode == 5) {
        // Mode 5: Textured PBR Material (Sample Albedo, Roughness, Metallic, AO)
        float2 uv = input.TexCoord * tileScale;
        float4 albedoSample = u_AlbedoMap.Sample(u_Sampler, uv);
        albedo = albedoSample * u_ColorPrimary;
        
        roughness = u_RoughnessMap.Sample(u_Sampler, uv).r * roughness;
        roughness = clamp(roughness, 0.04, 1.0);
        
        metallic = u_MetallicMap.Sample(u_Sampler, uv).r * metallic;
        metallic = clamp(metallic, 0.0, 1.0);
        
        ao = u_AOMap.Sample(u_Sampler, uv).r;
        emissive = u_EmissiveMap.Sample(u_Sampler, uv).rgb;
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
    } else if (shadingMode == 5 && abs(input.TangentWS.w) > 0.1) {
        float3 T = normalize(input.TangentWS.xyz);
        float3 B = cross(N, T) * input.TangentWS.w;
        float3x3 TBN = float3x3(T, B, N);
        
        float2 uv = input.TexCoord * tileScale;
        float3 normalSample = u_NormalMap.Sample(u_Sampler, uv).xyz * 2.0 - 1.0;
        N = normalize(mul(normalSample, TBN));
    }
    
    // 4. View Vector
    float3 camPos = (length(u_CameraPosition.xyz) > 0.001) ? u_CameraPosition.xyz : float3(0.0, 5.0, 10.0);
    float3 V = normalize(camPos - input.WorldPos);
    if (dot(N, V) < 0.0) {
        N = -N;
    }
    float NdotV = max(dot(N, V), 0.001);
    
    // 5. Base Reflectivity F0 (Dielectric = 0.04, Metal = Albedo)
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo.rgb, metallic);
    
    // Direct Radiance Accumulation
    float3 Lo = float3(0.0, 0.0, 0.0);

    // 6. Primary Directional Sun Light (Physical Illuminance in Lux)
    bool hasMultiLightSun = (u_LightCounts.z > 0 && u_SunLight.DirectionAndIlluminance.w > 0.0);
    float3 sunDir = hasMultiLightSun ? normalize(u_SunLight.DirectionAndIlluminance.xyz) : normalize(u_SunDirAndIntensity.xyz);
    if (length(sunDir) < 0.1) sunDir = normalize(float3(0.45, 0.85, 0.25));

    float sunIntensity = hasMultiLightSun ? (u_SunLight.DirectionAndIlluminance.w * 0.000035 + u_SunLight.ColorAndAmbient.w * 0.5) : max(0.5, u_SunDirAndIntensity.w);
    float3 sunColor = hasMultiLightSun ? u_SunLight.ColorAndAmbient.rgb : ((length(u_SunColor.rgb) > 0.01) ? u_SunColor.rgb : float3(1.0, 0.98, 0.92));
    float3 sunRadiance = sunColor * sunIntensity;

    float NdotL_Sun = max(dot(N, sunDir), 0.0);
    if (NdotL_Sun > 0.0) {
        float3 H = normalize(V + sunDir);
        float  NDF = DistributionGGX(N, H, roughness);
        float  G   = GeometrySmith(N, V, sunDir, roughness);
        float3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 numerator   = NDF * G * F;
        float  denominator = 4.0 * NdotV * NdotL_Sun + 0.0001;
        float3 specular    = numerator / denominator;

        float3 kS = F;
        float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);
        float3 diffuse = kD * albedo.rgb / PI;

        Lo += (diffuse + specular) * sunRadiance * NdotL_Sun;
    }

    // 7. Point Lights (up to 16 with smooth windowed inverse-square falloff)
    uint numPointLights = min(u_LightCounts.x, 16u);
    for (uint p = 0; p < numPointLights; ++p) {
        PointLightData pLight = u_PointLights[p];
        float radius = pLight.PositionAndRadius.w;
        if (radius <= 0.0) continue;

        float3 lightVec = pLight.PositionAndRadius.xyz - input.WorldPos;
        float dist = length(lightVec);
        if (dist >= radius || dist <= 0.0001) continue;

        float3 L = lightVec / dist;
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        // Smooth windowed inverse-square attenuation (Frostbite / UE standard)
        float minRadius = max(0.0025, pLight.Params.y);
        float distSq = max(dist * dist, minRadius);
        float invDistSq = 1.0 / distSq;
        float ratio = dist / radius;
        float ratio4 = ratio * ratio * ratio * ratio;
        float window = ClampZeroOne(1.0 - ratio4);
        float attenuation = invDistSq * (window * window);

        float falloffExp = pLight.Params.x;
        if (falloffExp > 0.1 && abs(falloffExp - 1.0) > 0.01) {
            attenuation = pow(max(attenuation, 0.0), falloffExp);
        }

        float3 radiance = pLight.ColorAndIntensity.rgb * (pLight.ColorAndIntensity.w * attenuation * 0.05);

        float3 H = normalize(V + L);
        float  NDF = DistributionGGX(N, H, roughness);
        float  G   = GeometrySmith(N, V, L, roughness);
        float3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 numerator   = NDF * G * F;
        float  denominator = 4.0 * NdotV * NdotL + 0.0001;
        float3 specular    = numerator / denominator;

        float3 kS = F;
        float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);
        float3 diffuse = kD * albedo.rgb / PI;

        Lo += (diffuse + specular) * radiance * NdotL;
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
        float3 radiance = sLight.ColorAndInnerAngle.rgb * (sLight.DirectionAndIntensity.w * totalAtten * 0.05);

        float3 H = normalize(V + L);
        float  NDF = DistributionGGX(N, H, roughness);
        float  G   = GeometrySmith(N, V, L, roughness);
        float3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 numerator   = NDF * G * F;
        float  denominator = 4.0 * NdotV * NdotL + 0.0001;
        float3 specular    = numerator / denominator;

        float3 kS = F;
        float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);
        float3 diffuse = kD * albedo.rgb / PI;

        Lo += (diffuse + specular) * radiance * NdotL;
    }

    // 9. Ambient Lighting: Image-Based Lighting (IBL) or Analytical Approximation
    float3 ambientF = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 ambientKD = (float3(1.0, 1.0, 1.0) - ambientF) * (1.0 - metallic);
    float3 ambient;

    if (u_LightCounts.w == 1u) {
        // Image-Based Lighting (IBL) via Split-Sum Approximation
        // A. Diffuse Irradiance: Cosine-weighted hemispherical integration
        float3 irradiance = u_IrradianceMap.Sample(u_Sampler, N).rgb;
        float3 diffuseAmbient = irradiance * albedo.rgb * ambientKD;

        // B. Specular Radiance: Pre-filtered environment radiance (roughness-dependent mip level)
        float3 R = reflect(-V, N);
        const float MAX_REFLECTION_LOD = 4.0;
        float3 prefilteredColor = u_PrefilterMap.SampleLevel(u_Sampler, R, roughness * MAX_REFLECTION_LOD).rgb;

        // C. Split-sum BRDF integration 2D LUT: scale (rg.x) and bias (rg.y) for Fresnel reflection
        float2 envBRDF = u_BRDFLUT.Sample(u_Sampler, float2(max(NdotV, 0.0), roughness)).rg;
        float3 specularAmbient = prefilteredColor * (ambientF * envBRDF.x + envBRDF.y);

        float envIntensity = hasMultiLightSun ? u_SunLight.ColorAndAmbient.w : 1.0;
        ambient = (diffuseAmbient + specularAmbient) * envIntensity * ao;
    } else {
        // Analytical hemisphere ambient reflection (matches realistic automotive sheen)
        float upWeight = saturate(N.y * 0.5 + 0.5);
        float zenithWeight = saturate(N.y);
        float3 skyZenith = float3(0.18, 0.42, 0.78);
        float3 skyHorizon = float3(0.70, 0.82, 0.95);
        float3 groundColor = float3(0.18, 0.16, 0.14);

        float3 skyUpper = lerp(skyHorizon, skyZenith, zenithWeight);
        float3 hemisphereLight = lerp(groundColor, skyUpper, upWeight);
        float3 ambientDiffuse = hemisphereLight * albedo.rgb * ambientKD;

        float3 R = reflect(-V, N);
        float specZenith = saturate(R.y);
        float3 specUpper = lerp(skyHorizon, skyZenith, specZenith);
        float horizonFactor = smoothstep(-0.06, 0.06, R.y);
        float3 specSky = lerp(groundColor, specUpper, horizonFactor);

        // Specular sun reflection lobe for glossy metals & car clearcoat
        float sunSpec = pow(max(dot(R, sunDir), 0.0), lerp(64.0, 8.0, roughness));
        specSky += sunColor * sunSpec * (1.0 - roughness * 0.8) * 0.5;

        float3 specularAmbient = specSky * ambientF * (1.0 - roughness * 0.5);

        float ambientIntensity = hasMultiLightSun ? max(0.28, u_SunLight.ColorAndAmbient.w) : 0.85;
        ambient = (ambientDiffuse * ambientIntensity + specularAmbient) * ao;
    }
    
    // 10. Total Radiance Accumulation (Linear HDR Radiance)
    float3 color = ambient + Lo + emissive;
    
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
    
    return float4(color, albedo.a);
}
