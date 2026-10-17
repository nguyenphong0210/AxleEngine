#version 450 core

layout(location = 0) out vec4 o_Color;

in vec3 v_WorldPos;
in vec3 v_LocalPos;
in vec2 v_TexCoord;
in vec4 v_FragPosLightSpace;

// 1. Procedural Pattern Uniforms
uniform int u_PatternMode;        // 0: World XZ Caro (Floor), 1: 3D Caro (Box/Mesh), 2: Spherical Caro, 3: Solid Color
uniform float u_TileScale;        // Checkerboard tile frequency
uniform vec4 u_Color1;            // Primary color or tint multiplier
uniform vec4 u_Color2;            // Secondary pattern color

// 2. Directional Sun Light & Real-Time Shadow Mapping
uniform vec3 u_LightDir;          // Directional sun light vector
uniform vec3 u_LightColor;        // Sun light RGB color
uniform float u_LightIntensity;   // Sun brightness multiplier
uniform float u_AmbientIntensity; // Ambient fill lighting
uniform int u_ShadowsEnabled;     // 1: Enable soft shadows, 0: Disable shadows
uniform mat4 u_Transform;

// 3. Texture Sampler Slots
uniform sampler2D u_AlbedoMap;     // Slot 0: Base Color / Diffuse Map
uniform sampler2DShadow u_ShadowMap; // Slot 1: Directional Light Depth Texture (Hardware Bilinear PCF)
uniform sampler2D u_NormalMap;     // Slot 2: Tangent-space Normal Map
uniform sampler2D u_RoughnessMap;  // Slot 3: Micro-surface Roughness Map
uniform sampler2D u_MetallicMap;   // Slot 4: Metallic Mask Map
uniform samplerCube u_IrradianceMap; // Slot 5: Diffuse Irradiance Cubemap
uniform samplerCube u_PrefilterMap;  // Slot 6: Specular Prefiltered Cubemap
uniform sampler2D u_BRDFLUT;         // Slot 7: BRDF Integration 2D LUT
uniform int u_UseIBL;                // 1: Enable HDR Image-Based Lighting, 0: Analytical
uniform float u_EnvironmentIntensity;// Multiplier for IBL radiance

uniform float u_ShadowBias;         // Dynamic light depth bias
uniform float u_ShadowFilterRadius; // Dynamic penumbra spread radius

// 4. Texture Mapping & Material Controls
uniform int u_UseAlbedoMap;        // 1: Sample u_AlbedoMap, 0: Procedural pattern
uniform int u_UseNormalMap;        // 1: Perturb normal via u_NormalMap, 0: Geometric normal
uniform float u_NormalStrength;    // Normal map intensity multiplier (default 1.0)
uniform int u_UseRoughnessMap;     // 1: Sample u_RoughnessMap, 0: Uniform u_Roughness
uniform int u_UseMetallicMap;      // 1: Sample u_MetallicMap, 0: Uniform u_Metallic
uniform vec2 u_UVScale;            // UV tiling multiplier (default 1.0, 1.0)
uniform float u_Roughness;         // Material roughness (0.04 .. 1.0, default 0.5)
uniform float u_Metallic;          // Material metallic (0.0 .. 1.0, default 0.0)
uniform vec3 u_EmissionColor;      // Emissive RGB color (default 0,0,0)
uniform float u_EmissionIntensity; // Emissive radiance multiplier (default 0.0)

// 5. Post-Processing & Tone Mapping (Phase 11: Sub-step 3.2)
uniform float u_Exposure;          // Exposure multiplier (default 1.0)
uniform int u_ToneMappingMode;     // 0: ACES Filmic, 1: Reinhard, 2: Uncharted 2, 3: Linear Bypass
uniform float u_Gamma;             // Display gamma exponent (default 2.2)

// 6. Atmospheric Skybox & Hemisphere Ambient (Phase 11: Sub-step 4.3)
uniform vec3 u_CameraPos;          // World-space camera position
uniform int u_UseHemisphereAmbient;// 1: Enable hemisphere irradiance, 0: Flat ambient
uniform vec3 u_SkyZenithColor;     // Sky radiance at zenith (+Y)
uniform vec3 u_SkyHorizonColor;    // Atmospheric radiance at horizon (XZ)
uniform vec3 u_SkyGroundColor;     // Terrain bounce radiance at ground (-Y)

