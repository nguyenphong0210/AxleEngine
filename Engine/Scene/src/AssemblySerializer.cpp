#include <Axle/AssemblySerializer.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include <queue>

namespace Axle {

    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    bool AssemblySerializer::IsAssemblyFile(const std::filesystem::path& filepath) {
        if (!std::filesystem::exists(filepath)) return false;

        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        std::string line;
        int count = 0;
        while (std::getline(file, line) && count++ < 30) {
            std::string trimmed = Trim(line);
            if (trimmed.rfind("AssetType:", 0) == 0) {
                std::string val = Trim(trimmed.substr(10));
                return (val == "Assembly");
            }
        }
        return false;
    }

    static void CollectHierarchy(Entity current, std::vector<Entity>& outEntities) {
        if (!current) return;
        outEntities.push_back(current);

        if (current.HasComponent<HierarchyComponent>()) {
            auto& hc = current.GetComponent<HierarchyComponent>();
            auto& reg = current.GetScene()->GetRegistry();
            for (auto childHandle : hc.Children) {
                if (childHandle != entt::null && reg.valid(childHandle)) {
                    Entity childEntity{ childHandle, current.GetScene() };
                    CollectHierarchy(childEntity, outEntities);
                }
            }
        }
    }

    bool AssemblySerializer::Serialize(const std::filesystem::path& filepath,
                                       Entity rootEntity,
                                       PhysicsScene* physicsScene,
                                       const SceneSerializerContext* context) {
        if (!rootEntity) {
            AXLE_ERROR("AssemblySerializer: rootEntity is invalid");
            return false;
        }

        std::ofstream fout(filepath);
        if (!fout.is_open()) {
            AXLE_ERROR("AssemblySerializer: Failed to open file for writing: '{0}'", filepath.string());
            return false;
        }

        std::vector<Entity> hierarchy;
        CollectHierarchy(rootEntity, hierarchy);

        // Ensure all entities have IDComponent
        for (auto& e : hierarchy) {
            if (!e.HasComponent<IDComponent>()) {
                e.AddComponent<IDComponent>(UUID());
            }
        }

        u64 rootUUID = rootEntity.GetComponent<IDComponent>().ID;
        std::string rootName = rootEntity.HasComponent<TagComponent>() ? 
                               rootEntity.GetComponent<TagComponent>().Tag : filepath.stem().string();

        fout << std::fixed << std::setprecision(6);
        fout << "# Axle Unified Asset File\n";
        fout << "AxleAsset:\n";
        fout << "  AssetType: Assembly\n";
        fout << "  Version: 1\n";
        fout << "  Name: " << rootName << "\n";
        fout << "  RootEntityID: " << rootUUID << "\n\n";

        fout << "Entities:\n";

        auto& reg = rootEntity.GetScene()->GetRegistry();

        for (auto& entity : hierarchy) {
            u64 uuid = entity.GetComponent<IDComponent>().ID;
            fout << "  - Entity: " << uuid << "\n";

            // TagComponent
            if (entity.HasComponent<TagComponent>()) {
                fout << "    TagComponent:\n";
                fout << "      Tag: " << entity.GetComponent<TagComponent>().Tag << "\n";
            }

            // TransformComponent (local coordinates)
            if (entity.HasComponent<TransformComponent>()) {
                auto& tc = entity.GetComponent<TransformComponent>();
                fout << "    TransformComponent:\n";
                fout << "      Translation: [ " << tc.Translation.x << " , " << tc.Translation.y << " , " << tc.Translation.z << " ]\n";
                fout << "      Rotation: [ " << tc.Rotation.w << " , " << tc.Rotation.x << " , " << tc.Rotation.y << " , " << tc.Rotation.z << " ]\n";
                fout << "      Scale: [ " << tc.Scale.x << " , " << tc.Scale.y << " , " << tc.Scale.z << " ]\n";
            }

            // MeshInstance3D
            if (entity.HasComponent<MeshInstance3D>()) {
                auto& mc = entity.GetComponent<MeshInstance3D>();
                std::string assetPath = mc.AssetPath;
                if (assetPath.empty() && mc.VAO && context) {
                    if (context->CubeMesh && mc.VAO == context->CubeMesh) assetPath = "builtin://cube";
                    else if (context->SphereMesh && mc.VAO == context->SphereMesh) assetPath = "builtin://sphere";
                    else if (context->CylinderMesh && mc.VAO == context->CylinderMesh) assetPath = "builtin://cylinder";
                    else if (context->ConeMesh && mc.VAO == context->ConeMesh) assetPath = "builtin://cone";
                    else if (context->PlaneMesh && mc.VAO == context->PlaneMesh) assetPath = "builtin://plane";
                }

                fout << "    MeshInstance3D:\n";
                fout << "      AssetPath: " << assetPath << "\n";
                fout << "      PatternMode: " << mc.PatternMode << "\n";
                fout << "      TileScale: " << mc.TileScale << "\n";
                fout << "      Color1: [ " << mc.Color1.r << " , " << mc.Color1.g << " , " << mc.Color1.b << " , " << mc.Color1.a << " ]\n";
                fout << "      Color2: [ " << mc.Color2.r << " , " << mc.Color2.g << " , " << mc.Color2.b << " , " << mc.Color2.a << " ]\n";
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
                if (entity.HasComponent<RigidBody3D>() && physicsScene) {
                    auto& rb = entity.GetComponent<RigidBody3D>();
                    if (!rb.RuntimeBodyID.IsInvalid() && physicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                        friction = physicsScene->GetBodyInterface().GetFriction(rb.RuntimeBodyID);
                        restitution = physicsScene->GetBodyInterface().GetRestitution(rb.RuntimeBodyID);
                    }
                }

                fout << "    CollisionShape3D:\n";
                fout << "      ShapeType: " << shapeName << "\n";
                fout << "      HalfExtent: [ " << cs.HalfExtent.x << " , " << cs.HalfExtent.y << " , " << cs.HalfExtent.z << " ]\n";
                fout << "      Radius: " << cs.Radius << "\n";
                fout << "      HalfHeight: " << cs.HalfHeight << "\n";
                fout << "      Offset: [ " << cs.Offset.x << " , " << cs.Offset.y << " , " << cs.Offset.z << " ]\n";
                fout << "      Friction: " << friction << "\n";
                fout << "      Restitution: " << restitution << "\n";
                fout << "      IsTrigger: " << (cs.IsTrigger ? "true" : "false") << "\n";
            }

            // RigidBody3D
            if (entity.HasComponent<RigidBody3D>()) {
                auto& rb = entity.GetComponent<RigidBody3D>();
                bool isDynamic = rb.IsDynamic;
                if (physicsScene && !rb.RuntimeBodyID.IsInvalid() && physicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                    isDynamic = (physicsScene->GetBodyInterface().GetMotionType(rb.RuntimeBodyID) == JPH::EMotionType::Dynamic);
                }

                std::string motionStr = isDynamic ? "Dynamic" : "Static";
                if (rb.MotionType == BodyMotionType::Kinematic) motionStr = "Kinematic";

                fout << "    RigidBody3D:\n";
                fout << "      MotionType: " << motionStr << "\n";
                fout << "      IsDynamic: " << (isDynamic ? "true" : "false") << "\n";
                fout << "      Mass: " << rb.Mass << "\n";
                fout << "      LinearDamping: " << rb.LinearDamping << "\n";
                fout << "      AngularDamping: " << rb.AngularDamping << "\n";
                fout << "      GravityFactor: " << rb.GravityFactor << "\n";
            }

            // MaterialComponent
            if (entity.HasComponent<MaterialComponent>()) {
                auto& mat = entity.GetComponent<MaterialComponent>();
                fout << "    MaterialComponent:\n";
                if (mat.HasAsset()) {
                    fout << "      MaterialHandle: " << (uint64_t)mat.MaterialHandle << "\n";
                    if (!mat.MaterialAsset->GetFilePath().empty()) {
                        fout << "      AssetPath: " << mat.MaterialAsset->GetFilePath().string() << "\n";
                    }
                }
                fout << "      AlbedoPath: " << mat.AlbedoPath << "\n";
                fout << "      UseAlbedoMap: " << (mat.UseAlbedoMap ? "true" : "false") << "\n";
                fout << "      AlbedoColor: [ " << mat.AlbedoColor.r << " , " << mat.AlbedoColor.g << " , " << mat.AlbedoColor.b << " , " << mat.AlbedoColor.a << " ]\n";
                fout << "      NormalPath: " << mat.NormalPath << "\n";
                fout << "      UseNormalMap: " << (mat.UseNormalMap ? "true" : "false") << "\n";
                fout << "      NormalStrength: " << mat.NormalStrength << "\n";
                fout << "      RoughnessPath: " << mat.RoughnessPath << "\n";
                fout << "      UseRoughnessMap: " << (mat.UseRoughnessMap ? "true" : "false") << "\n";
                fout << "      Roughness: " << mat.Roughness << "\n";
                fout << "      MetallicPath: " << mat.MetallicPath << "\n";
                fout << "      UseMetallicMap: " << (mat.UseMetallicMap ? "true" : "false") << "\n";
                fout << "      Metallic: " << mat.Metallic << "\n";
                fout << "      EmissionColor: [ " << mat.EmissionColor.r << " , " << mat.EmissionColor.g << " , " << mat.EmissionColor.b << " ]\n";
                fout << "      EmissionIntensity: " << mat.EmissionIntensity << "\n";
                fout << "      UVScale: [ " << mat.UVScale.x << " , " << mat.UVScale.y << " ]\n";
            }

            // NativeScriptComponent
            if (entity.HasComponent<NativeScriptComponent>()) {
                auto& nsc = entity.GetComponent<NativeScriptComponent>();
                fout << "    NativeScriptComponent:\n";
                fout << "      ScriptName: " << nsc.ScriptName << "\n";
            }

            // HierarchyComponent
            if (entity.HasComponent<HierarchyComponent>()) {
                auto& hc = entity.GetComponent<HierarchyComponent>();
                u64 parentUUID = 0;
                if (hc.Parent != entt::null && reg.valid(hc.Parent) && reg.all_of<IDComponent>(hc.Parent)) {
                    parentUUID = reg.get<IDComponent>(hc.Parent).ID;
                }

                fout << "    HierarchyComponent:\n";
                fout << "      Parent: " << parentUUID << "\n";
                fout << "      Children:\n";
                for (auto childH : hc.Children) {
                    if (childH != entt::null && reg.valid(childH) && reg.all_of<IDComponent>(childH)) {
                        fout << "        - " << reg.get<IDComponent>(childH).ID << "\n";
                    }
                }
            }

            // CameraComponent
            if (entity.HasComponent<CameraComponent>()) {
                auto& cam = entity.GetComponent<CameraComponent>();
                fout << "    CameraComponent:\n";
                fout << "      Primary: " << (cam.Primary ? "true" : "false") << "\n";
                fout << "      FOV: " << cam.FOV << "\n";
                fout << "      NearClip: " << cam.NearClip << "\n";
                fout << "      FarClip: " << cam.FarClip << "\n";
            }

            // AudioListenerComponent
            if (entity.HasComponent<AudioListenerComponent>()) {
                auto& alc = entity.GetComponent<AudioListenerComponent>();
                fout << "    AudioListenerComponent:\n";
                fout << "      Active: " << (alc.Active ? "true" : "false") << "\n";
            }

            // AudioSourceComponent
            if (entity.HasComponent<AudioSourceComponent>()) {
                auto& asc = entity.GetComponent<AudioSourceComponent>();
                fout << "    AudioSourceComponent:\n";
                fout << "      AssetPath: " << asc.AssetPath << "\n";
                fout << "      Volume: " << asc.Volume << "\n";
                fout << "      Pitch: " << asc.Pitch << "\n";
                fout << "      Loop: " << (asc.Loop ? "true" : "false") << "\n";
                fout << "      PlayOnAwake: " << (asc.PlayOnAwake ? "true" : "false") << "\n";
                fout << "      Spatialized: " << (asc.Spatialized ? "true" : "false") << "\n";
                fout << "      AttenuationModel: " << (int)asc.Attenuation << "\n";
                fout << "      MinDistance: " << asc.MinDistance << "\n";
                fout << "      MaxDistance: " << asc.MaxDistance << "\n";
                fout << "      RollOff: " << asc.RollOff << "\n";
                fout << "      InnerAngle: " << asc.InnerAngle << "\n";
                fout << "      OuterAngle: " << asc.OuterAngle << "\n";
                fout << "      OuterGain: " << asc.OuterGain << "\n";
                fout << "      DopplerFactor: " << asc.DopplerFactor << "\n";
            }

            // VehicleAudioComponent
            if (entity.HasComponent<VehicleAudioComponent>()) {
                auto& vac = entity.GetComponent<VehicleAudioComponent>();
                fout << "    VehicleAudioComponent:\n";
                fout << "      Enabled: " << (vac.Enabled ? "true" : "false") << "\n";
                fout << "      EnginePowerClipPath: " << vac.EnginePowerClipPath << "\n";
                fout << "      EngineCoastClipPath: " << vac.EngineCoastClipPath << "\n";
                fout << "      MinEnginePitch: " << vac.MinEnginePitch << "\n";
                fout << "      MaxEnginePitch: " << vac.MaxEnginePitch << "\n";
                fout << "      EngineMasterVolume: " << vac.EngineMasterVolume << "\n";
                fout << "      ThrottleSmoothing: " << vac.ThrottleSmoothing << "\n";
                fout << "      TireSquealClipPath: " << vac.TireSquealClipPath << "\n";
                fout << "      SlipThreshold: " << vac.SlipThreshold << "\n";
                fout << "      TireSquealMasterVolume: " << vac.TireSquealMasterVolume << "\n";
                fout << "      TireSquealFadeSpeed: " << vac.TireSquealFadeSpeed << "\n";
                fout << "      SuspensionThudClipPath: " << vac.SuspensionThudClipPath << "\n";
                fout << "      SuspensionThudThreshold: " << vac.SuspensionThudThreshold << "\n";
                fout << "      SuspensionMasterVolume: " << vac.SuspensionMasterVolume << "\n";
                fout << "      CollisionImpactClipPath: " << vac.CollisionImpactClipPath << "\n";
                fout << "      CollisionDecelThreshold: " << vac.CollisionDecelThreshold << "\n";
                fout << "      CollisionMasterVolume: " << vac.CollisionMasterVolume << "\n";
                fout << "      Spatialized: " << (vac.Spatialized ? "true" : "false") << "\n";
                fout << "      MinDistance: " << vac.MinDistance << "\n";
                fout << "      MaxDistance: " << vac.MaxDistance << "\n";
            }
        }

        fout.close();
        AXLE_INFO("AssemblySerializer: Successfully exported Assembly '{0}' ({1} entities) to '{2}'", 
                  rootName, hierarchy.size(), filepath.string());
        return true;
    }

