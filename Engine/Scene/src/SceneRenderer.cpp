#include <Axle/SceneRenderer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Window.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/Log.hpp>
#include <Axle/ParticleRenderer.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/RenderParticleData.hpp>
#include <Axle/PhysicsEngine.hpp>

#include <algorithm>
#include <filesystem>

namespace Axle {

    SceneRenderer::SceneRenderer() {
        Init();
    }

    bool SceneRenderer::IsReady() const {
        const auto valid = [](const auto& shader) { return shader && shader->GetRendererID() != 0; };
        return Window::HasActiveContext() && valid(m_Shader) && valid(m_ShadowShader) &&
            m_ShadowMap && m_ShadowMap->GetDepthTextureID() && m_ShadowMap->GetRendererID() &&
            m_Skybox && valid(m_Skybox->GetShader()) && m_SkinningPipeline &&
            valid(m_SkinningPipeline->GetShader()) && valid(m_SkinningPipeline->GetShadowShader()) &&
            m_ParticleRenderer && valid(m_ParticleRenderer->GetShader());
    }

    void SceneRenderer::Init() {
        InitShaders();

        // Initialize Shadow Mapping (2048x2048)
        m_ShadowMap = std::make_shared<ShadowMap>();

        // Initialize Procedural Atmospheric Skybox
        m_Skybox = std::make_shared<Skybox>();

        // Initialize GPU-Instanced Particle Renderer (Phase 31)
        m_ParticleRenderer = std::make_shared<ParticleRenderer>();

        // Initialize internal Physics Debug Renderer fallback
        if (!m_InternalDebugRenderer && Window::HasActiveContext()) {
            if (!PhysicsEngine::IsInitialized()) PhysicsEngine::Init();
            m_InternalDebugRenderer = std::make_unique<PhysicsDebugRenderer>();
        }

        AXLE_CORE_INFO("SceneRenderer: Decoupled Multi-Pass Graphics Pipeline successfully initialized.");
    }

