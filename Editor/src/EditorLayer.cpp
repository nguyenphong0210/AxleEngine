#include "EditorLayer.hpp"
#include <Axle/Application.hpp>
#include <Axle/Input.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/PlatformUtils.hpp>
#include <Axle/Project.hpp>
#include <Axle/Log.hpp>
#include <filesystem>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellapi.h>
#endif

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

EditorLayer::EditorLayer()
    : Layer("EditorLayer") {
}

void EditorLayer::OnAttach() {
    AXLE_INFO("EditorLayer attached - Initializing Axle Editor Environment");

    // Phase 13 Step 1 Verification
    const auto& scripts = Axle::ScriptRegistry::GetScripts();
    AXLE_INFO("--- [Phase 13 Step 1] ScriptRegistry Verification ---");
    AXLE_INFO("Registered scripts count: {0}", scripts.size());
    for (const auto& [name, meta] : scripts) {
        AXLE_INFO("  Script: '{0}' ({1} reflected fields):", name, meta.Fields.size());
        for (const auto& f : meta.Fields) {
            AXLE_INFO("    - Field '{0}' (Type: {1}, Min: {2}, Max: {3})", f.Name, (int)f.Type, f.Min, f.Max);
        }
    }
    AXLE_INFO("-----------------------------------------------------");

    auto& window = Axle::Application::Get().GetWindow();

    // 1. Perspective Camera
    m_CameraController = std::make_unique<Axle::PerspectiveCameraController>(
        45.0f,
        static_cast<Axle::f32>(window.GetWidth()) / static_cast<Axle::f32>(window.GetHeight()),
        0.1f,
        1000.0f
    );
    m_CameraController->SetPosition(glm::vec3(0.0f, 6.0f, 18.0f));
    m_CameraController->SetRotation(glm::vec3(-12.0f, 0.0f, 0.0f));

    // 2. Jolt Physics Scene
    m_PhysicsScene = std::make_unique<Axle::PhysicsScene>();

    // 3. Shaders loaded from Project Asset Directory (Sandbox/assets/shaders/)
    auto vertPath = Axle::FileSystem::ResolveAssetPath("shaders/Checkerboard.vert");
    auto fragPath = Axle::FileSystem::ResolveAssetPath("shaders/Checkerboard.frag");
    std::string vertexSrc = Axle::FileSystem::ReadTextFile(vertPath);
    std::string fragmentSrc = Axle::FileSystem::ReadTextFile(fragPath);

    if (vertexSrc.empty() || fragmentSrc.empty()) {
        AXLE_WARN("FileSystem: Shader files not found at '{0}'. Using fallback shaders.", vertPath.string());
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

            uniform sampler2D u_AlbedoMap;
            uniform sampler2D u_ShadowMap;
            uniform sampler2D u_NormalMap;
            uniform sampler2D u_RoughnessMap;
            uniform sampler2D u_MetallicMap;

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

            float CalculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
                vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
                projCoords = projCoords * 0.5 + 0.5;
                if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {
                    return 0.0;
                }
                float bias = max(0.0015 * (1.0 - dot(normal, lightDir)), 0.00015);
                float shadow = 0.0;
                vec2 texelSize = 1.0 / textureSize(u_ShadowMap, 0);
                for (int x = -1; x <= 1; ++x) {
                    for (int y = -1; y <= 1; ++y) {
                        float pcfDepth = texture(u_ShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
                        shadow += (projCoords.z - bias > pcfDepth) ? 1.0 : 0.0;
                    }
                }
                return shadow / 9.0;
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

            void main() {
                vec2 uvScale = (u_UVScale.x != 0.0 && u_UVScale.y != 0.0) ? u_UVScale : vec2(1.0);
                vec2 uv = v_TexCoord * uvScale;

                vec3 normal = vec3(0.0, 1.0, 0.0);
                if (u_PatternMode == 0) {
                    normal = vec3(0.0, 1.0, 0.0);
                } else if (u_PatternMode == 1) {
                    vec3 dx = dFdx(v_WorldPos);
                    vec3 dy = dFdy(v_WorldPos);
                    normal = normalize(cross(dx, dy));
                    if (dot(normal, normalize(-v_WorldPos)) < 0.0) normal = -normal;
                } else {
                    normal = normalize((u_Transform * vec4(v_LocalPos, 0.0)).xyz);
                }

                if (u_UseNormalMap == 1) {
                    vec3 viewDir = normalize(-v_WorldPos);
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
                    } else {
                        vec3 nLocal = normalize(v_LocalPos);
                        float uSph = atan(nLocal.z, nLocal.x) / (2.0 * 3.14159265359) + 0.5;
                        float vSph = asin(clamp(nLocal.y, -1.0, 1.0)) / 3.14159265359 + 0.5;
                        vec2 coord = floor(vec2(uSph * u_TileScale, vSph * (u_TileScale * 0.5)));
                        pattern = mod(coord.x + coord.y, 2.0);
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
                vec3 V = (length(u_CameraPos - v_WorldPos) > 1e-4) ? normalize(u_CameraPos - v_WorldPos) : normalize(-v_WorldPos);
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
                    shadow = CalculateShadow(v_FragPosLightSpace, N, L);
                }

                vec3 radiance = u_LightColor * u_LightIntensity;
                vec3 Lo = (kD * baseColor.rgb / PI + specular) * radiance * NdotL * (1.0 - shadow);
                vec3 ambient;
                if (u_UseHemisphereAmbient == 1) {
                    float upWeight = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
                    float zenithWeight = clamp(N.y, 0.0, 1.0);
                    vec3 skyUpper = mix(u_SkyHorizonColor, u_SkyZenithColor, zenithWeight);
                    vec3 hemisphereLight = mix(u_SkyGroundColor, skyUpper, upWeight);

                    vec3 diffuseAmbient = hemisphereLight * baseColor.rgb * kD;

                    vec3 R = reflect(-V, N);
                    float specUp = clamp(R.y * 0.5 + 0.5, 0.0, 1.0);
                    float specZenith = clamp(R.y, 0.0, 1.0);
                    vec3 specUpper = mix(u_SkyHorizonColor, u_SkyZenithColor, specZenith);
                    vec3 specSky = mix(u_SkyGroundColor, specUpper, specUp);

                    vec3 F_ambient = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
                    vec3 specularAmbient = specSky * F_ambient * (1.0 - roughness * 0.7);

                    ambient = (diffuseAmbient + specularAmbient) * u_AmbientIntensity;
                } else {
                    ambient = u_LightColor * u_AmbientIntensity * baseColor.rgb * (1.0 - metallic * 0.5);
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
        AXLE_INFO("Loaded shaders successfully from '{0}' and '{1}'", vertPath.string(), fragPath.string());
    }

    m_Shader = std::make_unique<Axle::Shader>(vertexSrc, fragmentSrc);
    m_Shader->Bind();
    m_Shader->SetInt("u_AlbedoMap", 0);
    m_Shader->SetInt("u_ShadowMap", 1);
    m_Shader->SetInt("u_NormalMap", 2);
    m_Shader->SetInt("u_RoughnessMap", 3);
    m_Shader->SetInt("u_MetallicMap", 4);
    m_Shader->SetFloat("u_NormalStrength", 1.0f);
    m_Shader->SetFloat3("u_EmissionColor", glm::vec3(0.0f));
    m_Shader->SetFloat("u_EmissionIntensity", 0.0f);
    m_Shader->SetFloat("u_Exposure", 1.0f);
    m_Shader->SetInt("u_ToneMappingMode", 0);
    m_Shader->SetFloat("u_Gamma", 2.2f);
    m_Shader->SetFloat3("u_CameraPos", m_CameraController->GetCamera().GetPosition());
    m_Shader->SetInt("u_UseHemisphereAmbient", 1);
    m_Shader->SetFloat3("u_SkyZenithColor", glm::vec3(0.18f, 0.42f, 0.78f));
    m_Shader->SetFloat3("u_SkyHorizonColor", glm::vec3(0.70f, 0.82f, 0.95f));
    m_Shader->SetFloat3("u_SkyGroundColor", glm::vec3(0.18f, 0.16f, 0.14f));

    // 3.1 Shadow Mapping System (Phase 11: Sub-step 1.1)
    m_ShadowMap = std::make_unique<Axle::ShadowMap>();

    std::string shadowVertSrc;
    std::string shadowFragSrc;
    auto shadowVertPath = Axle::FileSystem::ResolveAssetPath("shaders/ShadowDepth.vert");
    auto shadowFragPath = Axle::FileSystem::ResolveAssetPath("shaders/ShadowDepth.frag");
    if (std::filesystem::exists(shadowVertPath) && std::filesystem::exists(shadowFragPath)) {
        shadowVertSrc = Axle::FileSystem::ReadTextFile(shadowVertPath);
        shadowFragSrc = Axle::FileSystem::ReadTextFile(shadowFragPath);
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
    m_ShadowShader = std::make_unique<Axle::Shader>(shadowVertSrc, shadowFragSrc);
    AXLE_INFO("Shadow Mapping: Depth Framebuffer ({0}x{1}) & Light Space Matrix pipeline initialized",
        m_ShadowMap->GetWidth(), m_ShadowMap->GetHeight());

    // 4. Primitive Mesh Data (Cube, Sphere, Cylinder, Cone, Plane)
    m_CubeMesh = Axle::PrimitiveMesh::CreateCube();
    m_SphereMesh = Axle::PrimitiveMesh::CreateSphere(24, 32);
    m_CylinderMesh = Axle::PrimitiveMesh::CreateCylinder(32);
    m_ConeMesh = Axle::PrimitiveMesh::CreateCone(32);
    m_PlaneMesh = Axle::PrimitiveMesh::CreatePlane();

    // 4.1 Asset Pipeline Validation: Load SportsCar_Body.axlemesh
    auto carAxlePath = Axle::FileSystem::ResolveAssetPath("models/SportsCar_Body.axlemesh");
    if (std::filesystem::exists(carAxlePath)) {
        auto loaded = Axle::AssetSerializer::LoadMesh(carAxlePath);
        if (loaded.Success) {
            AXLE_INFO("Asset Pipeline: Loaded '{0}' [Cache: {1}] ({2} vertices, {3} indices)", 
                carAxlePath.filename().string(), 
                loaded.LoadedFromCache ? "HIT" : "MISS (Auto-baked cache)",
                loaded.SourceData.Vertices.size(), loaded.SourceData.Indices.size());
        }
    }

    // 4.2 Texture Mapping Pipeline Validation (Phase 11: Sub-step 2.1)
    auto testTexPath = Axle::FileSystem::ResolveAssetPath("textures/Prototype_Grid.png");
    if (std::filesystem::exists(testTexPath)) {
        auto testTex = Axle::Texture2D::Create(testTexPath.string());
        if (testTex && testTex->IsLoaded()) {
            AXLE_INFO("Texture Pipeline: Verified stb_image loader with '{0}' ({1}x{2}, ID: {3})",
                testTexPath.filename().string(), testTex->GetWidth(), testTex->GetHeight(), testTex->GetRendererID());
        }
    }

    // 4.3 Atmospheric Skybox Pipeline (Phase 11: Sub-step 4.1)
    m_Skybox = std::make_unique<Axle::Skybox>();

    // 5. Offscreen Framebuffer for Viewport
    Axle::FramebufferSpecification fbSpec;
    fbSpec.Width = window.GetWidth();
    fbSpec.Height = window.GetHeight();
    m_Framebuffer = Axle::Framebuffer::Create(fbSpec);
    m_ViewportWidth = fbSpec.Width;
    m_ViewportHeight = fbSpec.Height;

    // 6. Physics Debug Wireframe Renderer (JPH::DebugRenderer bridge)
    m_PhysicsDebugRenderer = std::make_unique<Axle::PhysicsDebugRenderer>();

    // 7. ECS Scene Initialization (Phase 18.2: Safe Scene State Simulation)
    m_EditorScene = std::make_shared<Axle::Scene>();
    m_Scene = m_EditorScene;

    // Connect Editor Panels
    m_SceneHierarchyPanel.SetContext(m_Scene.get(), m_PhysicsScene.get());
    m_SceneHierarchyPanel.SetMeshes(m_CubeMesh, m_SphereMesh, m_CylinderMesh, m_ConeMesh, m_PlaneMesh);
    m_SceneHierarchyPanel.SetAssetPreviewPanel(&m_AssetPreviewPanel);
    Axle::AssetManager::Init(Axle::FileSystem::GetAssetDirectory());
    m_ContentBrowserPanel.SetRootPath(Axle::FileSystem::GetAssetDirectory());
    m_AssetPreviewPanel.SetSceneContext(m_Scene.get(), m_PhysicsScene.get(), m_CubeMesh, &m_SerializerContext);
    m_ContentBrowserPanel.SetAssetPreviewPanel(&m_AssetPreviewPanel);

    // Setup Serializer Context for meshes & scripts
    m_SerializerContext.CubeMesh = m_CubeMesh;
    m_SerializerContext.SphereMesh = m_SphereMesh;
    m_SerializerContext.CylinderMesh = m_CylinderMesh;
    m_SerializerContext.ConeMesh = m_ConeMesh;
    m_SerializerContext.PlaneMesh = m_PlaneMesh;
    m_SerializerContext.ScriptBinder = [](Axle::Entity entity, const std::string& scriptName) {
        if (!entity.HasComponent<Axle::NativeScriptComponent>()) {
            entity.AddComponent<Axle::NativeScriptComponent>().Bind(scriptName);
        } else {
            entity.GetComponent<Axle::NativeScriptComponent>().Bind(scriptName);
        }
    };

    m_AssemblyEditorPanel.SetSceneContext(m_Scene.get(), m_PhysicsScene.get(), &m_SerializerContext);

    m_ContentBrowserPanel.SetCompileAssetsCallback([this]() {
        CompileAndHotReloadAssets(false);
    });
    m_ContentBrowserPanel.SetOpenSceneCallback([this](const std::filesystem::path& path) {
        OpenScene(path);
    });
    m_ContentBrowserPanel.SetOpenProjectCallback([this](const std::filesystem::path& path) {
        OpenProject(path);
    });
    m_ContentBrowserPanel.SetOpenAssemblyCallback([this](const std::filesystem::path& path) {
        m_AssemblyEditorPanel.SetAssembly(path);
        m_ShowAssemblyEditor = true;
    });
    m_ContentBrowserPanel.SetInstantiateAssemblyCallback([this](const std::filesystem::path& path) {
        auto ent = m_Scene->InstantiateAssembly(path, m_PhysicsScene.get(), glm::vec3(0.0f, 0.0f, 0.0f), &m_SerializerContext);
        if (ent) {
            m_SceneHierarchyPanel.SetSelectedEntity(ent);
        }
    });

    // Asset Selection from Content Browser -> Properties Panel
    m_ContentBrowserPanel.SetSelectAssetCallback([this](const std::filesystem::path& path) {
        m_SceneHierarchyPanel.SetSelectedAsset(path);
    });

    // Flow 3: Package Entity from Scene Hierarchy to Content Browser as Prefab Assembly (.axle)
    m_ContentBrowserPanel.SetPackageEntityCallback([this](uint32_t entityID, const std::filesystem::path& targetDir) {
        if (!m_Scene || !m_Scene->GetRegistry().valid((entt::entity)entityID)) return;
        Axle::Entity ent((entt::entity)entityID, m_Scene.get());
        std::string baseName = ent.GetName().empty() ? "Entity_Prefab" : ent.GetName();
        std::filesystem::path targetPath = targetDir / (baseName + ".axle");
        int counter = 1;
        while (std::filesystem::exists(targetPath)) {
            targetPath = targetDir / (baseName + "_" + std::to_string(counter++) + ".axle");
        }
        if (Axle::AssemblySerializer::Serialize(targetPath, ent, m_PhysicsScene.get(), &m_SerializerContext)) {
            Axle::AssetManager::GetOrCreateMetadata(targetPath);
            m_ContentBrowserPanel.SetStatusNotification("Packaged Prefab Assembly: " + targetPath.filename().string());
            AXLE_INFO("Packaged Entity '{0}' into Prefab Assembly '{1}'", ent.GetName(), targetPath.string());
        }
    });

    m_SceneHierarchyPanel.SetSpawnAssetCallback([this](const std::filesystem::path& path) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".axle") {
            auto ent = m_Scene->InstantiateAssembly(path, m_PhysicsScene.get(), glm::vec3(0.0f, 0.0f, 0.0f), &m_SerializerContext);
            if (ent) {
                m_SceneHierarchyPanel.SetSelectedEntity(ent);
            }
        } else if (ext == ".axlemesh" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
            std::filesystem::path axlePath = path;
            if (ext != ".axlemesh" && ext != ".axle") {
                axlePath = path.parent_path() / (path.stem().string() + ".axlemesh");
                if (!std::filesystem::exists(axlePath)) {
                    Axle::AssetSerializer::AutoConvertModel(path, axlePath, true);
                }
            }
            auto pkg = Axle::AssetSerializer::LoadPackage(axlePath);
            if (pkg.Success) {
                glm::vec3 spawnPos(0.0f, 0.0f, 0.0f);
                if (pkg.IsPackage) {
                    std::string rootTag = pkg.Name.empty() ? axlePath.stem().string() : pkg.Name;
                    auto rootEntity = m_Scene->CreateEntity(rootTag);
                    rootEntity.AddComponent<Axle::TransformComponent>(spawnPos, glm::vec3(1.0f));
                    auto& rootHierarchy = rootEntity.AddComponent<Axle::HierarchyComponent>();

                    for (const auto& part : pkg.Parts) {
                        auto childEntity = m_Scene->CreateEntity(part.Name);
                        auto& ct = childEntity.AddComponent<Axle::TransformComponent>(part.LocalTranslation, part.LocalScale);
                        ct.Rotation = part.LocalRotation;

                        auto partFullPath = axlePath.parent_path() / part.RelativePath;
                        childEntity.AddComponent<Axle::MeshInstance3D>(
                            part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_CubeMesh,
                            partFullPath.string(),
                            part.LoadedMesh.SourceData.PatternMode,
                            part.LoadedMesh.SourceData.TileScale,
                            part.LoadedMesh.SourceData.ColorPrimary,
                            part.LoadedMesh.SourceData.ColorSecondary
                        );

                        childEntity.AddComponent<Axle::HierarchyComponent>((entt::entity)rootEntity);
                        rootHierarchy.Children.push_back((entt::entity)childEntity);
                    }
                    m_SceneHierarchyPanel.SetSelectedEntity(rootEntity);
                } else if (!pkg.Parts.empty()) {
                    const auto& part = pkg.Parts[0];
                    std::string entityTag = pkg.Name.empty() ? axlePath.stem().string() : pkg.Name;
                    auto newEnt = m_Scene->CreateEntity(entityTag);
                    newEnt.AddComponent<Axle::TransformComponent>(spawnPos, glm::vec3(1.0f));
                    newEnt.AddComponent<Axle::MeshInstance3D>(
                        part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_CubeMesh,
                        axlePath.string(),
                        part.LoadedMesh.SourceData.PatternMode,
                        part.LoadedMesh.SourceData.TileScale,
                        part.LoadedMesh.SourceData.ColorPrimary,
                        part.LoadedMesh.SourceData.ColorSecondary
                    );
                    m_SceneHierarchyPanel.SetSelectedEntity(newEnt);
                }
            }
        }
    });

    m_SceneHierarchyPanel.SetOpenAssemblyEditorCallback([this](const std::filesystem::path& path) {
        m_AssemblyEditorPanel.SetAssembly(path);
        m_ShowAssemblyEditor = true;
    });

    // Initialize Project Configuration System (AxleEditor.axleproj)
    std::filesystem::path projFile;
    std::vector<std::filesystem::path> searchCandidates = {
        "AxleEditor.axleproj",
        "AxleSandbox.axleproj",
        "../../AxleEditor.axleproj",
        "../../../AxleEditor.axleproj",
        "../AxleEditor.axleproj"
    };

    auto assetDir = Axle::FileSystem::GetAssetDirectory();
    if (std::filesystem::exists(assetDir)) {
        searchCandidates.push_back(assetDir.parent_path() / "AxleEditor.axleproj");
        searchCandidates.push_back(assetDir.parent_path().parent_path() / "AxleEditor.axleproj");
    }

    for (const auto& candidate : searchCandidates) {
        if (std::filesystem::exists(candidate)) {
            projFile = std::filesystem::canonical(candidate);
            break;
        }
    }

    if (!projFile.empty() && std::filesystem::exists(projFile)) {
        OpenProject(projFile);
    } else {
        auto project = Axle::Project::New();
        project->GetConfig().Name = "Axle Project";
        if (std::filesystem::exists(assetDir)) {
            std::error_code ec;
            auto rel = std::filesystem::relative(assetDir, std::filesystem::current_path(), ec);
            if (!ec && !rel.empty()) {
                project->GetConfig().AssetDirectory = rel;
            } else {
                project->GetConfig().AssetDirectory = assetDir;
            }
        } else {
            project->GetConfig().AssetDirectory = "assets";
        }
        project->GetConfig().StartScene = "";
        Axle::Project::SetActive(project);
        std::filesystem::path defaultProjPath = "AxleEditor.axleproj";
        Axle::Project::SaveActive(defaultProjPath);
        m_CurrentProjectPath = std::filesystem::absolute(defaultProjPath);
        m_ContentBrowserPanel.SetRootPath(Axle::Project::GetAssetDirectory());
        NewScene();
    }

    UpdateWindowTitle();

    AXLE_INFO("Axle Editor initialized successfully");
}

void EditorLayer::OnDetach() {
    AXLE_INFO("EditorLayer detached");
    Axle::AssetManager::Shutdown();
    m_ShadowShader.reset();
    m_ShadowMap.reset();
    m_Vehicle.reset();
    m_Scene.reset();
    m_PhysicsDebugRenderer.reset();
    m_PhysicsScene.reset();
    m_Framebuffer.reset();
    m_CubeMesh.reset();
    m_SphereMesh.reset();
    m_CylinderMesh.reset();
    m_ConeMesh.reset();
    m_PlaneMesh.reset();
}

void EditorLayer::OnWindowResize(Axle::u32 width, Axle::u32 height) {
    if (m_CameraController) {
        m_CameraController->OnResize(width, height);
    }
}

void EditorLayer::OnFileDrop(const std::vector<std::string>& filepaths) {
    if (filepaths.empty()) return;

    // 1. Determine destination directory based on UI hover state
    std::filesystem::path destFolder;
    if (m_ContentBrowserPanel.IsWindowHovered()) {
        destFolder = m_ContentBrowserPanel.GetCurrentPath();
    } else {
        destFolder = Axle::FileSystem::ResolveAssetPath("models");
    }

    std::error_code ec;
    if (!std::filesystem::exists(destFolder, ec)) {
        std::filesystem::create_directories(destFolder, ec);
    }

    for (const auto& filepathStr : filepaths) {
        std::filesystem::path srcPath(filepathStr);
        if (!std::filesystem::exists(srcPath, ec)) continue;

        std::string ext = srcPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
        std::string stem = srcPath.stem().string();

        if (ext == ".axlescene" || ext == ".scene") {
            OpenScene(srcPath);
        } else if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
            // Queue 3D asset for conversion confirmation modal
            m_PendingImportQueue.push_back(srcPath);
        } else if (ext == ".axlemesh" || ext == ".axle") {
            std::filesystem::path destAxlePath = destFolder / srcPath.filename();
            if (srcPath != destAxlePath) {
                std::filesystem::copy_file(srcPath, destAxlePath, std::filesystem::copy_options::overwrite_existing, ec);
            }
            auto loaded = Axle::AssetSerializer::LoadMesh(destAxlePath);
            AXLE_INFO("Asset Pipeline: Ingested .axle asset '{0}' (Cache: {1})", 
                destAxlePath.filename().string(), loaded.LoadedFromCache ? "HIT" : "MISS");

            if (m_ViewportHovered && loaded.Success) {
                glm::mat4 invVP = glm::inverse(m_CameraController->GetCamera().GetViewProjectionMatrix());
                glm::vec4 nearPointNDC(0.0f, 0.0f, -1.0f, 1.0f);
                glm::vec4 farPointNDC(0.0f, 0.0f, 1.0f, 1.0f);

                glm::vec4 nearW = invVP * nearPointNDC;
                glm::vec4 farW = invVP * farPointNDC;
                if (std::abs(nearW.w) > 1e-6f) nearW /= nearW.w;
                if (std::abs(farW.w) > 1e-6f) farW /= farW.w;

                glm::vec3 rayOrigin = glm::vec3(nearW);
                glm::vec3 rayDir = glm::normalize(glm::vec3(farW - nearW));

                glm::vec3 spawnPos(0.0f, 0.0f, 0.0f);
                Axle::RayCastHit hit;
                if (m_PhysicsScene && m_PhysicsScene->CastRay(rayOrigin, rayDir, 500.0f, hit)) {
                    spawnPos = hit.Position;
                } else {
                    if (std::abs(rayDir.y) > 1e-4f) {
                        float t = -rayOrigin.y / rayDir.y;
                        if (t > 0.0f && t < 150.0f) {
                            glm::vec3 groundHit = rayOrigin + rayDir * t;
                            spawnPos = groundHit;
                        } else {
                            spawnPos = rayOrigin + rayDir * 10.0f;
                        }
                    } else {
                        spawnPos = rayOrigin + rayDir * 10.0f;
                    }
                }

                auto entity = m_Scene->CreateEntity(stem);
                entity.AddComponent<Axle::TransformComponent>(spawnPos, glm::vec3(1.0f));
                entity.AddComponent<Axle::MeshInstance3D>(
                    loaded.VAO, destAxlePath.string(),
                    loaded.SourceData.PatternMode, loaded.SourceData.TileScale,
                    loaded.SourceData.ColorPrimary, loaded.SourceData.ColorSecondary
                );

                m_SceneHierarchyPanel.SetSelectedEntity(entity);
            }
        } else {
            // General asset file copy (textures, shaders, scenes, documents)
            std::filesystem::path destPath = destFolder / srcPath.filename();
            if (srcPath != destPath) {
                std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
                AXLE_INFO("Asset Pipeline: Ingested file '{0}' into '{1}'", 
                    srcPath.filename().string(), destFolder.string());
            }
        }
    }

    // Trigger conversion confirmation modal if 3D assets were queued
    if (!m_PendingImportQueue.empty() && !m_ShowImportModal) {
        SetupNextImportModal();
    }
}

void EditorLayer::OnScenePlay() {
    if (m_SceneState == SceneState::Play) return;

    CompileAndHotReloadAssets(false);

    // Deep clone EditorScene into an ephemeral RuntimeScene (Phase 18.2: Safe Scene State)
    m_RuntimeScene = Axle::Scene::Copy(m_EditorScene);
    m_Scene = m_RuntimeScene;

    // Update panel contexts to point to the active runtime scene
    m_SceneHierarchyPanel.SetContext(m_Scene.get(), m_PhysicsScene.get());
    m_SceneHierarchyPanel.SetSelectedEntity({});

    // Discover vehicle entities dynamically from the active runtime scene
    DiscoverVehicleEntities();

    // If an entity with VehicleComponent exists, ensure Jolt Vehicle physics is instantiated and bound
    if (m_ChassisEntity && !m_Vehicle) {
        SetupVehiclePhysics(m_ChassisEntity);
    }

    m_SceneState = SceneState::Play;
    m_Scene->OnRuntimeStart();
    m_ChaseCamera = true;
    AXLE_INFO("Simulation started (Play Mode). Ephemeral Runtime Scene cloned from pristine Editor Scene.");
}

void EditorLayer::CompileAndHotReloadAssets(bool forceRebakeAll) {
    auto assetDir = Axle::FileSystem::GetAssetDirectory();
    auto result = Axle::AssetSerializer::CompileAssetDirectory(assetDir, forceRebakeAll);

    // Hot-reload all active entities in the Scene with MeshInstance3D
    size_t entitiesUpdated = 0;
    auto view = m_Scene->GetRegistry().view<Axle::MeshInstance3D>();
    for (auto entity : view) {
        auto& mc = view.get<Axle::MeshInstance3D>(entity);
        if (!mc.AssetPath.empty() && std::filesystem::exists(mc.AssetPath)) {
            auto loaded = Axle::AssetSerializer::LoadMesh(mc.AssetPath);
            if (loaded.Success && loaded.VAO) {
                mc.VAO = loaded.VAO;
                mc.PatternMode = loaded.SourceData.PatternMode;
                mc.TileScale = loaded.SourceData.TileScale;
                mc.Color1 = loaded.SourceData.ColorPrimary;
                mc.Color2 = loaded.SourceData.ColorSecondary;
                entitiesUpdated++;
            }
        }
    }

    // Refresh Content Browser Thumbnails and Asset Preview Panel
    m_ContentBrowserPanel.RefreshThumbnails();
    if (!m_AssetPreviewPanel.GetCurrentAsset().empty()) {
        m_AssetPreviewPanel.SetAsset(m_AssetPreviewPanel.GetCurrentAsset());
    }

    m_CompileNotificationTimer = 3.5f;
    m_CompileNotificationMessage = "Compiled " + std::to_string(result.RecompiledCount) + " modified (" +
                                   std::to_string(result.UpToDateCount) + " up-to-date, " +
                                   std::to_string(entitiesUpdated) + " scene meshes reloaded)";
    m_ContentBrowserPanel.SetStatusNotification(m_CompileNotificationMessage);

    AXLE_INFO("Asset Compiler: Finished! Recompiled {0} assets ({1} up-to-date). Hot-reloaded {2} active scene entities.",
        result.RecompiledCount, result.UpToDateCount, entitiesUpdated);
}

void EditorLayer::TriggerScriptCompilationAndReload() {
    if (Axle::ScriptCompiler::Get().IsCompiling()) {
        return;
    }

    m_ScriptCompileStatusText = "Compiling...";
    m_ScriptCompileStatusColor = ImVec4(0.95f, 0.75f, 0.15f, 1.0f);
    AXLE_INFO("Editor: Starting background script compilation for 'AxleScripts'...");

    Axle::ScriptCompiler::Get().CompileAsync([this](const Axle::CompilationResult& result) {
        if (result.Success) {
            auto reloadResult = Axle::ScriptHotReloadManager::Get().Reload(m_Scene, m_SceneState == SceneState::Play);
            if (reloadResult.Success) {
                char buf[64];
                snprintf(buf, sizeof(buf), "[OK] Reloaded in %.2fs", result.DurationSeconds);
                m_ScriptCompileStatusText = buf;
                m_ScriptCompileStatusColor = ImVec4(0.20f, 0.75f, 0.35f, 1.0f);
                m_CompileNotificationTimer = 4.0f;
                m_CompileNotificationMessage = m_ScriptCompileStatusText;
                AXLE_INFO("Editor: C++ Scripts recompiled and hot-reloaded successfully in {0:.2f}s ({1} script(s) active).",
                    result.DurationSeconds, reloadResult.ScriptCount);
            } else {
                m_ScriptCompileStatusText = "[!] Reload Failed";
                m_ScriptCompileStatusColor = ImVec4(0.95f, 0.35f, 0.20f, 1.0f);
                AXLE_ERROR("Editor: Script reload failed: {0}", reloadResult.Message);
            }
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "[X] Failed (%d errors)", result.ErrorCount);
            m_ScriptCompileStatusText = buf;
            m_ScriptCompileStatusColor = ImVec4(0.95f, 0.20f, 0.20f, 1.0f);
            AXLE_ERROR("Editor: Script compilation failed with {0} errors, {1} warnings.",
                result.ErrorCount, result.WarningCount);
        }
        m_ScriptCompileStatusTimer = 5.0f;
    });
}