    struct RawAssemblyEntity {
        u64 OldUUID = 0;
        std::string Tag = "Entity";
        TransformComponent Transform;
        bool HasTransform = false;

        bool HasMesh = false;
        std::string MeshAssetPath;
        int MeshPattern = 1;
        float MeshTileScale = 8.0f;
        glm::vec4 MeshColor1{ 0.85f, 0.88f, 0.92f, 1.0f };
        glm::vec4 MeshColor2{ 0.65f, 0.70f, 0.76f, 1.0f };

        bool HasCollisionShape = false;
        CollisionShape3D CollisionShape;

        bool HasRigidBody = false;
        RigidBody3D RigidBody;

        bool HasMaterial = false;
        MaterialComponent Material;

        bool HasNativeScript = false;
        std::string ScriptName;

        bool HasCamera = false;
        CameraComponent Camera;

        bool HasAudioListener = false;
        AudioListenerComponent AudioListener;

        bool HasAudioSource = false;
        AudioSourceComponent AudioSource;

        bool HasVehicleAudio = false;
        VehicleAudioComponent VehicleAudio;

        bool HasHierarchy = false;
        u64 ParentOldUUID = 0;
        std::vector<u64> ChildrenOldUUIDs;
    };

    enum class AsmSection {
        None,
        Tag,
        Transform,
        Mesh,
        CollisionShape,
        RigidBody,
        Material,
        NativeScript,
        Camera,
        AudioListener,
        AudioSource,
        VehicleAudio,
        Hierarchy,
        HierarchyChildren
    };