const float PI = 3.14159265358979323846;

// ----------------------------------------------------------------------------
// Shadow Calculation with 16-Tap Vogel Spiral / Poisson Disk PCF
// ----------------------------------------------------------------------------
const vec2 POISSON_DISK[16] = vec2[](
    vec2(-0.94201624, -0.39906216),
    vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870),
    vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845),
    vec2( 0.97484398,  0.75648377),
    vec2( 0.44323325, -0.97511554),
    vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507),
    vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367),
    vec2( 0.14383161, -0.14100790)
);

float InterleavedGradientNoise(vec2 screenPos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

vec2 Rotate2D(vec2 v, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

float CalculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float baseBias = (u_ShadowBias > 0.0) ? u_ShadowBias : 0.0012;
    float filterSpread = (u_ShadowFilterRadius > 0.0) ? u_ShadowFilterRadius : 2.0;

    float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
    float bias = max(baseBias * (1.0 - cosTheta), baseBias * 0.15);

    vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowMap, 0));
    vec2 filterRadius = texelSize * filterSpread;

    float randomAngle = InterleavedGradientNoise(gl_FragCoord.xy) * 6.28318530718;

    float shadowSum = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 offset = Rotate2D(POISSON_DISK[i], randomAngle) * filterRadius;
        float lit = texture(u_ShadowMap, vec3(projCoords.xy + offset, projCoords.z - bias));
        shadowSum += (1.0 - lit);
    }
    return shadowSum / 16.0;
}

// ----------------------------------------------------------------------------
// Cotangent-Frame Normal Perturbation from Screen-Space Derivatives
// ----------------------------------------------------------------------------
vec3 PerturbNormal(vec3 N, vec3 V, vec2 texcoord, float strength) {
    vec3 mapNormal = texture(u_NormalMap, texcoord).xyz * 2.0 - 1.0;
    mapNormal.xy *= strength;
    mapNormal = normalize(mapNormal);

    vec3 dp1 = dFdx(v_WorldPos);
    vec3 dp2 = dFdy(v_WorldPos);
    vec2 duv1 = dFdx(texcoord);
    vec2 duv2 = dFdy(texcoord);

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float lenT = length(T);
    float lenB = length(B);
    if (lenT < 1e-6 || lenB < 1e-6) {
        return N;
    }

    mat3 TBN = mat3(T / lenT, B / lenB, N);
    return normalize(TBN * mapNormal);
}

// ----------------------------------------------------------------------------
// Cook-Torrance Microfacet BRDF Functions
// ----------------------------------------------------------------------------

// 1. Trowbridge-Reitz GGX Normal Distribution Function (NDF)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 0.0000001);
}

// 2. Geometry Masking/Shadowing Function (Smith model with Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / max(denom, 0.0000001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// 3. Fresnel-Schlick Approximation
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ----------------------------------------------------------------------------
// Filmic Tone Mapping Operators (ACES, Reinhard, Uncharted 2)
// ----------------------------------------------------------------------------