void EditorLayer::OpenInVisualStudio() {
    std::filesystem::path slnPath = std::filesystem::current_path() / "build" / "AxleEngine.sln";
    if (!std::filesystem::exists(slnPath)) {
        slnPath = std::filesystem::current_path() / "AxleEngine.sln";
    }
#ifdef _WIN32
    if (std::filesystem::exists(slnPath)) {
        ShellExecuteA(NULL, "open", slnPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
        AXLE_INFO("Editor: Opened Visual Studio solution '{0}'", slnPath.string());
    } else {
        ShellExecuteA(NULL, "open", std::filesystem::current_path().string().c_str(), NULL, NULL, SW_SHOWNORMAL);
        AXLE_INFO("Editor: Opened project folder '{0}'", std::filesystem::current_path().string());
    }
#endif
}

void EditorLayer::OpenScriptsDirectory() {
    std::filesystem::path scriptsDir = std::filesystem::current_path() / "Editor" / "assets" / "scripts";
    if (!std::filesystem::exists(scriptsDir)) {
        scriptsDir = std::filesystem::current_path() / "Editor" / "src" / "Scripts";
    }
    if (!std::filesystem::exists(scriptsDir)) {
        std::filesystem::create_directories(scriptsDir);
    }
#ifdef _WIN32
    ShellExecuteA(NULL, "open", scriptsDir.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
    AXLE_INFO("Editor: Opened scripts directory '{0}'", scriptsDir.string());
#endif
}

void EditorLayer::NewScene() {
    if (m_SceneState != SceneState::Edit) {
        OnSceneStop();
    }

    // Release Vehicle before PhysicsScene is destroyed
    m_Vehicle.reset();

    // Clear cached entity handles
    m_ChassisEntity = {};
    m_WheelEntities.clear();
    m_InitialDynamicStates.clear();

    m_PhysicsScene = std::make_unique<Axle::PhysicsScene>();
    m_PhysicsDebugRenderer = std::make_unique<Axle::PhysicsDebugRenderer>();
    m_Scene->Clear();
    m_CurrentScenePath.clear();

    // Create default Directional Sun Light (Unity / Godot style default scene)
    auto sunEntity = m_Scene->CreateEntity("Directional Light");
    auto& tc = sunEntity.AddComponent<Axle::TransformComponent>();
    tc.Translation = glm::vec3(0.0f, 10.0f, 0.0f);
    tc.Rotation = glm::quat(glm::vec3(glm::radians(50.0f), glm::radians(-30.0f), 0.0f));
    sunEntity.AddComponent<Axle::DirectionalLightComponent>();

    auto skyEntity = m_Scene->CreateEntity("Skybox Environment");
    skyEntity.AddComponent<Axle::SkyboxComponent>();

    m_SceneHierarchyPanel.SetContext(m_Scene.get(), m_PhysicsScene.get());
    m_AssetPreviewPanel.SetSceneContext(m_Scene.get(), m_PhysicsScene.get(), m_CubeMesh, &m_SerializerContext);
    m_AssemblyEditorPanel.SetSceneContext(m_Scene.get(), m_PhysicsScene.get(), &m_SerializerContext);
    m_CompileNotificationMessage = "Created new empty scene";
    m_CompileNotificationTimer = 3.5f;
    UpdateWindowTitle();
    AXLE_INFO("Editor: Created new empty scene");
}

void EditorLayer::DiscoverVehicleEntities() {
    m_ChassisEntity = {};
    m_WheelEntities.clear();

    if (!m_Scene) return;

    // 1. Discover active vehicle chassis dynamically by VehicleComponent
    auto vehicleView = m_Scene->GetRegistry().view<Axle::VehicleComponent>();
    for (auto entityID : vehicleView) {
        m_ChassisEntity = Axle::Entity{ entityID, m_Scene.get() };
        break; // Track primary vehicle chassis
    }

    if (!m_ChassisEntity) return;

    // 2. Discover associated wheel entities
    // Strategy A (Godot/Unity-style Component-per-Wheel): Search direct child entities with WheelComponent
    for (auto child : m_ChassisEntity.GetChildren()) {
        if (child && child.HasComponent<Axle::WheelComponent>()) {
            m_WheelEntities.push_back(child);
        }
    }

    // Strategy B: If no child wheels found via Hierarchy, search all entities in scene with WheelComponent
    if (m_WheelEntities.empty()) {
        auto wheelView = m_Scene->GetRegistry().view<Axle::WheelComponent>();
        for (auto entityID : wheelView) {
            m_WheelEntities.push_back(Axle::Entity{ entityID, m_Scene.get() });
        }
    }

    // Strategy C (Legacy backward compatibility with playground tags "Wheel FL", "Wheel FR", etc.)
    if (m_WheelEntities.empty()) {
        m_WheelEntities.resize(4);
        auto tagView = m_Scene->GetRegistry().view<Axle::TagComponent>();
        for (auto entityID : tagView) {
            const auto& tag = tagView.get<Axle::TagComponent>(entityID).Tag;
            Axle::Entity e{ entityID, m_Scene.get() };
            if (tag.find("FL") != std::string::npos || tag.find("FrontLeft") != std::string::npos) {
                m_WheelEntities[0] = e;
            } else if (tag.find("FR") != std::string::npos || tag.find("FrontRight") != std::string::npos) {
                m_WheelEntities[1] = e;
            } else if (tag.find("RL") != std::string::npos || tag.find("RearLeft") != std::string::npos) {
                m_WheelEntities[2] = e;
            } else if (tag.find("RR") != std::string::npos || tag.find("RearRight") != std::string::npos) {
                m_WheelEntities[3] = e;
            }
        }
        m_WheelEntities.erase(
            std::remove_if(m_WheelEntities.begin(), m_WheelEntities.end(), [](const Axle::Entity& e) { return !e; }),
            m_WheelEntities.end()
        );
    }
}

void EditorLayer::SetupVehiclePhysics(Axle::Entity chassisEntity) {
    if (!chassisEntity || !chassisEntity.HasComponent<Axle::TransformComponent>() || !m_PhysicsScene) {
        return;
    }

    m_ChassisEntity = chassisEntity;
    auto& chassisTC = m_ChassisEntity.GetComponent<Axle::TransformComponent>();

    Axle::VehicleConfig config;
    config.InitialPosition = chassisTC.Translation;
    config.InitialRotation = chassisTC.Rotation;

    if (m_ChassisEntity.HasComponent<Axle::CollisionShape3D>()) {
        const auto& cs = m_ChassisEntity.GetComponent<Axle::CollisionShape3D>();
        if (cs.ShapeType == Axle::CollisionShapeType::Box) {
            config.ChassisHalfExtent = cs.HalfExtent;
        } else {
            config.ChassisHalfExtent = glm::max(chassisTC.Scale * 0.5f, glm::vec3(0.1f));
        }
    } else {
        config.ChassisHalfExtent = glm::max(chassisTC.Scale * 0.5f, glm::vec3(0.1f));
    }

    if (m_ChassisEntity.HasComponent<Axle::RigidBody3D>()) {
        const auto& rb = m_ChassisEntity.GetComponent<Axle::RigidBody3D>();
        if (rb.Mass > 0.0f) {
            config.ChassisMass = rb.Mass;
        }
    }

    // Build CustomWheels if any wheel entities have WheelComponent or custom geometry
    if (!m_WheelEntities.empty()) {
        bool hasWheelComponent = false;
        for (const auto& wEnt : m_WheelEntities) {
            if (wEnt && wEnt.HasComponent<Axle::WheelComponent>()) {
                hasWheelComponent = true;
                break;
            }
        }

        if (hasWheelComponent) {
            config.CustomWheels.reserve(m_WheelEntities.size());
            for (const auto& wEnt : m_WheelEntities) {
                if (!wEnt) continue;

                Axle::WheelConfig wc;
                if (wEnt.HasComponent<Axle::WheelComponent>()) {
                    const auto& comp = wEnt.GetComponent<Axle::WheelComponent>();
                    wc.Radius = comp.Radius;
                    wc.Width = comp.Width;
                    wc.SuspensionMinLength = comp.SuspensionMinLength;
                    wc.SuspensionMaxLength = comp.SuspensionMaxLength;
                    wc.SpringFrequency = comp.SpringFrequency;
                    wc.SpringDamping = comp.SpringDamping;
                    wc.CanSteer = comp.CanSteer;
                    wc.MaxSteerAngleDegrees = comp.MaxSteerAngle;
                    wc.IsDriven = comp.IsDriven;
                    wc.MaxBrakeTorque = comp.BrakeTorque;
                    wc.MaxHandBrakeTorque = comp.HasHandbrake ? comp.HandBrakeTorque : 0.0f;
                }

                if (wEnt.HasComponent<Axle::TransformComponent>()) {
                    const auto& wtc = wEnt.GetComponent<Axle::TransformComponent>();
                    if (wEnt.GetParent() == m_ChassisEntity) {
                        wc.Position = wtc.Translation;
                    } else {
                        glm::vec3 worldPos = glm::vec3(wEnt.GetWorldTransform()[3]);
                        wc.Position = glm::inverse(chassisTC.Rotation) * (worldPos - chassisTC.Translation);
                    }
                }
                config.CustomWheels.push_back(wc);
            }
        }
    }

    m_Vehicle = std::make_unique<Axle::Vehicle>(*m_PhysicsScene, config);
    m_Vehicle->Reset(chassisTC.Translation, chassisTC.Rotation);

    if (m_ChassisEntity.HasComponent<Axle::VehicleComponent>()) {
        m_ChassisEntity.GetComponent<Axle::VehicleComponent>().VehicleInstance = m_Vehicle.get();
    } else {
        m_ChassisEntity.AddComponent<Axle::VehicleComponent>(m_Vehicle.get());
    }

    if (m_ChassisEntity.HasComponent<Axle::RigidBody3D>()) {
        m_ChassisEntity.GetComponent<Axle::RigidBody3D>().RuntimeBodyID = m_Vehicle->GetChassisBodyID();
    } else {
        m_ChassisEntity.AddComponent<Axle::RigidBody3D>(m_Vehicle->GetChassisBodyID(), true);
    }

    if (!m_ChassisEntity.HasComponent<Axle::CollisionShape3D>()) {
        m_ChassisEntity.AddComponent<Axle::CollisionShape3D>(Axle::CollisionShapeType::Box, config.ChassisHalfExtent, 0.5f, 0.0f);
    }
}

void EditorLayer::OpenScene(const std::filesystem::path& filepath) {
    std::filesystem::path targetPath = filepath;
    if (targetPath.empty()) {
        auto scenesDir = Axle::FileSystem::ResolveAssetPath("scenes");
        if (!std::filesystem::exists(scenesDir)) {
            std::filesystem::create_directories(scenesDir);
        }
        std::string defaultPathStr = scenesDir.string();

        std::string selected = Axle::FileDialogs::OpenFile("Axle Scene (*.axlescene)\0*.axlescene\0All Files (*.*)\0*.*\0", defaultPathStr.c_str());
        if (selected.empty()) {
            return;
        }
        targetPath = selected;
    }

    if (!std::filesystem::exists(targetPath)) {
        AXLE_WARN("Editor: Scene file does not exist: '{0}'", targetPath.string());
        return;
    }

    if (m_SceneState != SceneState::Edit) {
        OnSceneStop();
    }

    // 1. MUST destroy m_Vehicle BEFORE destroying m_PhysicsScene!
    m_Vehicle.reset();

    // 2. Clear cached entity handles
    m_ChassisEntity = {};
    m_WheelEntities.clear();
    m_InitialDynamicStates.clear();

    // 3. Create fresh PhysicsScene and PhysicsDebugRenderer
    m_PhysicsScene = std::make_unique<Axle::PhysicsScene>();
    m_PhysicsDebugRenderer = std::make_unique<Axle::PhysicsDebugRenderer>();

    Axle::SceneSerializer serializer(m_Scene.get(), m_PhysicsScene.get());
    Axle::SceneSerializerContext ctx;
    ctx.CubeMesh = m_CubeMesh;
    ctx.SphereMesh = m_SphereMesh;
    ctx.CylinderMesh = m_CylinderMesh;
    ctx.ConeMesh = m_ConeMesh;
    ctx.PlaneMesh = m_PlaneMesh;
    ctx.ScriptBinder = [](Axle::Entity entity, const std::string& scriptName) {
        if (!entity.HasComponent<Axle::NativeScriptComponent>()) {
            entity.AddComponent<Axle::NativeScriptComponent>().Bind(scriptName);
        } else {
            entity.GetComponent<Axle::NativeScriptComponent>().Bind(scriptName);
        }
    };
    serializer.SetContext(ctx);

    if (serializer.Deserialize(targetPath)) {
        m_CurrentScenePath = targetPath;

        // Discover vehicle entities dynamically by component
        DiscoverVehicleEntities();

        // Ensure loaded scene has a Directional Sun Light and Skybox
        Axle::DirectionalLightComponent sceneSunLight;
        if (!m_Scene->GetPrimaryDirectionalLight(sceneSunLight)) {
            auto sunEntity = m_Scene->CreateEntity("Sun Light");
            sunEntity.AddComponent<Axle::TransformComponent>(glm::vec3(0.0f, 25.0f, 0.0f));
            sunEntity.AddComponent<Axle::DirectionalLightComponent>();
            sunEntity.AddComponent<Axle::SkyboxComponent>();
        } else {
            Axle::SkyboxComponent sceneSkybox;
            if (!m_Scene->GetPrimarySkybox(sceneSkybox)) {
                auto sunView = m_Scene->GetRegistry().view<Axle::DirectionalLightComponent>();
                for (auto e : sunView) {
                    Axle::Entity sunEnt{ e, m_Scene.get() };
                    sunEnt.AddComponent<Axle::SkyboxComponent>();
                    break;
                }
            }
        }

        // Cache initial dynamic states for ResetSimulation()
        auto rbView = m_Scene->GetRegistry().view<Axle::RigidBody3D, Axle::TransformComponent>();
        for (auto entityID : rbView) {
            const auto& rb = rbView.get<Axle::RigidBody3D>(entityID);
            const auto& tc = rbView.get<Axle::TransformComponent>(entityID);
            if (rb.IsDynamic && !rb.RuntimeBodyID.IsInvalid()) {
                m_InitialDynamicStates[rb.RuntimeBodyID.GetIndexAndSequenceNumber()] = { tc.Translation, tc.Rotation };
            }
        }

        // If the loaded scene contains a VehicleComponent, instantiate Vehicle physics and bind components
        if (m_ChassisEntity) {
            SetupVehiclePhysics(m_ChassisEntity);
        }

        m_SceneHierarchyPanel.SetContext(m_Scene.get(), m_PhysicsScene.get());
        m_AssetPreviewPanel.SetSceneContext(m_Scene.get(), m_PhysicsScene.get(), m_CubeMesh, &m_SerializerContext);
        m_AssemblyEditorPanel.SetSceneContext(m_Scene.get(), m_PhysicsScene.get(), &m_SerializerContext);

        m_CompileNotificationMessage = "Loaded scene: " + targetPath.filename().string();
        m_CompileNotificationTimer = 3.5f;
        UpdateWindowTitle();
        AXLE_INFO("Editor: Successfully loaded scene from '{0}'", targetPath.string());
    }
}

void EditorLayer::SaveScene(const std::filesystem::path& filepath) {
    if (filepath.empty() && m_CurrentScenePath.empty()) {
        SaveSceneAs();
        return;
    }

    auto targetPath = filepath.empty() ? m_CurrentScenePath : filepath;

    // Always serialize pristine EditorScene even if simulation is active (Phase 18.2)
    Axle::SceneSerializer serializer(m_EditorScene ? m_EditorScene.get() : m_Scene.get(), m_PhysicsScene.get());
    Axle::SceneSerializerContext ctx;
    ctx.CubeMesh = m_CubeMesh;
    ctx.SphereMesh = m_SphereMesh;
    ctx.CylinderMesh = m_CylinderMesh;
    ctx.ConeMesh = m_ConeMesh;
    ctx.PlaneMesh = m_PlaneMesh;
    ctx.ScriptBinder = [](Axle::Entity entity, const std::string& scriptName) {
        if (!entity.HasComponent<Axle::NativeScriptComponent>()) {
            entity.AddComponent<Axle::NativeScriptComponent>().Bind(scriptName);
        } else {
            entity.GetComponent<Axle::NativeScriptComponent>().Bind(scriptName);
        }
    };
    serializer.SetContext(ctx);

    if (serializer.Serialize(targetPath)) {
        m_CurrentScenePath = targetPath;
        m_CompileNotificationMessage = "Saved scene: " + targetPath.filename().string();
        m_CompileNotificationTimer = 3.5f;
        UpdateWindowTitle();
        AXLE_INFO("Editor: Successfully saved scene to '{0}'", targetPath.string());
    }
}

void EditorLayer::SaveSceneAs() {
    auto scenesDir = Axle::FileSystem::ResolveAssetPath("scenes");
    if (!std::filesystem::exists(scenesDir)) {
        std::filesystem::create_directories(scenesDir);
    }
    std::string defaultPathStr = scenesDir.string();

    std::string filepath = Axle::FileDialogs::SaveFile("Axle Scene (*.axlescene)\0*.axlescene\0All Files (*.*)\0*.*\0", defaultPathStr.c_str(), "axlescene");
    if (!filepath.empty()) {
        std::filesystem::path target(filepath);
        if (target.extension() != ".axlescene") {
            target += ".axlescene";
        }
        SaveScene(target);
    }
}

void EditorLayer::NewProject() {
    auto defaultDir = std::filesystem::current_path();
    std::string filepath = Axle::FileDialogs::SaveFile("Axle Project (*.axleproj)\0*.axleproj\0All Files (*.*)\0*.*\0", defaultDir.string().c_str(), "axleproj");
    if (filepath.empty()) return;

    std::filesystem::path target(filepath);
    if (target.extension() != ".axleproj") {
        target += ".axleproj";
    }

    auto projectDir = target.parent_path();
    auto assetDir = projectDir / "assets";
    std::filesystem::create_directories(assetDir / "scenes");
    std::filesystem::create_directories(assetDir / "models");
    std::filesystem::create_directories(assetDir / "textures");
    std::filesystem::create_directories(assetDir / "shaders");

    auto project = Axle::Project::New();
    project->GetConfig().Name = target.stem().string();
    project->GetConfig().AssetDirectory = "assets";
    project->GetConfig().StartScene = "scenes/Untitled.axlescene";

    Axle::Project::SetActive(project);
    Axle::Project::SaveActive(target);
    m_CurrentProjectPath = target;

    // Create new scene in project's scenes directory
    NewScene();
    SaveScene(assetDir / "scenes/Untitled.axlescene");

    m_ContentBrowserPanel.SetRootPath(Axle::Project::GetAssetDirectory());
    m_CompileNotificationMessage = "Created project: " + project->GetConfig().Name;
    m_CompileNotificationTimer = 4.0f;
    UpdateWindowTitle();
    AXLE_INFO("Editor: Created and activated new project '{0}' at '{1}'", project->GetConfig().Name, target.string());
}

void EditorLayer::OpenProject(const std::filesystem::path& filepath) {
    std::filesystem::path target = filepath;
    if (target.empty()) {
        auto currentDir = m_CurrentProjectPath.empty() ? std::filesystem::current_path() : m_CurrentProjectPath.parent_path();
        std::string selected = Axle::FileDialogs::OpenFile("Axle Project (*.axleproj)\0*.axleproj\0All Files (*.*)\0*.*\0", currentDir.string().c_str());
        if (selected.empty()) return;
        target = selected;
    }

    if (!std::filesystem::exists(target)) {
        AXLE_WARN("Editor: Project file does not exist: '{0}'", target.string());
        return;
    }

    auto project = Axle::Project::Load(target);
    if (project) {
        m_CurrentProjectPath = target;
        m_ContentBrowserPanel.SetRootPath(Axle::Project::GetAssetDirectory());

        // Load project's StartScene if specified
        const auto& startSceneRel = project->GetConfig().StartScene;
        if (!startSceneRel.empty()) {
            auto startScenePath = Axle::Project::GetAssetDirectory() / startSceneRel;
            if (std::filesystem::exists(startScenePath)) {
                OpenScene(startScenePath);
            } else {
                NewScene();
            }
        } else {
            NewScene();
        }

        m_CompileNotificationMessage = "Loaded project: " + project->GetConfig().Name;
        m_CompileNotificationTimer = 4.0f;
        UpdateWindowTitle();
        AXLE_INFO("Editor: Successfully opened project '{0}'", target.string());
    } else {
        AXLE_ERROR("Editor: Failed to load project from '{0}'", target.string());
    }
}

void EditorLayer::SaveProject() {
    if (m_CurrentProjectPath.empty()) {
        SaveProjectAs();
        return;
    }
    if (Axle::Project::SaveActive(m_CurrentProjectPath)) {
        m_CompileNotificationMessage = "Saved project configuration";
        m_CompileNotificationTimer = 3.5f;
        AXLE_INFO("Editor: Saved project to '{0}'", m_CurrentProjectPath.string());
    }
}

void EditorLayer::SaveProjectAs() {
    auto currentDir = m_CurrentProjectPath.empty() ? std::filesystem::current_path() : m_CurrentProjectPath.parent_path();
    std::string filepath = Axle::FileDialogs::SaveFile("Axle Project (*.axleproj)\0*.axleproj\0All Files (*.*)\0*.*\0", currentDir.string().c_str(), "axleproj");
    if (!filepath.empty()) {
        std::filesystem::path target(filepath);
        if (target.extension() != ".axleproj") {
            target += ".axleproj";
        }
        m_CurrentProjectPath = target;
        if (Axle::Project::GetActive()) {
            Axle::Project::GetActive()->GetConfig().Name = target.stem().string();
        }
        SaveProject();
        UpdateWindowTitle();
    }
}

void EditorLayer::UpdateWindowTitle() {
    std::string projName = "No Project";
    if (auto active = Axle::Project::GetActive()) {
        projName = active->GetConfig().Name;
    }
    std::string sceneName = m_CurrentScenePath.empty() ? "Untitled" : m_CurrentScenePath.filename().string();
    Axle::Application::Get().GetWindow().SetTitle("Axle Engine - " + projName + " - [" + sceneName + "]");
}

void EditorLayer::OnScenePause() {
    m_SceneState = SceneState::Pause;
    AXLE_INFO("Simulation paused. Viewport Free-Fly inspection enabled.");
}

void EditorLayer::OnSceneStop() {
    if (m_SceneState == SceneState::Edit) return;

    m_SceneState = SceneState::Edit;

    if (m_RuntimeScene) {
        m_RuntimeScene->OnRuntimeStop();
        m_RuntimeScene.reset();
    }

    // Restore pristine EditorScene (Phase 18.2: Zero Physics Drift)
    m_Scene = m_EditorScene;

    // Zero-drift physics restoration to pristine editor transforms
    if (m_Scene && m_PhysicsScene) {
        m_Scene->RestorePhysicsBodies(m_PhysicsScene.get());
    }

    ResetSimulation();

    // Rediscover vehicle entities in the restored EditorScene
    DiscoverVehicleEntities();
    if (m_ChassisEntity && !m_Vehicle) {
        SetupVehiclePhysics(m_ChassisEntity);
    }

    // Restore panel contexts to pristine EditorScene
    m_SceneHierarchyPanel.SetContext(m_Scene.get(), m_PhysicsScene.get());
    m_SceneHierarchyPanel.SetSelectedEntity({});

    m_ChaseCamera = false;
    AXLE_INFO("Simulation stopped (Edit Mode). Restored pristine Editor Scene with zero physics drift.");
}

void EditorLayer::ResetSimulation() {
    AXLE_INFO("Resetting physics simulation and vehicle state");

    // Reset all registered dynamic bodies (pyramid crates, cones, tires, demo shapes) to initial poses
    for (const auto& [bodyIDRaw, state] : m_InitialDynamicStates) {
        Axle::BodyID bodyID(bodyIDRaw);
        if (!bodyID.IsInvalid() && m_PhysicsScene && m_PhysicsScene->IsBodyValid(bodyID)) {
            m_PhysicsScene->SetBodyPositionAndRotation(bodyID, state.Position, state.Rotation, true);
            m_PhysicsScene->SetLinearVelocity(bodyID, glm::vec3(0.0f));
            m_PhysicsScene->GetBodyInterface().SetAngularVelocity(bodyID, JPH::Vec3::sZero());
        }
    }

    if (m_Vehicle && m_ChassisEntity && m_ChassisEntity.HasComponent<Axle::TransformComponent>()) {
        m_Vehicle->Reset(glm::vec3(0.0f, 0.95f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        m_Vehicle->SetDriverInput(0.0f, 0.0f, 0.0f, 0.0f);
        auto& chassisTC = m_ChassisEntity.GetComponent<Axle::TransformComponent>();
        chassisTC.Translation = m_Vehicle->GetPosition();
        chassisTC.Rotation = m_PhysicsScene->GetBodyRotation(m_Vehicle->GetChassisBodyID());

        for (size_t i = 0; i < m_WheelEntities.size(); ++i) {
            if (m_WheelEntities[i] && m_WheelEntities[i].HasComponent<Axle::TransformComponent>()) {
                const glm::mat4 wheelMat = m_Vehicle->GetWheelTransform(static_cast<Axle::u32>(i));
                auto& tc = m_WheelEntities[i].GetComponent<Axle::TransformComponent>();
                if (m_WheelEntities[i].HasComponent<Axle::HierarchyComponent>() &&
                    m_WheelEntities[i].GetComponent<Axle::HierarchyComponent>().InheritTransform &&
                    m_WheelEntities[i].GetParent() == m_ChassisEntity) {
                    glm::mat4 chassisWorld = m_Vehicle->GetChassisTransform();
                    glm::mat4 localWheelMat = glm::inverse(chassisWorld) * wheelMat;
                    tc.Translation = glm::vec3(localWheelMat[3]);
                    tc.Rotation = glm::quat_cast(localWheelMat);
                } else {
                    tc.Translation = glm::vec3(wheelMat[3]);
                    tc.Rotation = glm::quat_cast(wheelMat);
                }
            }
        }
    }
    m_OrbitYaw = 180.0f;
    m_OrbitPitch = 15.0f;
}

void EditorLayer::OnUpdate(Axle::Timestep ts) {
    m_LastDeltaTime = ts.GetSeconds();

    // Hotkey Controls (debounced)
    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F5)) {
        if (!m_F5KeyWasPressed) {
            if (m_SceneState != SceneState::Play) {
                OnScenePlay();
            }
            m_F5KeyWasPressed = true;
        }
    } else {
        m_F5KeyWasPressed = false;
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F6)) {
        if (!m_F6KeyWasPressed) {
            if (m_SceneState == SceneState::Play) {
                OnScenePause();
            } else if (m_SceneState == SceneState::Pause) {
                OnScenePlay();
            }
            m_F6KeyWasPressed = true;
        }
    } else {
        m_F6KeyWasPressed = false;
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F7) || Axle::Input::IsKeyPressed(Axle::KeyCode::Escape)) {
        if (!m_F7KeyWasPressed && !m_EscKeyWasPressed) {
            if (m_SceneState != SceneState::Edit) {
                OnSceneStop();
            }
            m_F7KeyWasPressed = true;
            m_EscKeyWasPressed = true;
        }
    } else {
        m_F7KeyWasPressed = false;
        m_EscKeyWasPressed = false;
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::O)) {
        if (!m_OKeyWasPressed) {
            if (m_SceneState == SceneState::Pause) {
                m_StepSingleFrame = true;
            }
            m_OKeyWasPressed = true;
        }
    } else {
        m_OKeyWasPressed = false;
    }

    if (m_CompileNotificationTimer > 0.0f) {
        m_CompileNotificationTimer -= ts.GetSeconds();
    }
    if (m_ScriptCompileStatusTimer > 0.0f) {
        m_ScriptCompileStatusTimer -= ts.GetSeconds();
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F9)) {
        if (!m_F9KeyWasPressed) {
            TriggerScriptCompilationAndReload();
            m_F9KeyWasPressed = true;
        }
    } else {
        m_F9KeyWasPressed = false;
    }

    const bool isHoldingRMB = Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight);
    const bool isCtrlDown = Axle::Input::IsKeyPressed(Axle::KeyCode::LeftControl) || Axle::Input::IsKeyPressed(Axle::KeyCode::RightControl);
    const bool isShiftDown = Axle::Input::IsKeyPressed(Axle::KeyCode::LeftShift) || Axle::Input::IsKeyPressed(Axle::KeyCode::RightShift);

    // File Shortcut Hotkeys (Ctrl+S, Ctrl+Shift+S, Ctrl+O, Ctrl+N, Ctrl+B)
    if (isCtrlDown && !ImGui::GetIO().WantTextInput) {
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::B)) {
            if (!m_BKeyWasPressed) {
                TriggerScriptCompilationAndReload();
                m_BKeyWasPressed = true;
            }
        } else {
            m_BKeyWasPressed = false;
        }

        if (Axle::Input::IsKeyPressed(Axle::KeyCode::S)) {
            if (!m_SKeyWasPressed) {
                if (isShiftDown) {
                    SaveSceneAs();
                } else {
                    SaveScene();
                }
                m_SKeyWasPressed = true;
            }
        } else {
            m_SKeyWasPressed = false;
        }

        if (Axle::Input::IsKeyPressed(Axle::KeyCode::N)) {
            if (!m_NKeyWasPressed) {
                NewScene();
                m_NKeyWasPressed = true;
            }
        } else {
            m_NKeyWasPressed = false;
        }

        if (Axle::Input::IsKeyPressed(Axle::KeyCode::O)) {
            if (!m_OKeyWasPressed) {
                if (isShiftDown) {
                    OpenProject();
                } else {
                    OpenScene();
                }
                m_OKeyWasPressed = true;
            }
        } else {
            m_OKeyWasPressed = false;
        }
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::R)) {
        if (!m_RKeyWasPressed) {
            if (isCtrlDown || m_SceneState == SceneState::Play) {
                ResetSimulation();
            } else if (!isHoldingRMB && !ImGui::GetIO().WantTextInput) {
                m_GizmoType = ImGuizmo::OPERATION::SCALE;
            }
            m_RKeyWasPressed = true;
        }
    } else {
        m_RKeyWasPressed = false;
    }

    // Gizmo Hotkeys (Q: Select, W: Move, E: Rotate) when not holding RMB, not typing in UI, and not playing
    if (!isHoldingRMB && !ImGui::GetIO().WantTextInput && m_SceneState != SceneState::Play) {
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Q)) {
            m_GizmoType = -1;
        } else if (Axle::Input::IsKeyPressed(Axle::KeyCode::W)) {
            m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
        } else if (Axle::Input::IsKeyPressed(Axle::KeyCode::E)) {
            m_GizmoType = ImGuizmo::OPERATION::ROTATE;
        }
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::C)) {
        if (!m_CKeyWasPressed) {
            m_ChaseCamera = !m_ChaseCamera;
            m_CKeyWasPressed = true;
        }
    } else {
        m_CKeyWasPressed = false;
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F1)) {
        if (!m_F1KeyWasPressed) {
            m_ShowPhysicsDebug = !m_ShowPhysicsDebug;
            m_F1KeyWasPressed = true;
        }
    } else {
        m_F1KeyWasPressed = false;
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F11)) {
        if (!m_F11KeyWasPressed) {
            auto& window = Axle::Application::Get().GetWindow();
            if (window.IsMaximized()) {
                window.Restore();
            } else {
                window.Maximize();
            }
            m_F11KeyWasPressed = true;
        }
    } else {
        m_F1KeyWasPressed = false;
    }

    // C++ Native Scripting Runtime Execution (Phase 8: CarPlayerScript, Camera, Hazard scripts)
    if (m_SceneState == SceneState::Play) {
        m_Scene->OnUpdateRuntime(ts);
    }

    // Camera Update
    if (m_SceneState == SceneState::Play && m_ChaseCamera && m_Vehicle) {
        // Vehicle Chase Camera
        const auto [mouseX, mouseY] = Axle::Input::GetMousePosition();
        const glm::vec2 currentMousePos{ mouseX, mouseY };

        if (m_FirstMouseUpdate) {
            m_LastMousePos = currentMousePos;
            m_FirstMouseUpdate = false;
        }

        const glm::vec2 mouseDelta = currentMousePos - m_LastMousePos;
        m_LastMousePos = currentMousePos;

        const bool isDragging = (m_ViewportHovered || m_ViewportFocused) 
                             && (Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight)
                              || Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonLeft));

        if (isDragging) {
            m_OrbitYaw -= mouseDelta.x * 0.25f;
            m_OrbitPitch -= mouseDelta.y * 0.25f;
            m_OrbitPitch = std::clamp(m_OrbitPitch, -45.0f, 75.0f);
        }

        const glm::vec3 carPos = m_Vehicle->GetPosition();
        const glm::vec3 carFwd = m_Vehicle->GetForwardVector();

        if (!isDragging) {
            const Axle::f32 carSpeed = glm::length(m_PhysicsScene->GetLinearVelocity(m_Vehicle->GetChassisBodyID()));
            if (carSpeed > 0.5f) {
                const Axle::f32 targetYaw = glm::degrees(std::atan2(-carFwd.x, -carFwd.z));
                Axle::f32 diff = targetYaw - m_OrbitYaw;
                while (diff < -180.0f) diff += 360.0f;
                while (diff > 180.0f) diff -= 360.0f;
                m_OrbitYaw += diff * std::min(1.0f, ts.GetSeconds() * 3.5f);
            }
        }

        const Axle::f32 radYaw = glm::radians(m_OrbitYaw);
        const Axle::f32 radPitch = glm::radians(m_OrbitPitch);
        constexpr Axle::f32 dist = 7.5f;

        glm::vec3 offset;
        offset.x = -std::sin(radYaw) * std::cos(radPitch) * dist;
        offset.y = std::sin(radPitch) * dist + 1.2f;
        offset.z = -std::cos(radYaw) * std::cos(radPitch) * dist;

        const glm::vec3 lookTarget = carPos + glm::vec3(0.0f, 0.8f, 0.0f);
        const glm::vec3 camPos = lookTarget - offset;
        const glm::vec3 lookDir = glm::normalize(lookTarget - camPos);

        const Axle::f32 pitch = glm::degrees(std::asin(std::clamp(lookDir.y, -0.999f, 0.999f)));
        const Axle::f32 yaw = glm::degrees(std::atan2(-lookDir.x, -lookDir.z));

        m_CameraController->SetPosition(camPos);
        m_CameraController->SetRotation(glm::vec3(pitch, yaw, 0.0f));
    } else {
        // Editor Free-Fly Camera (Active in Edit mode, Pause mode, or Free-Fly mode)
        if ((m_ViewportHovered || m_ViewportFocused) && !ImGui::GetIO().WantTextInput) {
            const bool isManipulatingGizmo = (m_SceneState == SceneState::Edit) && ImGuizmo::IsUsing();
            if (!isManipulatingGizmo) {
                m_CameraController->OnUpdate(ts);
            }
        }
    }

    // Physics Simulation Stepping
    if (m_SceneState == SceneState::Play && m_PhysicsScene) {
        Axle::Timestep simTs = ts.GetSeconds() * m_SimulationSpeed;
        m_PhysicsScene->Step(simTs);
    } else if (m_SceneState == SceneState::Pause && m_StepSingleFrame && m_PhysicsScene) {
        m_PhysicsScene->Step(1.0f / 60.0f);
        m_StepSingleFrame = false;
    }

    // Synchronize vehicle chassis and 4 wheel entities with physics simulation
    if (m_Vehicle && m_ChassisEntity && m_ChassisEntity.HasComponent<Axle::TransformComponent>()) {
        if (m_SceneState == SceneState::Edit) {
            auto& chassisTC = m_ChassisEntity.GetComponent<Axle::TransformComponent>();
            m_Vehicle->Reset(chassisTC.Translation, chassisTC.Rotation);
        } else {
            auto& chassisTC = m_ChassisEntity.GetComponent<Axle::TransformComponent>();
            chassisTC.Translation = m_Vehicle->GetPosition();
            chassisTC.Rotation = m_PhysicsScene->GetBodyRotation(m_Vehicle->GetChassisBodyID());
        }

        for (size_t i = 0; i < m_WheelEntities.size(); ++i) {
            if (m_WheelEntities[i] && m_WheelEntities[i].HasComponent<Axle::TransformComponent>()) {
                const glm::mat4 wheelMat = m_Vehicle->GetWheelTransform(static_cast<Axle::u32>(i));
                auto& tc = m_WheelEntities[i].GetComponent<Axle::TransformComponent>();
                if (m_WheelEntities[i].HasComponent<Axle::HierarchyComponent>() &&
                    m_WheelEntities[i].GetComponent<Axle::HierarchyComponent>().InheritTransform &&
                    m_WheelEntities[i].GetParent() == m_ChassisEntity) {
                    glm::mat4 chassisWorld = m_Vehicle->GetChassisTransform();
                    glm::mat4 localWheelMat = glm::inverse(chassisWorld) * wheelMat;
                    tc.Translation = glm::vec3(localWheelMat[3]);
                    tc.Rotation = glm::quat_cast(localWheelMat);
                } else {
                    tc.Translation = glm::vec3(wheelMat[3]);
                    tc.Rotation = glm::quat_cast(wheelMat);
                }
            }
        }
    }

    // Query active Directional Sun Light if present in scene
    Axle::DirectionalLightComponent activeSunLight;
    bool hasSunLight = m_Scene && m_Scene->GetPrimaryDirectionalLight(activeSunLight);
    if (hasSunLight) {
        m_LightDir = activeSunLight.GetDirection();
    }
    bool castShadows = hasSunLight && activeSunLight.CastShadows;

    // ==========================================
    // PASS 1: SHADOW MAP DEPTH PASS (Phase 11: Sub-step 1.1)
    // ==========================================
    if (castShadows && m_ShadowMap && m_ShadowShader && m_Scene) {
        glm::vec3 focusCenter = (m_Vehicle && m_ChassisEntity) ? m_Vehicle->GetPosition() : glm::vec3(0.0f);
        m_LightSpaceMatrix = m_ShadowMap->CalculateLightSpaceMatrix(m_LightDir, focusCenter);

        m_ShadowMap->Bind();
        m_Scene->RenderShadowPass(*m_ShadowShader, m_LightSpaceMatrix);
        m_ShadowMap->Unbind();
    }

    // ==========================================
    // PASS 2: RENDER PASS TO OFFSCREEN FRAMEBUFFER
    // ==========================================
    if (m_Framebuffer) {
        m_Framebuffer->Bind();
        Axle::RenderCommand::SetViewport(0, 0, m_ViewportWidth, m_ViewportHeight);
        Axle::RenderCommand::SetClearColor({ 0.10f, 0.12f, 0.15f, 1.0f });
        Axle::RenderCommand::Clear();

        // Bind texture slots (0: Albedo, 1: ShadowMap, 2: Normal, 3: Roughness, 4: Metallic)
        Axle::Texture2D::GetWhiteTexture()->Bind(0);
        if (m_ShadowMap) {
            m_ShadowMap->BindTexture(1);
        }
        Axle::Texture2D::GetFlatNormalTexture()->Bind(2);
        Axle::Texture2D::GetDefaultRoughnessTexture()->Bind(3);
        Axle::Texture2D::GetBlackTexture()->Bind(4);

        // 1. Render all ECS entities
        m_Scene->OnUpdate(ts, m_PhysicsScene.get(), m_CameraController->GetCamera(), *m_Shader,
                          m_LightSpaceMatrix, m_LightDir, 1);

        // 2. Render Atmospheric Skybox (Phase 11: Sub-step 4.1 & 4.2)
        if (m_Skybox) {
            Axle::SkyboxComponent activeSkybox;
            bool hasSkybox = m_Scene && m_Scene->GetPrimarySkybox(activeSkybox);

            if (hasSkybox && activeSkybox.Enabled) {
                auto& spec = m_Skybox->GetSpecification();
                spec.ZenithColor = activeSkybox.ZenithColor;
                spec.HorizonColor = activeSkybox.HorizonColor;
                spec.GroundColor = activeSkybox.GroundColor;
                spec.SunSize = activeSkybox.SunSize;
                spec.SunGlow = activeSkybox.SunGlow;
                spec.Turbidity = activeSkybox.Turbidity;

                if (!activeSkybox.SyncWithSun) {
                    float radAz = glm::radians(activeSkybox.CustomAzimuth);
                    float radEl = glm::radians(std::clamp(activeSkybox.CustomElevation, 1.0f, 89.9f));
                    glm::vec3 customSunDir = glm::normalize(glm::vec3(
                        std::cos(radEl) * std::sin(radAz),
                        std::sin(radEl),
                        std::cos(radEl) * std::cos(radAz)
                    ));
                    m_Skybox->SetSunDirection(customSunDir);
                }

                if (hasSunLight && activeSkybox.SyncWithSun) {
                    m_Skybox->SetSunDirection(activeSunLight.GetDirection());
                    m_Skybox->SetSunColor(activeSunLight.Color * activeSunLight.Intensity);
                } else if (!hasSunLight) {
                    m_Skybox->SetSunColor(glm::vec3(0.0f));
                }

                if (hasSunLight) {
                    m_Skybox->SetExposure(activeSunLight.Exposure);
                    m_Skybox->SetToneMapping(activeSunLight.ToneMappingMode, activeSunLight.Gamma);
                }

                m_Skybox->Render(m_CameraController->GetCamera().GetViewMatrix(),
                                 m_CameraController->GetCamera().GetProjectionMatrix());
            }
        }

        // 3. Render JPH::DebugRenderer physics collider wireframes when enabled
        if (m_ShowPhysicsDebug && m_PhysicsDebugRenderer) {
            m_PhysicsDebugRenderer->Begin(m_CameraController->GetCamera().GetPosition());
            m_PhysicsScene->DrawDebug(*m_PhysicsDebugRenderer, m_PhysicsDrawSettings);
            m_PhysicsDebugRenderer->End(m_CameraController->GetCamera().GetViewProjectionMatrix());
        }

        Axle::Texture2D::GetWhiteTexture()->Unbind(0);
        if (m_ShadowMap) {
            m_ShadowMap->UnbindTexture(1);
        }
        Axle::Texture2D::GetFlatNormalTexture()->Unbind(2);
        Axle::Texture2D::GetDefaultRoughnessTexture()->Unbind(3);
        Axle::Texture2D::GetBlackTexture()->Unbind(4);

        m_Framebuffer->Unbind();
    }
}