    static std::vector<RawAssemblyEntity> ParseAssemblyEntities(std::istream& stream, u64& outRootUUID) {
        std::vector<RawAssemblyEntity> entities;
        std::string line;
        RawAssemblyEntity current;
        bool inEntity = false;
        AsmSection currentSection = AsmSection::None;

        while (std::getline(stream, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.rfind("RootEntityID:", 0) == 0) {
                try {
                    outRootUUID = std::stoull(Trim(trimmed.substr(13)));
                } catch (...) {
                    outRootUUID = 0;
                }
                continue;
            }

            if (trimmed.rfind("- Entity:", 0) == 0) {
                if (inEntity) {
                    entities.push_back(current);
                }
                current = RawAssemblyEntity{};
                try {
                    current.OldUUID = std::stoull(Trim(trimmed.substr(9)));
                } catch (...) {
                    current.OldUUID = 0;
                }
                inEntity = true;
                currentSection = AsmSection::None;
                continue;
            }

            if (!inEntity) continue;

            // Section markers
            if (trimmed == "TagComponent:") { currentSection = AsmSection::Tag; continue; }
            if (trimmed == "TransformComponent:") { currentSection = AsmSection::Transform; current.HasTransform = true; continue; }
            if (trimmed == "MeshInstance3D:" || trimmed == "MeshComponent:") { currentSection = AsmSection::Mesh; current.HasMesh = true; continue; }
            if (trimmed == "CollisionShape3D:" || trimmed == "CollisionShape:") { currentSection = AsmSection::CollisionShape; current.HasCollisionShape = true; continue; }
            if (trimmed == "RigidBody3D:" || trimmed == "RigidBodyComponent:") { currentSection = AsmSection::RigidBody; current.HasRigidBody = true; continue; }
            if (trimmed == "MaterialComponent:") { currentSection = AsmSection::Material; current.HasMaterial = true; continue; }
            if (trimmed == "NativeScriptComponent:") { currentSection = AsmSection::NativeScript; current.HasNativeScript = true; continue; }
            if (trimmed == "CameraComponent:") { currentSection = AsmSection::Camera; current.HasCamera = true; continue; }
            if (trimmed == "AudioListenerComponent:") { currentSection = AsmSection::AudioListener; current.HasAudioListener = true; continue; }
            if (trimmed == "AudioSourceComponent:") { currentSection = AsmSection::AudioSource; current.HasAudioSource = true; continue; }
            if (trimmed == "VehicleAudioComponent:") { currentSection = AsmSection::VehicleAudio; current.HasVehicleAudio = true; continue; }
            if (trimmed == "HierarchyComponent:") { currentSection = AsmSection::Hierarchy; current.HasHierarchy = true; continue; }

            switch (currentSection) {
                case AsmSection::Tag: {
                    if (trimmed.rfind("Tag:", 0) == 0) {
                        current.Tag = Trim(trimmed.substr(4));
                    }
                    break;
                }
                case AsmSection::Transform: {
                    if (trimmed.rfind("Translation:", 0) == 0) {
                        float x = 0, y = 0, z = 0;
                        sscanf(trimmed.c_str(), "Translation: [ %f , %f , %f ]", &x, &y, &z);
                        current.Transform.Translation = glm::vec3(x, y, z);
                    } else if (trimmed.rfind("Rotation:", 0) == 0) {
                        float w = 1, x = 0, y = 0, z = 0;
                        sscanf(trimmed.c_str(), "Rotation: [ %f , %f , %f , %f ]", &w, &x, &y, &z);
                        current.Transform.Rotation = glm::quat(w, x, y, z);
                    } else if (trimmed.rfind("Scale:", 0) == 0) {
                        float x = 1, y = 1, z = 1;
                        sscanf(trimmed.c_str(), "Scale: [ %f , %f , %f ]", &x, &y, &z);
                        current.Transform.Scale = glm::vec3(x, y, z);
                    }
                    break;
                }
                case AsmSection::Mesh: {
                    if (trimmed.rfind("AssetPath:", 0) == 0) {
                        current.MeshAssetPath = Trim(trimmed.substr(10));
                    } else if (trimmed.rfind("PatternMode:", 0) == 0) {
                        current.MeshPattern = std::stoi(Trim(trimmed.substr(12)));
                    } else if (trimmed.rfind("TileScale:", 0) == 0) {
                        current.MeshTileScale = std::stof(Trim(trimmed.substr(10)));
                    } else if (trimmed.rfind("Color1:", 0) == 0) {
                        float r = 1, g = 1, b = 1, a = 1;
                        sscanf(trimmed.c_str(), "Color1: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                        current.MeshColor1 = glm::vec4(r, g, b, a);
                    } else if (trimmed.rfind("Color2:", 0) == 0) {
                        float r = 0, g = 0, b = 0, a = 1;
                        sscanf(trimmed.c_str(), "Color2: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                        current.MeshColor2 = glm::vec4(r, g, b, a);
                    }
                    break;
                }
                case AsmSection::CollisionShape: {
                    if (trimmed.rfind("ShapeType:", 0) == 0) {
                        std::string shape = Trim(trimmed.substr(10));
                        if (shape == "Plane") current.CollisionShape.ShapeType = CollisionShapeType::Plane;
                        else if (shape == "Sphere") current.CollisionShape.ShapeType = CollisionShapeType::Sphere;
                        else if (shape == "Cylinder") current.CollisionShape.ShapeType = CollisionShapeType::Cylinder;
                        else if (shape == "Cone") current.CollisionShape.ShapeType = CollisionShapeType::Cone;
                        else if (shape == "Mesh") current.CollisionShape.ShapeType = CollisionShapeType::Mesh;
                        else current.CollisionShape.ShapeType = CollisionShapeType::Box;
                    } else if (trimmed.rfind("HalfExtent:", 0) == 0) {
                        float x = 0.5f, y = 0.5f, z = 0.5f;
                        sscanf(trimmed.c_str(), "HalfExtent: [ %f , %f , %f ]", &x, &y, &z);
                        current.CollisionShape.HalfExtent = glm::vec3(x, y, z);
                    } else if (trimmed.rfind("Radius:", 0) == 0) {
                        current.CollisionShape.Radius = std::stof(Trim(trimmed.substr(7)));
                    } else if (trimmed.rfind("HalfHeight:", 0) == 0) {
                        current.CollisionShape.HalfHeight = std::stof(Trim(trimmed.substr(11)));
                    } else if (trimmed.rfind("Offset:", 0) == 0) {
                        float x = 0, y = 0, z = 0;
                        sscanf(trimmed.c_str(), "Offset: [ %f , %f , %f ]", &x, &y, &z);
                        current.CollisionShape.Offset = glm::vec3(x, y, z);
                    } else if (trimmed.rfind("Friction:", 0) == 0) {
                        current.CollisionShape.Friction = std::stof(Trim(trimmed.substr(9)));
                    } else if (trimmed.rfind("Restitution:", 0) == 0) {
                        current.CollisionShape.Restitution = std::stof(Trim(trimmed.substr(12)));
                    } else if (trimmed.rfind("IsTrigger:", 0) == 0) {
                        current.CollisionShape.IsTrigger = (Trim(trimmed.substr(10)) == "true");
                    }
                    break;
                }
                case AsmSection::RigidBody: {
                    if (trimmed.rfind("MotionType:", 0) == 0) {
                        std::string m = Trim(trimmed.substr(11));
                        if (m == "Static") current.RigidBody.MotionType = BodyMotionType::Static;
                        else if (m == "Kinematic") current.RigidBody.MotionType = BodyMotionType::Kinematic;
                        else current.RigidBody.MotionType = BodyMotionType::Dynamic;
                    } else if (trimmed.rfind("IsDynamic:", 0) == 0) {
                        current.RigidBody.IsDynamic = (Trim(trimmed.substr(10)) == "true");
                    } else if (trimmed.rfind("Mass:", 0) == 0) {
                        current.RigidBody.Mass = std::stof(Trim(trimmed.substr(5)));
                    } else if (trimmed.rfind("LinearDamping:", 0) == 0) {
                        current.RigidBody.LinearDamping = std::stof(Trim(trimmed.substr(14)));
                    } else if (trimmed.rfind("AngularDamping:", 0) == 0) {
                        current.RigidBody.AngularDamping = std::stof(Trim(trimmed.substr(15)));
                    } else if (trimmed.rfind("GravityFactor:", 0) == 0) {
                        current.RigidBody.GravityFactor = std::stof(Trim(trimmed.substr(14)));
                    }
                    break;
                }
                case AsmSection::Material: {
                    if (trimmed.rfind("MaterialHandle:", 0) == 0) {
                        current.Material.MaterialHandle = std::stoull(Trim(trimmed.substr(15)));
                        if (current.Material.MaterialHandle != 0) {
                            current.Material.MaterialAsset = AssetManager::GetMaterial(current.Material.MaterialHandle);
                        }
                    } else if (trimmed.rfind("AssetPath:", 0) == 0) {
                        std::string path = Trim(trimmed.substr(10));
                        if (!path.empty() && !current.Material.MaterialAsset) {
                            current.Material.MaterialAsset = AssetManager::GetMaterial(path);
                            if (current.Material.MaterialAsset) current.Material.MaterialHandle = current.Material.MaterialAsset->GetHandle();
                        }
                    } else if (trimmed.rfind("AlbedoPath:", 0) == 0) {
                        current.Material.AlbedoPath = Trim(trimmed.substr(11));
                    } else if (trimmed.rfind("UseAlbedoMap:", 0) == 0) {
                        current.Material.UseAlbedoMap = (Trim(trimmed.substr(13)) == "true");
                    } else if (trimmed.rfind("AlbedoColor:", 0) == 0) {
                        float r = 1, g = 1, b = 1, a = 1;
                        sscanf(trimmed.c_str(), "AlbedoColor: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                        current.Material.AlbedoColor = glm::vec4(r, g, b, a);
                    } else if (trimmed.rfind("NormalPath:", 0) == 0) {
                        current.Material.NormalPath = Trim(trimmed.substr(11));
                    } else if (trimmed.rfind("UseNormalMap:", 0) == 0) {
                        current.Material.UseNormalMap = (Trim(trimmed.substr(13)) == "true");
                    } else if (trimmed.rfind("NormalStrength:", 0) == 0) {
                        current.Material.NormalStrength = std::stof(Trim(trimmed.substr(15)));
                    } else if (trimmed.rfind("RoughnessPath:", 0) == 0) {
                        current.Material.RoughnessPath = Trim(trimmed.substr(14));
                    } else if (trimmed.rfind("UseRoughnessMap:", 0) == 0) {
                        current.Material.UseRoughnessMap = (Trim(trimmed.substr(16)) == "true");
                    } else if (trimmed.rfind("Roughness:", 0) == 0) {
                        current.Material.Roughness = std::stof(Trim(trimmed.substr(10)));
                    } else if (trimmed.rfind("MetallicPath:", 0) == 0) {
                        current.Material.MetallicPath = Trim(trimmed.substr(13));
                    } else if (trimmed.rfind("UseMetallicMap:", 0) == 0) {
                        current.Material.UseMetallicMap = (Trim(trimmed.substr(15)) == "true");
                    } else if (trimmed.rfind("Metallic:", 0) == 0) {
                        current.Material.Metallic = std::stof(Trim(trimmed.substr(9)));
                    } else if (trimmed.rfind("EmissionColor:", 0) == 0) {
                        float r = 0, g = 0, b = 0;
                        sscanf(trimmed.c_str(), "EmissionColor: [ %f , %f , %f ]", &r, &g, &b);
                        current.Material.EmissionColor = glm::vec3(r, g, b);
                    } else if (trimmed.rfind("EmissionIntensity:", 0) == 0) {
                        current.Material.EmissionIntensity = std::stof(Trim(trimmed.substr(18)));
                    } else if (trimmed.rfind("UVScale:", 0) == 0) {
                        float x = 1, y = 1;
                        sscanf(trimmed.c_str(), "UVScale: [ %f , %f ]", &x, &y);
                        current.Material.UVScale = glm::vec2(x, y);
                    }
                    break;
                }
                case AsmSection::NativeScript: {
                    if (trimmed.rfind("ScriptName:", 0) == 0) {
                        current.ScriptName = Trim(trimmed.substr(11));
                    }
                    break;
                }
                case AsmSection::Camera: {
                    if (trimmed.rfind("Primary:", 0) == 0) {
                        current.Camera.Primary = (Trim(trimmed.substr(8)) == "true");
                    } else if (trimmed.rfind("FOV:", 0) == 0) {
                        current.Camera.FOV = std::stof(Trim(trimmed.substr(4)));
                    } else if (trimmed.rfind("NearClip:", 0) == 0) {
                        current.Camera.NearClip = std::stof(Trim(trimmed.substr(9)));
                    } else if (trimmed.rfind("FarClip:", 0) == 0) {
                        current.Camera.FarClip = std::stof(Trim(trimmed.substr(8)));
                    }
                    break;
                }
                case AsmSection::AudioListener: {
                    if (trimmed.rfind("Active:", 0) == 0) {
                        current.AudioListener.Active = (Trim(trimmed.substr(7)) == "true");
                    }
                    break;
                }
                case AsmSection::AudioSource: {
                    if (trimmed.rfind("AssetPath:", 0) == 0) current.AudioSource.AssetPath = Trim(trimmed.substr(10));
                    else if (trimmed.rfind("Volume:", 0) == 0) {
                        try { current.AudioSource.Volume = std::stof(Trim(trimmed.substr(7))); } catch (...) {}
                    } else if (trimmed.rfind("Pitch:", 0) == 0) {
                        try { current.AudioSource.Pitch = std::stof(Trim(trimmed.substr(6))); } catch (...) {}
                    } else if (trimmed.rfind("Loop:", 0) == 0) {
                        current.AudioSource.Loop = (Trim(trimmed.substr(5)) == "true");
                    } else if (trimmed.rfind("PlayOnAwake:", 0) == 0) {
                        current.AudioSource.PlayOnAwake = (Trim(trimmed.substr(12)) == "true");
                    } else if (trimmed.rfind("Spatialized:", 0) == 0) {
                        current.AudioSource.Spatialized = (Trim(trimmed.substr(12)) == "true");
                    } else if (trimmed.rfind("AttenuationModel:", 0) == 0) {
                        try { current.AudioSource.Attenuation = static_cast<AttenuationModel>(std::stoi(Trim(trimmed.substr(17)))); } catch (...) {}
                    } else if (trimmed.rfind("MinDistance:", 0) == 0) {
                        try { current.AudioSource.MinDistance = std::stof(Trim(trimmed.substr(12))); } catch (...) {}
                    } else if (trimmed.rfind("MaxDistance:", 0) == 0) {
                        try { current.AudioSource.MaxDistance = std::stof(Trim(trimmed.substr(12))); } catch (...) {}
                    } else if (trimmed.rfind("RollOff:", 0) == 0) {
                        try { current.AudioSource.RollOff = std::stof(Trim(trimmed.substr(8))); } catch (...) {}
                    } else if (trimmed.rfind("InnerAngle:", 0) == 0) {
                        try { current.AudioSource.InnerAngle = std::stof(Trim(trimmed.substr(11))); } catch (...) {}
                    } else if (trimmed.rfind("OuterAngle:", 0) == 0) {
                        try { current.AudioSource.OuterAngle = std::stof(Trim(trimmed.substr(11))); } catch (...) {}
                    } else if (trimmed.rfind("OuterGain:", 0) == 0) {
                        try { current.AudioSource.OuterGain = std::stof(Trim(trimmed.substr(10))); } catch (...) {}
                    } else if (trimmed.rfind("DopplerFactor:", 0) == 0) {
                        try { current.AudioSource.DopplerFactor = std::stof(Trim(trimmed.substr(14))); } catch (...) {}
                    }
                    break;
                }
                case AsmSection::VehicleAudio: {
                    if (trimmed.rfind("Enabled:", 0) == 0) {
                        current.VehicleAudio.Enabled = (Trim(trimmed.substr(8)) == "true");
                    } else if (trimmed.rfind("EnginePowerClipPath:", 0) == 0) {
                        current.VehicleAudio.EnginePowerClipPath = Trim(trimmed.substr(20));
                    } else if (trimmed.rfind("EngineCoastClipPath:", 0) == 0) {
                        current.VehicleAudio.EngineCoastClipPath = Trim(trimmed.substr(20));
                    } else if (trimmed.rfind("MinEnginePitch:", 0) == 0) {
                        try { current.VehicleAudio.MinEnginePitch = std::stof(Trim(trimmed.substr(15))); } catch (...) {}
                    } else if (trimmed.rfind("MaxEnginePitch:", 0) == 0) {
                        try { current.VehicleAudio.MaxEnginePitch = std::stof(Trim(trimmed.substr(15))); } catch (...) {}
                    } else if (trimmed.rfind("EngineMasterVolume:", 0) == 0) {
                        try { current.VehicleAudio.EngineMasterVolume = std::stof(Trim(trimmed.substr(19))); } catch (...) {}
                    } else if (trimmed.rfind("ThrottleSmoothing:", 0) == 0) {
                        try { current.VehicleAudio.ThrottleSmoothing = std::stof(Trim(trimmed.substr(18))); } catch (...) {}
                    } else if (trimmed.rfind("TireSquealClipPath:", 0) == 0) {
                        current.VehicleAudio.TireSquealClipPath = Trim(trimmed.substr(19));
                    } else if (trimmed.rfind("SlipThreshold:", 0) == 0) {
                        try { current.VehicleAudio.SlipThreshold = std::stof(Trim(trimmed.substr(14))); } catch (...) {}
                    } else if (trimmed.rfind("TireSquealMasterVolume:", 0) == 0) {
                        try { current.VehicleAudio.TireSquealMasterVolume = std::stof(Trim(trimmed.substr(23))); } catch (...) {}
                    } else if (trimmed.rfind("TireSquealFadeSpeed:", 0) == 0) {
                        try { current.VehicleAudio.TireSquealFadeSpeed = std::stof(Trim(trimmed.substr(20))); } catch (...) {}
                    } else if (trimmed.rfind("SuspensionThudClipPath:", 0) == 0) {
                        current.VehicleAudio.SuspensionThudClipPath = Trim(trimmed.substr(23));
                    } else if (trimmed.rfind("SuspensionThudThreshold:", 0) == 0) {
                        try { current.VehicleAudio.SuspensionThudThreshold = std::stof(Trim(trimmed.substr(24))); } catch (...) {}
                    } else if (trimmed.rfind("SuspensionMasterVolume:", 0) == 0) {
                        try { current.VehicleAudio.SuspensionMasterVolume = std::stof(Trim(trimmed.substr(23))); } catch (...) {}
                    } else if (trimmed.rfind("CollisionImpactClipPath:", 0) == 0) {
                        current.VehicleAudio.CollisionImpactClipPath = Trim(trimmed.substr(24));
                    } else if (trimmed.rfind("CollisionDecelThreshold:", 0) == 0) {
                        try { current.VehicleAudio.CollisionDecelThreshold = std::stof(Trim(trimmed.substr(24))); } catch (...) {}
                    } else if (trimmed.rfind("CollisionMasterVolume:", 0) == 0) {
                        try { current.VehicleAudio.CollisionMasterVolume = std::stof(Trim(trimmed.substr(22))); } catch (...) {}
                    } else if (trimmed.rfind("Spatialized:", 0) == 0) {
                        current.VehicleAudio.Spatialized = (Trim(trimmed.substr(12)) == "true");
                    } else if (trimmed.rfind("MinDistance:", 0) == 0) {
                        try { current.VehicleAudio.MinDistance = std::stof(Trim(trimmed.substr(12))); } catch (...) {}
                    } else if (trimmed.rfind("MaxDistance:", 0) == 0) {
                        try { current.VehicleAudio.MaxDistance = std::stof(Trim(trimmed.substr(12))); } catch (...) {}
                    }
                    break;
                }
                case AsmSection::Hierarchy: {
                    if (trimmed.rfind("Parent:", 0) == 0) {
                        try {
                            std::string pStr = Trim(trimmed.substr(7));
                            if (!pStr.empty()) {
                                current.ParentOldUUID = std::stoull(pStr);
                            }
                        } catch (...) {
                            current.ParentOldUUID = 0;
                        }
                    } else if (trimmed == "Children:") {
                        currentSection = AsmSection::HierarchyChildren;
                    }
                    break;
                }
                case AsmSection::HierarchyChildren: {
                    if (trimmed.rfind("- ", 0) == 0) {
                        try {
                            std::string cStr = Trim(trimmed.substr(2));
                            if (!cStr.empty()) {
                                current.ChildrenOldUUIDs.push_back(std::stoull(cStr));
                            }
                        } catch (...) {}
                    }
                    break;
                }
                default:
                    break;
            }
        }

        if (inEntity) {
            entities.push_back(current);
        }

        return entities;
    }

    Entity AssemblySerializer::Deserialize(const std::filesystem::path& filepath,
                                          Scene* targetScene,
                                          PhysicsScene* physicsScene,
                                          const glm::vec3& spawnPosition,
                                          const SceneSerializerContext* context) {
        if (!targetScene) return {};

        std::string content = FileSystem::ReadTextFile(filepath);
        if (content.empty()) {
            AXLE_ERROR("AssemblySerializer: Could not read assembly file '{0}'", filepath.string());
            return {};
        }

        std::istringstream fin(content);
        u64 rootOldUUID = 0;
        auto rawEntities = ParseAssemblyEntities(fin, rootOldUUID);

        if (rawEntities.empty()) {
            AXLE_ERROR("AssemblySerializer: No entities found in '{0}'", filepath.string());
            return {};
        }

        // Determine root entity index
        size_t rootIndex = 0;
        if (rootOldUUID != 0) {
            for (size_t i = 0; i < rawEntities.size(); ++i) {
                if (rawEntities[i].OldUUID == rootOldUUID) {
                    rootIndex = i;
                    break;
                }
            }
        }

        // Map OldUUID -> NewUUID and OldUUID -> Created Entity
        std::unordered_map<u64, u64> oldToNewUUID;
        std::unordered_map<u64, Entity> oldToEntity;

        for (const auto& raw : rawEntities) {
            UUID newUUID;
            oldToNewUUID[raw.OldUUID] = newUUID;
        }

        // Pass 1: Instantiate entities and assign components
        for (size_t i = 0; i < rawEntities.size(); ++i) {
            const auto& raw = rawEntities[i];
            u64 newUUID = oldToNewUUID[raw.OldUUID];

            Entity entity = targetScene->CreateEntityWithUUID(newUUID, raw.Tag);
            oldToEntity[raw.OldUUID] = entity;

            // TransformComponent: newly created entities do not have TransformComponent yet
            auto& tc = entity.HasComponent<TransformComponent>() ?
                       entity.GetComponent<TransformComponent>() :
                       entity.AddComponent<TransformComponent>();
            if (raw.HasTransform) {
                tc = raw.Transform;
            }
            if (i == rootIndex) {
                // Apply spawn position offset to the root entity
                tc.Translation += spawnPosition;
            }

            // MeshInstance3D
            if (raw.HasMesh) {
                std::shared_ptr<VertexArray> vao = nullptr;
                std::string path = raw.MeshAssetPath;

                if (path == "builtin://cube" && context) vao = context->CubeMesh;
                else if (path == "builtin://sphere" && context) vao = context->SphereMesh;
                else if (path == "builtin://cylinder" && context) vao = context->CylinderMesh;
                else if (path == "builtin://cone" && context) vao = context->ConeMesh;
                else if (path == "builtin://plane" && context) vao = context->PlaneMesh;
                else if (!path.empty()) {
                    auto resolved = FileSystem::ResolveAssetPath(path);
                    if (std::filesystem::exists(resolved)) {
                        auto loaded = AssetSerializer::LoadMesh(resolved);
                        if (loaded.Success) vao = loaded.VAO;
                    }
                }

                if (!vao && context) {
                    vao = context->CubeMesh;
                }

                entity.AddComponent<MeshInstance3D>(vao, path, raw.MeshPattern, raw.MeshTileScale, raw.MeshColor1, raw.MeshColor2);
            }

            // CollisionShape3D
            if (raw.HasCollisionShape) {
                auto& cs = entity.AddComponent<CollisionShape3D>();
                cs = raw.CollisionShape;
            }

            // RigidBody3D
            if (raw.HasRigidBody) {
                auto& rb = entity.AddComponent<RigidBody3D>();
                rb.MotionType = raw.RigidBody.MotionType;
                rb.IsDynamic = raw.RigidBody.IsDynamic;
                rb.Mass = raw.RigidBody.Mass;
                rb.LinearDamping = raw.RigidBody.LinearDamping;
                rb.AngularDamping = raw.RigidBody.AngularDamping;
                rb.GravityFactor = raw.RigidBody.GravityFactor;
            }

            // MaterialComponent
            if (raw.HasMaterial) {
                auto& mat = entity.AddComponent<MaterialComponent>();
                mat = raw.Material;
                if (!mat.AlbedoPath.empty() && mat.UseAlbedoMap) {
                    auto resolved = FileSystem::ResolveAssetPath(mat.AlbedoPath);
                    if (std::filesystem::exists(resolved)) {
                        mat.AlbedoMap = Texture2D::Create(resolved.string());
                    }
                }
            }

            // NativeScriptComponent
            if (raw.HasNativeScript && !raw.ScriptName.empty()) {
                entity.AddComponent<NativeScriptComponent>(raw.ScriptName);
            }

            // CameraComponent
            if (raw.HasCamera) {
                entity.AddComponent<CameraComponent>(raw.Camera);
            }

            // AudioListenerComponent
            if (raw.HasAudioListener) {
                entity.AddComponent<AudioListenerComponent>(raw.AudioListener);
            }

            // AudioSourceComponent
            if (raw.HasAudioSource) {
                entity.AddComponent<AudioSourceComponent>(raw.AudioSource);
            }

            // VehicleAudioComponent
            if (raw.HasVehicleAudio) {
                entity.AddComponent<VehicleAudioComponent>(raw.VehicleAudio);
            }
        }

        // Pass 2: Reconstruct Hierarchy parent-child links
        for (const auto& raw : rawEntities) {
            Entity entity = oldToEntity[raw.OldUUID];
            auto& hc = entity.AddComponent<HierarchyComponent>();

            if (raw.ParentOldUUID != 0 && oldToEntity.find(raw.ParentOldUUID) != oldToEntity.end()) {
                hc.Parent = (entt::entity)oldToEntity[raw.ParentOldUUID];
            } else {
                hc.Parent = entt::null;
            }

            for (u64 childOldUUID : raw.ChildrenOldUUIDs) {
                if (oldToEntity.find(childOldUUID) != oldToEntity.end()) {
                    hc.Children.push_back((entt::entity)oldToEntity[childOldUUID]);
                }
            }
        }

        // Pass 3: Physics scene integration
        if (physicsScene) {
            for (size_t i = 0; i < rawEntities.size(); ++i) {
                const auto& raw = rawEntities[i];
                Entity entity = oldToEntity[raw.OldUUID];

                if (entity.HasComponent<RigidBody3D>() && entity.HasComponent<CollisionShape3D>()) {
                    auto& rb = entity.GetComponent<RigidBody3D>();
                    auto& cs = entity.GetComponent<CollisionShape3D>();

                    glm::mat4 worldMat = targetScene->GetWorldTransform((entt::entity)entity);
                    glm::vec3 worldPos = glm::vec3(worldMat[3]);
                    glm::quat worldRot = glm::quat_cast(worldMat);

                    // Spawn position including local collision offset
                    glm::vec3 spawnPos = worldPos + worldRot * cs.Offset;
                    bool isDyn = (rb.MotionType == BodyMotionType::Dynamic);

                    BodyID bodyId;
                    switch (cs.ShapeType) {
                        case CollisionShapeType::Sphere: {
                            SphereBodyProps props;
                            props.Position = spawnPos;
                            props.Rotation = worldRot;
                            props.Radius = cs.Radius;
                            props.IsDynamic = isDyn;
                            props.Friction = cs.Friction;
                            props.Restitution = cs.Restitution;
                            bodyId = physicsScene->CreateSphere(props);
                            break;
                        }
                        case CollisionShapeType::Cylinder: {
                            CylinderBodyProps props;
                            props.Position = spawnPos;
                            props.Rotation = worldRot;
                            props.HalfHeight = cs.HalfHeight;
                            props.Radius = cs.Radius;
                            props.IsDynamic = isDyn;
                            props.Friction = cs.Friction;
                            props.Restitution = cs.Restitution;
                            bodyId = physicsScene->CreateCylinder(props);
                            break;
                        }
                        case CollisionShapeType::Cone: {
                            ConeBodyProps props;
                            props.Position = spawnPos;
                            props.Rotation = worldRot;
                            props.HalfHeight = cs.HalfHeight;
                            props.Radius = cs.Radius;
                            props.IsDynamic = isDyn;
                            props.Friction = cs.Friction;
                            props.Restitution = cs.Restitution;
                            bodyId = physicsScene->CreateCone(props);
                            break;
                        }
                        case CollisionShapeType::Plane: {
                            BoxBodyProps props;
                            props.Position = spawnPos;
                            props.Rotation = worldRot;
                            props.HalfExtent = cs.HalfExtent;
                            props.IsDynamic = false;
                            props.Friction = cs.Friction;
                            props.Restitution = cs.Restitution;
                            bodyId = physicsScene->CreateBox(props);
                            break;
                        }
                        case CollisionShapeType::Box:
                        default: {
                            BoxBodyProps props;
                            props.Position = spawnPos;
                            props.Rotation = worldRot;
                            props.HalfExtent = cs.HalfExtent;
                            props.IsDynamic = isDyn;
                            props.Friction = cs.Friction;
                            props.Restitution = cs.Restitution;
                            bodyId = physicsScene->CreateBox(props);
                            break;
                        }
                    }

                    rb.RuntimeBodyID = bodyId;
                    if (physicsScene && !bodyId.IsInvalid()) {
                        if (isDyn) {
                            physicsScene->GetBodyInterface().SetGravityFactor(bodyId, rb.GravityFactor);
                        }
                        physicsScene->GetBodyInterface().SetFriction(bodyId, cs.Friction);
                        physicsScene->GetBodyInterface().SetRestitution(bodyId, cs.Restitution);
                    }
                }
            }
        }

        Entity rootEntity = oldToEntity[rawEntities[rootIndex].OldUUID];

        // Attach AssemblyComponent on Root Entity for source tracking
        rootEntity.AddComponent<AssemblyComponent>(filepath.string(), rawEntities[rootIndex].OldUUID);

        AXLE_INFO("AssemblySerializer: Instantiated Assembly '{0}' ({1} entities) into scene at ({2:.2f}, {3:.2f}, {4:.2f})",
                  rawEntities[rootIndex].Tag, rawEntities.size(), spawnPosition.x, spawnPosition.y, spawnPosition.z);

        return rootEntity;
    }

    bool AssemblySerializer::CreateEmptyAssembly(const std::filesystem::path& filepath, const std::string& name) {
        std::ofstream fout(filepath);
        if (!fout.is_open()) return false;

        UUID rootUUID;
        fout << "# Axle Unified Asset File\n";
        fout << "AxleAsset:\n";
        fout << "  AssetType: Assembly\n";
        fout << "  Version: 1\n";
        fout << "  Name: " << name << "\n";
        fout << "  RootEntityID: " << (u64)rootUUID << "\n\n";

        fout << "Entities:\n";
        fout << "  - Entity: " << (u64)rootUUID << "\n";
        fout << "    TagComponent:\n";
        fout << "      Tag: " << name << "\n";
        fout << "    TransformComponent:\n";
        fout << "      Translation: [ 0.000000 , 0.000000 , 0.000000 ]\n";
        fout << "      Rotation: [ 1.000000 , 0.000000 , 0.000000 , 0.000000 ]\n";
        fout << "      Scale: [ 1.000000 , 1.000000 , 1.000000 ]\n";
        fout << "    MeshInstance3D:\n";
        fout << "      AssetPath: builtin://cube\n";
        fout << "      PatternMode: 1\n";
        fout << "      TileScale: 4.000000\n";
        fout << "      Color1: [ 0.850000 , 0.880000 , 0.920000 , 1.000000 ]\n";
        fout << "      Color2: [ 0.650000 , 0.700000 , 0.760000 , 1.000000 ]\n";
        fout << "    CollisionShape3D:\n";
        fout << "      ShapeType: Box\n";
        fout << "      HalfExtent: [ 0.500000 , 0.500000 , 0.500000 ]\n";
        fout << "      Radius: 0.500000\n";
        fout << "      HalfHeight: 0.500000\n";
        fout << "      Offset: [ 0.000000 , 0.000000 , 0.000000 ]\n";
        fout << "      Friction: 0.500000\n";
        fout << "      Restitution: 0.000000\n";
        fout << "      IsTrigger: false\n";
        fout << "    RigidBody3D:\n";
        fout << "      MotionType: Dynamic\n";
        fout << "      IsDynamic: true\n";
        fout << "      Mass: 1.000000\n";
        fout << "      LinearDamping: 0.050000\n";
        fout << "      AngularDamping: 0.050000\n";
        fout << "      GravityFactor: 1.000000\n";
        fout << "    HierarchyComponent:\n";
        fout << "      Parent: 0\n";
        fout << "      Children:\n";

        fout.close();
        AXLE_INFO("AssemblySerializer: Created new Assembly '{0}' at '{1}'", name, filepath.string());
        return true;
    }

    AssemblyData AssemblySerializer::LoadAssemblyData(const std::filesystem::path& filepath,
                                                     const SceneSerializerContext* context) {
        AssemblyData data;
        data.SourcePath = filepath;

        std::ifstream fin(filepath);
        if (!fin.is_open()) return data;

        u64 rootUUID = 0;
        auto rawEntities = ParseAssemblyEntities(fin, rootUUID);
        fin.close();

        if (rawEntities.empty()) return data;

        data.Name = filepath.stem().string();
        data.IsValid = true;

        // Build node map
        std::unordered_map<u64, AssemblyNode> nodeMap;
        for (const auto& raw : rawEntities) {
            AssemblyNode node;
            node.Tag = raw.Tag;
            node.Transform = raw.Transform;
            node.HasMesh = raw.HasMesh;
            node.Mesh = MeshInstance3D(context ? context->CubeMesh : nullptr, raw.MeshAssetPath, raw.MeshPattern, raw.MeshTileScale, raw.MeshColor1, raw.MeshColor2);
            node.HasCollisionShape = raw.HasCollisionShape;
            node.CollisionShape = raw.CollisionShape;
            node.HasRigidBody = raw.HasRigidBody;
            node.RigidBody = raw.RigidBody;
            node.HasMaterial = raw.HasMaterial;
            node.Material = raw.Material;
            nodeMap[raw.OldUUID] = node;
        }

        // Link hierarchy
        u64 rootNodeUUID = (rootUUID != 0) ? rootUUID : rawEntities[0].OldUUID;
        for (const auto& raw : rawEntities) {
            for (u64 childUUID : raw.ChildrenOldUUIDs) {
                if (nodeMap.find(childUUID) != nodeMap.end()) {
                    nodeMap[raw.OldUUID].Children.push_back(nodeMap[childUUID]);
                }
            }
        }

        data.RootNode = nodeMap[rootNodeUUID];
        return data;
    }

    static void WriteAssemblyNode(std::ostream& out, const AssemblyNode& node, u64 uuid, u64 parentUUID) {
        out << "  - Entity: " << uuid << "\n";
        out << "    TagComponent:\n";
        out << "      Tag: " << node.Tag << "\n";
        out << "    TransformComponent:\n";
        out << "      Translation: [ " << node.Transform.Translation.x << " , " << node.Transform.Translation.y << " , " << node.Transform.Translation.z << " ]\n";
        out << "      Rotation: [ " << node.Transform.Rotation.w << " , " << node.Transform.Rotation.x << " , " << node.Transform.Rotation.y << " , " << node.Transform.Rotation.z << " ]\n";
        out << "      Scale: [ " << node.Transform.Scale.x << " , " << node.Transform.Scale.y << " , " << node.Transform.Scale.z << " ]\n";

        if (node.HasMesh) {
            out << "    MeshInstance3D:\n";
            out << "      AssetPath: " << node.Mesh.AssetPath << "\n";
            out << "      PatternMode: " << node.Mesh.PatternMode << "\n";
            out << "      TileScale: " << node.Mesh.TileScale << "\n";
            out << "      Color1: [ " << node.Mesh.Color1.r << " , " << node.Mesh.Color1.g << " , " << node.Mesh.Color1.b << " , " << node.Mesh.Color1.a << " ]\n";
            out << "      Color2: [ " << node.Mesh.Color2.r << " , " << node.Mesh.Color2.g << " , " << node.Mesh.Color2.b << " , " << node.Mesh.Color2.a << " ]\n";
        }

        if (node.HasCollisionShape) {
            std::string shapeName = "Box";
            if (node.CollisionShape.ShapeType == CollisionShapeType::Plane) shapeName = "Plane";
            else if (node.CollisionShape.ShapeType == CollisionShapeType::Sphere) shapeName = "Sphere";
            else if (node.CollisionShape.ShapeType == CollisionShapeType::Cylinder) shapeName = "Cylinder";
            else if (node.CollisionShape.ShapeType == CollisionShapeType::Cone) shapeName = "Cone";
            else if (node.CollisionShape.ShapeType == CollisionShapeType::Mesh) shapeName = "Mesh";

            out << "    CollisionShape3D:\n";
            out << "      ShapeType: " << shapeName << "\n";
            out << "      HalfExtent: [ " << node.CollisionShape.HalfExtent.x << " , " << node.CollisionShape.HalfExtent.y << " , " << node.CollisionShape.HalfExtent.z << " ]\n";
            out << "      Radius: " << node.CollisionShape.Radius << "\n";
            out << "      HalfHeight: " << node.CollisionShape.HalfHeight << "\n";
            out << "      Offset: [ " << node.CollisionShape.Offset.x << " , " << node.CollisionShape.Offset.y << " , " << node.CollisionShape.Offset.z << " ]\n";
            out << "      Friction: " << node.CollisionShape.Friction << "\n";
            out << "      Restitution: " << node.CollisionShape.Restitution << "\n";
            out << "      IsTrigger: " << (node.CollisionShape.IsTrigger ? "true" : "false") << "\n";
        }

        if (node.HasRigidBody) {
            std::string motionStr = (node.RigidBody.MotionType == BodyMotionType::Dynamic) ? "Dynamic" :
                                    (node.RigidBody.MotionType == BodyMotionType::Kinematic) ? "Kinematic" : "Static";
            out << "    RigidBody3D:\n";
            out << "      MotionType: " << motionStr << "\n";
            out << "      IsDynamic: " << (node.RigidBody.IsDynamic ? "true" : "false") << "\n";
            out << "      Mass: " << node.RigidBody.Mass << "\n";
            out << "      LinearDamping: " << node.RigidBody.LinearDamping << "\n";
            out << "      AngularDamping: " << node.RigidBody.AngularDamping << "\n";
            out << "      GravityFactor: " << node.RigidBody.GravityFactor << "\n";
        }

        out << "    HierarchyComponent:\n";
        out << "      Parent: " << parentUUID << "\n";
        out << "      Children:\n";
        std::vector<u64> childUUIDs;
        for (size_t i = 0; i < node.Children.size(); ++i) {
            u64 childUUID = (u64)UUID();
            childUUIDs.push_back(childUUID);
            out << "        - " << childUUID << "\n";
        }

        for (size_t i = 0; i < node.Children.size(); ++i) {
            WriteAssemblyNode(out, node.Children[i], childUUIDs[i], uuid);
        }
    }

    bool AssemblySerializer::SaveAssemblyData(const std::filesystem::path& filepath,
                                             const AssemblyData& data) {
        std::ofstream fout(filepath);
        if (!fout.is_open()) return false;

        UUID rootUUID;
        fout << std::fixed << std::setprecision(6);
        fout << "# Axle Unified Asset File\n";
        fout << "AxleAsset:\n";
        fout << "  AssetType: Assembly\n";
        fout << "  Version: 1\n";
        fout << "  Name: " << data.Name << "\n";
        fout << "  RootEntityID: " << (u64)rootUUID << "\n\n";

        fout << "Entities:\n";
        WriteAssemblyNode(fout, data.RootNode, (u64)rootUUID, 0);

        fout.close();
        AXLE_INFO("AssemblySerializer: Saved Assembly data '{0}' to '{1}'", data.Name, filepath.string());
        return true;
    }

}
