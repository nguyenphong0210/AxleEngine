#include <Axle/SceneSerializer.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/SkeletalMesh.hpp>
#include <Axle/SkeletalMeshImporter.hpp>
#include <Axle/Skeleton.hpp>
#include <Axle/AnimationClip.hpp>
#include <Axle/Animator.hpp>
#include <Axle/AnimationStateMachine.hpp>

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
                ss << "      InheritScale: " << (hc.InheritScale ? "true" : "false") << "\n";
            }

            // NativeScriptComponent
            if (entity.HasComponent<NativeScriptComponent>()) {
                auto& nsc = entity.GetComponent<NativeScriptComponent>();
                ss << "    NativeScriptComponent:\n";
                ss << "      ScriptName: " << nsc.ScriptName << "\n";
            }

            // VehicleComponent
            if (entity.HasComponent<VehicleComponent>()) {
                const auto& vc = entity.GetComponent<VehicleComponent>();
                ss << "    VehicleComponent:\n";
                ss << "      ChassisMass: " << vc.ChassisMass << "\n";
                ss << "      MaxEngineTorque: " << vc.MaxEngineTorque << "\n";
                ss << "      MaxRPM: " << vc.MaxRPM << "\n";
                ss << "      MinRPM: " << vc.MinRPM << "\n";
                ss << "      MaxBrakeTorque: " << vc.MaxBrakeTorque << "\n";
                ss << "      MaxHandBrakeTorque: " << vc.MaxHandBrakeTorque << "\n";
                ss << "      AntiRollBarStiffness: " << vc.AntiRollBarStiffness << "\n";
                ss << "      DefaultSuspensionFrequency: " << vc.DefaultSuspensionFrequency << "\n";
                ss << "      DefaultSuspensionDamping: " << vc.DefaultSuspensionDamping << "\n";
                ss << "      FourWheelDrive: " << (vc.FourWheelDrive ? "true" : "false") << "\n";
                ss << "      CenterOfMassOffset: [ " << vc.CenterOfMassOffset.x << " , " << vc.CenterOfMassOffset.y << " , " << vc.CenterOfMassOffset.z << " ]\n";
            }

            // WheelComponent (VehicleWheel3D)
            if (entity.HasComponent<WheelComponent>()) {
                const auto& wc = entity.GetComponent<WheelComponent>();
                ss << "    WheelComponent:\n";
                ss << "      Radius: " << wc.Radius << "\n";
                ss << "      Width: " << wc.Width << "\n";
                ss << "      SuspensionMinLength: " << wc.SuspensionMinLength << "\n";
                ss << "      SuspensionMaxLength: " << wc.SuspensionMaxLength << "\n";
                ss << "      SpringFrequency: " << wc.SpringFrequency << "\n";
                ss << "      SpringDamping: " << wc.SpringDamping << "\n";
                ss << "      CanSteer: " << (wc.CanSteer ? "true" : "false") << "\n";
                ss << "      MaxSteerAngle: " << wc.MaxSteerAngle << "\n";
                ss << "      IsDriven: " << (wc.IsDriven ? "true" : "false") << "\n";
                ss << "      HasHandbrake: " << (wc.HasHandbrake ? "true" : "false") << "\n";
                ss << "      BrakeTorque: " << wc.BrakeTorque << "\n";
                ss << "      HandBrakeTorque: " << wc.HandBrakeTorque << "\n";
                ss << "      AutoSyncMesh: " << (wc.AutoSyncMesh ? "true" : "false") << "\n";
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
                ss << "      ShadowOrthoSize: " << dlc.ShadowOrthoSize << "\n";
                ss << "      ShadowBias: " << dlc.ShadowBias << "\n";
                ss << "      ShadowFilterRadius: " << dlc.ShadowFilterRadius << "\n";
                ss << "      ShadowQuality: " << dlc.ShadowQuality << "\n";
                ss << "      Exposure: " << dlc.Exposure << "\n";
                ss << "      ToneMappingMode: " << dlc.ToneMappingMode << "\n";
                ss << "      Gamma: " << dlc.Gamma << "\n";
            }

            // MaterialComponent
            if (entity.HasComponent<MaterialComponent>()) {
                auto& mat = entity.GetComponent<MaterialComponent>();
                ss << "    MaterialComponent:\n";
                if (mat.HasAsset()) {
                    ss << "      MaterialHandle: " << (uint64_t)mat.MaterialHandle << "\n";
                    if (!mat.MaterialAsset->GetFilePath().empty()) {
                        ss << "      AssetPath: " << mat.MaterialAsset->GetFilePath().string() << "\n";
                    }
                }
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
                ss << "      SkyboxType: " << (int)sbc.Type << "\n";
                ss << "      HDRPath: " << sbc.HDRPath << "\n";
                ss << "      Intensity: " << sbc.Intensity << "\n";
                ss << "      Exposure: " << sbc.Exposure << "\n";
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

            // PointLightComponent
            if (entity.HasComponent<PointLightComponent>()) {
                auto& plc = entity.GetComponent<PointLightComponent>();
                ss << "    PointLightComponent:\n";
                ss << "      Color: [ " << plc.Color.r << " , " << plc.Color.g << " , " << plc.Color.b << " ]\n";
                ss << "      Intensity: " << plc.Intensity << "\n";
                ss << "      Radius: " << plc.Radius << "\n";
                ss << "      Falloff: " << plc.Falloff << "\n";
            }

            // SpotLightComponent
            if (entity.HasComponent<SpotLightComponent>()) {
                auto& slc = entity.GetComponent<SpotLightComponent>();
                ss << "    SpotLightComponent:\n";
                ss << "      Enabled: " << (slc.Enabled ? "true" : "false") << "\n";
                ss << "      Color: [ " << slc.Color.r << " , " << slc.Color.g << " , " << slc.Color.b << " ]\n";
                ss << "      Intensity: " << slc.Intensity << "\n";
                ss << "      Range: " << slc.Range << "\n";
                ss << "      InnerConeAngle: " << slc.InnerConeAngle << "\n";
                ss << "      OuterConeAngle: " << slc.OuterConeAngle << "\n";
            }

            // CameraComponent
            if (entity.HasComponent<CameraComponent>()) {
                auto& cam = entity.GetComponent<CameraComponent>();
                ss << "    CameraComponent:\n";
                ss << "      Primary: " << (cam.Primary ? "true" : "false") << "\n";
                ss << "      FOV: " << cam.FOV << "\n";
                ss << "      NearClip: " << cam.NearClip << "\n";
                ss << "      FarClip: " << cam.FarClip << "\n";
            }

            // AudioListenerComponent
            if (entity.HasComponent<AudioListenerComponent>()) {
                auto& alc = entity.GetComponent<AudioListenerComponent>();
                ss << "    AudioListenerComponent:\n";
                ss << "      Active: " << (alc.Active ? "true" : "false") << "\n";
            }

            // AudioSourceComponent
            if (entity.HasComponent<AudioSourceComponent>()) {
                auto& asc = entity.GetComponent<AudioSourceComponent>();
                ss << "    AudioSourceComponent:\n";
                ss << "      AssetPath: " << asc.AssetPath << "\n";
                ss << "      Volume: " << asc.Volume << "\n";
                ss << "      Pitch: " << asc.Pitch << "\n";
                ss << "      Loop: " << (asc.Loop ? "true" : "false") << "\n";
                ss << "      PlayOnAwake: " << (asc.PlayOnAwake ? "true" : "false") << "\n";
                ss << "      Spatialized: " << (asc.Spatialized ? "true" : "false") << "\n";
                ss << "      AttenuationModel: " << (int)asc.Attenuation << "\n";
                ss << "      MinDistance: " << asc.MinDistance << "\n";
                ss << "      MaxDistance: " << asc.MaxDistance << "\n";
                ss << "      RollOff: " << asc.RollOff << "\n";
                ss << "      InnerAngle: " << asc.InnerAngle << "\n";
                ss << "      OuterAngle: " << asc.OuterAngle << "\n";
                ss << "      OuterGain: " << asc.OuterGain << "\n";
                ss << "      DopplerFactor: " << asc.DopplerFactor << "\n";
            }

            // VehicleAudioComponent
            if (entity.HasComponent<VehicleAudioComponent>()) {
                auto& vac = entity.GetComponent<VehicleAudioComponent>();
                ss << "    VehicleAudioComponent:\n";
                ss << "      Enabled: " << (vac.Enabled ? "true" : "false") << "\n";
                ss << "      EnginePowerClipPath: " << vac.EnginePowerClipPath << "\n";
                ss << "      EngineCoastClipPath: " << vac.EngineCoastClipPath << "\n";
                ss << "      MinEnginePitch: " << vac.MinEnginePitch << "\n";
                ss << "      MaxEnginePitch: " << vac.MaxEnginePitch << "\n";
                ss << "      EngineMasterVolume: " << vac.EngineMasterVolume << "\n";
                ss << "      ThrottleSmoothing: " << vac.ThrottleSmoothing << "\n";
                ss << "      TireSquealClipPath: " << vac.TireSquealClipPath << "\n";
                ss << "      SlipThreshold: " << vac.SlipThreshold << "\n";
                ss << "      TireSquealMasterVolume: " << vac.TireSquealMasterVolume << "\n";
                ss << "      TireSquealFadeSpeed: " << vac.TireSquealFadeSpeed << "\n";
                ss << "      SuspensionThudClipPath: " << vac.SuspensionThudClipPath << "\n";
                ss << "      SuspensionThudThreshold: " << vac.SuspensionThudThreshold << "\n";
                ss << "      SuspensionMasterVolume: " << vac.SuspensionMasterVolume << "\n";
                ss << "      CollisionImpactClipPath: " << vac.CollisionImpactClipPath << "\n";
                ss << "      CollisionDecelThreshold: " << vac.CollisionDecelThreshold << "\n";
                ss << "      CollisionMasterVolume: " << vac.CollisionMasterVolume << "\n";
                ss << "      Spatialized: " << (vac.Spatialized ? "true" : "false") << "\n";
                ss << "      MinDistance: " << vac.MinDistance << "\n";
                ss << "      MaxDistance: " << vac.MaxDistance << "\n";
            }

            // SkeletalMeshComponent
            if (entity.HasComponent<SkeletalMeshComponent>()) {
                auto& smc = entity.GetComponent<SkeletalMeshComponent>();
                ss << "    SkeletalMeshComponent:\n";
                ss << "      AssetPath: " << smc.AssetPath << "\n";
                ss << "      PatternMode: " << smc.PatternMode << "\n";
                ss << "      TileScale: " << smc.TileScale << "\n";
                ss << "      Color1: [ " << smc.Color1.r << " , " << smc.Color1.g << " , " << smc.Color1.b << " , " << smc.Color1.a << " ]\n";
                ss << "      Color2: [ " << smc.Color2.r << " , " << smc.Color2.g << " , " << smc.Color2.b << " , " << smc.Color2.a << " ]\n";
                ss << "      CastShadows: " << (smc.CastShadows ? "true" : "false") << "\n";
                ss << "      Visible: " << (smc.Visible ? "true" : "false") << "\n";
            }

            // AnimatorComponent
            if (entity.HasComponent<AnimatorComponent>()) {
                auto& ac = entity.GetComponent<AnimatorComponent>();
                ss << "    AnimatorComponent:\n";
                ss << "      Playing: " << (ac.Playing ? "true" : "false") << "\n";
                ss << "      PlaybackSpeed: " << ac.PlaybackSpeed << "\n";
                ss << "      Speed: " << ac.Speed << "\n";
                ss << "      IsGrounded: " << (ac.IsGrounded ? "true" : "false") << "\n";
                ss << "      VerticalVelocity: " << ac.VerticalVelocity << "\n";
            }

            // CharacterControllerComponent
            if (entity.HasComponent<CharacterControllerComponent>()) {
                auto& ccc = entity.GetComponent<CharacterControllerComponent>();
                ss << "    CharacterControllerComponent:\n";
                ss << "      WalkSpeed: " << ccc.WalkSpeed << "\n";
                ss << "      RunSpeed: " << ccc.RunSpeed << "\n";
                ss << "      JumpSpeed: " << ccc.JumpSpeed << "\n";
                ss << "      TurnSmoothing: " << ccc.TurnSmoothing << "\n";
                ss << "      OrientRotationToMovement: " << (ccc.OrientRotationToMovement ? "true" : "false") << "\n";
                ss << "      Radius: " << ccc.Settings.Radius << "\n";
                ss << "      HalfHeight: " << ccc.Settings.HalfHeight << "\n";
                ss << "      Mass: " << ccc.Settings.Mass << "\n";
                ss << "      MaxSlopeAngle: " << ccc.Settings.MaxSlopeAngle << "\n";
                ss << "      StepOffset: " << ccc.Settings.StepOffset << "\n";
            }

            // Custom Entity Serializer Hook (for plugins e.g. SplineComponent, TerrainComponent)
            if (m_Context.CustomEntitySerializer) {
                m_Context.CustomEntitySerializer(entity, ss);
            }
        }

        return ss.str();
    }

    bool SceneSerializer::Deserialize(const std::filesystem::path& filepath) {
        std::string content = FileSystem::ReadTextFile(filepath);
        if (content.empty()) {
            AXLE_ERROR("SceneSerializer: Could not read scene file: '{0}'", filepath.string());
            return false;
        }

        bool success = DeserializeFromString(content);
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
            bool InheritScale = true;
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
            PointLight,
            SpotLight,
            Skybox,
            Camera,
            Wheel,
            Vehicle,
            AudioListener,
            AudioSource,
            VehicleAudio,
            SkeletalMesh,
            Animator,
            CharacterController,
            Custom
        };
        ComponentSection currentSection = ComponentSection::None;
        std::string currentCustomSectionName;
        std::vector<std::string> customSectionLines;

        auto flushCustomSection = [&]() {
            if (currentSection == ComponentSection::Custom && currentEntity && !currentCustomSectionName.empty()) {
                if (m_Context.CustomSectionDeserializer) {
                    m_Context.CustomSectionDeserializer(currentEntity, currentCustomSectionName, customSectionLines);
                }
                customSectionLines.clear();
                currentCustomSectionName.clear();
            }
        };

        while (std::getline(stream, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.rfind("- Entity:", 0) == 0) {
                flushCustomSection();
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
                flushCustomSection();
                currentSection = ComponentSection::Tag;
            } else if (trimmed == "TransformComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::Transform;
            } else if (trimmed == "MeshInstance3D:" || trimmed == "MeshComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::Mesh;
            } else if (trimmed == "MaterialComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::Material;
            } else if (trimmed == "RigidBody3D:" || trimmed == "RigidBodyComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::RigidBody;
            } else if (trimmed == "CollisionShape3D:" || trimmed == "CollisionShape:") {
                flushCustomSection();
                currentSection = ComponentSection::CollisionShape;
            } else if (trimmed == "HierarchyComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::Hierarchy;
            } else if (trimmed == "NativeScriptComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::NativeScript;
            } else if (trimmed == "DirectionalLightComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::DirectionalLight;
            } else if (trimmed == "PointLightComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::PointLight;
                if (!currentEntity.HasComponent<PointLightComponent>()) {
                    currentEntity.AddComponent<PointLightComponent>();
                }
            } else if (trimmed == "SpotLightComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::SpotLight;
                if (!currentEntity.HasComponent<SpotLightComponent>()) {
                    currentEntity.AddComponent<SpotLightComponent>();
                }
            } else if (trimmed == "SkyboxComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::Skybox;
            } else if (trimmed == "CameraComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::Camera;
                if (!currentEntity.HasComponent<CameraComponent>()) {
                    currentEntity.AddComponent<CameraComponent>();
                }
            } else if (trimmed == "AudioListenerComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::AudioListener;
                if (!currentEntity.HasComponent<AudioListenerComponent>()) {
                    currentEntity.AddComponent<AudioListenerComponent>();
                }
            } else if (trimmed == "AudioSourceComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::AudioSource;
                if (!currentEntity.HasComponent<AudioSourceComponent>()) {
                    currentEntity.AddComponent<AudioSourceComponent>();
                }
            } else if (trimmed == "VehicleAudioComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::VehicleAudio;
                if (!currentEntity.HasComponent<VehicleAudioComponent>()) {
                    currentEntity.AddComponent<VehicleAudioComponent>();
                }
            } else if (trimmed == "WheelComponent:" || trimmed == "VehicleWheel3D:") {
                flushCustomSection();
                currentSection = ComponentSection::Wheel;
                if (!currentEntity.HasComponent<WheelComponent>()) {
                    currentEntity.AddComponent<WheelComponent>();
                }
            } else if (trimmed == "VehicleComponent:" || trimmed == "VehicleComponent: true") {
                flushCustomSection();
                currentSection = ComponentSection::Vehicle;
                if (!currentEntity.HasComponent<VehicleComponent>()) {
                    currentEntity.AddComponent<VehicleComponent>();
                }
            } else if (trimmed == "SkeletalMeshComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::SkeletalMesh;
                if (!currentEntity.HasComponent<SkeletalMeshComponent>()) {
                    currentEntity.AddComponent<SkeletalMeshComponent>();
                }
            } else if (trimmed == "AnimatorComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::Animator;
                if (!currentEntity.HasComponent<AnimatorComponent>()) {
                    currentEntity.AddComponent<AnimatorComponent>();
                }
            } else if (trimmed == "CharacterControllerComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::CharacterController;
                if (!currentEntity.HasComponent<CharacterControllerComponent>()) {
                    currentEntity.AddComponent<CharacterControllerComponent>();
                }
            } else if (trimmed.back() == ':' && ((trimmed.length() >= 10 && trimmed.substr(trimmed.length() - 10) == "Component:") || (line.find_first_not_of(" \t") == 4 && trimmed.find(' ') == std::string::npos))) {
                flushCustomSection();
                currentSection = ComponentSection::Custom;
                currentCustomSectionName = trimmed.substr(0, trimmed.length() - 1);
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
                                if (FileSystem::Exists(fullPath)) {
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
                            pendingHierarchy.push_back({ currentEntity, pUUID, true, true });
                        } else if (trimmed.rfind("InheritTransform:", 0) == 0) {
                            if (!pendingHierarchy.empty() && pendingHierarchy.back().ChildEntity == currentEntity) {
                                pendingHierarchy.back().InheritTransform = (Trim(trimmed.substr(17)) == "true");
                            }
                        } else if (trimmed.rfind("InheritScale:", 0) == 0) {
                            if (!pendingHierarchy.empty() && pendingHierarchy.back().ChildEntity == currentEntity) {
                                pendingHierarchy.back().InheritScale = (Trim(trimmed.substr(13)) == "true");
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
                        } else if (trimmed.rfind("ShadowOrthoSize:", 0) == 0) {
                            dlc.ShadowOrthoSize = std::stof(Trim(trimmed.substr(16)));
                        } else if (trimmed.rfind("ShadowBias:", 0) == 0) {
                            dlc.ShadowBias = std::stof(Trim(trimmed.substr(11)));
                        } else if (trimmed.rfind("ShadowFilterRadius:", 0) == 0) {
                            dlc.ShadowFilterRadius = std::stof(Trim(trimmed.substr(19)));
                        } else if (trimmed.rfind("ShadowQuality:", 0) == 0) {
                            dlc.ShadowQuality = std::stoi(Trim(trimmed.substr(14)));
                        } else if (trimmed.rfind("Exposure:", 0) == 0) {
                            dlc.Exposure = std::stof(Trim(trimmed.substr(9)));
                        } else if (trimmed.rfind("ToneMappingMode:", 0) == 0) {
                            dlc.ToneMappingMode = std::stoi(Trim(trimmed.substr(16)));
                        } else if (trimmed.rfind("Gamma:", 0) == 0) {
                            dlc.Gamma = std::stof(Trim(trimmed.substr(6)));
                        }
                        break;
                    }
                    case ComponentSection::PointLight: {
                        if (!currentEntity.HasComponent<PointLightComponent>()) {
                            currentEntity.AddComponent<PointLightComponent>();
                        }
                        auto& plc = currentEntity.GetComponent<PointLightComponent>();
                        if (trimmed.rfind("Color:", 0) == 0) {
                            float r = 1, g = 1, b = 1;
                            sscanf(trimmed.c_str(), "Color: [ %f , %f , %f ]", &r, &g, &b);
                            plc.Color = glm::vec3(r, g, b);
                        } else if (trimmed.rfind("Intensity:", 0) == 0) {
                            plc.Intensity = std::stof(Trim(trimmed.substr(10)));
                        } else if (trimmed.rfind("Radius:", 0) == 0) {
                            plc.Radius = std::stof(Trim(trimmed.substr(7)));
                        } else if (trimmed.rfind("Falloff:", 0) == 0) {
                            plc.Falloff = std::stof(Trim(trimmed.substr(8)));
                        }
                        break;
                    }
                    case ComponentSection::SpotLight: {
                        if (!currentEntity.HasComponent<SpotLightComponent>()) {
                            currentEntity.AddComponent<SpotLightComponent>();
                        }
                        auto& slc = currentEntity.GetComponent<SpotLightComponent>();
                        if (trimmed.rfind("Enabled:", 0) == 0) {
                            slc.Enabled = (Trim(trimmed.substr(8)) == "true");
                        } else if (trimmed.rfind("Color:", 0) == 0) {
                            float r = 1, g = 1, b = 1;
                            sscanf(trimmed.c_str(), "Color: [ %f , %f , %f ]", &r, &g, &b);
                            slc.Color = glm::vec3(r, g, b);
                        } else if (trimmed.rfind("Intensity:", 0) == 0) {
                            slc.Intensity = std::stof(Trim(trimmed.substr(10)));
                        } else if (trimmed.rfind("Range:", 0) == 0) {
                            slc.Range = std::stof(Trim(trimmed.substr(6)));
                        } else if (trimmed.rfind("InnerConeAngle:", 0) == 0) {
                            slc.InnerConeAngle = std::stof(Trim(trimmed.substr(15)));
                        } else if (trimmed.rfind("OuterConeAngle:", 0) == 0) {
                            slc.OuterConeAngle = std::stof(Trim(trimmed.substr(15)));
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
                        } else if (trimmed.rfind("SkyboxType:", 0) == 0) {
                            sbc.Type = static_cast<SkyboxType>(std::stoi(Trim(trimmed.substr(11))));
                        } else if (trimmed.rfind("HDRPath:", 0) == 0) {
                            sbc.HDRPath = Trim(trimmed.substr(8));
                        } else if (trimmed.rfind("Intensity:", 0) == 0) {
                            sbc.Intensity = std::stof(Trim(trimmed.substr(10)));
                        } else if (trimmed.rfind("Exposure:", 0) == 0) {
                            sbc.Exposure = std::stof(Trim(trimmed.substr(9)));
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
                    case ComponentSection::Camera: {
                        if (!currentEntity.HasComponent<CameraComponent>()) {
                            currentEntity.AddComponent<CameraComponent>();
                        }
                        auto& cam = currentEntity.GetComponent<CameraComponent>();
                        if (trimmed.rfind("Primary:", 0) == 0) {
                            cam.Primary = (Trim(trimmed.substr(8)) == "true");
                        } else if (trimmed.rfind("FOV:", 0) == 0) {
                            cam.FOV = std::stof(Trim(trimmed.substr(4)));
                        } else if (trimmed.rfind("NearClip:", 0) == 0) {
                            cam.NearClip = std::stof(Trim(trimmed.substr(9)));
                        } else if (trimmed.rfind("FarClip:", 0) == 0) {
                            cam.FarClip = std::stof(Trim(trimmed.substr(8)));
                        }
                        break;
                    }
                    case ComponentSection::Material: {
                        if (!currentEntity.HasComponent<MaterialComponent>()) {
                            currentEntity.AddComponent<MaterialComponent>();
                        }
                        auto& mat = currentEntity.GetComponent<MaterialComponent>();
                        if (trimmed.rfind("MaterialHandle:", 0) == 0) {
                            mat.MaterialHandle = std::stoull(Trim(trimmed.substr(15)));
                            if (mat.MaterialHandle != 0) {
                                mat.MaterialAsset = AssetManager::GetMaterial(mat.MaterialHandle);
                            }
                        } else if (trimmed.rfind("AssetPath:", 0) == 0) {
                            std::string path = Trim(trimmed.substr(10));
                            if (!path.empty() && !mat.MaterialAsset) {
                                mat.MaterialAsset = AssetManager::GetMaterial(path);
                                if (mat.MaterialAsset) mat.MaterialHandle = mat.MaterialAsset->GetHandle();
                            }
                        } else if (trimmed.rfind("AlbedoPath:", 0) == 0) {
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
                    case ComponentSection::Wheel: {
                        if (!currentEntity.HasComponent<WheelComponent>()) {
                            currentEntity.AddComponent<WheelComponent>();
                        }
                        auto& wc = currentEntity.GetComponent<WheelComponent>();
                        if (trimmed.rfind("Radius:", 0) == 0) {
                            wc.Radius = std::stof(Trim(trimmed.substr(7)));
                        } else if (trimmed.rfind("Width:", 0) == 0) {
                            wc.Width = std::stof(Trim(trimmed.substr(6)));
                        } else if (trimmed.rfind("SuspensionMinLength:", 0) == 0) {
                            wc.SuspensionMinLength = std::stof(Trim(trimmed.substr(20)));
                        } else if (trimmed.rfind("SuspensionMaxLength:", 0) == 0) {
                            wc.SuspensionMaxLength = std::stof(Trim(trimmed.substr(20)));
                        } else if (trimmed.rfind("SpringFrequency:", 0) == 0) {
                            wc.SpringFrequency = std::stof(Trim(trimmed.substr(16)));
                        } else if (trimmed.rfind("SpringDamping:", 0) == 0) {
                            wc.SpringDamping = std::stof(Trim(trimmed.substr(14)));
                        } else if (trimmed.rfind("CanSteer:", 0) == 0) {
                            wc.CanSteer = (Trim(trimmed.substr(9)) == "true");
                        } else if (trimmed.rfind("MaxSteerAngle:", 0) == 0) {
                            wc.MaxSteerAngle = std::stof(Trim(trimmed.substr(14)));
                        } else if (trimmed.rfind("IsDriven:", 0) == 0) {
                            wc.IsDriven = (Trim(trimmed.substr(9)) == "true");
                        } else if (trimmed.rfind("HasHandbrake:", 0) == 0) {
                            wc.HasHandbrake = (Trim(trimmed.substr(13)) == "true");
                        } else if (trimmed.rfind("BrakeTorque:", 0) == 0) {
                            wc.BrakeTorque = std::stof(Trim(trimmed.substr(12)));
                        } else if (trimmed.rfind("HandBrakeTorque:", 0) == 0) {
                            wc.HandBrakeTorque = std::stof(Trim(trimmed.substr(16)));
                        } else if (trimmed.rfind("AutoSyncMesh:", 0) == 0) {
                            wc.AutoSyncMesh = (Trim(trimmed.substr(13)) == "true");
                        }
                        break;
                    }
                    case ComponentSection::Vehicle: {
                        if (!currentEntity.HasComponent<VehicleComponent>()) {
                            currentEntity.AddComponent<VehicleComponent>();
                        }
                        auto& vc = currentEntity.GetComponent<VehicleComponent>();
                        if (trimmed.rfind("ChassisMass:", 0) == 0) {
                            vc.ChassisMass = std::stof(Trim(trimmed.substr(12)));
                        } else if (trimmed.rfind("MaxEngineTorque:", 0) == 0) {
                            vc.MaxEngineTorque = std::stof(Trim(trimmed.substr(16)));
                        } else if (trimmed.rfind("MaxRPM:", 0) == 0) {
                            vc.MaxRPM = std::stof(Trim(trimmed.substr(7)));
                        } else if (trimmed.rfind("MinRPM:", 0) == 0) {
                            vc.MinRPM = std::stof(Trim(trimmed.substr(7)));
                        } else if (trimmed.rfind("MaxBrakeTorque:", 0) == 0) {
                            vc.MaxBrakeTorque = std::stof(Trim(trimmed.substr(15)));
                        } else if (trimmed.rfind("MaxHandBrakeTorque:", 0) == 0) {
                            vc.MaxHandBrakeTorque = std::stof(Trim(trimmed.substr(19)));
                        } else if (trimmed.rfind("AntiRollBarStiffness:", 0) == 0) {
                            vc.AntiRollBarStiffness = std::stof(Trim(trimmed.substr(21)));
                        } else if (trimmed.rfind("DefaultSuspensionFrequency:", 0) == 0) {
                            vc.DefaultSuspensionFrequency = std::stof(Trim(trimmed.substr(27)));
                        } else if (trimmed.rfind("DefaultSuspensionDamping:", 0) == 0) {
                            vc.DefaultSuspensionDamping = std::stof(Trim(trimmed.substr(25)));
                        } else if (trimmed.rfind("FourWheelDrive:", 0) == 0) {
                            vc.FourWheelDrive = (Trim(trimmed.substr(15)) == "true");
                        } else if (trimmed.rfind("CenterOfMassOffset:", 0) == 0) {
                            float x = 0.0f, y = 0.0f, z = 0.0f;
                            sscanf(trimmed.c_str(), "CenterOfMassOffset: [ %f , %f , %f ]", &x, &y, &z);
                            vc.CenterOfMassOffset = glm::vec3(x, y, z);
                        }
                        break;
                    }
                    case ComponentSection::AudioListener: {
                        if (!currentEntity.HasComponent<AudioListenerComponent>()) {
                            currentEntity.AddComponent<AudioListenerComponent>();
                        }
                        auto& alc = currentEntity.GetComponent<AudioListenerComponent>();
                        if (trimmed.rfind("Active:", 0) == 0) {
                            alc.Active = (Trim(trimmed.substr(7)) == "true");
                        }
                        break;
                    }
                    case ComponentSection::AudioSource: {
                        if (!currentEntity.HasComponent<AudioSourceComponent>()) {
                            currentEntity.AddComponent<AudioSourceComponent>();
                        }
                        auto& asc = currentEntity.GetComponent<AudioSourceComponent>();
                        if (trimmed.rfind("AssetPath:", 0) == 0) {
                            asc.AssetPath = Trim(trimmed.substr(10));
                        } else if (trimmed.rfind("Volume:", 0) == 0) {
                            try { asc.Volume = std::stof(Trim(trimmed.substr(7))); } catch (...) {}
                        } else if (trimmed.rfind("Pitch:", 0) == 0) {
                            try { asc.Pitch = std::stof(Trim(trimmed.substr(6))); } catch (...) {}
                        } else if (trimmed.rfind("Loop:", 0) == 0) {
                            asc.Loop = (Trim(trimmed.substr(5)) == "true");
                        } else if (trimmed.rfind("PlayOnAwake:", 0) == 0) {
                            asc.PlayOnAwake = (Trim(trimmed.substr(12)) == "true");
                        } else if (trimmed.rfind("Spatialized:", 0) == 0) {
                            asc.Spatialized = (Trim(trimmed.substr(12)) == "true");
                        } else if (trimmed.rfind("AttenuationModel:", 0) == 0) {
                            try { asc.Attenuation = static_cast<AttenuationModel>(std::stoi(Trim(trimmed.substr(17)))); } catch (...) {}
                        } else if (trimmed.rfind("MinDistance:", 0) == 0) {
                            try { asc.MinDistance = std::stof(Trim(trimmed.substr(12))); } catch (...) {}
                        } else if (trimmed.rfind("MaxDistance:", 0) == 0) {
                            try { asc.MaxDistance = std::stof(Trim(trimmed.substr(12))); } catch (...) {}
                        } else if (trimmed.rfind("RollOff:", 0) == 0) {
                            try { asc.RollOff = std::stof(Trim(trimmed.substr(8))); } catch (...) {}
                        } else if (trimmed.rfind("InnerAngle:", 0) == 0) {
                            try { asc.InnerAngle = std::stof(Trim(trimmed.substr(11))); } catch (...) {}
                        } else if (trimmed.rfind("OuterAngle:", 0) == 0) {
                            try { asc.OuterAngle = std::stof(Trim(trimmed.substr(11))); } catch (...) {}
                        } else if (trimmed.rfind("OuterGain:", 0) == 0) {
                            try { asc.OuterGain = std::stof(Trim(trimmed.substr(10))); } catch (...) {}
                        } else if (trimmed.rfind("DopplerFactor:", 0) == 0) {
                            try { asc.DopplerFactor = std::stof(Trim(trimmed.substr(14))); } catch (...) {}
                        }
                        break;
                    }
                    case ComponentSection::VehicleAudio: {
                        if (!currentEntity.HasComponent<VehicleAudioComponent>()) {
                            currentEntity.AddComponent<VehicleAudioComponent>();
                        }
                        auto& vac = currentEntity.GetComponent<VehicleAudioComponent>();
                        if (trimmed.rfind("Enabled:", 0) == 0) {
                            vac.Enabled = (Trim(trimmed.substr(8)) == "true");
                        } else if (trimmed.rfind("EnginePowerClipPath:", 0) == 0) {
                            vac.EnginePowerClipPath = Trim(trimmed.substr(20));
                        } else if (trimmed.rfind("EngineCoastClipPath:", 0) == 0) {
                            vac.EngineCoastClipPath = Trim(trimmed.substr(20));
                        } else if (trimmed.rfind("MinEnginePitch:", 0) == 0) {
                            try { vac.MinEnginePitch = std::stof(Trim(trimmed.substr(15))); } catch (...) {}
                        } else if (trimmed.rfind("MaxEnginePitch:", 0) == 0) {
                            try { vac.MaxEnginePitch = std::stof(Trim(trimmed.substr(15))); } catch (...) {}
                        } else if (trimmed.rfind("EngineMasterVolume:", 0) == 0) {
                            try { vac.EngineMasterVolume = std::stof(Trim(trimmed.substr(19))); } catch (...) {}
                        } else if (trimmed.rfind("ThrottleSmoothing:", 0) == 0) {
                            try { vac.ThrottleSmoothing = std::stof(Trim(trimmed.substr(18))); } catch (...) {}
                        } else if (trimmed.rfind("TireSquealClipPath:", 0) == 0) {
                            vac.TireSquealClipPath = Trim(trimmed.substr(19));
                        } else if (trimmed.rfind("SlipThreshold:", 0) == 0) {
                            try { vac.SlipThreshold = std::stof(Trim(trimmed.substr(14))); } catch (...) {}
                        } else if (trimmed.rfind("TireSquealMasterVolume:", 0) == 0) {
                            try { vac.TireSquealMasterVolume = std::stof(Trim(trimmed.substr(23))); } catch (...) {}
                        } else if (trimmed.rfind("TireSquealFadeSpeed:", 0) == 0) {
                            try { vac.TireSquealFadeSpeed = std::stof(Trim(trimmed.substr(20))); } catch (...) {}
                        } else if (trimmed.rfind("SuspensionThudClipPath:", 0) == 0) {
                            vac.SuspensionThudClipPath = Trim(trimmed.substr(23));
                        } else if (trimmed.rfind("SuspensionThudThreshold:", 0) == 0) {
                            try { vac.SuspensionThudThreshold = std::stof(Trim(trimmed.substr(24))); } catch (...) {}
                        } else if (trimmed.rfind("SuspensionMasterVolume:", 0) == 0) {
                            try { vac.SuspensionMasterVolume = std::stof(Trim(trimmed.substr(23))); } catch (...) {}
                        } else if (trimmed.rfind("CollisionImpactClipPath:", 0) == 0) {
                            vac.CollisionImpactClipPath = Trim(trimmed.substr(24));
                        } else if (trimmed.rfind("CollisionDecelThreshold:", 0) == 0) {
                            try { vac.CollisionDecelThreshold = std::stof(Trim(trimmed.substr(24))); } catch (...) {}
                        } else if (trimmed.rfind("CollisionMasterVolume:", 0) == 0) {
                            try { vac.CollisionMasterVolume = std::stof(Trim(trimmed.substr(22))); } catch (...) {}
                        } else if (trimmed.rfind("Spatialized:", 0) == 0) {
                            vac.Spatialized = (Trim(trimmed.substr(12)) == "true");
                        } else if (trimmed.rfind("MinDistance:", 0) == 0) {
                            try { vac.MinDistance = std::stof(Trim(trimmed.substr(12))); } catch (...) {}
                        } else if (trimmed.rfind("MaxDistance:", 0) == 0) {
                            try { vac.MaxDistance = std::stof(Trim(trimmed.substr(12))); } catch (...) {}
                        }
                        break;
                    }
                    case ComponentSection::SkeletalMesh: {
                        if (!currentEntity.HasComponent<SkeletalMeshComponent>()) {
                            currentEntity.AddComponent<SkeletalMeshComponent>();
                        }
                        auto& smc = currentEntity.GetComponent<SkeletalMeshComponent>();
                        if (trimmed.rfind("AssetPath:", 0) == 0) {
                            smc.AssetPath = Trim(trimmed.substr(10));
                        } else if (trimmed.rfind("PatternMode:", 0) == 0) {
                            smc.PatternMode = std::stoi(Trim(trimmed.substr(12)));
                        } else if (trimmed.rfind("TileScale:", 0) == 0) {
                            smc.TileScale = std::stof(Trim(trimmed.substr(10)));
                        } else if (trimmed.rfind("Color1:", 0) == 0) {
                            float r = 1, g = 1, b = 1, a = 1;
                            sscanf(trimmed.c_str(), "Color1: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                            smc.Color1 = glm::vec4(r, g, b, a);
                        } else if (trimmed.rfind("Color2:", 0) == 0) {
                            float r = 0, g = 0, b = 0, a = 1;
                            sscanf(trimmed.c_str(), "Color2: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                            smc.Color2 = glm::vec4(r, g, b, a);
                        } else if (trimmed.rfind("CastShadows:", 0) == 0) {
                            smc.CastShadows = (Trim(trimmed.substr(12)) == "true");
                        } else if (trimmed.rfind("Visible:", 0) == 0) {
                            smc.Visible = (Trim(trimmed.substr(8)) == "true");
                        }
                        break;
                    }
                    case ComponentSection::Animator: {
                        if (!currentEntity.HasComponent<AnimatorComponent>()) {
                            currentEntity.AddComponent<AnimatorComponent>();
                        }
                        auto& ac = currentEntity.GetComponent<AnimatorComponent>();
                        if (trimmed.rfind("Playing:", 0) == 0) {
                            ac.Playing = (Trim(trimmed.substr(8)) == "true");
                        } else if (trimmed.rfind("PlaybackSpeed:", 0) == 0) {
                            ac.PlaybackSpeed = std::stof(Trim(trimmed.substr(14)));
                        } else if (trimmed.rfind("Speed:", 0) == 0) {
                            ac.Speed = std::stof(Trim(trimmed.substr(6)));
                        } else if (trimmed.rfind("IsGrounded:", 0) == 0) {
                            ac.IsGrounded = (Trim(trimmed.substr(11)) == "true");
                        } else if (trimmed.rfind("VerticalVelocity:", 0) == 0) {
                            ac.VerticalVelocity = std::stof(Trim(trimmed.substr(17)));
                        }
                        break;
                    }
                    case ComponentSection::CharacterController: {
                        if (!currentEntity.HasComponent<CharacterControllerComponent>()) {
                            currentEntity.AddComponent<CharacterControllerComponent>();
                        }
                        auto& ccc = currentEntity.GetComponent<CharacterControllerComponent>();
                        if (trimmed.rfind("WalkSpeed:", 0) == 0) {
                            ccc.WalkSpeed = std::stof(Trim(trimmed.substr(10)));
                        } else if (trimmed.rfind("RunSpeed:", 0) == 0) {
                            ccc.RunSpeed = std::stof(Trim(trimmed.substr(9)));
                        } else if (trimmed.rfind("JumpSpeed:", 0) == 0) {
                            ccc.JumpSpeed = std::stof(Trim(trimmed.substr(10)));
                        } else if (trimmed.rfind("TurnSmoothing:", 0) == 0) {
                            ccc.TurnSmoothing = std::stof(Trim(trimmed.substr(14)));
                        } else if (trimmed.rfind("OrientRotationToMovement:", 0) == 0) {
                            ccc.OrientRotationToMovement = (Trim(trimmed.substr(25)) == "true");
                        } else if (trimmed.rfind("Radius:", 0) == 0) {
                            ccc.Settings.Radius = std::stof(Trim(trimmed.substr(7)));
                        } else if (trimmed.rfind("HalfHeight:", 0) == 0) {
                            ccc.Settings.HalfHeight = std::stof(Trim(trimmed.substr(11)));
                        } else if (trimmed.rfind("Mass:", 0) == 0) {
                            ccc.Settings.Mass = std::stof(Trim(trimmed.substr(5)));
                        } else if (trimmed.rfind("MaxSlopeAngle:", 0) == 0) {
                            ccc.Settings.MaxSlopeAngle = std::stof(Trim(trimmed.substr(14)));
                        } else if (trimmed.rfind("StepOffset:", 0) == 0) {
                            ccc.Settings.StepOffset = std::stof(Trim(trimmed.substr(11)));
                        }
                        break;
                    }
                    case ComponentSection::Custom: {
                        customSectionLines.push_back(line);
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        flushCustomSection();

        // 1. Resolve MeshInstance3D meshes if VAO is null
        auto meshView = m_Scene->GetRegistry().view<MeshInstance3D>();
        for (auto entityID : meshView) {
            auto& mc = meshView.get<MeshInstance3D>(entityID);
            if (!mc.VAO) {
                if (mc.AssetPath == "builtin://cube" || mc.AssetPath.empty()) {
                    mc.VAO = m_Context.CubeMesh;
                    if (mc.AssetPath.empty()) mc.AssetPath = "builtin://cube";
                } else if (mc.AssetPath == "builtin://sphere") {
                    mc.VAO = m_Context.SphereMesh;
                } else if (mc.AssetPath == "builtin://cylinder") {
                    mc.VAO = m_Context.CylinderMesh;
                } else if (mc.AssetPath == "builtin://cone") {
                    mc.VAO = m_Context.ConeMesh;
                } else if (mc.AssetPath == "builtin://plane") {
                    mc.VAO = m_Context.PlaneMesh;
                } else {
                    auto fullPath = FileSystem::ResolveAssetPath(mc.AssetPath);
                    if (FileSystem::Exists(fullPath)) {
                        auto loaded = AssetSerializer::LoadMesh(fullPath);
                        if (loaded.Success) {
                            mc.VAO = loaded.VAO;
                        }
                    }
                    if (!mc.VAO) {
                        mc.VAO = m_Context.CubeMesh;
                    }
                }
            }
        }

        // 2. Resolve SkeletalMeshComponent and AnimatorComponent
        auto skeletalView = m_Scene->GetRegistry().view<SkeletalMeshComponent>();
        for (auto entityID : skeletalView) {
            auto& smc = skeletalView.get<SkeletalMeshComponent>(entityID);
            if (!smc.Mesh) {
                bool isProcedural = (smc.AssetPath == "procedural://biped" || smc.AssetPath.empty());
                bool fileLoaded = false;
                if (!isProcedural) {
                    auto fullPath = FileSystem::ResolveAssetPath(smc.AssetPath);
                    if (FileSystem::Exists(fullPath)) {
                        SkeletalMeshData meshData;
                        std::shared_ptr<Skeleton> skeleton;
                        std::vector<std::shared_ptr<AnimationClip>> clips;
                        if (SkeletalMeshImporter::Load(fullPath, meshData, skeleton, clips)) {
                            smc.Mesh = std::make_shared<SkeletalMesh>(meshData);
                            fileLoaded = true;
                            if (m_Scene->GetRegistry().all_of<AnimatorComponent>(entityID)) {
                                auto& ac = m_Scene->GetRegistry().get<AnimatorComponent>(entityID);
                                if (!ac.AnimatorInstance && skeleton) {
                                    ac.AnimatorInstance = std::make_shared<Animator>(skeleton);
                                }
                            }
                        }
                    }
                }

                if (!fileLoaded) {
                    // Fallback to procedural biped generator
                    SkeletalMeshData bipedData;
                    std::shared_ptr<Skeleton> bipedSkeleton;
                    std::vector<std::shared_ptr<AnimationClip>> bipedClips;
                    if (SkeletalMeshImporter::CreateProceduralBiped(bipedData, bipedSkeleton, bipedClips)) {
                        smc.Mesh = std::make_shared<SkeletalMesh>(bipedData);
                        if (smc.AssetPath.empty()) {
                            smc.AssetPath = "procedural://biped";
                        }

                        if (m_Scene->GetRegistry().all_of<AnimatorComponent>(entityID)) {
                            auto& ac = m_Scene->GetRegistry().get<AnimatorComponent>(entityID);
                            if (!ac.AnimatorInstance) {
                                auto animator = std::make_shared<Animator>(bipedSkeleton);
                                auto stateMachine = std::make_shared<AnimationStateMachine>("LocomotionStateMachine");

                                if (bipedClips.size() >= 2) {
                                    AnimationState stateIdle;
                                    stateIdle.Name = "Idle";
                                    stateIdle.Type = AnimationNodeType::Clip;
                                    stateIdle.Clip = bipedClips[0];
                                    stateIdle.Loop = true;
                                    stateMachine->AddState(stateIdle);

                                    AnimationState stateWalk;
                                    stateWalk.Name = "Walk";
                                    stateWalk.Type = AnimationNodeType::Clip;
                                    stateWalk.Clip = bipedClips[1];
                                    stateWalk.Loop = true;
                                    stateMachine->AddState(stateWalk);

                                    AnimationTransition tIdleToWalk;
                                    tIdleToWalk.FromState = "Idle";
                                    tIdleToWalk.ToState = "Walk";
                                    tIdleToWalk.Duration = 0.2f;
                                    tIdleToWalk.Condition = [stateMachine]() { return stateMachine->GetFloat("Speed") > 0.1f; };
                                    stateMachine->AddTransition(tIdleToWalk);

                                    AnimationTransition tWalkToIdle;
                                    tWalkToIdle.FromState = "Walk";
                                    tWalkToIdle.ToState = "Idle";
                                    tWalkToIdle.Duration = 0.2f;
                                    tWalkToIdle.Condition = [stateMachine]() { return stateMachine->GetFloat("Speed") <= 0.1f; };
                                    stateMachine->AddTransition(tWalkToIdle);

                                    animator->SetStateMachine(stateMachine);
                                    animator->Update(0.0f);
                                }
                                ac.AnimatorInstance = animator;
                            }
                        }
                    }
                }
            }
        }

        // Recreate Physics Bodies in PhysicsScene if active
        if (m_PhysicsScene) {
            auto rbView = m_Scene->GetRegistry().view<RigidBody3D, TransformComponent>();
            for (auto entityID : rbView) {
                auto& rb = rbView.get<RigidBody3D>(entityID);
                auto& tc = rbView.get<TransformComponent>(entityID);

                if (m_Scene->GetRegistry().all_of<VehicleComponent>(entityID) || m_Scene->GetRegistry().all_of<CharacterControllerComponent>(entityID)) {
                    continue; // Skip static body creation; handled by Vehicle or CharacterController
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
                        bodyID = m_PhysicsScene->CreateSphere(spawnPos, radius, isDynamic, friction, restitution, rb.Mass);
                        break;
                    case CollisionShapeType::Cylinder:
                        bodyID = m_PhysicsScene->CreateCylinder(spawnPos, halfHeight, radius, isDynamic, friction, restitution, rb.Mass);
                        break;
                    case CollisionShapeType::Cone:
                        bodyID = m_PhysicsScene->CreateCone(spawnPos, halfHeight, radius, isDynamic, friction, restitution, rb.Mass);
                        break;
                    case CollisionShapeType::Box:
                    default:
                        bodyID = m_PhysicsScene->CreateBox(spawnPos, halfExtent, isDynamic, friction, restitution, rb.Mass);
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
                    static_cast<entt::entity>(parentEntity), item.InheritTransform, item.InheritScale);

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
