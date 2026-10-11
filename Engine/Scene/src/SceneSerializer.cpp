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

            // MeshComponent
            if (entity.HasComponent<MeshComponent>()) {
                auto& mc = entity.GetComponent<MeshComponent>();
                std::string assetPath = mc.AssetPath;

                if (assetPath.empty() && mc.VAO) {
                    if (m_Context.CubeMesh && mc.VAO == m_Context.CubeMesh) assetPath = "builtin://cube";
                    else if (m_Context.SphereMesh && mc.VAO == m_Context.SphereMesh) assetPath = "builtin://sphere";
                    else if (m_Context.CylinderMesh && mc.VAO == m_Context.CylinderMesh) assetPath = "builtin://cylinder";
                    else if (m_Context.ConeMesh && mc.VAO == m_Context.ConeMesh) assetPath = "builtin://cone";
                    else if (m_Context.PlaneMesh && mc.VAO == m_Context.PlaneMesh) assetPath = "builtin://plane";
                }

                ss << "    MeshComponent:\n";
                ss << "      AssetPath: " << assetPath << "\n";
                ss << "      PatternMode: " << mc.PatternMode << "\n";
                ss << "      TileScale: " << mc.TileScale << "\n";
                ss << "      Color1: [ " << mc.Color1.r << " , " << mc.Color1.g << " , " << mc.Color1.b << " , " << mc.Color1.a << " ]\n";
                ss << "      Color2: [ " << mc.Color2.r << " , " << mc.Color2.g << " , " << mc.Color2.b << " , " << mc.Color2.a << " ]\n";
            }

            // RigidBodyComponent
            if (entity.HasComponent<RigidBodyComponent>()) {
                auto& rb = entity.GetComponent<RigidBodyComponent>();
                bool isDynamic = rb.IsDynamic;
                float friction = rb.Friction;
                float restitution = rb.Restitution;

                if (m_PhysicsScene && !rb.RuntimeBodyID.IsInvalid()) {
                    isDynamic = (m_PhysicsScene->GetBodyInterface().GetMotionType(rb.RuntimeBodyID) == JPH::EMotionType::Dynamic);
                    friction = m_PhysicsScene->GetBodyInterface().GetFriction(rb.RuntimeBodyID);
                    restitution = m_PhysicsScene->GetBodyInterface().GetRestitution(rb.RuntimeBodyID);
                }

                std::string shapeName = "Box";
                if (rb.ShapeType == BodyShapeType::Plane) shapeName = "Plane";
                else if (rb.ShapeType == BodyShapeType::Sphere) shapeName = "Sphere";
                else if (rb.ShapeType == BodyShapeType::Cylinder) shapeName = "Cylinder";
                else if (rb.ShapeType == BodyShapeType::Cone) shapeName = "Cone";
                else if (entity.HasComponent<MeshComponent>()) {
                    auto& mc = entity.GetComponent<MeshComponent>();
                    std::string aPath = mc.AssetPath;
                    if (aPath.empty() && mc.VAO) {
                        if (m_Context.CubeMesh && mc.VAO == m_Context.CubeMesh) aPath = "builtin://cube";
                        else if (m_Context.SphereMesh && mc.VAO == m_Context.SphereMesh) aPath = "builtin://sphere";
                        else if (m_Context.CylinderMesh && mc.VAO == m_Context.CylinderMesh) aPath = "builtin://cylinder";
                        else if (m_Context.ConeMesh && mc.VAO == m_Context.ConeMesh) aPath = "builtin://cone";
                        else if (m_Context.PlaneMesh && mc.VAO == m_Context.PlaneMesh) aPath = "builtin://plane";
                    }
                    if (aPath == "builtin://plane") shapeName = "Plane";
                    else if (aPath == "builtin://sphere") shapeName = "Sphere";
                    else if (aPath == "builtin://cylinder") shapeName = "Cylinder";
                    else if (aPath == "builtin://cone") shapeName = "Cone";
                    else shapeName = "Box";
                }

                glm::vec3 halfExtent = rb.HalfExtent;
                float radius = rb.Radius;
                float halfHeight = rb.HalfHeight;

                if (entity.HasComponent<TransformComponent>()) {
                    auto& tc = entity.GetComponent<TransformComponent>();
                    halfExtent = glm::max(tc.Scale * 0.5f, glm::vec3(0.01f));
                    radius = glm::max(tc.Scale.x * 0.5f, 0.01f);
                    halfHeight = glm::max(tc.Scale.y * 0.5f, 0.01f);
                }

                ss << "    RigidBodyComponent:\n";
                ss << "      ShapeType: " << shapeName << "\n";
                ss << "      IsDynamic: " << (isDynamic ? "true" : "false") << "\n";
                ss << "      Friction: " << friction << "\n";
                ss << "      Restitution: " << restitution << "\n";
                ss << "      HalfExtent: [ " << halfExtent.x << " , " << halfExtent.y << " , " << halfExtent.z << " ]\n";
                ss << "      Radius: " << radius << "\n";
                ss << "      HalfHeight: " << halfHeight << "\n";
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
            RigidBody,
            Hierarchy,
            NativeScript,
            DirectionalLight
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
            } else if (trimmed == "MeshComponent:") {
                currentSection = ComponentSection::Mesh;
            } else if (trimmed == "RigidBodyComponent:") {
                currentSection = ComponentSection::RigidBody;
            } else if (trimmed == "HierarchyComponent:") {
                currentSection = ComponentSection::Hierarchy;
            } else if (trimmed == "NativeScriptComponent:") {
                currentSection = ComponentSection::NativeScript;
            } else if (trimmed == "DirectionalLightComponent:") {
                currentSection = ComponentSection::DirectionalLight;
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
                        if (!currentEntity.HasComponent<MeshComponent>()) {
                            currentEntity.AddComponent<MeshComponent>();
                        }
                        auto& mc = currentEntity.GetComponent<MeshComponent>();
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
                        if (!currentEntity.HasComponent<RigidBodyComponent>()) {
                            currentEntity.AddComponent<RigidBodyComponent>();
                        }
                        auto& rb = currentEntity.GetComponent<RigidBodyComponent>();
                        if (trimmed.rfind("ShapeType:", 0) == 0) {
                            std::string sName = Trim(trimmed.substr(10));
                            if (sName == "Plane") rb.ShapeType = BodyShapeType::Plane;
                            else if (sName == "Sphere") rb.ShapeType = BodyShapeType::Sphere;
                            else if (sName == "Cylinder") rb.ShapeType = BodyShapeType::Cylinder;
                            else if (sName == "Cone") rb.ShapeType = BodyShapeType::Cone;
                            else rb.ShapeType = BodyShapeType::Box;
                        } else if (trimmed.rfind("IsDynamic:", 0) == 0) {
                            rb.IsDynamic = (Trim(trimmed.substr(10)) == "true");
                        } else if (trimmed.rfind("Friction:", 0) == 0) {
                            rb.Friction = std::stof(Trim(trimmed.substr(9)));
                        } else if (trimmed.rfind("Restitution:", 0) == 0) {
                            rb.Restitution = std::stof(Trim(trimmed.substr(12)));
                        } else if (trimmed.rfind("HalfExtent:", 0) == 0) {
                            float x = 0.5f, y = 0.5f, z = 0.5f;
                            sscanf(trimmed.c_str(), "HalfExtent: [ %f , %f , %f ]", &x, &y, &z);
                            rb.HalfExtent = glm::vec3(x, y, z);
                        } else if (trimmed.rfind("Radius:", 0) == 0) {
                            rb.Radius = std::stof(Trim(trimmed.substr(7)));
                        } else if (trimmed.rfind("HalfHeight:", 0) == 0) {
                            rb.HalfHeight = std::stof(Trim(trimmed.substr(11)));
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
            auto rbView = m_Scene->GetRegistry().view<RigidBodyComponent, TransformComponent>();
            for (auto entityID : rbView) {
                auto& rb = rbView.get<RigidBodyComponent>(entityID);
                auto& tc = rbView.get<TransformComponent>(entityID);

                if (m_Scene->GetRegistry().all_of<TagComponent>(entityID)) {
                    if (m_Scene->GetRegistry().get<TagComponent>(entityID).Tag == "Vehicle Chassis") {
                        continue;
                    }
                }

                BodyID bodyID;
                switch (rb.ShapeType) {
                    case BodyShapeType::Plane:
                        bodyID = m_PhysicsScene->CreatePlane(tc.Translation, glm::vec3(0, 1, 0), tc.Scale.x * 0.5f, rb.Friction, rb.Restitution);
                        break;
                    case BodyShapeType::Sphere:
                        bodyID = m_PhysicsScene->CreateSphere(tc.Translation, rb.Radius, rb.IsDynamic, rb.Friction, rb.Restitution);
                        break;
                    case BodyShapeType::Cylinder:
                        bodyID = m_PhysicsScene->CreateCylinder(tc.Translation, rb.HalfHeight, rb.Radius, rb.IsDynamic, rb.Friction, rb.Restitution);
                        break;
                    case BodyShapeType::Cone:
                        bodyID = m_PhysicsScene->CreateCone(tc.Translation, rb.HalfHeight, rb.Radius, rb.IsDynamic, rb.Friction, rb.Restitution);
                        break;
                    case BodyShapeType::Box:
                    default:
                        bodyID = m_PhysicsScene->CreateBox(tc.Translation, rb.HalfExtent, rb.IsDynamic, rb.Friction, rb.Restitution);
                        break;
                }

                if (!bodyID.IsInvalid()) {
                    m_PhysicsScene->SetBodyPositionAndRotation(bodyID, tc.Translation, tc.Rotation);
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