void EditorLayer::RenderToolbar() {
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const float toolbarHeight = 36.0f;

    ImGui::SetNextWindowPos(mainViewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, toolbarHeight));
    ImGui::SetNextWindowViewport(mainViewport->ID);

    ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_NoDocking 
                                  | ImGuiWindowFlags_NoTitleBar 
                                  | ImGuiWindowFlags_NoResize 
                                  | ImGuiWindowFlags_NoMove 
                                  | ImGuiWindowFlags_NoScrollbar 
                                  | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));

    if (ImGui::Begin("##FixedTopToolbar", nullptr, toolbarFlags)) {
        auto DrawToolButton = [](const char* label, bool active, ImVec2 size) -> bool {
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.55f, 0.95f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.35f, 0.75f, 1.0f));
            }
            bool clicked = ImGui::Button(label, size);
            if (active) {
                ImGui::PopStyleColor(3);
            }
            return clicked;
        };

        // --- 1. QUICK SCENE SAVE (Unreal Engine Style) ---
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.44f, 0.74f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.54f, 0.88f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.36f, 0.62f, 1.0f));
        if (ImGui::Button("Save", ImVec2(56.0f, 26.0f))) {
            SaveScene();
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Save active scene to file (Ctrl+S)");
        }

        ImGui::SameLine();

        // --- 2. UNREAL-STYLE SCRIPT COMPILE & HOT-RELOAD BUTTON ---
        auto& scriptCompiler = Axle::ScriptCompiler::Get();
        const bool isCompilingScripts = scriptCompiler.IsCompiling();

        if (isCompilingScripts) {
            float time = (float)ImGui::GetTime();
            float pulse = 0.70f + 0.30f * sinf(time * 6.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f * pulse, 0.60f * pulse, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.70f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.50f, 0.05f, 1.0f));

            int dotCount = ((int)(time * 3.0f)) % 4;
            std::string dots(dotCount, '.');
            std::string compText = "Compiling" + dots;
            ImGui::Button(compText.c_str(), ImVec2(105.0f, 26.0f));
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Building AxleScripts.dll asynchronously in background...");
            }
        } else if (m_ScriptCompileStatusTimer > 0.0f) {
            ImGui::PushStyleColor(ImGuiCol_Button, m_ScriptCompileStatusColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_ScriptCompileStatusColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_ScriptCompileStatusColor);
            if (ImGui::Button(m_ScriptCompileStatusText.c_str(), ImVec2(125.0f, 26.0f))) {
                TriggerScriptCompilationAndReload();
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) {
                auto lastRes = scriptCompiler.GetLastResult();
                if (!lastRes.Success && !lastRes.Diagnostics.empty()) {
                    std::string errSummary = "Compilation Diagnostics:\n";
                    for (const auto& diag : lastRes.Diagnostics) {
                        errSummary += " - " + diag.File + "(" + std::to_string(diag.Line) + "): " + diag.Message + "\n";
                    }
                    ImGui::SetTooltip("%s", errSummary.c_str());
                } else {
                    ImGui::SetTooltip("Click to Recompile C++ Scripts (F9 / Ctrl+B)");
                }
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.23f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.33f, 0.40f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.19f, 0.23f, 1.0f));
            if (ImGui::Button("Compile (F9)", ImVec2(105.0f, 26.0f))) {
                TriggerScriptCompilationAndReload();
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Compile C++ Scripts & Hot-Reload DLL (F9 / Ctrl+B)");
            }
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.38f, 0.44f, 1.0f), "|");
        ImGui::SameLine();

        // --- 3. GIZMO TRANSFORMATION TOOLS ---
        if (DrawToolButton("Select (Q)", m_GizmoType == -1, ImVec2(76.0f, 26.0f))) {
            m_GizmoType = -1;
        }
        ImGui::SameLine();
        if (DrawToolButton("Move (W)", m_GizmoType == ImGuizmo::OPERATION::TRANSLATE, ImVec2(70.0f, 26.0f))) {
            m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
        }
        ImGui::SameLine();
        if (DrawToolButton("Rotate (E)", m_GizmoType == ImGuizmo::OPERATION::ROTATE, ImVec2(76.0f, 26.0f))) {
            m_GizmoType = ImGuizmo::OPERATION::ROTATE;
        }
        ImGui::SameLine();
        if (DrawToolButton("Scale (R)", m_GizmoType == ImGuizmo::OPERATION::SCALE, ImVec2(70.0f, 26.0f))) {
            m_GizmoType = ImGuizmo::OPERATION::SCALE;
        }
        ImGui::SameLine();
        const char* modeLabel = m_GizmoModeLocal ? "Coord: Local" : "Coord: World";
        if (ImGui::Button(modeLabel, ImVec2(96.0f, 26.0f))) {
            m_GizmoModeLocal = !m_GizmoModeLocal;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &m_SnapEnabled);
        if (m_SnapEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(65.0f);
            if (m_GizmoType == ImGuizmo::OPERATION::ROTATE) {
                ImGui::DragFloat("##SnapRot", &m_SnapRotation, 1.0f, 1.0f, 90.0f, "%.0f deg");
            } else if (m_GizmoType == ImGuizmo::OPERATION::SCALE) {
                ImGui::DragFloat("##SnapScale", &m_SnapScale, 0.05f, 0.05f, 5.0f, "%.2f");
            } else {
                ImGui::DragFloat("##SnapTrans", &m_SnapTranslation, 0.05f, 0.05f, 5.0f, "%.2f m");
            }
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "|");
        ImGui::SameLine();

        // --- 1. PLAY BUTTON ---
        if (m_SceneState == SceneState::Play) {
            ImGui::BeginDisabled(true);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.50f, 0.25f, 0.5f));
            ImGui::Button("Playing (F5)", ImVec2(100.0f, 26.0f));
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.72f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.85f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.60f, 0.22f, 1.0f));
            if (ImGui::Button("Play (F5)", ImVec2(100.0f, 26.0f))) {
                OnScenePlay();
            }
            ImGui::PopStyleColor(3);
        }

        // --- 2. PAUSE BUTTON ---
        ImGui::SameLine();
        if (m_SceneState == SceneState::Play) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.88f, 0.48f, 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.58f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.40f, 0.10f, 1.0f));
            if (ImGui::Button("Pause (F6)", ImVec2(100.0f, 26.0f))) {
                OnScenePause();
            }
            ImGui::PopStyleColor(3);
        } else if (m_SceneState == SceneState::Pause) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.72f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.85f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.60f, 0.22f, 1.0f));
            if (ImGui::Button("Resume (F6)", ImVec2(100.0f, 26.0f))) {
                OnScenePlay();
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::BeginDisabled(true);
            ImGui::Button("Pause", ImVec2(100.0f, 26.0f));
            ImGui::EndDisabled();
        }

        // --- 3. STOP BUTTON ---
        ImGui::SameLine();
        if (m_SceneState != SceneState::Edit) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.25f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.14f, 0.16f, 1.0f));
            if (ImGui::Button("Stop (F7)", ImVec2(90.0f, 26.0f))) {
                OnSceneStop();
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::BeginDisabled(true);
            ImGui::Button("Stop", ImVec2(90.0f, 26.0f));
            ImGui::EndDisabled();
        }

        // --- 4. STEP 1 FRAME BUTTON ---
        ImGui::SameLine();
        if (m_SceneState == SceneState::Pause) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.88f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.62f, 0.98f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.42f, 0.75f, 1.0f));
            if (ImGui::Button("Step (O)", ImVec2(80.0f, 26.0f))) {
                m_StepSingleFrame = true;
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::BeginDisabled(true);
            ImGui::Button("Step", ImVec2(80.0f, 26.0f));
            ImGui::EndDisabled();
        }

        // --- 5. RESET SCENE BUTTON ---
        ImGui::SameLine();
        if (ImGui::Button("Reset (Ctrl+R)", ImVec2(98.0f, 26.0f))) {
            ResetSimulation();
        }

        // --- 6. SIMULATION SPEED SLIDER ---
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::SliderFloat("Speed", &m_SimulationSpeed, 0.1f, 2.0f, "%.1fx");

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "|");

        // --- 7. CAMERA MODE TOGGLE ---
        ImGui::SameLine();
        const char* camBtnText = m_ChaseCamera ? "Cam: Chase [C]" : "Cam: Free-Fly [C]";
        if (ImGui::Button(camBtnText, ImVec2(125.0f, 26.0f))) {
            m_ChaseCamera = !m_ChaseCamera;
        }

        // --- 8. WIREFRAME TOGGLE ---
        ImGui::SameLine();
        ImGui::Checkbox("Wireframe [F1]", &m_ShowPhysicsDebug);

        // --- 9. STATUS / NAVIGATION HINT ---
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "|");

        ImGui::SameLine();
        if (m_CompileNotificationTimer > 0.0f) {
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "[OK] %s", m_CompileNotificationMessage.c_str());
        } else if (m_SceneState == SceneState::Edit) {
            ImGui::TextColored(ImVec4(0.35f, 0.70f, 1.0f, 1.0f), "EDIT MODE: Q:Select W:Move E:Rotate R:Scale | Click to select | RMB+WASD Fly");
        } else if (m_SceneState == SceneState::Play) {
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "PLAYING: Drive with WASD/Arrows (Space: Handbrake)");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "PAUSED: Physics frozen. Hold RMB+WASD or manipulate entities");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void EditorLayer::OnImGuiRender() {
    ImGuizmo::BeginFrame();

    // 1. Global Main Menu Bar (At the absolute top of the application window)
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            // Project Management
            if (ImGui::MenuItem("New Project...")) {
                NewProject();
            }
            if (ImGui::MenuItem("Open Project...", "Ctrl+Shift+O")) {
                OpenProject();
            }
            if (ImGui::MenuItem("Save Project")) {
                SaveProject();
            }
            if (ImGui::MenuItem("Save Project As...")) {
                SaveProjectAs();
            }
            ImGui::Separator();

            // Scene Operations
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                NewScene();
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                OpenScene();
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                SaveScene();
            }
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
                SaveSceneAs();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Simulation", "Ctrl+R")) {
                ResetSimulation();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                Axle::Application::Get().Close();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Asset")) {
            if (ImGui::MenuItem("Compile Modified Assets", "Ctrl+Shift+B")) {
                CompileAndHotReloadAssets(false);
            }
            if (ImGui::MenuItem("Rebuild All Binary Caches", "Shift+F9")) {
                CompileAndHotReloadAssets(true);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Build")) {
            bool canCompile = !Axle::ScriptCompiler::Get().IsCompiling();
            if (ImGui::MenuItem("Compile Scripts", "F9 / Ctrl+B", false, canCompile)) {
                TriggerScriptCompilationAndReload();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open in Visual Studio")) {
                OpenInVisualStudio();
            }
            if (ImGui::MenuItem("Open Scripts Directory")) {
                OpenScriptsDirectory();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Entity")) {
            if (ImGui::MenuItem("Create Empty Entity")) {
                auto entity = m_Scene->CreateEntity("Empty Entity");
                entity.AddComponent<Axle::TransformComponent>();
                m_SceneHierarchyPanel.SetSelectedEntity(entity);
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("3D Object")) {
                if (ImGui::MenuItem("Cube")) {
                    m_SceneHierarchyPanel.CreatePrimitive(0);
                }
                if (ImGui::MenuItem("Sphere")) {
                    m_SceneHierarchyPanel.CreatePrimitive(1);
                }
                if (ImGui::MenuItem("Cylinder")) {
                    m_SceneHierarchyPanel.CreatePrimitive(2);
                }
                if (ImGui::MenuItem("Cone")) {
                    m_SceneHierarchyPanel.CreatePrimitive(3);
                }
                if (ImGui::MenuItem("Plane")) {
                    m_SceneHierarchyPanel.CreatePrimitive(4);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Physics Templates")) {
                if (ImGui::MenuItem("Dynamic Box (Falling)")) {
                    m_SceneHierarchyPanel.CreatePrimitive(0, glm::vec3(0.0f, 2.0f, 0.0f), true);
                }
                if (ImGui::MenuItem("Dynamic Sphere (Falling)")) {
                    m_SceneHierarchyPanel.CreatePrimitive(1, glm::vec3(0.0f, 2.0f, 0.0f), true);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Simulation")) {
            if (ImGui::MenuItem("Play", "F5", m_SceneState == SceneState::Play, m_SceneState != SceneState::Play)) {
                OnScenePlay();
            }
            if (ImGui::MenuItem("Pause", "F6", m_SceneState == SceneState::Pause, m_SceneState == SceneState::Play)) {
                OnScenePause();
            }
            if (ImGui::MenuItem("Stop", "F7", m_SceneState == SceneState::Edit, m_SceneState != SceneState::Edit)) {
                OnSceneStop();
            }
            if (ImGui::MenuItem("Step 1 Frame", "O", false, m_SceneState == SceneState::Pause)) {
                m_StepSingleFrame = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Scene", "Ctrl+R")) {
                ResetSimulation();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Gizmo")) {
            if (ImGui::MenuItem("Select Tool", "Q", m_GizmoType == -1)) {
                m_GizmoType = -1;
            }
            if (ImGui::MenuItem("Translate", "W", m_GizmoType == ImGuizmo::OPERATION::TRANSLATE)) {
                m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
            }
            if (ImGui::MenuItem("Rotate", "E", m_GizmoType == ImGuizmo::OPERATION::ROTATE)) {
                m_GizmoType = ImGuizmo::OPERATION::ROTATE;
            }
            if (ImGui::MenuItem("Scale", "R", m_GizmoType == ImGuizmo::OPERATION::SCALE)) {
                m_GizmoType = ImGuizmo::OPERATION::SCALE;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Local Coordinates", nullptr, m_GizmoModeLocal)) {
                m_GizmoModeLocal = !m_GizmoModeLocal;
            }
            if (ImGui::MenuItem("Grid Snapping", nullptr, m_SnapEnabled)) {
                m_SnapEnabled = !m_SnapEnabled;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Scene Hierarchy", nullptr, &m_ShowSceneHierarchy);
            ImGui::MenuItem("Properties / Inspector", nullptr, &m_ShowProperties);
            ImGui::MenuItem("Console Log", nullptr, &m_ShowConsole);
            ImGui::MenuItem("Content Browser", nullptr, &m_ShowContentBrowser);
            ImGui::MenuItem("Assembly Editor", nullptr, &m_ShowAssemblyEditor);
            ImGui::MenuItem("Performance & Debug", nullptr, &m_ShowTelemetry);
            ImGui::MenuItem("Physics Wireframe", "F1", &m_ShowPhysicsDebug);
            ImGui::Separator();
            bool isMaximized = Axle::Application::Get().GetWindow().IsMaximized();
            if (ImGui::MenuItem(isMaximized ? "Restore Window" : "Maximize Window", "F11")) {
                if (isMaximized) {
                    Axle::Application::Get().GetWindow().Restore();
                } else {
                    Axle::Application::Get().GetWindow().Maximize();
                }
            }
            if (ImGui::MenuItem("Reset Default Layout")) {
                m_ResetLayout = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Camera")) {
            if (ImGui::MenuItem("Vehicle Chase Camera", "C", m_ChaseCamera)) {
                m_ChaseCamera = true;
            }
            if (ImGui::MenuItem("Free-Fly Camera", "C", !m_ChaseCamera)) {
                m_ChaseCamera = false;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // 2. Render Fixed Toolbar directly beneath Main Menu Bar
    RenderToolbar();

    // 3. Fullscreen DockSpace Container (Below Toolbar)
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const float toolbarHeight = 36.0f;
    const ImVec2 dockspacePos = ImVec2(mainViewport->WorkPos.x, mainViewport->WorkPos.y + toolbarHeight);
    const ImVec2 dockspaceSize = ImVec2(mainViewport->WorkSize.x, mainViewport->WorkSize.y - toolbarHeight);

    ImGui::SetNextWindowPos(dockspacePos);
    ImGui::SetNextWindowSize(dockspaceSize);
    ImGui::SetNextWindowViewport(mainViewport->ID);

    ImGuiWindowFlags dockspaceWindowFlags = ImGuiWindowFlags_NoDocking
                                         | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
                                         | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                                         | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("AxleEngineDockSpace", nullptr, dockspaceWindowFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceID = ImGui::GetID("AxleMainDockspace");
    ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    // Build default docked layout if not yet configured or if reset requested
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceID);
    if (node == nullptr || node->IsEmpty() || !node->IsSplitNode() || m_ResetLayout) {
        m_ResetLayout = false;

        ImGui::DockBuilderRemoveNode(dockspaceID);
        ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceID, dockspaceSize);

        ImGuiID dock_main = dockspaceID;
        ImGuiID dock_left = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.20f, nullptr, &dock_main);
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.28f, nullptr, &dock_main);
        ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.30f, nullptr, &dock_main);

        ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_left);
        ImGui::DockBuilderDockWindow("Viewport", dock_main);
        ImGui::DockBuilderDockWindow("Content Browser", dock_bottom);
        ImGui::DockBuilderDockWindow("Console", dock_bottom);
        ImGui::DockBuilderDockWindow("Properties", dock_right);
        ImGui::DockBuilderDockWindow("Performance & Debug", dock_right);

        ImGui::DockBuilderFinish(dockspaceID);
    }
    ImGui::End(); // End AxleEngineDockSpace

    // 3. Render 3D Viewport Window
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport");

    m_ViewportFocused = ImGui::IsWindowFocused();
    m_ViewportHovered = ImGui::IsWindowHovered();

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x > 0.0f && viewportSize.y > 0.0f &&
        (m_ViewportWidth != static_cast<Axle::u32>(viewportSize.x) || 
         m_ViewportHeight != static_cast<Axle::u32>(viewportSize.y))) {
        m_ViewportWidth = static_cast<Axle::u32>(viewportSize.x);
        m_ViewportHeight = static_cast<Axle::u32>(viewportSize.y);
        m_Framebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
        m_CameraController->OnResize(m_ViewportWidth, m_ViewportHeight);
    }

    if (m_Framebuffer) {
        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
        // Flip UV coordinates vertically for OpenGL
        ImGui::Image(reinterpret_cast<void*>(static_cast<intptr_t>(textureID)), 
                     viewportSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }

    ImVec2 viewportMin = ImGui::GetItemRectMin();
    ImVec2 viewportMax = ImGui::GetItemRectMax();
    ImVec2 viewportBounds = ImGui::GetItemRectSize();

    // Drag & Drop Target: Drag 3D models (.obj) from Content Browser into Viewport to instantiate
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            const char* pathStr = static_cast<const char*>(payload->Data);
            std::filesystem::path assetPath(pathStr);
            std::string ext = assetPath.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });

            if (ext == ".axlescene" || ext == ".scene") {
                OpenScene(assetPath);
            } else if (ext == ".axle") {
                glm::vec3 spawnPos(0.0f, 0.0f, 0.0f);
                ImVec2 mousePos = ImGui::GetMousePos();
                float mouseX = mousePos.x - viewportMin.x;
                float mouseY = mousePos.y - viewportMin.y;

                if (mouseX >= 0.0f && mouseX < viewportBounds.x && mouseY >= 0.0f && mouseY < viewportBounds.y) {
                    float ndcX = (2.0f * (mouseX / viewportBounds.x)) - 1.0f;
                    float ndcY = 1.0f - (2.0f * (mouseY / viewportBounds.y));

                    glm::mat4 invVP = glm::inverse(m_CameraController->GetCamera().GetViewProjectionMatrix());
                    glm::vec4 nearPointNDC(ndcX, ndcY, -1.0f, 1.0f);
                    glm::vec4 farPointNDC(ndcX, ndcY, 1.0f, 1.0f);

                    glm::vec4 nearW = invVP * nearPointNDC;
                    glm::vec4 farW = invVP * farPointNDC;
                    if (std::abs(nearW.w) > 1e-6f) nearW /= nearW.w;
                    if (std::abs(farW.w) > 1e-6f) farW /= farW.w;

                    glm::vec3 rayOrigin = glm::vec3(nearW);
                    glm::vec3 rayDir = glm::normalize(glm::vec3(farW - nearW));

                    Axle::RayCastHit hit;
                    if (m_PhysicsScene && m_PhysicsScene->CastRay(rayOrigin, rayDir, 500.0f, hit)) {
                        spawnPos = hit.Position;
                    } else {
                        if (std::abs(rayDir.y) > 1e-4f) {
                            float t = -rayOrigin.y / rayDir.y;
                            if (t > 0.0f && t < 150.0f) {
                                glm::vec3 groundHit = rayOrigin + rayDir * t;
                                spawnPos = groundHit;
                            } else {
                                spawnPos = rayOrigin + rayDir * 10.0f;
                            }
                        } else {
                            spawnPos = rayOrigin + rayDir * 10.0f;
                        }
                    }
                }

                auto spawned = m_Scene->InstantiateAssembly(assetPath, m_PhysicsScene.get(), spawnPos, &m_SerializerContext);
                if (spawned) {
                    m_SceneHierarchyPanel.SetSelectedEntity(spawned);
                    AXLE_INFO("Instantiated Assembly '{0}' into Viewport at ({1:.2f}, {2:.2f}, {3:.2f})",
                        assetPath.stem().string(), spawnPos.x, spawnPos.y, spawnPos.z);
                }
            } else if (ext == ".axlemesh" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                if (ext != ".axlemesh" && ext != ".axle") {
                    std::filesystem::path axlePath = assetPath.parent_path() / (assetPath.stem().string() + ".axlemesh");
                    if (!std::filesystem::exists(axlePath)) {
                        Axle::AssetSerializer::AutoConvertModel(assetPath, axlePath, true);
                    }
                    assetPath = axlePath;
                }

                auto pkg = Axle::AssetSerializer::LoadPackage(assetPath);
                if (pkg.Success) {
                    glm::vec3 spawnPos(0.0f, 0.0f, 0.0f);
                    ImVec2 mousePos = ImGui::GetMousePos();
                    float mouseX = mousePos.x - viewportMin.x;
                    float mouseY = mousePos.y - viewportMin.y;

                    if (mouseX >= 0.0f && mouseX < viewportBounds.x && mouseY >= 0.0f && mouseY < viewportBounds.y) {
                        float ndcX = (2.0f * (mouseX / viewportBounds.x)) - 1.0f;
                        float ndcY = 1.0f - (2.0f * (mouseY / viewportBounds.y));

                        glm::mat4 invVP = glm::inverse(m_CameraController->GetCamera().GetViewProjectionMatrix());
                        glm::vec4 nearPointNDC(ndcX, ndcY, -1.0f, 1.0f);
                        glm::vec4 farPointNDC(ndcX, ndcY, 1.0f, 1.0f);

                        glm::vec4 nearW = invVP * nearPointNDC;
                        glm::vec4 farW = invVP * farPointNDC;
                        if (std::abs(nearW.w) > 1e-6f) nearW /= nearW.w;
                        if (std::abs(farW.w) > 1e-6f) farW /= farW.w;

                        glm::vec3 rayOrigin = glm::vec3(nearW);
                        glm::vec3 rayDir = glm::normalize(glm::vec3(farW - nearW));

                        Axle::RayCastHit hit;
                        if (m_PhysicsScene && m_PhysicsScene->CastRay(rayOrigin, rayDir, 500.0f, hit)) {
                            spawnPos = hit.Position;
                        } else {
                            if (std::abs(rayDir.y) > 1e-4f) {
                                float t = -rayOrigin.y / rayDir.y;
                                if (t > 0.0f && t < 150.0f) {
                                    glm::vec3 groundHit = rayOrigin + rayDir * t;
                                    spawnPos = groundHit;
                                } else {
                                    spawnPos = rayOrigin + rayDir * 10.0f;
                                }
                            } else {
                                spawnPos = rayOrigin + rayDir * 10.0f;
                            }
                        }
                    }

                    if (pkg.IsPackage) {
                        std::string rootTag = pkg.Name.empty() ? assetPath.stem().string() : pkg.Name;
                        auto rootEntity = m_Scene->CreateEntity(rootTag);
                        rootEntity.AddComponent<Axle::TransformComponent>(spawnPos, glm::vec3(1.0f));
                        auto& rootHierarchy = rootEntity.AddComponent<Axle::HierarchyComponent>();

                        for (const auto& part : pkg.Parts) {
                            auto childEntity = m_Scene->CreateEntity(part.Name);
                            auto& ct = childEntity.AddComponent<Axle::TransformComponent>(part.LocalTranslation, part.LocalScale);
                            ct.Rotation = part.LocalRotation;

                            auto partFullPath = assetPath.parent_path() / part.RelativePath;
                            childEntity.AddComponent<Axle::MeshInstance3D>(
                                part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_CubeMesh,
                                partFullPath.string(),
                                part.LoadedMesh.SourceData.PatternMode,
                                part.LoadedMesh.SourceData.TileScale,
                                part.LoadedMesh.SourceData.ColorPrimary,
                                part.LoadedMesh.SourceData.ColorSecondary
                            );

                            childEntity.AddComponent<Axle::HierarchyComponent>((entt::entity)rootEntity);
                            rootHierarchy.Children.push_back((entt::entity)childEntity);
                        }

                        m_SceneHierarchyPanel.SetSelectedEntity(rootEntity);
                        AXLE_INFO("Instantiated Model Package '{0}' ({1} submeshes) into Viewport at ({2:.2f}, {3:.2f}, {4:.2f})", 
                            rootTag, pkg.Parts.size(), spawnPos.x, spawnPos.y, spawnPos.z);
                    } else if (!pkg.Parts.empty()) {
                        const auto& part = pkg.Parts[0];
                        std::string entityTag = pkg.Name.empty() ? assetPath.stem().string() : pkg.Name;
                        auto entity = m_Scene->CreateEntity(entityTag);
                        entity.AddComponent<Axle::TransformComponent>(spawnPos, glm::vec3(1.0f));

                        entity.AddComponent<Axle::MeshInstance3D>(
                            part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_CubeMesh,
                            assetPath.string(),
                            part.LoadedMesh.SourceData.PatternMode,
                            part.LoadedMesh.SourceData.TileScale,
                            part.LoadedMesh.SourceData.ColorPrimary,
                            part.LoadedMesh.SourceData.ColorSecondary
                        );

                        m_SceneHierarchyPanel.SetSelectedEntity(entity);
                        AXLE_INFO("Instantiated 3D model '{0}' into Viewport at ({1:.2f}, {2:.2f}, {3:.2f})", 
                            entityTag, spawnPos.x, spawnPos.y, spawnPos.z);
                    }
                }
            } else if (ext == ".amat" || ext == ".axlemat" || ext == ".mat") {
                // Drop Material into Viewport onto an Entity under the cursor
                ImVec2 mousePos = ImGui::GetMousePos();
                float mouseX = mousePos.x - viewportMin.x;
                float mouseY = mousePos.y - viewportMin.y;

                Axle::Entity hitEntity;
                if (mouseX >= 0.0f && mouseX < viewportBounds.x && mouseY >= 0.0f && mouseY < viewportBounds.y) {
                    float ndcX = (2.0f * (mouseX / viewportBounds.x)) - 1.0f;
                    float ndcY = 1.0f - (2.0f * (mouseY / viewportBounds.y));

                    glm::mat4 invVP = glm::inverse(m_CameraController->GetCamera().GetViewProjectionMatrix());
                    glm::vec4 nearW = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
                    glm::vec4 farW = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                    if (std::abs(nearW.w) > 1e-6f) nearW /= nearW.w;
                    if (std::abs(farW.w) > 1e-6f) farW /= farW.w;

                    glm::vec3 rayOrigin = glm::vec3(nearW);
                    glm::vec3 rayDir = glm::normalize(glm::vec3(farW - nearW));

                    Axle::RayCastHit hit;
                    if (m_PhysicsScene && m_PhysicsScene->CastRay(rayOrigin, rayDir, 500.0f, hit)) {
                        hitEntity = m_Scene->FindEntityByBodyID(hit.HitBodyID);
                    }
                }

                Axle::MaterialComponent droppedMat;
                if (Axle::SceneHierarchyPanel::LoadMaterialAsset(assetPath, droppedMat)) {
                    if (hitEntity) {
                        if (hitEntity.HasComponent<Axle::MaterialComponent>()) {
                            hitEntity.GetComponent<Axle::MaterialComponent>() = droppedMat;
                        } else {
                            hitEntity.AddComponent<Axle::MaterialComponent>(droppedMat);
                        }
                        m_SceneHierarchyPanel.SetSelectedEntity(hitEntity);
                        AXLE_INFO("Applied Material '{0}' onto Entity '{1}' in Viewport",
                            assetPath.stem().string(), hitEntity.GetName());
                    } else {
                        m_SceneHierarchyPanel.SetSelectedAsset(assetPath);
                    }
                }
            } else if (ext == ".hpp" || ext == ".cpp") {
                // Drop Script into Viewport onto an Entity under the cursor
                std::string scriptName = assetPath.stem().string();
                if (Axle::ScriptRegistry::Has(scriptName)) {
                    ImVec2 mousePos = ImGui::GetMousePos();
                    float mouseX = mousePos.x - viewportMin.x;
                    float mouseY = mousePos.y - viewportMin.y;

                    Axle::Entity hitEntity;
                    if (mouseX >= 0.0f && mouseX < viewportBounds.x && mouseY >= 0.0f && mouseY < viewportBounds.y) {
                        float ndcX = (2.0f * (mouseX / viewportBounds.x)) - 1.0f;
                        float ndcY = 1.0f - (2.0f * (mouseY / viewportBounds.y));

                        glm::mat4 invVP = glm::inverse(m_CameraController->GetCamera().GetViewProjectionMatrix());
                        glm::vec4 nearW = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
                        glm::vec4 farW = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                        if (std::abs(nearW.w) > 1e-6f) nearW /= nearW.w;
                        if (std::abs(farW.w) > 1e-6f) farW /= farW.w;

                        glm::vec3 rayOrigin = glm::vec3(nearW);
                        glm::vec3 rayDir = glm::normalize(glm::vec3(farW - nearW));

                        Axle::RayCastHit hit;
                        if (m_PhysicsScene && m_PhysicsScene->CastRay(rayOrigin, rayDir, 500.0f, hit)) {
                            hitEntity = m_Scene->FindEntityByBodyID(hit.HitBodyID);
                        }
                    }

                    if (!hitEntity) {
                        hitEntity = m_SceneHierarchyPanel.GetSelectedEntity();
                    }

                    if (hitEntity) {
                        if (!hitEntity.HasComponent<Axle::NativeScriptComponent>()) {
                            hitEntity.AddComponent<Axle::NativeScriptComponent>();
                        }
                        if (Axle::ScriptRegistry::BindByName(hitEntity, scriptName)) {
                            m_SceneHierarchyPanel.SetSelectedEntity(hitEntity);
                            AXLE_INFO("Attached NativeScript '{0}' onto Entity '{1}'", scriptName, hitEntity.GetName());
                        }
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Configure ImGuizmo rendering bounds
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportBounds.x, viewportBounds.y);

    const glm::mat4& cameraView = m_CameraController->GetCamera().GetViewMatrix();
    const glm::mat4& cameraProj = m_CameraController->GetCamera().GetProjectionMatrix();

    // 1. 3D Transform Gizmo Manipulation for Selected Entity (Active in Edit & Pause modes)
    if (m_SceneState != SceneState::Play) {
        auto selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (selectedEntity && selectedEntity.HasComponent<Axle::TransformComponent>() && m_GizmoType != -1) {
            auto& tc = selectedEntity.GetComponent<Axle::TransformComponent>();
            glm::mat4 worldTransform = m_Scene->GetWorldTransform(selectedEntity);
            glm::mat4 transform = worldTransform;

            ImGuizmo::MODE mode = m_GizmoModeLocal ? ImGuizmo::MODE::LOCAL : ImGuizmo::MODE::WORLD;
            if (m_GizmoType == ImGuizmo::OPERATION::SCALE) {
                mode = ImGuizmo::MODE::LOCAL;
            }

            float snapValues[3] = { 0.0f, 0.0f, 0.0f };
            if (m_SnapEnabled) {
                if (m_GizmoType == ImGuizmo::OPERATION::TRANSLATE) {
                    snapValues[0] = snapValues[1] = snapValues[2] = m_SnapTranslation;
                } else if (m_GizmoType == ImGuizmo::OPERATION::ROTATE) {
                    snapValues[0] = snapValues[1] = snapValues[2] = m_SnapRotation;
                } else if (m_GizmoType == ImGuizmo::OPERATION::SCALE) {
                    snapValues[0] = snapValues[1] = snapValues[2] = m_SnapScale;
                }
            }

            if (ImGuizmo::Manipulate(
                    glm::value_ptr(cameraView),
                    glm::value_ptr(cameraProj),
                    static_cast<ImGuizmo::OPERATION>(m_GizmoType),
                    mode,
                    glm::value_ptr(transform),
                    nullptr,
                    m_SnapEnabled ? snapValues : nullptr
                )) {
                if (selectedEntity.HasComponent<Axle::HierarchyComponent>() &&
                    selectedEntity.GetComponent<Axle::HierarchyComponent>().InheritTransform &&
                    selectedEntity.GetComponent<Axle::HierarchyComponent>().Parent != entt::null &&
                    m_Scene->GetRegistry().valid(selectedEntity.GetComponent<Axle::HierarchyComponent>().Parent)) {

                    entt::entity parentID = selectedEntity.GetComponent<Axle::HierarchyComponent>().Parent;
                    glm::mat4 parentWorld = m_Scene->GetWorldTransform(parentID);
                    glm::mat4 localMat = glm::inverse(parentWorld) * transform;

                    float translation[3], rotation[3], scale[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localMat), translation, rotation, scale);
                    tc.Translation = glm::vec3(translation[0], translation[1], translation[2]);
                    tc.Rotation = glm::quat(glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2])));
                    tc.Scale = glm::vec3(scale[0], scale[1], scale[2]);
                } else {
                    float translation[3], rotation[3], scale[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(transform), translation, rotation, scale);
                    tc.Translation = glm::vec3(translation[0], translation[1], translation[2]);
                    tc.Rotation = glm::quat(glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2])));
                    tc.Scale = glm::vec3(scale[0], scale[1], scale[2]);
                }

                // Real-time synchronization with Jolt Physics bodies
                if (selectedEntity == m_ChassisEntity && m_Vehicle) {
                    m_Vehicle->Reset(tc.Translation, tc.Rotation);
                    for (size_t i = 0; i < m_WheelEntities.size(); ++i) {
                        if (m_WheelEntities[i] && m_WheelEntities[i].HasComponent<Axle::TransformComponent>()) {
                            const glm::mat4 wheelMat = m_Vehicle->GetWheelTransform(static_cast<Axle::u32>(i));
                            auto& wtc = m_WheelEntities[i].GetComponent<Axle::TransformComponent>();
                            if (m_WheelEntities[i].HasComponent<Axle::HierarchyComponent>() &&
                                m_WheelEntities[i].GetComponent<Axle::HierarchyComponent>().InheritTransform &&
                                m_WheelEntities[i].GetParent() == m_ChassisEntity) {
                                glm::mat4 chassisWorld = m_Vehicle->GetChassisTransform();
                                glm::mat4 localWheelMat = glm::inverse(chassisWorld) * wheelMat;
                                wtc.Translation = glm::vec3(localWheelMat[3]);
                                wtc.Rotation = glm::quat_cast(localWheelMat);
                            } else {
                                wtc.Translation = glm::vec3(wheelMat[3]);
                                wtc.Rotation = glm::quat_cast(wheelMat);
                            }
                        }
                    }
                } else if (selectedEntity.HasComponent<Axle::RigidBody3D>() && m_PhysicsScene) {
                    auto& rb = selectedEntity.GetComponent<Axle::RigidBody3D>();
                    if (!rb.RuntimeBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                        bool isDynamic = (m_PhysicsScene->GetBodyInterface().GetMotionType(rb.RuntimeBodyID) == JPH::EMotionType::Dynamic);
                        m_PhysicsScene->SetBodyPositionAndRotation(rb.RuntimeBodyID, tc.Translation, tc.Rotation, isDynamic);
                        if (isDynamic) {
                            m_PhysicsScene->SetLinearVelocity(rb.RuntimeBodyID, glm::vec3(0.0f));
                            m_PhysicsScene->GetBodyInterface().SetAngularVelocity(rb.RuntimeBodyID, JPH::Vec3::sZero());
                        }
                    }
                }
            }
        }
    }

    // 3. Viewport Mouse Picking (Click on 3D meshes to select entity)
    if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        AXLE_INFO("[PICK] Left click detected in Viewport. Hovered=true, IsOverGizmo={}, IsUsingGizmo={}, RMBPressed={}",
            ImGuizmo::IsOver(), ImGuizmo::IsUsing(), Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight));
        if (!ImGuizmo::IsOver() && !ImGuizmo::IsUsing() && !Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight)) {
            ImVec2 mousePos = ImGui::GetMousePos();
            float mouseX = mousePos.x - viewportMin.x;
            float mouseY = mousePos.y - viewportMin.y;

            if (mouseX >= 0.0f && mouseX < viewportBounds.x && mouseY >= 0.0f && mouseY < viewportBounds.y) {
                float ndcX = (2.0f * (mouseX / viewportBounds.x)) - 1.0f;
                float ndcY = 1.0f - (2.0f * (mouseY / viewportBounds.y));

                glm::mat4 invVP = glm::inverse(m_CameraController->GetCamera().GetViewProjectionMatrix());
                glm::vec4 nearPointNDC(ndcX, ndcY, -1.0f, 1.0f);
                glm::vec4 farPointNDC(ndcX, ndcY, 1.0f, 1.0f);

                glm::vec4 nearWorld = invVP * nearPointNDC;
                if (std::abs(nearWorld.w) > 1e-6f) nearWorld /= nearWorld.w;

                glm::vec4 farWorld = invVP * farPointNDC;
                if (std::abs(farWorld.w) > 1e-6f) farWorld /= farWorld.w;

                glm::vec3 rayOrigin = glm::vec3(nearWorld);
                glm::vec3 delta = glm::vec3(farWorld - nearWorld);
                if (glm::dot(delta, delta) > 1e-6f) {
                    glm::vec3 rayDir = glm::normalize(delta);

                    bool isFiniteRay = std::isfinite(rayOrigin.x) && std::isfinite(rayOrigin.y) && std::isfinite(rayOrigin.z) &&
                                       std::isfinite(rayDir.x) && std::isfinite(rayDir.y) && std::isfinite(rayDir.z);

                    if (isFiniteRay) {
                        AXLE_INFO("[PICK] Raycast: origin=({:.2f}, {:.2f}, {:.2f}), dir=({:.2f}, {:.2f}, {:.2f})",
                            rayOrigin.x, rayOrigin.y, rayOrigin.z, rayDir.x, rayDir.y, rayDir.z);

                        Axle::Entity selectedHitEntity{};
                        float closestDist = 100000.0f;

                        // 1. Check Jolt Physics raycast first
                        Axle::RayCastHit hit;
                        if (m_PhysicsScene && m_PhysicsScene->CastRay(rayOrigin, rayDir, 1000.0f, hit)) {
                            Axle::Entity hitEntity = m_Scene->FindEntityByBodyID(hit.HitBodyID);
                            if (hitEntity) {
                                selectedHitEntity = hitEntity;
                                closestDist = hit.Distance;
                                AXLE_INFO("[PICK] Physics Raycast HIT body ID: {}", hit.HitBodyID.GetIndexAndSequenceNumber());
                            }
                        }

                        // 2. Ray-OBB intersection test for MeshInstance3D entities (including pure visual primitives without physics)
                        auto meshView = m_Scene->GetRegistry().view<Axle::TransformComponent, Axle::MeshInstance3D>();
                        for (auto entityID : meshView) {
                            Axle::Entity candidate(entityID, m_Scene.get());
                            if (candidate == selectedHitEntity) continue;

                            glm::mat4 worldTransform = candidate.GetWorldTransform();
                            glm::mat4 invTransform = glm::inverse(worldTransform);

                            glm::vec3 localOrigin = glm::vec3(invTransform * glm::vec4(rayOrigin, 1.0f));
                            glm::vec3 localDir = glm::vec3(invTransform * glm::vec4(rayDir, 0.0f));
                            float localDirLen = glm::length(localDir);
                            if (localDirLen < 1e-6f) continue;
                            localDir /= localDirLen;

                            glm::vec3 boxMin(-0.5f);
                            glm::vec3 boxMax(0.5f);

                            // Give Plane mesh a small thickness in Y so it's easy to click
                            if (candidate.GetComponent<Axle::MeshInstance3D>().VAO == m_PlaneMesh) {
                                boxMin.y = -0.05f;
                                boxMax.y = 0.05f;
                            }

                            // Slab intersection test
                            glm::vec3 invD = 1.0f / localDir;
                            glm::vec3 t0 = (boxMin - localOrigin) * invD;
                            glm::vec3 t1 = (boxMax - localOrigin) * invD;
                            glm::vec3 tmin = glm::min(t0, t1);
                            glm::vec3 tmax = glm::max(t0, t1);
                            float tEnter = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
                            float tExit = glm::min(glm::min(tmax.x, tmax.y), tmax.z);

                            if (tEnter <= tExit && tExit >= 0.0f) {
                                float hitLocalT = (tEnter >= 0.0f) ? tEnter : tExit;
                                glm::vec3 worldHitPoint = glm::vec3(worldTransform * glm::vec4(localOrigin + localDir * hitLocalT, 1.0f));
                                float dist = glm::length(worldHitPoint - rayOrigin);
                                if (dist < closestDist) {
                                    closestDist = dist;
                                    selectedHitEntity = candidate;
                                }
                            }
                        }

                        if (selectedHitEntity) {
                            std::string tag = selectedHitEntity.HasComponent<Axle::TagComponent>() ? selectedHitEntity.GetComponent<Axle::TagComponent>().Tag : "Unnamed";
                            AXLE_INFO("[PICK] Selected Entity ID: {}, Tag: '{}' (dist={:.2f}m)", (uint32_t)selectedHitEntity, tag, closestDist);
                            m_SceneHierarchyPanel.SetSelectedEntity(selectedHitEntity);
                        } else {
                            AXLE_INFO("[PICK] Raycast MISSED (empty space), clearing selection");
                            m_SceneHierarchyPanel.SetSelectedEntity({});
                        }
                    } else {
                        AXLE_WARN("[PICK] Non-finite ray origin/dir calculated, skipping CastRay");
                    }
                }
            }
        }
    }

    // Viewport Overlay Badges
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 badgePos = ImVec2(winPos.x + 12.0f, winPos.y + 12.0f);

    const char* statusText = (m_SceneState == SceneState::Edit) ? "EDIT MODE (STOPPED)"
                           : (m_SceneState == SceneState::Play) ? "SIMULATION ACTIVE"
                           : "SIMULATION PAUSED";
    ImU32 statusBg = (m_SceneState == SceneState::Edit) ? IM_COL32(35, 75, 150, 220)
                   : (m_SceneState == SceneState::Play) ? IM_COL32(25, 140, 55, 220)
                   : IM_COL32(190, 95, 15, 220);

    drawList->AddRectFilled(badgePos, ImVec2(badgePos.x + 175.0f, badgePos.y + 24.0f), statusBg, 4.0f);
    drawList->AddText(ImVec2(badgePos.x + 8.0f, badgePos.y + 4.0f), IM_COL32(255, 255, 255, 255), statusText);

    if (m_SceneState == SceneState::Play && m_Vehicle) {
        const auto vTele = m_Vehicle->GetTelemetry();
        char speedStr[48];
        snprintf(speedStr, sizeof(speedStr), "%.1f KM/H | GEAR %d", vTele.SpeedKmh, vTele.CurrentGear);
        ImVec2 speedPos = ImVec2(badgePos.x + 185.0f, badgePos.y);
        drawList->AddRectFilled(speedPos, ImVec2(speedPos.x + 155.0f, speedPos.y + 24.0f), IM_COL32(20, 30, 45, 220), 4.0f);
        drawList->AddText(ImVec2(speedPos.x + 8.0f, speedPos.y + 4.0f), IM_COL32(0, 230, 180, 255), speedStr);
    } else if (m_SceneState == SceneState::Edit) {
        ImVec2 hintPos = ImVec2(badgePos.x + 185.0f, badgePos.y);
        drawList->AddRectFilled(hintPos, ImVec2(hintPos.x + 440.0f, hintPos.y + 24.0f), IM_COL32(20, 25, 35, 200), 4.0f);
        drawList->AddText(ImVec2(hintPos.x + 8.0f, hintPos.y + 4.0f), IM_COL32(200, 210, 225, 255), "Gizmo: W/E/R | Q: Select | Click Mesh | RMB+WASD Fly");
    }

    ImGui::End();
    ImGui::PopStyleVar();

    // 5. Scene Hierarchy & Inspector Panels
    m_SceneHierarchyPanel.OnImGuiRender(&m_ShowSceneHierarchy, &m_ShowProperties);

    // 6. Console Log Panel
    m_ConsolePanel.OnImGuiRender(&m_ShowConsole);

    // 8. Content Browser Panel
    m_ContentBrowserPanel.OnImGuiRender(&m_ShowContentBrowser);



    // 10. Assembly Editor Panel (Phase 12)
    m_AssemblyEditorPanel.OnImGuiRender(&m_ShowAssemblyEditor);

    // 11. Performance & Telemetry Panel
    if (m_ShowTelemetry) {
        Axle::TelemetryData telemetry{
            .DeltaTime = m_LastDeltaTime,
            .FPS = m_LastDeltaTime > 0.0f ? 1.0f / m_LastDeltaTime : 0.0f,
            .CameraPosition = m_CameraController->GetCamera().GetPosition(),
            .CameraRotation = m_CameraController->GetCamera().GetRotation(),
            .HasPhysics = (m_PhysicsScene != nullptr),
            .BoxPosition = glm::vec3(0.0f),
            .BoxVelocity = glm::vec3(0.0f),
            .IsBoxActive = false,
            .SpherePosition = glm::vec3(0.0f),
            .SphereVelocity = glm::vec3(0.0f),
            .IsSphereActive = false,
            .TotalBodies = m_PhysicsScene ? m_PhysicsScene->GetNumBodies() : 0,
            .ActiveBodies = m_PhysicsScene ? m_PhysicsScene->GetNumActiveBodies() : 0,
            .ShowPhysicsWireframe = m_ShowPhysicsDebug,
            .DrawBoundingBoxes = m_PhysicsDrawSettings.DrawBoundingBoxes,
            .DrawVelocity = m_PhysicsDrawSettings.DrawVelocity,
            .DrawCenterOfMass = m_PhysicsDrawSettings.DrawCenterOfMass,
            .DebugLineCount = m_PhysicsDebugRenderer ? m_PhysicsDebugRenderer->GetLineCount() : 0,
            .HasVehicle = (m_Vehicle != nullptr),
            .IsChaseCamera = m_ChaseCamera
        };

        if (m_Vehicle && m_ChassisEntity && m_ChassisEntity.HasComponent<Axle::TransformComponent>()) {
            const auto vTele = m_Vehicle->GetTelemetry();
            telemetry.VehicleSpeedKmh = vTele.SpeedKmh;
            telemetry.VehicleRPM = vTele.EngineRPM;
            telemetry.VehicleGear = vTele.CurrentGear;
            for (Axle::u32 i = 0; i < 4; ++i) {
                telemetry.WheelContacts[i] = vTele.Wheels[i].HasContact;
                telemetry.WheelSuspension[i] = vTele.Wheels[i].SuspensionLength;
            }
        }

        m_DebugPanel.OnImGuiRender(telemetry, &m_ShowTelemetry);

        m_ShowPhysicsDebug = telemetry.ShowPhysicsWireframe;
        m_PhysicsDrawSettings.DrawBoundingBoxes = telemetry.DrawBoundingBoxes;
        m_PhysicsDrawSettings.DrawVelocity = telemetry.DrawVelocity;
        m_PhysicsDrawSettings.DrawCenterOfMass = telemetry.DrawCenterOfMass;
    }

    // 10. 3D Model Asset Ingestion Confirmation Modal
    RenderImportModal();
}