// ACES Filmic Curve (Narkowicz fit)
vec3 ToneMapACES(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Reinhard Curve
vec3 ToneMapReinhard(vec3 x) {
    return x / (x + vec3(1.0));
}

// Uncharted 2 Filmic Curve (Hable)
vec3 Uncharted2TonemapPartial(vec3 x) {
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 ToneMapUncharted2(vec3 x) {
    const float W = 11.2;
    vec3 curr = Uncharted2TonemapPartial(x * 2.0);
    vec3 whiteScale = vec3(1.0) / Uncharted2TonemapPartial(vec3(W));
    return clamp(curr * whiteScale, 0.0, 1.0);
}

// ----------------------------------------------------------------------------
// Main Shader Routine
// ----------------------------------------------------------------------------
void main() {
    vec2 uvScale = (u_UVScale.x != 0.0 && u_UVScale.y != 0.0) ? u_UVScale : vec2(1.0);
    vec2 uv = v_TexCoord * uvScale;

    // View direction in world space (surface to camera)
    vec3 viewDir = (length(u_CameraPos - v_WorldPos) > 1e-4) ? normalize(u_CameraPos - v_WorldPos) : vec3(0.0, 1.0, 0.0);

    // 1. Geometric surface normal (computed from 3D surface derivatives for accurate lighting)
    vec3 normal;
    if (u_PatternMode == 2) {
        normal = normalize((u_Transform * vec4(v_LocalPos, 0.0)).xyz);
        if (dot(normal, viewDir) < 0.0) normal = -normal;
    } else {
        vec3 dx = dFdx(v_WorldPos);
        vec3 dy = dFdy(v_WorldPos);
        vec3 crossN = cross(dx, dy);
        normal = (length(crossN) > 1e-6) ? normalize(crossN) : vec3(0.0, 1.0, 0.0);
        if (dot(normal, viewDir) < 0.0) normal = -normal;
    }

    // 2. Cotangent normal map perturbation
    if (u_UseNormalMap == 1) {
        float normStrength = (u_NormalStrength >= 0.0) ? u_NormalStrength : 1.0;
        normal = PerturbNormal(normal, viewDir, uv, normStrength);
    }

    // 3. Base color (Albedo texture map or procedural checkerboard)
    vec4 baseColor;
    if (u_UseAlbedoMap == 1) {
        vec4 texColor = texture(u_AlbedoMap, uv);
        baseColor = texColor * u_Color1;
    } else {
        float pattern = 0.0;
        if (u_PatternMode == 0) {
            vec2 coord = floor(v_WorldPos.xz * u_TileScale);
            pattern = mod(coord.x + coord.y, 2.0);
        } else if (u_PatternMode == 1) {
            vec3 coord = floor((v_LocalPos + vec3(0.5)) * u_TileScale + vec3(0.0001));
            pattern = mod(coord.x + coord.y + coord.z, 2.0);
        } else if (u_PatternMode == 2) {
            vec3 nLocal = normalize(v_LocalPos);
            float uSph = atan(nLocal.z, nLocal.x) / (2.0 * PI) + 0.5;
            float vSph = asin(clamp(nLocal.y, -1.0, 1.0)) / PI + 0.5;
            vec2 coord = floor(vec2(uSph * u_TileScale, vSph * (u_TileScale * 0.5)));
            pattern = mod(coord.x + coord.y, 2.0);
        } else {
            pattern = 0.0;
        }
        baseColor = mix(u_Color1, u_Color2, pattern);
    }

    // 4. Material properties (Roughness & Metallic)
    float roughness = (u_UseRoughnessMap == 1) ? texture(u_RoughnessMap, uv).r : u_Roughness;
    roughness = clamp(roughness, 0.04, 1.0);

    float metallic = (u_UseMetallicMap == 1) ? texture(u_MetallicMap, uv).r : u_Metallic;
    metallic = clamp(metallic, 0.0, 1.0);

    // 5. PBR Cook-Torrance Direct Lighting
    vec3 N = normal;
    vec3 V = viewDir;
    vec3 L = normalize(u_LightDir);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // Surface reflection at zero incidence (F0)
    // 0.04 for standard dielectrics, tinted baseColor for metals
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, baseColor.rgb, metallic);

    // Cook-Torrance Specular BRDF terms
    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular     = numerator / denominator;

    // Energy conservation: kS is specular fraction, kD is diffuse fraction
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic; // Pure metals absorb diffuse light

    // Real-time PCF soft shadow
    float shadow = 0.0;
    if (u_ShadowsEnabled == 1) {
        shadow = CalculateShadow(v_FragPosLightSpace, N, L);
    }

    // Direct lighting radiance
    vec3 radiance = u_LightColor * u_LightIntensity;
    vec3 Lo = (kD * baseColor.rgb / PI + specular) * radiance * NdotL * (1.0 - shadow);

    // Ambient Irradiance & Environment Reflection (IBL or Hemisphere)
    vec3 ambient;
    if (u_UseIBL == 1) {
        vec3 F_ibl = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
        vec3 kS_ibl = F_ibl;
        vec3 kD_ibl = (vec3(1.0) - kS_ibl) * (1.0 - metallic);

        vec3 irradiance = texture(u_IrradianceMap, N).rgb;
        vec3 diffuseAmbient = irradiance * baseColor.rgb * kD_ibl;

        vec3 R = reflect(-V, N);
        const float MAX_REFLECTION_LOD = 4.0;
        vec3 prefilteredColor = textureLod(u_PrefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
        vec2 envBRDF = texture(u_BRDFLUT, vec2(max(NdotV, 0.0), roughness)).rg;
        vec3 specularAmbient = prefilteredColor * (F_ibl * envBRDF.x + envBRDF.y);

        float envIntensity = (u_EnvironmentIntensity > 0.0) ? u_EnvironmentIntensity : 1.0;
        ambient = (diffuseAmbient + specularAmbient) * envIntensity;
    } else if (u_UseHemisphereAmbient == 1) {
        // Vertical normal component maps [-1, 1] to [0, 1] for ground-to-sky gradient
        float upWeight = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
        float zenithWeight = clamp(N.y, 0.0, 1.0);
        vec3 skyUpper = mix(u_SkyHorizonColor, u_SkyZenithColor, zenithWeight);
        vec3 hemisphereLight = mix(u_SkyGroundColor, skyUpper, upWeight);

        // Diffuse hemisphere irradiance (energy-conserved via kD)
        vec3 diffuseAmbient = hemisphereLight * baseColor.rgb * kD;

        // Specular ambient environment approximation
        vec3 R = reflect(-V, N);
        float specZenith = clamp(R.y, 0.0, 1.0);
        vec3 specUpper = mix(u_SkyHorizonColor, u_SkyZenithColor, specZenith);

        // Distinct horizon transition for realistic automotive metallic sheen
        float horizonFactor = smoothstep(-0.06, 0.06, R.y);
        vec3 specSky = mix(u_SkyGroundColor, specUpper, horizonFactor);

        // Environment sun reflection lobe (sharp highlight for smooth metals)
        float sunSpec = pow(max(dot(R, L), 0.0), mix(64.0, 8.0, roughness));
        specSky += u_LightColor * u_LightIntensity * sunSpec * (1.0 - roughness * 0.8) * 0.5;

        // Ambient Fresnel with roughness attenuation
        vec3 F_ambient = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
        vec3 specularAmbient = specSky * F_ambient * (1.0 - roughness * 0.5);

        // Physically accurate combination: diffuse is attenuated by ambient intensity,
        // specular environment reflections retain full Fresnel reflectivity
        ambient = diffuseAmbient * u_AmbientIntensity + specularAmbient;
    } else {
        vec3 R = reflect(-V, N);
        vec3 specSky = mix(vec3(0.12), vec3(0.85, 0.90, 0.98), clamp(R.y * 0.5 + 0.5, 0.0, 1.0));
        vec3 F_ambient = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
        vec3 specularAmbient = specSky * F_ambient * (1.0 - roughness * 0.5);
        vec3 diffuseAmbient = u_LightColor * u_AmbientIntensity * baseColor.rgb * kD;
        ambient = diffuseAmbient + specularAmbient;
    }

    // Emissive lighting term (Automotive lights / Glow)
    vec3 emissive = u_EmissionColor * max(u_EmissionIntensity, 0.0);

    vec3 linearColor = ambient + Lo + emissive;

    // 6. Camera Exposure, Filmic Tone Mapping & Gamma Correction
    float exposure = (u_Exposure > 0.0) ? u_Exposure : 1.0;
    vec3 exposed = linearColor * exposure;

    vec3 mappedColor;
    if (u_ToneMappingMode == 0) {
        mappedColor = ToneMapACES(exposed);
    } else if (u_ToneMappingMode == 1) {
        mappedColor = ToneMapReinhard(exposed);
    } else if (u_ToneMappingMode == 2) {
        mappedColor = ToneMapUncharted2(exposed);
    } else {
        mappedColor = clamp(exposed, 0.0, 1.0);
    }

    float gamma = (u_Gamma > 0.0) ? u_Gamma : 2.2;
    vec3 srgbColor = pow(mappedColor, vec3(1.0 / gamma));

    o_Color = vec4(srgbColor, baseColor.a);
}
