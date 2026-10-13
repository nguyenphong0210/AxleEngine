#include <Axle/SceneSerializer.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>

namespace Axle {

    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    SceneSerializer::SceneSerializer(Scene* scene, PhysicsScene* physicsScene)
        : m_Scene(scene), m_PhysicsScene(physicsScene) {
    }

    bool SceneSerializer::Serialize(const std::filesystem::path& filepath) {
        std::ofstream fout(filepath);
        if (!fout.is_open()) {
            AXLE_ERROR("SceneSerializer: Could not open file for writing: '{0}'", filepath.string());
            return false;
        }

        std::string serialized = SerializeToString();
        fout << serialized;
        fout.close();

        AXLE_INFO("SceneSerializer: Successfully saved scene to '{0}'", filepath.string());
        return true;
    }

    std::string SceneSerializer::SerializeToString() {
        if (!m_Scene) return "";

        std::stringstream ss;
        ss << std::fixed << std::setprecision(6);

        ss << "# Axle Scene Definition File\n";
        ss << "Scene: Untitled\n";
        ss << "Entities:\n";

        auto& registry = m_Scene->GetRegistry();

        // Ensure every entity has an IDComponent
        auto tagView = registry.view<TagComponent>();
        for (auto entityID : tagView) {
            Entity entity{ entityID, m_Scene };
            if (!entity.HasComponent<IDComponent>()) {
                entity.AddComponent<IDComponent>(UUID());
            }
        }

        auto idView = registry.view<IDComponent>();
        for (auto entityID : idView) {
            Entity entity{ entityID, m_Scene };
            u64 uuid = entity.GetComponent<IDComponent>().ID;

            ss << "  - Entity: " << uuid << "\n";

            // TagComponent
            if (entity.HasComponent<TagComponent>()) {
                auto& tc = entity.GetComponent<TagComponent>();
                ss << "    TagComponent:\n";
                ss << "      Tag: " << tc.Tag << "\n";
            }

            // TransformComponent
            if (entity.HasComponent<TransformComponent>()) {
                auto& tc = entity.GetComponent<TransformComponent>();
                ss << "    TransformComponent:\n";
                ss << "      Translation: [ " << tc.Translation.x << " , " << tc.Translation.y << " , " << tc.Translation.z << " ]\n";
                ss << "      Rotation: [ " << tc.Rotation.w << " , " << tc.Rotation.x << " , " << tc.Rotation.y << " , " << tc.Rotation.z << " ]\n";
                ss << "      Scale: [ " << tc.Scale.x << " , " << tc.Scale.y << " , " << tc.Scale.z << " ]\n";
            }

            // MeshInstance3D
            if (entity.HasComponent<MeshInstance3D>()) {
                auto& mc = entity.GetComponent<MeshInstance3D>();
                std::string assetPath = mc.AssetPath;

                if (assetPath.empty() && mc.VAO) {
                    if (m_Context.CubeMesh && mc.VAO == m_Context.CubeMesh) assetPath = "builtin://cube";
                    else if (m_Context.SphereMesh && mc.VAO == m_Context.SphereMesh) assetPath = "builtin://sphere";
                    else if (m_Context.CylinderMesh && mc.VAO == m_Context.CylinderMesh) assetPath = "builtin://cylinder";
                    else if (m_Context.ConeMesh && mc.VAO == m_Context.ConeMesh) assetPath = "builtin://cone";
                    else if (m_Context.PlaneMesh && mc.VAO == m_Context.PlaneMesh) assetPath = "builtin://plane";
                }

                ss << "    MeshInstance3D:\n";
                ss << "      AssetPath: " << assetPath << "\n";
                ss << "      PatternMode: " << mc.PatternMode << "\n";
                ss << "      TileScale: " << mc.TileScale << "\n";
                ss << "      Color1: [ " << mc.Color1.r << " , " << mc.Color1.g << " , " << mc.Color1.b << " , " << mc.Color1.a << " ]\n";
                ss << "      Color2: [ " << mc.Color2.r << " , " << mc.Color2.g << " , " << mc.Color2.b << " , " << mc.Color2.a << " ]\n";
            }

            // RigidBody3D
            if (entity.HasComponent<RigidBody3D>()) {
                auto& rb = entity.GetComponent<RigidBody3D>();
                bool isDynamic = rb.IsDynamic;
                if (m_PhysicsScene && !rb.RuntimeBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                    isDynamic = (m_PhysicsScene->GetBodyInterface().GetMotionType(rb.RuntimeBodyID) == JPH::EMotionType::Dynamic);
                }

                std::string motionStr = isDynamic ? "Dynamic" : "Static";
                if (rb.MotionType == BodyMotionType::Kinematic) motionStr = "Kinematic";

                ss << "    RigidBody3D:\n";
                ss << "      MotionType: " << motionStr << "\n";
                ss << "      IsDynamic: " << (isDynamic ? "true" : "false") << "\n";
                ss << "      Mass: " << rb.Mass << "\n";
                ss << "      LinearDamping: " << rb.LinearDamping << "\n";
                ss << "      AngularDamping: " << rb.AngularDamping << "\n";
                ss << "      GravityFactor: " << rb.GravityFactor << "\n";
            }

            // CollisionShape3D
            if (entity.HasComponent<CollisionShape3D>()) {
                auto& cs = entity.GetComponent<CollisionShape3D>();
                std::string shapeName = "Box";
                if (cs.ShapeType == CollisionShapeType::Plane) shapeName = "Plane";
                else if (cs.ShapeType == CollisionShapeType::Sphere) shapeName = "Sphere";
                else if (cs.ShapeType == CollisionShapeType::Cylinder) shapeName = "Cylinder";
                else if (cs.ShapeType == CollisionShapeType::Cone) shapeName = "Cone";
                else if (cs.ShapeType == CollisionShapeType::Mesh) shapeName = "Mesh";

                float friction = cs.Friction;
                float restitution = cs.Restitution;
                if (entity.HasComponent<RigidBody3D>() && m_PhysicsScene) {
                    auto& rb = entity.GetComponent<RigidBody3D>();
                    if (!rb.RuntimeBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                        friction = m_PhysicsScene->GetBodyInterface().GetFriction(rb.RuntimeBodyID);
                        restitution = m_PhysicsScene->GetBodyInterface().GetRestitution(rb.RuntimeBodyID);
                    }
                }

                ss << "    CollisionShape3D:\n";
                ss << "      ShapeType: " << shapeName << "\n";
                ss << "      HalfExtent: [ " << cs.HalfExtent.x << " , " << cs.HalfExtent.y << " , " << cs.HalfExtent.z << " ]\n";
                ss << "      Radius: " << cs.Radius << "\n";
                ss << "      HalfHeight: " << cs.HalfHeight << "\n";
                ss << "      Offset: [ " << cs.Offset.x << " , " << cs.Offset.y << " , " << cs.Offset.z << " ]\n";
                ss << "      Friction: " << friction << "\n";
                ss << "      Restitution: " << restitution << "\n";
                ss << "      IsTrigger: " << (cs.IsTrigger ? "true" : "false") << "\n";
            }

            // HierarchyComponent
            if (entity.HasComponent<HierarchyComponent>()) {
                auto& hc = entity.GetComponent<HierarchyComponent>();
                u64 parentUUID = 0;
                if (hc.Parent != entt::null && registry.valid(hc.Parent) && registry.all_of<IDComponent>(hc.Parent)) {
                    parentUUID = registry.get<IDComponent>(hc.Parent).ID;
                }
                ss << "    HierarchyComponent:\n";
                ss << "      Parent: " << parentUUID << "\n";
                ss << "      InheritTransform: " << (hc.InheritTransform ? "true" : "false") << "\n";
            }

            // NativeScriptComponent
            if (entity.HasComponent<NativeScriptComponent>()) {
                auto& nsc = entity.GetComponent<NativeScriptComponent>();
                ss << "    NativeScriptComponent:\n";
                ss << "      ScriptName: " << nsc.ScriptName << "\n";
            }

            // VehicleComponent
            if (entity.HasComponent<VehicleComponent>()) {
                ss << "    VehicleComponent: true\n";
            }

            // DirectionalLightComponent
            if (entity.HasComponent<DirectionalLightComponent>()) {
                auto& dlc = entity.GetComponent<DirectionalLightComponent>();
                ss << "    DirectionalLightComponent:\n";
                ss << "      Color: [ " << dlc.Color.r << " , " << dlc.Color.g << " , " << dlc.Color.b << " ]\n";
                ss << "      Intensity: " << dlc.Intensity << "\n";
                ss << "      AmbientIntensity: " << dlc.AmbientIntensity << "\n";
                ss << "      Azimuth: " << dlc.Azimuth << "\n";
                ss << "      Elevation: " << dlc.Elevation << "\n";
                ss << "      CastShadows: " << (dlc.CastShadows ? "true" : "false") << "\n";
                ss << "      Exposure: " << dlc.Exposure << "\n";
                ss << "      ToneMappingMode: " << dlc.ToneMappingMode << "\n";
                ss << "      Gamma: " << dlc.Gamma << "\n";
            }

            // MaterialComponent
            if (entity.HasComponent<MaterialComponent>()) {
                auto& mat = entity.GetComponent<MaterialComponent>();
                ss << "    MaterialComponent:\n";
                ss << "      AlbedoPath: " << mat.AlbedoPath << "\n";
                ss << "      UseAlbedoMap: " << (mat.UseAlbedoMap ? "true" : "false") << "\n";
                ss << "      AlbedoColor: [ " << mat.AlbedoColor.r << " , " << mat.AlbedoColor.g << " , " << mat.AlbedoColor.b << " , " << mat.AlbedoColor.a << " ]\n";
                ss << "      NormalPath: " << mat.NormalPath << "\n";
                ss << "      UseNormalMap: " << (mat.UseNormalMap ? "true" : "false") << "\n";
                ss << "      NormalStrength: " << mat.NormalStrength << "\n";
                ss << "      RoughnessPath: " << mat.RoughnessPath << "\n";
                ss << "      UseRoughnessMap: " << (mat.UseRoughnessMap ? "true" : "false") << "\n";
                ss << "      Roughness: " << mat.Roughness << "\n";
                ss << "      MetallicPath: " << mat.MetallicPath << "\n";
                ss << "      UseMetallicMap: " << (mat.UseMetallicMap ? "true" : "false") << "\n";
                ss << "      Metallic: " << mat.Metallic << "\n";
                ss << "      EmissionColor: [ " << mat.EmissionColor.r << " , " << mat.EmissionColor.g << " , " << mat.EmissionColor.b << " ]\n";
                ss << "      EmissionIntensity: " << mat.EmissionIntensity << "\n";
                ss << "      UVScale: [ " << mat.UVScale.x << " , " << mat.UVScale.y << " ]\n";
            }

            // SkyboxComponent
            if (entity.HasComponent<SkyboxComponent>()) {
                auto& sbc = entity.GetComponent<SkyboxComponent>();
                ss << "    SkyboxComponent:\n";
                ss << "      Enabled: " << (sbc.Enabled ? "true" : "false") << "\n";
                ss << "      ZenithColor: [ " << sbc.ZenithColor.r << " , " << sbc.ZenithColor.g << " , " << sbc.ZenithColor.b << " ]\n";
                ss << "      HorizonColor: [ " << sbc.HorizonColor.r << " , " << sbc.HorizonColor.g << " , " << sbc.HorizonColor.b << " ]\n";
                ss << "      GroundColor: [ " << sbc.GroundColor.r << " , " << sbc.GroundColor.g << " , " << sbc.GroundColor.b << " ]\n";
                ss << "      SunSize: " << sbc.SunSize << "\n";
                ss << "      SunGlow: " << sbc.SunGlow << "\n";
                ss << "      Turbidity: " << sbc.Turbidity << "\n";
                ss << "      SyncWithSun: " << (sbc.SyncWithSun ? "true" : "false") << "\n";
                ss << "      CustomAzimuth: " << sbc.CustomAzimuth << "\n";
                ss << "      CustomElevation: " << sbc.CustomElevation << "\n";
                ss << "      HemisphereAmbient: " << (sbc.HemisphereAmbient ? "true" : "false") << "\n";
            }
        }

        return ss.str();
    }