void EditorLayer::SetupNextImportModal() {
    if (m_PendingImportQueue.empty()) {
        m_ShowImportModal = false;
        return;
    }

    m_ImportModalData.SourcePath = m_PendingImportQueue.front();
    m_PendingImportQueue.erase(m_PendingImportQueue.begin());

    if (m_ContentBrowserPanel.IsWindowHovered()) {
        m_ImportModalData.TargetFolder = m_ContentBrowserPanel.GetCurrentPath();
    } else {
        m_ImportModalData.TargetFolder = Axle::FileSystem::ResolveAssetPath("models");
    }

    std::string stem = m_ImportModalData.SourcePath.stem().string();
    std::string outName = stem + ".axle";
    strncpy_s(m_ImportModalData.OutputFileName, sizeof(m_ImportModalData.OutputFileName), outName.c_str(), _TRUNCATE);

    m_ImportModalData.SplitSubmeshes = true;
    m_ImportModalData.PatternMode = 1;
    m_ImportModalData.TileScale = 8.0f;
    m_ImportModalData.ColorPrimary = glm::vec4(0.85f, 0.88f, 0.92f, 1.0f);
    m_ImportModalData.ColorSecondary = glm::vec4(0.65f, 0.70f, 0.76f, 1.0f);
    m_ImportModalData.ErrorMessage.clear();

    m_ShowImportModal = true;
    m_OpenImportModalPopup = true;
}