    void SceneRenderer::InitShaders() {
        // 1. Load or fallback PBR lighting shader
        auto vertPath = FileSystem::ResolveAssetPath("shaders/Checkerboard.vert");
        auto fragPath = FileSystem::ResolveAssetPath("shaders/Checkerboard.frag");
        std::string vertexSrc = FileSystem::ReadTextFile(vertPath);
        std::string fragmentSrc = FileSystem::ReadTextFile(fragPath);

        if (vertexSrc.empty() || fragmentSrc.empty()) {
            AXLE_CORE_WARN("SceneRenderer: Shader files not found at '{0}'. Using fallback embedded PBR shaders.", vertPath.string());
            vertexSrc = R"(
                #version 450 core
                layout(location = 0) in vec3 a_Position;
                layout(location = 1) in vec2 a_TexCoord;
                uniform mat4 u_ViewProjection;
                uniform mat4 u_Transform;
                uniform mat4 u_LightSpaceMatrix;
                out vec3 v_WorldPos;
                out vec3 v_LocalPos;
                out vec2 v_TexCoord;
                out vec4 v_FragPosLightSpace;
                void main() {
                    v_LocalPos = a_Position;
                    v_TexCoord = a_TexCoord;
                    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
                    v_WorldPos = worldPos.xyz;
                    v_FragPosLightSpace = u_LightSpaceMatrix * worldPos;
                    gl_Position = u_ViewProjection * worldPos;
                }
            )";
            fragmentSrc = R"(
                #version 450 core
                layout(location = 0) out vec4 o_Color;

                in vec3 v_WorldPos;
                in vec3 v_LocalPos;
                in vec2 v_TexCoord;
                in vec4 v_FragPosLightSpace;

                uniform int u_PatternMode;
                uniform float u_TileScale;
                uniform vec4 u_Color1;
                uniform vec4 u_Color2;
                uniform vec3 u_LightDir;
                uniform vec3 u_LightColor;
                uniform float u_LightIntensity;
                uniform float u_AmbientIntensity;
                uniform int u_ShadowsEnabled;
                uniform mat4 u_Transform;
                uniform mat4 u_View;

                uniform sampler2D u_AlbedoMap;
                uniform sampler2DArrayShadow u_ShadowMap;
                uniform sampler2D u_NormalMap;
                uniform sampler2D u_RoughnessMap;
                uniform sampler2D u_MetallicMap;
                uniform samplerCube u_IrradianceMap;
                uniform samplerCube u_PrefilterMap;
                uniform sampler2D u_BRDFLUT;
                uniform int u_UseIBL;
                uniform float u_EnvironmentIntensity;

                uniform mat4 u_CascadeLightSpaceMatrices[4];
                uniform float u_CascadeSplitDistances[4];
                uniform float u_ShadowBias;
                uniform float u_ShadowSlopeBias;
                uniform float u_ShadowNormalBias;
                uniform float u_ShadowFilterRadius;

                // Point & Spot Lighting
                struct PointLight {
                    vec3 Position;
                    vec3 Color;
                    float Intensity;
                    float Radius;
                };
                struct SpotLight {
                    vec3 Position;
                    vec3 Direction;
                    vec3 Color;
                    float Intensity;
                    float Range;
                    float InnerCutOff;
                    float OuterCutOff;
                };
                uniform int u_NumPointLights;
                uniform PointLight u_PointLights[4];
                uniform int u_NumSpotLights;
                uniform SpotLight u_SpotLights[4];

                uniform int u_UseAlbedoMap;
                uniform int u_UseNormalMap;
                uniform float u_NormalStrength;
                uniform int u_UseRoughnessMap;
                uniform int u_UseMetallicMap;
                uniform vec2 u_UVScale;
                uniform float u_Roughness;
                uniform float u_Metallic;
                uniform vec3 u_EmissionColor;
                uniform float u_EmissionIntensity;

                uniform float u_Exposure;
                uniform int u_ToneMappingMode;
                uniform float u_Gamma;

                uniform vec3 u_CameraPos;
                uniform int u_UseHemisphereAmbient;
                uniform vec3 u_SkyZenithColor;
                uniform vec3 u_SkyHorizonColor;
                uniform vec3 u_SkyGroundColor;

                float SampleCascadePCF5x5(int cascadeIdx, vec4 lightSpacePos, vec2 texelSize, float spreadMult, float bias) {
                    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
                    projCoords = projCoords * 0.5 + 0.5;

                    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {
                        return 0.0;
                    }

                    vec2 spread = texelSize * spreadMult;
                    float shadowSum = 0.0;
                    for (int x = -2; x <= 2; ++x) {
                        for (int y = -2; y <= 2; ++y) {
                            vec2 offset = vec2(float(x), float(y)) * spread;
                            float lit = texture(u_ShadowMap, vec4(projCoords.xy + offset, float(cascadeIdx), projCoords.z - bias));
                            shadowSum += (1.0 - lit);
                        }
                    }
                    return shadowSum / 25.0;
                }

                float CalculateShadow(vec3 worldPos, vec3 normal, vec3 lightDir) {
                    if (u_ShadowsEnabled == 0) return 0.0;

                    vec4 viewPos = u_View * vec4(worldPos, 1.0);
                    float depthValue = abs(viewPos.z);
                    if (depthValue >= u_CascadeSplitDistances[3]) {
                        return 0.0;
                    }

                    int cascadeIndex = 3;
                    for (int i = 0; i < 4; ++i) {
                        if (depthValue < u_CascadeSplitDistances[i]) {
                            cascadeIndex = i;
                            break;
                        }
                    }

                    float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
                    float slope = sqrt(clamp(1.0 - cosTheta * cosTheta, 0.0, 1.0)) / max(cosTheta, 0.001);
                    float baseBias = (u_ShadowBias > 0.0) ? u_ShadowBias : 0.0008;
                    float slopeScale = (u_ShadowSlopeBias > 0.0) ? u_ShadowSlopeBias : 0.0020;
                    float normalBiasVal = (u_ShadowNormalBias > 0.0) ? u_ShadowNormalBias : 0.0150;

                    float cascadeScale = 1.0 + float(cascadeIndex) * 0.75;
                    float bias = (baseBias + slopeScale * clamp(slope, 0.0, 4.0)) * cascadeScale;
                    vec3 biasedPos = worldPos + normal * (normalBiasVal * cascadeScale);
                    vec4 fragPosLightSpace = u_CascadeLightSpaceMatrices[cascadeIndex] * vec4(biasedPos, 1.0);

                    vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowMap, 0).xy);
                    float filterSpread = (u_ShadowFilterRadius > 0.0) ? u_ShadowFilterRadius : 1.0;

                    float shadow = SampleCascadePCF5x5(cascadeIndex, fragPosLightSpace, texelSize, filterSpread, bias);

                    float currentSplit = u_CascadeSplitDistances[cascadeIndex];
                    float prevSplit = (cascadeIndex > 0) ? u_CascadeSplitDistances[cascadeIndex - 1] : 0.0;
                    float cascadeRange = currentSplit - prevSplit;
                    float distFromSplit = currentSplit - depthValue;
                    float blendBand = cascadeRange * 0.15;

                    if (distFromSplit < blendBand && cascadeIndex < 3) {
                        int nextCascade = cascadeIndex + 1;
                        float nextScale = 1.0 + float(nextCascade) * 0.75;
                        float nextBias = (baseBias + slopeScale * clamp(slope, 0.0, 4.0)) * nextScale;
                        vec3 nextBiasedPos = worldPos + normal * (normalBiasVal * nextScale);
                        vec4 nextLightPos = u_CascadeLightSpaceMatrices[nextCascade] * vec4(nextBiasedPos, 1.0);

                        float nextShadow = SampleCascadePCF5x5(nextCascade, nextLightPos, texelSize, filterSpread, nextBias);
                        float blendFactor = clamp(distFromSplit / blendBand, 0.0, 1.0);
                        shadow = mix(nextShadow, shadow, blendFactor);
                    }