    bool SceneSerializer::Deserialize(const std::filesystem::path& filepath) {
        std::ifstream fin(filepath);
        if (!fin.is_open()) {
            AXLE_ERROR("SceneSerializer: Could not open file for reading: '{0}'", filepath.string());
            return false;
        }

        std::stringstream ss;
        ss << fin.rdbuf();
        fin.close();

        bool success = DeserializeFromString(ss.str());
        if (success) {
            AXLE_INFO("SceneSerializer: Successfully loaded scene from '{0}'", filepath.string());
        }
        return success;
    }

    bool SceneSerializer::DeserializeFromString(const std::string& data) {
        if (!m_Scene) return false;

        m_Scene->Clear();

        std::istringstream stream(data);
        std::string line;

        struct PendingHierarchy {
            Entity ChildEntity;
            u64 ParentUUID = 0;
            bool InheritTransform = true;
        };
        std::vector<PendingHierarchy> pendingHierarchy;

        Entity currentEntity{};
        enum class ComponentSection {
            None,
            Tag,
            Transform,
            Mesh,
            Material,
            RigidBody,
            CollisionShape,
            Hierarchy,
            NativeScript,
            DirectionalLight,
            Skybox
        };
        ComponentSection currentSection = ComponentSection::None;

        while (std::getline(stream, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.rfind("- Entity:", 0) == 0) {
                u64 uuid = 0;
                try {
                    uuid = std::stoull(Trim(trimmed.substr(9)));
                } catch (...) {
                    uuid = UUID();
                }
                currentEntity = m_Scene->CreateEntityWithUUID(uuid, "Entity");
                currentSection = ComponentSection::None;
            } else if (!currentEntity) {
                continue;
            } else if (trimmed == "TagComponent:") {
                currentSection = ComponentSection::Tag;
            } else if (trimmed == "TransformComponent:") {
                currentSection = ComponentSection::Transform;
            } else if (trimmed == "MeshInstance3D:" || trimmed == "MeshComponent:") {
                currentSection = ComponentSection::Mesh;
            } else if (trimmed == "MaterialComponent:") {
                currentSection = ComponentSection::Material;
            } else if (trimmed == "RigidBody3D:" || trimmed == "RigidBodyComponent:") {
                currentSection = ComponentSection::RigidBody;
            } else if (trimmed == "CollisionShape3D:" || trimmed == "CollisionShape:") {
                currentSection = ComponentSection::CollisionShape;
            } else if (trimmed == "HierarchyComponent:") {
                currentSection = ComponentSection::Hierarchy;
            } else if (trimmed == "NativeScriptComponent:") {
                currentSection = ComponentSection::NativeScript;
            } else if (trimmed == "DirectionalLightComponent:") {
                currentSection = ComponentSection::DirectionalLight;
            } else if (trimmed == "SkyboxComponent:") {
                currentSection = ComponentSection::Skybox;
            } else if (trimmed == "VehicleComponent: true") {
                if (!currentEntity.HasComponent<VehicleComponent>()) {
                    currentEntity.AddComponent<VehicleComponent>();
                }
            } else {
                switch (currentSection) {
                    case ComponentSection::Tag: {
                        if (trimmed.rfind("Tag:", 0) == 0) {
                            currentEntity.GetComponent<TagComponent>().Tag = Trim(trimmed.substr(4));
                        }
                        break;
                    }
                    case ComponentSection::Transform: {
                        if (!currentEntity.HasComponent<TransformComponent>()) {
                            currentEntity.AddComponent<TransformComponent>();
                        }
                        auto& tc = currentEntity.GetComponent<TransformComponent>();
                        if (trimmed.rfind("Translation:", 0) == 0) {
                            float x = 0, y = 0, z = 0;
                            sscanf(trimmed.c_str(), "Translation: [ %f , %f , %f ]", &x, &y, &z);
                            tc.Translation = glm::vec3(x, y, z);
                        } else if (trimmed.rfind("Rotation:", 0) == 0) {
                            float w = 1, x = 0, y = 0, z = 0;
                            sscanf(trimmed.c_str(), "Rotation: [ %f , %f , %f , %f ]", &w, &x, &y, &z);
                            tc.Rotation = glm::quat(w, x, y, z);
                        } else if (trimmed.rfind("Scale:", 0) == 0) {
                            float x = 1, y = 1, z = 1;
                            sscanf(trimmed.c_str(), "Scale: [ %f , %f , %f ]", &x, &y, &z);
                            tc.Scale = glm::vec3(x, y, z);
                        }
                        break;
                    }
                    case ComponentSection::Mesh: {
                        if (!currentEntity.HasComponent<MeshInstance3D>()) {
                            currentEntity.AddComponent<MeshInstance3D>();
                        }
                        auto& mc = currentEntity.GetComponent<MeshInstance3D>();
                        if (trimmed.rfind("AssetPath:", 0) == 0) {
                            mc.AssetPath = Trim(trimmed.substr(10));
                            if (mc.AssetPath == "builtin://cube") mc.VAO = m_Context.CubeMesh;
                            else if (mc.AssetPath == "builtin://sphere") mc.VAO = m_Context.SphereMesh;
                            else if (mc.AssetPath == "builtin://cylinder") mc.VAO = m_Context.CylinderMesh;
                            else if (mc.AssetPath == "builtin://cone") mc.VAO = m_Context.ConeMesh;
                            else if (mc.AssetPath == "builtin://plane") mc.VAO = m_Context.PlaneMesh;
                            else if (!mc.AssetPath.empty()) {
                                auto fullPath = FileSystem::ResolveAssetPath(mc.AssetPath);
                                if (std::filesystem::exists(fullPath)) {
                                    auto loaded = AssetSerializer::LoadMesh(fullPath);
                                    if (loaded.Success) {
                                        mc.VAO = loaded.VAO;
                                    }
                                }
                            }
                        } else if (trimmed.rfind("PatternMode:", 0) == 0) {
                            mc.PatternMode = std::stoi(Trim(trimmed.substr(12)));
                        } else if (trimmed.rfind("TileScale:", 0) == 0) {
                            mc.TileScale = std::stof(Trim(trimmed.substr(10)));
                        } else if (trimmed.rfind("Color1:", 0) == 0) {
                            float r = 1, g = 1, b = 1, a = 1;
                            sscanf(trimmed.c_str(), "Color1: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                            mc.Color1 = glm::vec4(r, g, b, a);
                        } else if (trimmed.rfind("Color2:", 0) == 0) {
                            float r = 0, g = 0, b = 0, a = 1;
                            sscanf(trimmed.c_str(), "Color2: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                            mc.Color2 = glm::vec4(r, g, b, a);
                        }
                        break;
                    }
                    case ComponentSection::RigidBody: {
                        if (!currentEntity.HasComponent<RigidBody3D>()) {
                            currentEntity.AddComponent<RigidBody3D>();
                        }
                        auto& rb = currentEntity.GetComponent<RigidBody3D>();
                        if (trimmed.rfind("MotionType:", 0) == 0) {
                            std::string mType = Trim(trimmed.substr(11));
                            if (mType == "Static") { rb.MotionType = BodyMotionType::Static; rb.IsDynamic = false; }
                            else if (mType == "Kinematic") { rb.MotionType = BodyMotionType::Kinematic; rb.IsDynamic = false; }
                            else { rb.MotionType = BodyMotionType::Dynamic; rb.IsDynamic = true; }
                        } else if (trimmed.rfind("IsDynamic:", 0) == 0) {
                            rb.IsDynamic = (Trim(trimmed.substr(10)) == "true");
                            rb.MotionType = rb.IsDynamic ? BodyMotionType::Dynamic : BodyMotionType::Static;
                        } else if (trimmed.rfind("Mass:", 0) == 0) {
                            rb.Mass = std::stof(Trim(trimmed.substr(5)));
                        } else if (trimmed.rfind("LinearDamping:", 0) == 0) {
                            rb.LinearDamping = std::stof(Trim(trimmed.substr(14)));
                        } else if (trimmed.rfind("AngularDamping:", 0) == 0) {
                            rb.AngularDamping = std::stof(Trim(trimmed.substr(15)));
                        } else if (trimmed.rfind("GravityFactor:", 0) == 0) {
                            rb.GravityFactor = std::stof(Trim(trimmed.substr(14)));
                        }
                        // Backward compatibility: If legacy scene stored shape under RigidBody:
                        else if (trimmed.rfind("ShapeType:", 0) == 0 || trimmed.rfind("HalfExtent:", 0) == 0 ||
                                 trimmed.rfind("Radius:", 0) == 0 || trimmed.rfind("HalfHeight:", 0) == 0 ||
                                 trimmed.rfind("Friction:", 0) == 0 || trimmed.rfind("Restitution:", 0) == 0) {
                            if (!currentEntity.HasComponent<CollisionShape3D>()) {
                                currentEntity.AddComponent<CollisionShape3D>();
                            }
                            auto& cs = currentEntity.GetComponent<CollisionShape3D>();
                            if (trimmed.rfind("ShapeType:", 0) == 0) {
                                std::string sName = Trim(trimmed.substr(10));
                                if (sName == "Plane") cs.ShapeType = CollisionShapeType::Plane;
                                else if (sName == "Sphere") cs.ShapeType = CollisionShapeType::Sphere;
                                else if (sName == "Cylinder") cs.ShapeType = CollisionShapeType::Cylinder;
                                else if (sName == "Cone") cs.ShapeType = CollisionShapeType::Cone;
                                else cs.ShapeType = CollisionShapeType::Box;
                            } else if (trimmed.rfind("Friction:", 0) == 0) {
                                cs.Friction = std::stof(Trim(trimmed.substr(9)));
                            } else if (trimmed.rfind("Restitution:", 0) == 0) {
                                cs.Restitution = std::stof(Trim(trimmed.substr(12)));
                            } else if (trimmed.rfind("HalfExtent:", 0) == 0) {
                                float x = 0.5f, y = 0.5f, z = 0.5f;
                                sscanf(trimmed.c_str(), "HalfExtent: [ %f , %f , %f ]", &x, &y, &z);
                                cs.HalfExtent = glm::vec3(x, y, z);
                            } else if (trimmed.rfind("Radius:", 0) == 0) {
                                cs.Radius = std::stof(Trim(trimmed.substr(7)));
                            } else if (trimmed.rfind("HalfHeight:", 0) == 0) {
                                cs.HalfHeight = std::stof(Trim(trimmed.substr(11)));
                            }
                        }
                        break;
                    }
                    case ComponentSection::CollisionShape: {
                        if (!currentEntity.HasComponent<CollisionShape3D>()) {
                            currentEntity.AddComponent<CollisionShape3D>();
                        }
                        auto& cs = currentEntity.GetComponent<CollisionShape3D>();
                        if (trimmed.rfind("ShapeType:", 0) == 0) {
                            std::string sName = Trim(trimmed.substr(10));
                            if (sName == "Plane") cs.ShapeType = CollisionShapeType::Plane;
                            else if (sName == "Sphere") cs.ShapeType = CollisionShapeType::Sphere;
                            else if (sName == "Cylinder") cs.ShapeType = CollisionShapeType::Cylinder;
                            else if (sName == "Cone") cs.ShapeType = CollisionShapeType::Cone;
                            else if (sName == "Mesh") cs.ShapeType = CollisionShapeType::Mesh;
                            else cs.ShapeType = CollisionShapeType::Box;
                        } else if (trimmed.rfind("HalfExtent:", 0) == 0) {
                            float x = 0.5f, y = 0.5f, z = 0.5f;
                            sscanf(trimmed.c_str(), "HalfExtent: [ %f , %f , %f ]", &x, &y, &z);
                            cs.HalfExtent = glm::vec3(x, y, z);
                        } else if (trimmed.rfind("Radius:", 0) == 0) {
                            cs.Radius = std::stof(Trim(trimmed.substr(7)));
                        } else if (trimmed.rfind("HalfHeight:", 0) == 0) {
                            cs.HalfHeight = std::stof(Trim(trimmed.substr(11)));
                        } else if (trimmed.rfind("Offset:", 0) == 0) {
                            float x = 0.0f, y = 0.0f, z = 0.0f;
                            sscanf(trimmed.c_str(), "Offset: [ %f , %f , %f ]", &x, &y, &z);
                            cs.Offset = glm::vec3(x, y, z);
                        } else if (trimmed.rfind("Friction:", 0) == 0) {
                            cs.Friction = std::stof(Trim(trimmed.substr(9)));
                        } else if (trimmed.rfind("Restitution:", 0) == 0) {
                            cs.Restitution = std::stof(Trim(trimmed.substr(12)));
                        } else if (trimmed.rfind("IsTrigger:", 0) == 0) {
                            cs.IsTrigger = (Trim(trimmed.substr(10)) == "true");
                        }
                        break;
                    }
                    case ComponentSection::Hierarchy: {
                        if (trimmed.rfind("Parent:", 0) == 0) {
                            u64 pUUID = 0;
                            try {
                                pUUID = std::stoull(Trim(trimmed.substr(7)));
                            } catch (...) {}
                            pendingHierarchy.push_back({ currentEntity, pUUID, true });
                        } else if (trimmed.rfind("InheritTransform:", 0) == 0) {
                            if (!pendingHierarchy.empty() && pendingHierarchy.back().ChildEntity == currentEntity) {
                                pendingHierarchy.back().InheritTransform = (Trim(trimmed.substr(17)) == "true");
                            }
                        }
                        break;
                    }
                    case ComponentSection::NativeScript: {
                        if (trimmed.rfind("ScriptName:", 0) == 0) {
                            std::string scriptName = Trim(trimmed.substr(11));
                            if (!currentEntity.HasComponent<NativeScriptComponent>()) {
                                currentEntity.AddComponent<NativeScriptComponent>(scriptName);
                            } else {
                                currentEntity.GetComponent<NativeScriptComponent>().Bind(scriptName);
                            }
                            if (m_Context.ScriptBinder) {
                                m_Context.ScriptBinder(currentEntity, scriptName);
                            }
                        }
                        break;
                    }
                    case ComponentSection::DirectionalLight: {
                        if (!currentEntity.HasComponent<DirectionalLightComponent>()) {
                            currentEntity.AddComponent<DirectionalLightComponent>();
                        }
                        auto& dlc = currentEntity.GetComponent<DirectionalLightComponent>();
                        if (trimmed.rfind("Color:", 0) == 0) {
                            float r = 1, g = 1, b = 1;
                            sscanf(trimmed.c_str(), "Color: [ %f , %f , %f ]", &r, &g, &b);
                            dlc.Color = glm::vec3(r, g, b);
                        } else if (trimmed.rfind("Intensity:", 0) == 0) {
                            dlc.Intensity = std::stof(Trim(trimmed.substr(10)));
                        } else if (trimmed.rfind("AmbientIntensity:", 0) == 0) {
                            dlc.AmbientIntensity = std::stof(Trim(trimmed.substr(17)));
                        } else if (trimmed.rfind("Azimuth:", 0) == 0) {
                            dlc.Azimuth = std::stof(Trim(trimmed.substr(8)));
                        } else if (trimmed.rfind("Elevation:", 0) == 0) {
                            dlc.Elevation = std::stof(Trim(trimmed.substr(10)));
                        } else if (trimmed.rfind("CastShadows:", 0) == 0) {
                            dlc.CastShadows = (Trim(trimmed.substr(12)) == "true");
                        } else if (trimmed.rfind("Exposure:", 0) == 0) {
                            dlc.Exposure = std::stof(Trim(trimmed.substr(9)));
                        } else if (trimmed.rfind("ToneMappingMode:", 0) == 0) {
                            dlc.ToneMappingMode = std::stoi(Trim(trimmed.substr(16)));
                        } else if (trimmed.rfind("Gamma:", 0) == 0) {
                            dlc.Gamma = std::stof(Trim(trimmed.substr(6)));
                        }
                        break;
                    }
                    case ComponentSection::Skybox: {
                        if (!currentEntity.HasComponent<SkyboxComponent>()) {
                            currentEntity.AddComponent<SkyboxComponent>();
                        }
                        auto& sbc = currentEntity.GetComponent<SkyboxComponent>();
                        if (trimmed.rfind("Enabled:", 0) == 0) {
                            sbc.Enabled = (Trim(trimmed.substr(8)) == "true");
                        } else if (trimmed.rfind("ZenithColor:", 0) == 0) {
                            float r = 0.18f, g = 0.42f, b = 0.78f;
                            sscanf(trimmed.c_str(), "ZenithColor: [ %f , %f , %f ]", &r, &g, &b);
                            sbc.ZenithColor = glm::vec3(r, g, b);
                        } else if (trimmed.rfind("HorizonColor:", 0) == 0) {
                            float r = 0.70f, g = 0.82f, b = 0.95f;
                            sscanf(trimmed.c_str(), "HorizonColor: [ %f , %f , %f ]", &r, &g, &b);
                            sbc.HorizonColor = glm::vec3(r, g, b);
                        } else if (trimmed.rfind("GroundColor:", 0) == 0) {
                            float r = 0.18f, g = 0.16f, b = 0.14f;
                            sscanf(trimmed.c_str(), "GroundColor: [ %f , %f , %f ]", &r, &g, &b);
                            sbc.GroundColor = glm::vec3(r, g, b);
                        } else if (trimmed.rfind("SunSize:", 0) == 0) {
                            sbc.SunSize = std::stof(Trim(trimmed.substr(8)));
                        } else if (trimmed.rfind("SunGlow:", 0) == 0) {
                            sbc.SunGlow = std::stof(Trim(trimmed.substr(8)));
                        } else if (trimmed.rfind("Turbidity:", 0) == 0) {
                            sbc.Turbidity = std::stof(Trim(trimmed.substr(10)));
                        } else if (trimmed.rfind("SyncWithSun:", 0) == 0) {
                            sbc.SyncWithSun = (Trim(trimmed.substr(12)) == "true");
                        } else if (trimmed.rfind("CustomAzimuth:", 0) == 0) {
                            sbc.CustomAzimuth = std::stof(Trim(trimmed.substr(14)));
                        } else if (trimmed.rfind("CustomElevation:", 0) == 0) {
                            sbc.CustomElevation = std::stof(Trim(trimmed.substr(16)));
                        } else if (trimmed.rfind("HemisphereAmbient:", 0) == 0) {
                            sbc.HemisphereAmbient = (Trim(trimmed.substr(18)) == "true");
                        }
                        break;
                    }
                    case ComponentSection::Material: {
                        if (!currentEntity.HasComponent<MaterialComponent>()) {
                            currentEntity.AddComponent<MaterialComponent>();
                        }
                        auto& mat = currentEntity.GetComponent<MaterialComponent>();
                        if (trimmed.rfind("AlbedoPath:", 0) == 0) {
                            std::string p = Trim(trimmed.substr(11));
                            if (!p.empty()) mat.SetAlbedo(p);
                        } else if (trimmed.rfind("UseAlbedoMap:", 0) == 0) {
                            mat.UseAlbedoMap = (Trim(trimmed.substr(13)) == "true");
                        } else if (trimmed.rfind("AlbedoColor:", 0) == 0) {
                            float r = 1, g = 1, b = 1, a = 1;
                            sscanf(trimmed.c_str(), "AlbedoColor: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                            mat.AlbedoColor = glm::vec4(r, g, b, a);
                        } else if (trimmed.rfind("NormalPath:", 0) == 0) {
                            std::string p = Trim(trimmed.substr(11));
                            if (!p.empty()) mat.SetNormal(p);
                        } else if (trimmed.rfind("UseNormalMap:", 0) == 0) {
                            mat.UseNormalMap = (Trim(trimmed.substr(13)) == "true");
                        } else if (trimmed.rfind("NormalStrength:", 0) == 0) {
                            mat.NormalStrength = std::stof(Trim(trimmed.substr(15)));
                        } else if (trimmed.rfind("RoughnessPath:", 0) == 0) {
                            std::string p = Trim(trimmed.substr(14));
                            if (!p.empty()) mat.SetRoughnessMap(p);
                        } else if (trimmed.rfind("UseRoughnessMap:", 0) == 0) {
                            mat.UseRoughnessMap = (Trim(trimmed.substr(16)) == "true");
                        } else if (trimmed.rfind("Roughness:", 0) == 0) {
                            mat.Roughness = std::stof(Trim(trimmed.substr(10)));
                        } else if (trimmed.rfind("MetallicPath:", 0) == 0) {
                            std::string p = Trim(trimmed.substr(13));
                            if (!p.empty()) mat.SetMetallicMap(p);
                        } else if (trimmed.rfind("UseMetallicMap:", 0) == 0) {
                            mat.UseMetallicMap = (Trim(trimmed.substr(15)) == "true");
                        } else if (trimmed.rfind("Metallic:", 0) == 0) {
                            mat.Metallic = std::stof(Trim(trimmed.substr(9)));
                        } else if (trimmed.rfind("EmissionColor:", 0) == 0) {
                            float r = 0, g = 0, b = 0;
                            sscanf(trimmed.c_str(), "EmissionColor: [ %f , %f , %f ]", &r, &g, &b);
                            mat.EmissionColor = glm::vec3(r, g, b);
                        } else if (trimmed.rfind("EmissionIntensity:", 0) == 0) {
                            mat.EmissionIntensity = std::stof(Trim(trimmed.substr(18)));
                        } else if (trimmed.rfind("UVScale:", 0) == 0) {
                            float u = 1, v = 1;
                            sscanf(trimmed.c_str(), "UVScale: [ %f , %f ]", &u, &v);
                            mat.UVScale = glm::vec2(u, v);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        // Recreate Physics Bodies in PhysicsScene if active
        if (m_PhysicsScene) {
            auto rbView = m_Scene->GetRegistry().view<RigidBody3D, TransformComponent>();
            for (auto entityID : rbView) {
                auto& rb = rbView.get<RigidBody3D>(entityID);
                auto& tc = rbView.get<TransformComponent>(entityID);

                if (m_Scene->GetRegistry().all_of<TagComponent>(entityID)) {
                    if (m_Scene->GetRegistry().get<TagComponent>(entityID).Tag == "Vehicle Chassis") {
                        continue;
                    }
                }

                // Retrieve or initialize CollisionShape3D
                CollisionShapeType shapeType = CollisionShapeType::Box;
                glm::vec3 halfExtent = glm::max(tc.Scale * 0.5f, glm::vec3(0.01f));
                float radius = glm::max(tc.Scale.x * 0.5f, 0.01f);
                float halfHeight = glm::max(tc.Scale.y * 0.5f, 0.01f);
                float friction = 0.5f;
                float restitution = 0.0f;
                glm::vec3 offset(0.0f);

                if (m_Scene->GetRegistry().all_of<CollisionShape3D>(entityID)) {
                    auto& cs = m_Scene->GetRegistry().get<CollisionShape3D>(entityID);
                    shapeType = cs.ShapeType;
                    halfExtent = cs.HalfExtent;
                    radius = cs.Radius;
                    halfHeight = cs.HalfHeight;
                    friction = cs.Friction;
                    restitution = cs.Restitution;
                    offset = cs.Offset;
                } else {
                    // Create default CollisionShape3D matching mesh or box
                    if (m_Scene->GetRegistry().all_of<MeshInstance3D>(entityID)) {
                        auto& mc = m_Scene->GetRegistry().get<MeshInstance3D>(entityID);
                        std::string aPath = mc.AssetPath;
                        if (aPath.empty() && mc.VAO) {
                            if (m_Context.CubeMesh && mc.VAO == m_Context.CubeMesh) aPath = "builtin://cube";
                            else if (m_Context.SphereMesh && mc.VAO == m_Context.SphereMesh) aPath = "builtin://sphere";
                            else if (m_Context.CylinderMesh && mc.VAO == m_Context.CylinderMesh) aPath = "builtin://cylinder";
                            else if (m_Context.ConeMesh && mc.VAO == m_Context.ConeMesh) aPath = "builtin://cone";
                            else if (m_Context.PlaneMesh && mc.VAO == m_Context.PlaneMesh) aPath = "builtin://plane";
                        }
                        if (aPath == "builtin://plane") shapeType = CollisionShapeType::Plane;
                        else if (aPath == "builtin://sphere") shapeType = CollisionShapeType::Sphere;
                        else if (aPath == "builtin://cylinder") shapeType = CollisionShapeType::Cylinder;
                        else if (aPath == "builtin://cone") shapeType = CollisionShapeType::Cone;
                        else shapeType = CollisionShapeType::Box;
                    }
                    m_Scene->GetRegistry().emplace<CollisionShape3D>(entityID, shapeType, halfExtent, friction, restitution);
                }

                BodyID bodyID;
                glm::vec3 spawnPos = tc.Translation + tc.Rotation * offset;
                bool isDynamic = (rb.MotionType == BodyMotionType::Dynamic);
                switch (shapeType) {
                    case CollisionShapeType::Plane:
                        bodyID = m_PhysicsScene->CreatePlane(spawnPos, glm::vec3(0, 1, 0), tc.Scale.x * 0.5f, friction, restitution);
                        break;
                    case CollisionShapeType::Sphere:
                        bodyID = m_PhysicsScene->CreateSphere(spawnPos, radius, isDynamic, friction, restitution);
                        break;
                    case CollisionShapeType::Cylinder:
                        bodyID = m_PhysicsScene->CreateCylinder(spawnPos, halfHeight, radius, isDynamic, friction, restitution);
                        break;
                    case CollisionShapeType::Cone:
                        bodyID = m_PhysicsScene->CreateCone(spawnPos, halfHeight, radius, isDynamic, friction, restitution);
                        break;
                    case CollisionShapeType::Box:
                    default:
                        bodyID = m_PhysicsScene->CreateBox(spawnPos, halfExtent, isDynamic, friction, restitution);
                        break;
                }

                if (!bodyID.IsInvalid()) {
                    m_PhysicsScene->SetBodyPositionAndRotation(bodyID, spawnPos, tc.Rotation);
                    rb.RuntimeBodyID = bodyID;
                }
            }

            m_PhysicsScene->OptimizeBroadPhase();
        }

        // Resolve Hierarchies
        std::unordered_map<u64, Entity> entityMap;
        auto idView = m_Scene->GetRegistry().view<IDComponent>();
        for (auto entityID : idView) {
            entityMap[idView.get<IDComponent>(entityID).ID] = Entity{ entityID, m_Scene };
        }

        for (const auto& item : pendingHierarchy) {
            if (item.ParentUUID != 0 && entityMap.find(item.ParentUUID) != entityMap.end()) {
                Entity parentEntity = entityMap[item.ParentUUID];
                Entity childEntity = item.ChildEntity;
                auto& childHc = childEntity.AddComponent<HierarchyComponent>(
                    static_cast<entt::entity>(parentEntity), item.InheritTransform);

                if (!parentEntity.HasComponent<HierarchyComponent>()) {
                    parentEntity.AddComponent<HierarchyComponent>();
                }
                parentEntity.GetComponent<HierarchyComponent>().Children.push_back(
                    static_cast<entt::entity>(childEntity));
            }
        }

        return true;
    }

}