void EditorLayer::RenderImportModal() {
    if (m_OpenImportModalPopup) {
        ImGui::OpenPopup("Import 3D Asset##Modal");
        m_OpenImportModalPopup = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(540.0f, 0.0f), ImGuiCond_Appearing);

    bool modalOpen = m_ShowImportModal;
    if (ImGui::BeginPopupModal("Import 3D Asset##Modal", &modalOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        m_ShowImportModal = modalOpen;

        // Detect model extension and format badge
        std::string ext = m_ImportModalData.SourcePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });

        const char* formatName = "3D Asset";
        ImVec4 badgeColor(0.2f, 0.6f, 1.0f, 1.0f);
        if (ext == ".obj") {
            formatName = "Wavefront OBJ (.obj)";
            badgeColor = ImVec4(0.95f, 0.6f, 0.2f, 1.0f);
        } else if (ext == ".gltf") {
            formatName = "glTF 2.0 Scene (.gltf)";
            badgeColor = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
        } else if (ext == ".glb") {
            formatName = "glTF 2.0 Binary (.glb)";
            badgeColor = ImVec4(0.2f, 0.75f, 0.9f, 1.0f);
        } else if (ext == ".fbx") {
            formatName = "Autodesk FBX (.fbx)";
            badgeColor = ImVec4(0.85f, 0.4f, 0.95f, 1.0f);
        }

        ImGui::TextColored(badgeColor, "[ %s ]", formatName);
        ImGui::SameLine();
        ImGui::TextDisabled("Model Ingestion Pipeline");
        ImGui::Separator();
        ImGui::Spacing();

        // Source details table
        if (ImGui::BeginTable("##ImportInfoTable", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Source File:");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m_ImportModalData.SourcePath.filename().string().c_str());

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Source Path:");
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", m_ImportModalData.SourcePath.string().c_str());

            std::error_code ec;
            auto sz = std::filesystem::file_size(m_ImportModalData.SourcePath, ec);
            if (!ec) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("File Size:");
                ImGui::TableNextColumn();
                if (sz < 1024) {
                    ImGui::Text("%llu Bytes", sz);
                } else if (sz < 1024 * 1024) {
                    ImGui::Text("%.1f KB", sz / 1024.0f);
                } else {
                    ImGui::Text("%.2f MB", sz / (1024.0f * 1024.0f));
                }
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Target Folder:");
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", m_ImportModalData.TargetFolder.string().c_str());

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Target filename
        ImGui::TextUnformatted("Output Asset Name (.axle):");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##OutputFileName", m_ImportModalData.OutputFileName, sizeof(m_ImportModalData.OutputFileName));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Submesh splitting / hierarchy
        ImGui::Checkbox("Split into Separate Parts / Hierarchy", &m_ImportModalData.SplitSubmeshes);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When enabled, decomposes distinct submesh parts (doors, wheels, body) into separate .axle files organized in a dedicated folder, creating a hierarchical scene package.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Material Presets
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "Default Material & Procedural Surface Appearance");
        ImGui::Spacing();

        ImGui::TextDisabled("Quick Palettes:");
        ImGui::SameLine();
        if (ImGui::SmallButton("Titanium Silver")) {
            m_ImportModalData.ColorPrimary = glm::vec4(0.85f, 0.88f, 0.92f, 1.0f);
            m_ImportModalData.ColorSecondary = glm::vec4(0.65f, 0.70f, 0.76f, 1.0f);
            m_ImportModalData.TileScale = 8.0f;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Slate Graphite")) {
            m_ImportModalData.ColorPrimary = glm::vec4(0.35f, 0.38f, 0.42f, 1.0f);
            m_ImportModalData.ColorSecondary = glm::vec4(0.20f, 0.22f, 0.26f, 1.0f);
            m_ImportModalData.TileScale = 8.0f;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Arctic Navy")) {
            m_ImportModalData.ColorPrimary = glm::vec4(0.22f, 0.28f, 0.38f, 1.0f);
            m_ImportModalData.ColorSecondary = glm::vec4(0.14f, 0.18f, 0.25f, 1.0f);
            m_ImportModalData.TileScale = 8.0f;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Ceramic White")) {
            m_ImportModalData.ColorPrimary = glm::vec4(0.92f, 0.92f, 0.94f, 1.0f);
            m_ImportModalData.ColorSecondary = glm::vec4(0.80f, 0.82f, 0.85f, 1.0f);
            m_ImportModalData.TileScale = 8.0f;
        }

        ImGui::ColorEdit4("Primary Color", glm::value_ptr(m_ImportModalData.ColorPrimary));
        ImGui::ColorEdit4("Secondary Color", glm::value_ptr(m_ImportModalData.ColorSecondary));

        const char* patternNames[] = {
            "0: Solid Pure",
            "1: Checkerboard",
            "2: Cyber Grid",
            "3: Warning Stripes",
            "4: Carbon Fiber",
            "5: Hexagon Dots"
        };
        ImGui::Combo("Pattern Preset", &m_ImportModalData.PatternMode, patternNames, IM_ARRAYSIZE(patternNames));
        ImGui::SliderFloat("Pattern Scale", &m_ImportModalData.TileScale, 0.5f, 32.0f, "%.1fx");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Pipeline info notice (Auto-Bake is always active under the hood, and immediate instantiate is removed as requested)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.75f, 1.0f, 1.0f));
        ImGui::TextWrapped("The model geometry will be converted into Axle's structured text format (.axle) and pre-baked into high-speed binary cache (.axlebin) inside the Content Browser.");
        ImGui::PopStyleColor();

        if (!m_ImportModalData.ErrorMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_ImportModalData.ErrorMessage.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Action Buttons
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.34f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.68f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.45f, 0.28f, 1.0f));

        if (ImGui::Button("Convert & Import", ImVec2(160, 32))) {
            std::string outNameStr = m_ImportModalData.OutputFileName;
            if (outNameStr.empty()) {
                outNameStr = m_ImportModalData.SourcePath.stem().string() + ".axle";
            }
            if (outNameStr.length() < 5 || outNameStr.substr(outNameStr.length() - 5) != ".axle") {
                outNameStr += ".axle";
            }

            std::filesystem::path destAxlePath = m_ImportModalData.TargetFolder / outNameStr;

            bool success = Axle::AssetSerializer::AutoConvertModel(
                m_ImportModalData.SourcePath, destAxlePath,
                m_ImportModalData.SplitSubmeshes,
                m_ImportModalData.PatternMode,
                m_ImportModalData.TileScale,
                m_ImportModalData.ColorPrimary,
                m_ImportModalData.ColorSecondary
            );

            if (success) {
                AXLE_INFO("Asset Pipeline: Successfully converted '{0}' -> '{1}' (with binary cache)", 
                    m_ImportModalData.SourcePath.filename().string(), destAxlePath.filename().string());

                ImGui::CloseCurrentPopup();
                if (!m_PendingImportQueue.empty()) {
                    SetupNextImportModal();
                } else {
                    m_ShowImportModal = false;
                }
            } else {
                m_ImportModalData.ErrorMessage = "Conversion failed! Check console logs for error details.";
                AXLE_ERROR("Asset Pipeline: Failed to convert model '{0}'", m_ImportModalData.SourcePath.string());
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(100, 32))) {
            ImGui::CloseCurrentPopup();
            m_PendingImportQueue.clear();
            m_ShowImportModal = false;
        }

        ImGui::EndPopup();
    } else {
        if (!modalOpen) {
            m_PendingImportQueue.clear();
            m_ShowImportModal = false;
        }
    }
}