                    float fadeStart = u_CascadeSplitDistances[3] * 0.85;
                    float fadeEnd = u_CascadeSplitDistances[3];
                    if (depthValue > fadeStart) {
                        float fade = 1.0 - clamp((depthValue - fadeStart) / (fadeEnd - fadeStart), 0.0, 1.0);
                        shadow *= fade;
                    }

                    return shadow;
                }

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
                    if (lenT < 1e-6 || lenB < 1e-6) return N;
                    mat3 TBN = mat3(T / lenT, B / lenB, N);
                    return normalize(TBN * mapNormal);
                }
            )"
            R"(
                void main() {
                    vec2 uvScale = (u_UVScale.x != 0.0 && u_UVScale.y != 0.0) ? u_UVScale : vec2(1.0);
                    vec2 uv = v_TexCoord * uvScale;

                    // View direction in world space (surface to camera)
                    vec3 viewDir = (length(u_CameraPos - v_WorldPos) > 1e-4) ? normalize(u_CameraPos - v_WorldPos) : vec3(0.0, 1.0, 0.0);

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

                    if (u_UseNormalMap == 1) {
                        float normStrength = (u_NormalStrength >= 0.0) ? u_NormalStrength : 1.0;
                        normal = PerturbNormal(normal, viewDir, uv, normStrength);
                    }

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
                            float uSph = atan(nLocal.z, nLocal.x) / (2.0 * 3.14159265359) + 0.5;
                            float vSph = asin(clamp(nLocal.y, -1.0, 1.0)) / 3.14159265359 + 0.5;
                            vec2 coord = floor(vec2(uSph * u_TileScale, vSph * (u_TileScale * 0.5)));
                            pattern = mod(coord.x + coord.y, 2.0);
                        } else {
                            pattern = 0.0;
                        }
                        baseColor = mix(u_Color1, u_Color2, pattern);
                    }

                    float roughness = (u_UseRoughnessMap == 1) ? texture(u_RoughnessMap, uv).r : u_Roughness;
                    roughness = clamp(roughness, 0.04, 1.0);

                    float metallic = (u_UseMetallicMap == 1) ? texture(u_MetallicMap, uv).r : u_Metallic;
                    metallic = clamp(metallic, 0.0, 1.0);

                    const float PI = 3.14159265358979323846;

                    // 1. Distribution GGX
                    float a = roughness * roughness;
                    float a2 = a * a;
                    vec3 N = normal;
                    vec3 V = viewDir;
                    vec3 L = normalize(u_LightDir);
                    vec3 H = normalize(V + L);
                    float NdotH = max(dot(N, H), 0.0);
                    float NdotH2 = NdotH * NdotH;
                    float denomD = (NdotH2 * (a2 - 1.0) + 1.0);
                    denomD = PI * denomD * denomD;
                    float NDF = a2 / max(denomD, 0.0000001);

                    // 2. Geometry Smith
                    float NdotV = max(dot(N, V), 0.0);
                    float NdotL = max(dot(N, L), 0.0);
                    float rK = (roughness + 1.0);
                    float k = (rK * rK) / 8.0;
                    float ggxV = NdotV / max(NdotV * (1.0 - k) + k, 0.0000001);
                    float ggxL = NdotL / max(NdotL * (1.0 - k) + k, 0.0000001);
                    float G = ggxV * ggxL;

                    // 3. Fresnel Schlick
                    vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);
                    vec3 F = F0 + (1.0 - F0) * pow(clamp(1.0 - max(dot(H, V), 0.0), 0.0, 1.0), 5.0);

                    vec3 numerator = NDF * G * F;
                    float denominator = 4.0 * NdotV * NdotL + 0.0001;
                    vec3 specular = numerator / denominator;

                    vec3 kS = F;
                    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

                    float shadow = 0.0;
                    if (u_ShadowsEnabled == 1) {
                        shadow = CalculateShadow(v_WorldPos, N, L);
                    }

                    vec3 radiance = u_LightColor * u_LightIntensity;
                    vec3 Lo = (kD * baseColor.rgb / PI + specular) * radiance * NdotL * (1.0 - shadow);

                    for (int i = 0; i < u_NumPointLights && i < 4; ++i) {
                        vec3 lightVec = u_PointLights[i].Position - v_WorldPos;
                        float dist = length(lightVec);
                        if (dist > u_PointLights[i].Radius || dist < 0.001) continue;
                        vec3 lDir = normalize(lightVec);
                        vec3 hDir = normalize(V + lDir);
                        float nDotL_p = max(dot(N, lDir), 0.0);
                        if (nDotL_p <= 0.0) continue;
                        float atten = clamp(1.0 - pow(dist / u_PointLights[i].Radius, 4.0), 0.0, 1.0);
                        atten = (atten * atten) / (dist * dist + 1.0);
                        float ndf_p = DistributionGGX(N, hDir, roughness);
                        float g_p   = GeometrySmith(N, V, lDir, roughness);
                        vec3  f_p   = FresnelSchlick(max(dot(hDir, V), 0.0), F0);
                        vec3 spec_p = (ndf_p * g_p * f_p) / (4.0 * NdotV * nDotL_p + 0.0001);
                        vec3 kS_p = f_p;
                        vec3 kD_p = (vec3(1.0) - kS_p) * (1.0 - metallic);
                        vec3 rad_p = u_PointLights[i].Color * u_PointLights[i].Intensity * atten;
                        Lo += (kD_p * baseColor.rgb / PI + spec_p) * rad_p * nDotL_p;
                    }

                    for (int i = 0; i < u_NumSpotLights && i < 4; ++i) {
                        vec3 lightVec = u_SpotLights[i].Position - v_WorldPos;
                        float dist = length(lightVec);
                        if (dist > u_SpotLights[i].Range || dist < 0.001) continue;
                        vec3 lDir = normalize(lightVec);
                        vec3 hDir = normalize(V + lDir);
                        float nDotL_s = max(dot(N, lDir), 0.0);
                        if (nDotL_s <= 0.0) continue;
                        float cosTheta = dot(-lDir, normalize(u_SpotLights[i].Direction));
                        float epsilon = u_SpotLights[i].InnerCutOff - u_SpotLights[i].OuterCutOff;
                        float spotFactor = clamp((cosTheta - u_SpotLights[i].OuterCutOff) / max(epsilon, 0.0001), 0.0, 1.0);
                        if (spotFactor <= 0.0) continue;
                        float atten = clamp(1.0 - pow(dist / u_SpotLights[i].Range, 4.0), 0.0, 1.0);
                        atten = (atten * atten) / (dist * dist + 1.0) * spotFactor;
                        float ndf_s = DistributionGGX(N, hDir, roughness);
                        float g_s   = GeometrySmith(N, V, lDir, roughness);
                        vec3  f_s   = FresnelSchlick(max(dot(hDir, V), 0.0), F0);
                        vec3 spec_s = (ndf_s * g_s * f_s) / (4.0 * NdotV * nDotL_s + 0.0001);
                        vec3 kS_s = f_s;
                        vec3 kD_s = (vec3(1.0) - kS_s) * (1.0 - metallic);
                        vec3 rad_s = u_SpotLights[i].Color * u_SpotLights[i].Intensity * atten;
                        Lo += (kD_s * baseColor.rgb / PI + spec_s) * rad_s * nDotL_s;
                    }
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
                        float upWeight = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
                        float zenithWeight = clamp(N.y, 0.0, 1.0);
                        vec3 skyUpper = mix(u_SkyHorizonColor, u_SkyZenithColor, zenithWeight);
                        vec3 hemisphereLight = mix(u_SkyGroundColor, skyUpper, upWeight);

                        vec3 diffuseAmbient = hemisphereLight * baseColor.rgb * kD;

                        vec3 R = reflect(-V, N);
                        float specZenith = clamp(R.y, 0.0, 1.0);
                        vec3 specUpper = mix(u_SkyHorizonColor, u_SkyZenithColor, specZenith);

                        // Distinct horizon transition for realistic automotive metallic sheen
                        float horizonFactor = smoothstep(-0.06, 0.06, R.y);
                        vec3 specSky = mix(u_SkyGroundColor, specUpper, horizonFactor);

                        // Environment sun reflection lobe (sharp highlight for smooth metals)
                        float sunSpec = pow(max(dot(R, L), 0.0), mix(64.0, 8.0, roughness));
                        specSky += u_LightColor * u_LightIntensity * sunSpec * (1.0 - roughness * 0.8) * 0.5;

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
                    vec3 emissive = u_EmissionColor * max(u_EmissionIntensity, 0.0);
                    vec3 linearColor = ambient + Lo + emissive;

                    // Exposure & Tone Mapping
                    float exposure = (u_Exposure > 0.0) ? u_Exposure : 1.0;
                    vec3 exposed = linearColor * exposure;

                    vec3 mappedColor;
                    if (u_ToneMappingMode == 0) {
                        // ACES Filmic (Narkowicz fit)
                        const float a = 2.51;
                        const float b = 0.03;
                        const float c = 2.43;
                        const float d = 0.59;
                        const float e = 0.14;
                        mappedColor = clamp((exposed * (a * exposed + b)) / (exposed * (c * exposed + d) + e), 0.0, 1.0);
                    } else if (u_ToneMappingMode == 1) {
                        // Reinhard
                        mappedColor = exposed / (exposed + vec3(1.0));
                    } else if (u_ToneMappingMode == 2) {
                        // Uncharted 2
                        vec3 x = exposed * 2.0;
                        const float A = 0.15; const float B = 0.50; const float C = 0.10; const float D = 0.20; const float E = 0.02; const float F = 0.30;
                        vec3 curr = ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
                        const float W = 11.2;
                        vec3 white = ((vec3(W) * (A * vec3(W) + C * B) + D * E) / (vec3(W) * (A * vec3(W) + B) + D * F)) - E / F;
                        mappedColor = clamp(curr / white, 0.0, 1.0);
                    } else {
                        mappedColor = clamp(exposed, 0.0, 1.0);
                    }

                    float gamma = (u_Gamma > 0.0) ? u_Gamma : 2.2;
                    vec3 srgbColor = pow(mappedColor, vec3(1.0 / gamma));

                    o_Color = vec4(srgbColor, baseColor.a);
                }
            )";
        } else {
            AXLE_CORE_INFO("SceneRenderer: Loaded PBR shaders from '{0}' and '{1}'", vertPath.string(), fragPath.string());
        }

        m_Shader = std::make_shared<Shader>(vertexSrc, fragmentSrc);
        m_Shader->Bind();
        m_Shader->SetInt("u_AlbedoMap", 0);
        m_Shader->SetInt("u_ShadowMap", 1);
        m_Shader->SetInt("u_NormalMap", 2);
        m_Shader->SetInt("u_RoughnessMap", 3);
        m_Shader->SetInt("u_MetallicMap", 4);
        m_Shader->SetInt("u_IrradianceMap", 5);
        m_Shader->SetInt("u_PrefilterMap", 6);
        m_Shader->SetInt("u_BRDFLUT", 7);
        m_Shader->SetInt("u_UseIBL", 0);
        m_Shader->SetFloat("u_EnvironmentIntensity", 1.0f);
        m_Shader->SetFloat("u_NormalStrength", 1.0f);
        m_Shader->SetFloat3("u_EmissionColor", glm::vec3(0.0f));
        m_Shader->SetFloat("u_EmissionIntensity", 0.0f);
        m_Shader->SetFloat("u_Exposure", 1.0f);
        m_Shader->SetInt("u_ToneMappingMode", 0);
        m_Shader->SetFloat("u_Gamma", 2.2f);
        m_Shader->SetInt("u_UseHemisphereAmbient", 1);
        m_Shader->SetFloat3("u_SkyZenithColor", glm::vec3(0.18f, 0.42f, 0.78f));
        m_Shader->SetFloat3("u_SkyHorizonColor", glm::vec3(0.70f, 0.82f, 0.95f));
        m_Shader->SetFloat3("u_SkyGroundColor", glm::vec3(0.18f, 0.16f, 0.14f));
        m_Shader->SetInt("u_NumPointLights", 0);
        m_Shader->SetInt("u_NumSpotLights", 0);

        // 2. Load or fallback Shadow Depth shader
        std::string shadowVertSrc;
        std::string shadowFragSrc;
        auto shadowVertPath = FileSystem::ResolveAssetPath("shaders/ShadowDepth.vert");
        auto shadowFragPath = FileSystem::ResolveAssetPath("shaders/ShadowDepth.frag");
        if (FileSystem::Exists(shadowVertPath) && FileSystem::Exists(shadowFragPath)) {
            shadowVertSrc = FileSystem::ReadTextFile(shadowVertPath);
            shadowFragSrc = FileSystem::ReadTextFile(shadowFragPath);
        }
        if (shadowVertSrc.empty() || shadowFragSrc.empty()) {
            shadowVertSrc = R"(
                #version 450 core
                layout (location = 0) in vec3 a_Position;
                uniform mat4 u_LightSpaceMatrix;
                uniform mat4 u_Transform;
                void main() {
                    gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
                }
            )";
            shadowFragSrc = R"(
                #version 450 core
                void main() {}
            )";
        }
        m_ShadowShader = std::make_shared<Shader>(shadowVertSrc, shadowFragSrc);

        // 3. Initialize GPU Skinning Pipeline for Skeletal Meshes
        m_SkinningPipeline = std::make_shared<SkinningPipeline>();
        m_SkinningPipeline->Init();
    }

    void SceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene,
                                   const PerspectiveCamera& camera,
                                   const std::shared_ptr<Framebuffer>& targetFBO,
                                   PhysicsScene* physicsScene,
                                   PhysicsDebugRenderer* physicsDebugRenderer,
                                   const SceneRenderSettings& settings) {
        if (!scene) return;
        auto world = ExtractRenderWorld(*scene);
        if (!ResolveRenderWorldResources(world)) return;
        RenderWorldView(world, camera, targetFBO, physicsScene, physicsDebugRenderer, settings);
    }

    void SceneRenderer::RenderWorldView(const RenderWorld& world,
                                        const PerspectiveCamera& camera,
                                        const std::shared_ptr<Framebuffer>& targetFBO,
                                        PhysicsScene* physicsScene,
                                        PhysicsDebugRenderer* physicsDebugRenderer,
                                        const SceneRenderSettings& settings) {
        if (!Window::HasActiveContext() || !m_Shader) return;

        // 1. Lighting setup from extracted RenderWorld
        if (world.DirectionalLight.has_value()) {
            m_LightDir = world.DirectionalLight->Direction;
        }
        bool castShadows = settings.EnableShadows && world.DirectionalLight.has_value() && world.DirectionalLight->CastsShadows;

        // 2. Cascaded Shadow Map Pass
        if (castShadows && m_ShadowMap && m_ShadowShader) {
            uint32_t targetRes = world.DirectionalLight->ShadowQuality == 0 ? 1024 : world.DirectionalLight->ShadowQuality == 2 ? 4096 : 2048;
            if (m_ShadowMap->GetWidth() != targetRes || m_ShadowMap->GetHeight() != targetRes) {
                m_ShadowMap->Resize(targetRes, targetRes);
            }

            m_ShadowMap->CalculateCascades(camera, m_LightDir, world.DirectionalLight->MaxShadowDistance, world.DirectionalLight->CascadeSplitLambda);
            const auto& cascadeMatrices = m_ShadowMap->GetCascadeMatrices();

            m_ShadowShader->Bind();
            for (uint32_t c = 0; c < cascadeMatrices.size(); ++c) {
                m_ShadowMap->BindCascade(c);
                m_ShadowShader->SetMat4("u_LightSpaceMatrix", cascadeMatrices[c]);
                for (const auto& meshInst : world.Meshes) {
                    if (!meshInst.GLMesh || !meshInst.CastsShadows) continue;
                    m_ShadowShader->SetMat4("u_Transform", meshInst.WorldTransform);
                    meshInst.GLMesh->Bind();
                    RenderCommand::DrawIndexed(meshInst.GLMesh);
                    meshInst.GLMesh->Unbind();
                }
                if (m_SkinningPipeline && m_SkinningPipeline->GetShadowShader()) {
                    auto shader = m_SkinningPipeline->GetShadowShader(); shader->Bind();
                    shader->SetMat4("u_LightSpaceMatrix", cascadeMatrices[c]);
                    for (const auto& skin : world.Skins) {
                        if (!skin.GLMesh || !skin.CastsShadows) continue;
                        shader->SetMat4("u_Transform", skin.WorldTransform);
                        if (skin.BoneMatrices.empty()) m_SkinningPipeline->UploadIdentityPalette(*shader);
                        else m_SkinningPipeline->UploadBonePalette(*shader, skin.BoneMatrices);
                        RenderCommand::DrawIndexed(skin.GLMesh);
                    }
                }
            }
            m_ShadowMap->Unbind();
            m_LightSpaceMatrix = m_ShadowMap->GetLightSpaceMatrix(0);
        }

        // 3. Geometry & Lighting Pass
        u32 renderWidth = m_ViewportWidth;
        u32 renderHeight = m_ViewportHeight;

        if (targetFBO) {
            renderWidth = targetFBO->GetSpecification().Width;
            renderHeight = targetFBO->GetSpecification().Height;
            targetFBO->Bind();
        } else {
            RenderCommand::BindDefaultFramebuffer();
        }

        RenderCommand::SetViewport(0, 0, renderWidth, renderHeight);
        RenderCommand::SetClearColor(settings.ClearColor);
        RenderCommand::Clear();

        Texture2D::GetWhiteTexture()->Bind(0);
        if (m_ShadowMap) {
            m_ShadowMap->BindTexture(1);
        }
        Texture2D::GetFlatNormalTexture()->Bind(2);
        Texture2D::GetDefaultRoughnessTexture()->Bind(3);
        Texture2D::GetBlackTexture()->Bind(4);

        m_Shader->Bind();
        m_Shader->SetMat4("u_View", camera.GetViewMatrix());
        m_Shader->SetMat4("u_Projection", camera.GetProjectionMatrix());
        m_Shader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
        m_Shader->SetFloat3("u_CameraPos", camera.GetPosition());

        if (world.DirectionalLight.has_value()) {
            const auto& dl = *world.DirectionalLight;
            m_Shader->SetFloat3("u_LightDir", dl.Direction);
            m_Shader->SetFloat3("u_LightColor", dl.Color);
            m_Shader->SetFloat("u_LightIntensity", dl.Intensity);
            m_Shader->SetFloat("u_AmbientIntensity", dl.AmbientIntensity);
        } else {
            m_Shader->SetFloat3("u_LightDir", m_LightDir);
            m_Shader->SetFloat3("u_LightColor", glm::vec3(1.0f));
            m_Shader->SetFloat("u_LightIntensity", 1.0f);
            m_Shader->SetFloat("u_AmbientIntensity", 0.28f);
        }

        const auto& cascadeMatrices = (castShadows && m_ShadowMap) ? m_ShadowMap->GetCascadeMatrices() : std::vector<glm::mat4>{};
        const auto& splitDists = (castShadows && m_ShadowMap) ? m_ShadowMap->GetCascadeSplitDistances() : std::vector<float>{};

        if (!cascadeMatrices.empty()) {
            m_Shader->SetMat4Array("u_CascadeLightSpaceMatrices", cascadeMatrices.data(), static_cast<u32>(cascadeMatrices.size()));
            m_Shader->SetFloatArray("u_CascadeSplitDistances", splitDists.data(), static_cast<u32>(splitDists.size()));
        }
        m_Shader->SetInt("u_ShadowsEnabled", castShadows ? 1 : 0);

        m_Shader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrix);
        m_Shader->SetInt("u_ShadowMap", 1); m_Shader->SetInt("u_AlbedoMap", 0);
        m_Shader->SetInt("u_NormalMap", 2); m_Shader->SetInt("u_RoughnessMap", 3); m_Shader->SetInt("u_MetallicMap", 4);
        m_Shader->SetInt("u_IrradianceMap", 5); m_Shader->SetInt("u_PrefilterMap", 6); m_Shader->SetInt("u_BRDFLUT", 7);
        const auto sun = world.DirectionalLight.value_or(RenderDirectionalLight{});
        m_Shader->SetFloat("u_ShadowBias", sun.ShadowBias); m_Shader->SetFloat("u_ShadowSlopeBias", sun.ShadowSlopeBias);
        m_Shader->SetFloat("u_ShadowNormalBias", sun.ShadowNormalBias); m_Shader->SetFloat("u_ShadowFilterRadius", sun.ShadowFilterRadius);
        m_Shader->SetFloat("u_Exposure", sun.Exposure); m_Shader->SetFloat("u_Gamma", sun.Gamma);
        m_Shader->SetInt("u_ToneMappingMode", sun.ToneMappingMode);
        m_Shader->SetInt("u_UseHemisphereAmbient", world.Skybox.Enabled && world.Skybox.HemisphereAmbient ? 1 : 0);
        m_Shader->SetFloat3("u_SkyZenithColor", glm::vec3(world.Skybox.ZenithColor));
        m_Shader->SetFloat3("u_SkyHorizonColor", glm::vec3(world.Skybox.HorizonColor));
        m_Shader->SetFloat3("u_SkyGroundColor", glm::vec3(world.Skybox.GroundColor));
        bool useIBL = false;
        if (world.Skybox.Enabled && world.Skybox.Type == 1 && !world.Skybox.HDRPath.empty() && m_Skybox) {
            if (m_Skybox->GetSpecification().HDRPath != world.Skybox.HDRPath) m_Skybox->LoadHDR(world.Skybox.HDRPath);
            if (m_Skybox->GetIrradianceMap() && m_Skybox->GetPrefilterMap() && m_Skybox->GetBRDFLUT()) {
                m_Skybox->GetIrradianceMap()->Bind(5); m_Skybox->GetPrefilterMap()->Bind(6); m_Skybox->GetBRDFLUT()->Bind(7); useIBL = true;
            }
        }
        m_Shader->SetInt("u_UseIBL", useIBL); m_Shader->SetFloat("u_EnvironmentIntensity", world.Skybox.Intensity);

        // Point lights
        int numPointLights = std::min(static_cast<int>(world.PointLights.size()), 4);
        m_Shader->SetInt("u_NumPointLights", numPointLights);
        for (int i = 0; i < numPointLights; ++i) {
            std::string prefix = "u_PointLights[" + std::to_string(i) + "].";
            m_Shader->SetFloat3(prefix + "Position", world.PointLights[i].Position);
            m_Shader->SetFloat3(prefix + "Color", world.PointLights[i].Color);
            m_Shader->SetFloat(prefix + "Intensity", world.PointLights[i].Intensity);
            m_Shader->SetFloat(prefix + "Radius", world.PointLights[i].Radius);
        }

        // Spot lights
        int numSpotLights = std::min(static_cast<int>(world.SpotLights.size()), 4);
        m_Shader->SetInt("u_NumSpotLights", numSpotLights);
        for (int i = 0; i < numSpotLights; ++i) {
            std::string prefix = "u_SpotLights[" + std::to_string(i) + "].";
            m_Shader->SetFloat3(prefix + "Position", world.SpotLights[i].Position);
            m_Shader->SetFloat3(prefix + "Direction", world.SpotLights[i].Direction);
            m_Shader->SetFloat3(prefix + "Color", world.SpotLights[i].Color);
            m_Shader->SetFloat(prefix + "Intensity", world.SpotLights[i].Intensity);
            m_Shader->SetFloat(prefix + "Range", world.SpotLights[i].Range);
            m_Shader->SetFloat(prefix + "InnerCutOff", std::cos(glm::radians(world.SpotLights[i].InnerAngle)));
            m_Shader->SetFloat(prefix + "OuterCutOff", std::cos(glm::radians(world.SpotLights[i].OuterAngle)));
        }

        if (settings.Wireframe) {
            RenderCommand::SetWireframeMode(true);
        }

        // Render extracted meshes
        for (const auto& meshInst : world.Meshes) {
            if (!meshInst.GLMesh) continue;

            glm::vec4 color1 = meshInst.Material.AlbedoColor;
            if (meshInst.Selected) {
                color1 = glm::mix(color1, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f), 0.35f);
            }

            m_Shader->SetMat4("u_Transform", meshInst.WorldTransform);
            if (meshInst.Material.MaterialAsset) {
                meshInst.Material.MaterialAsset->Bind(*m_Shader);
                m_Shader->SetInt("u_PatternMode", 3);
                m_Shader->SetFloat4("u_Color1", color1);
                m_Shader->SetFloat4("u_Color2", color1);
            } else {
                Material::BindDefault(*m_Shader, color1, meshInst.Material.SecondaryColor);
                m_Shader->SetFloat("u_TileScale", meshInst.Material.TileScale.x);
                m_Shader->SetInt("u_PatternMode", meshInst.Material.PatternMode);
            }

            meshInst.GLMesh->Bind();
            RenderCommand::DrawIndexed(meshInst.GLMesh);
            meshInst.GLMesh->Unbind();
        }

        if (settings.Wireframe) {
            RenderCommand::SetWireframeMode(false);
        }

        auto renderSkins = [&](bool shadow, const glm::mat4& lightMatrix) {
            if (!m_SkinningPipeline) return;
            auto shader = shadow ? m_SkinningPipeline->GetShadowShader() : m_SkinningPipeline->GetShader();
            if (!shader) return;
            shader->Bind(); shader->SetMat4("u_LightSpaceMatrix", lightMatrix);
            shader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix()); shader->SetMat4("u_View", camera.GetViewMatrix());
            shader->SetFloat3("u_LightDir", m_LightDir); shader->SetFloat3("u_CameraPos", camera.GetPosition());
            shader->SetInt("u_ShadowsEnabled", castShadows); shader->SetInt("u_ShadowMap", 1);
            if (!cascadeMatrices.empty()) shader->SetMat4Array("u_CascadeLightSpaceMatrices", cascadeMatrices.data(), static_cast<u32>(cascadeMatrices.size()));
            if (!splitDists.empty()) shader->SetFloatArray("u_CascadeSplitDistances", splitDists.data(), static_cast<u32>(splitDists.size()));
            for (const auto& skin : world.Skins) {
                if (!skin.GLMesh || (shadow && !skin.CastsShadows)) continue;
                shader->SetMat4("u_Transform", skin.WorldTransform); shader->SetFloat4("u_Color1", skin.Color);
                if (skin.BoneMatrices.empty()) m_SkinningPipeline->UploadIdentityPalette(*shader);
                else m_SkinningPipeline->UploadBonePalette(*shader, skin.BoneMatrices);
                RenderCommand::DrawIndexed(skin.GLMesh);
            }
        };
        renderSkins(false, m_LightSpaceMatrix);
        if (settings.EnableParticles && m_ParticleRenderer && !world.Particles.empty()) {
            m_ParticleRenderer->Begin(camera, targetFBO, camera.GetNearClip(), camera.GetFarClip());
            for (const auto& batch : world.Particles)
                m_ParticleRenderer->Render(batch->Pool, batch->Config, batch->Texture, batch->Normal, batch->Config.SoftnessDistance);
            m_ParticleRenderer->End();
        }

        // 4. Skybox pass
        if (settings.EnableSkybox && m_Skybox && world.Skybox.Enabled) {
            m_Skybox->SetMode(world.Skybox.Type == 1 ? SkyboxMode::HDRCubemap : SkyboxMode::ProceduralAtmospheric);
            m_Skybox->SetHDRIntensity(world.Skybox.Intensity); m_Skybox->SetExposure(world.Skybox.Exposure);
            m_Skybox->SetToneMapping(sun.ToneMappingMode, sun.Gamma);
            auto& spec = m_Skybox->GetSpecification();
            spec.ZenithColor = glm::vec3(world.Skybox.ZenithColor);
            spec.HorizonColor = glm::vec3(world.Skybox.HorizonColor);
            spec.GroundColor = glm::vec3(world.Skybox.GroundColor);
            spec.SunSize = world.Skybox.SunSize; spec.SunGlow = world.Skybox.SunGlow; spec.Turbidity = world.Skybox.Turbidity;
            m_Skybox->SetSunDirection(glm::vec3(world.Skybox.SunDirection));
            m_Skybox->Render(camera.GetViewMatrix(), camera.GetProjectionMatrix());
        }

        // 5. Physics debug wireframes
        PhysicsDebugRenderer* activeDebug = physicsDebugRenderer ? physicsDebugRenderer : m_InternalDebugRenderer.get();
        if (settings.ShowPhysicsDebug && activeDebug && physicsScene) {
            activeDebug->Begin(camera.GetPosition());
            physicsScene->DrawDebug(*activeDebug, settings.PhysicsDrawSettings);
            activeDebug->End(camera.GetViewProjectionMatrix());
        }

        // 6. Cleanup
        Texture2D::GetWhiteTexture()->Unbind(0);
        if (m_ShadowMap) {
            m_ShadowMap->UnbindTexture(1);
        }
        Texture2D::GetFlatNormalTexture()->Unbind(2);
        Texture2D::GetDefaultRoughnessTexture()->Unbind(3);
        Texture2D::GetBlackTexture()->Unbind(4);

        if (targetFBO) {
            targetFBO->Unbind();
        }
    }

}
