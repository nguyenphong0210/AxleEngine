#include <Axle/AnimationSceneBinding.hpp>
#include <Axle/ShaderPropertyPersistence.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/MaterialComponentCodec.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/SceneRenderBridge.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/SkeletalMesh.hpp>
#include <Axle/SkeletalMeshImporter.hpp>
#include <Axle/Skeleton.hpp>
#include <Axle/AnimationClip.hpp>
#include <Axle/Animator.hpp>
#include <Axle/AnimationStateMachine.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/UI/UIParser.hpp>

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

    static std::string EncodeVFXSceneData(const std::string& text) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string encoded;
        encoded.reserve(text.size() * 2);
        for (unsigned char c : text) { encoded += digits[c >> 4]; encoded += digits[c & 15]; }
        return encoded;
    }
    static bool DecodeVFXSceneData(const std::string& text, std::string& decoded) {
        if (text.size() % 2) return false;
        auto digit = [](char c) -> int { return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : -1; };
        for (size_t i = 0; i < text.size(); i += 2) {
            int a = digit(text[i]), b = digit(text[i+1]);
            if (a < 0 || b < 0) return false;
            decoded += static_cast<char>((a << 4) | b);
        }
        return true;
    }

    SceneSerializer::SceneSerializer(Scene* scene, PhysicsScene* physicsScene)
        : m_Scene(scene), m_PhysicsScene(physicsScene) {
    }

    SceneSerializer::SceneSerializer(SceneData* scene, PhysicsScene* physicsScene)
        : m_Scene(scene), m_PhysicsScene(physicsScene) {
    }

    bool SceneSerializer::Serialize(const std::filesystem::path& filepath) {
        const auto serialized = SerializeToString();
        return !serialized.empty() && FileSystem::WriteIdentifiedTextFile(filepath, serialized);
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

                if (assetPath.empty()) assetPath = IdentifyBuiltinMesh(mc.VAO, m_Context);

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
                ss << "      ShadowSlopeBias: " << dlc.ShadowSlopeBias << "\n";
                ss << "      ShadowNormalBias: " << dlc.ShadowNormalBias << "\n";
                ss << "      ShadowFilterRadius: " << dlc.ShadowFilterRadius << "\n";
                ss << "      ShadowQuality: " << dlc.ShadowQuality << "\n";
                ss << "      CascadeSplitLambda: " << dlc.CascadeSplitLambda << "\n";
                ss << "      MaxShadowDistance: " << dlc.MaxShadowDistance << "\n";
                ss << "      Exposure: " << dlc.Exposure << "\n";
                ss << "      ToneMappingMode: " << dlc.ToneMappingMode << "\n";
                ss << "      Gamma: " << dlc.Gamma << "\n";
            }

            // MaterialComponent
            if (entity.HasComponent<MaterialComponent>()) {
                WriteMaterialComponent(ss,entity.GetComponent<MaterialComponent>());
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

            // SkeletalMeshComponent
            if (entity.HasComponent<SkeletalMeshComponent>()) {
                auto& smc = entity.GetComponent<SkeletalMeshComponent>();
                std::string rigFallback, unusedController;
                if (!CaptureAnimationBinding(&smc, nullptr, rigFallback, unusedController)) { return ""; }
                ss << "    SkeletalMeshComponent:\n";
                ss << "      RigFallbackData: " << rigFallback << "\n";
                ss << "      AssetPath: " << smc.AssetPath << "\n";
                if (!smc.SkeletonPath.empty()) {
                    ss << "      SkeletonPath: " << smc.SkeletonPath << "\n";
                }
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
                ss << "      ControllerPath: " << ac.ControllerPath << "\n";
                ss << "      IsUnique: " << (ac.IsUnique ? "true" : "false") << "\n";
                ss << "      Playing: " << (ac.Playing ? "true" : "false") << "\n";
                ss << "      PlaybackSpeed: " << ac.PlaybackSpeed << "\n";
                ss << "      Speed: " << ac.Speed << "\n";
                ss << "      IsGrounded: " << (ac.IsGrounded ? "true" : "false") << "\n";
                ss << "      VerticalVelocity: " << ac.VerticalVelocity << "\n";
                std::string unusedRig, controllerFallback;
                if (!CaptureAnimationBinding(nullptr, &ac, unusedRig, controllerFallback)) { return ""; }
                ss << "      InlineFallbackData: " << controllerFallback << "\n";
                for (const auto& [param, val] : ac.FloatOverrides) {
                    ss << "      FloatOverride: " << param << " " << val << "\n";
                }
                for (const auto& [param, val] : ac.IntOverrides) {
                    ss << "      IntOverride: " << param << " " << val << "\n";
                }
                for (const auto& [param, val] : ac.BoolOverrides) {
                    ss << "      BoolOverride: " << param << " " << (val ? "true" : "false") << "\n";
                }
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

            // ParticleEmitterComponent
            if (entity.HasComponent<ParticleEmitterComponent>()) {
                auto& pec = entity.GetComponent<ParticleEmitterComponent>();
                ss << "    ParticleEmitterComponent:\n";
                if (!pec.AssetPath.empty()) {
                    ss << "      AssetPath: " << pec.AssetPath << "\n";
                }
                ss << "      Name: " << pec.Config.Name << "\n";
                ss << "      Active: " << (pec.Active ? "true" : "false") << "\n";
                ss << "      AutoPlay: " << (pec.AutoPlay ? "true" : "false") << "\n";
                ss << "      Visible: " << (pec.Visible ? "true" : "false") << "\n";
                ss << "      SimulationSpace: " << (pec.SimulationSpace == ParticleSimulationSpace::World ? "World" : "Local") << "\n";
                ss << "      EmissionRate: " << pec.Config.EmissionRate << "\n";
                ss << "      BurstCount: " << pec.Config.BurstCount << "\n";
                ss << "      BurstInterval: " << pec.Config.BurstInterval << "\n";
                ss << "      MaxParticles: " << pec.Config.MaxParticles << "\n";
                ss << "      Lifetime: [ " << pec.Config.Lifetime.Min << " , " << pec.Config.Lifetime.Max << " ]\n";
                ss << "      InitialSpeed: [ " << pec.Config.InitialSpeed.Min << " , " << pec.Config.InitialSpeed.Max << " ]\n";
                ss << "      StartSize: [ " << pec.Config.StartSize.Min << " , " << pec.Config.StartSize.Max << " ]\n";
                ss << "      EndSize: [ " << pec.Config.EndSize.Min << " , " << pec.Config.EndSize.Max << " ]\n";
                ss << "      StartRotation: [ " << pec.Config.StartRotation.Min << " , " << pec.Config.StartRotation.Max << " ]\n";
                ss << "      AngularVelocity: [ " << pec.Config.AngularVelocity.Min << " , " << pec.Config.AngularVelocity.Max << " ]\n";
                ss << "      GravityModifier: " << pec.Config.GravityModifier << "\n";
                ss << "      Drag: " << pec.Config.Drag << "\n";
                ss << "      StartColor: [ " << pec.Config.ColorOverLifetime.StartColor.r << " , " << pec.Config.ColorOverLifetime.StartColor.g << " , " << pec.Config.ColorOverLifetime.StartColor.b << " , " << pec.Config.ColorOverLifetime.StartColor.a << " ]\n";
                ss << "      EndColor: [ " << pec.Config.ColorOverLifetime.EndColor.r << " , " << pec.Config.ColorOverLifetime.EndColor.g << " , " << pec.Config.ColorOverLifetime.EndColor.b << " , " << pec.Config.ColorOverLifetime.EndColor.a << " ]\n";
                ss << "      BlendMode: " << static_cast<int>(pec.Config.BlendMode) << "\n";
                ss << "      Orientation: " << static_cast<int>(pec.Config.Orientation) << "\n";
                ss << "      ShapeType: " << static_cast<int>(pec.Config.Shape.Type) << "\n";
                ss << "      ShapeRadius: " << pec.Config.Shape.Radius << "\n";
                ss << "      ShapeConeAngle: " << pec.Config.Shape.ConeAngle << "\n";
                ss << "      ShapeBoxExtents: [ " << pec.Config.Shape.BoxExtents.x << " , " << pec.Config.Shape.BoxExtents.y << " , " << pec.Config.Shape.BoxExtents.z << " ]\n";
                ss << "      ShapeLineLength: " << pec.Config.Shape.LineLength << "\n";
                ss << "      ShapeDirection: [ " << pec.Config.Shape.Direction.x << " , " << pec.Config.Shape.Direction.y << " , " << pec.Config.Shape.Direction.z << " ]\n";
                ss << "      ShapeSurfaceOnly: " << (pec.Config.Shape.SurfaceOnly ? "true" : "false") << "\n";
                ss << "      TexturePath: " << pec.TexturePath << "\n";
                ss << "      NormalMapPath: " << pec.NormalMapPath << "\n";
                ss << "      TextureRows: " << pec.Config.TextureRows << "\n";
                ss << "      TextureCols: " << pec.Config.TextureCols << "\n";
                ss << "      TotalFrames: " << pec.Config.TotalFrames << "\n";
                ss << "      SubFrameBlending: " << (pec.Config.SubFrameBlending ? "true" : "false") << "\n";
                ss << "      LightingEnabled: " << (pec.Config.LightingEnabled ? "true" : "false") << "\n";
                ss << "      WrapLighting: " << pec.Config.WrapLighting << "\n";
                ss << "      ScatteringFactor: " << pec.Config.ScatteringFactor << "\n";
                ss << "      NormalMapStrength: " << pec.Config.NormalMapStrength << "\n";
                ss << "      NearFadeEnabled: " << (pec.Config.NearFadeEnabled ? "true" : "false") << "\n";
                ss << "      NearFadeStart: " << pec.Config.NearFadeStart << "\n";
                ss << "      NearFadeEnd: " << pec.Config.NearFadeEnd << "\n";
                ss << "      FarFadeEnabled: " << (pec.Config.FarFadeEnabled ? "true" : "false") << "\n";
                ss << "      FarFadeStart: " << pec.Config.FarFadeStart << "\n";
                ss << "      FarFadeEnd: " << pec.Config.FarFadeEnd << "\n";
                ss << "      SoftnessDistance: " << pec.Config.SoftnessDistance << "\n";
                ss << "      CurlNoiseEnabled: " << (pec.Config.CurlNoise.Enabled ? "true" : "false") << "\n";
                ss << "      CurlNoiseStrength: " << pec.Config.CurlNoise.Strength << "\n";
                ss << "      CurlNoiseFrequency: " << pec.Config.CurlNoise.Frequency << "\n";
                ss << "      CollisionEnabled: " << (pec.Config.Collision.Enabled ? "true" : "false") << "\n";
                ss << "      CollisionRestitution: " << pec.Config.Collision.Restitution << "\n";
                ss << "      CollisionFriction: " << pec.Config.Collision.Friction << "\n";
                ss << "      CollisionParticleRadius: " << pec.Config.Collision.ParticleRadius << "\n";
                ss << "      EmitterStackCount: " << pec.EmitterStacks.size() << "\n";
                for (size_t i = 0; i < pec.EmitterStacks.size(); ++i) {
                    const auto& s = pec.EmitterStacks[i];
                    ss << "      Stack_" << i << "_Name: " << s.Name << "\n";
                    ss << "      Stack_" << i << "_Enabled: " << (s.Enabled ? "true" : "false") << "\n";
                    ss << "      Stack_" << i << "_Visible: " << (s.Visible ? "true" : "false") << "\n";
                    ss << "      Stack_" << i << "_TexturePath: " << s.TexturePath << "\n";
                    ss << "      Stack_" << i << "_NormalMapPath: " << s.NormalMapPath << "\n";
                    ss << "      Stack_" << i << "_MaterialPath: " << s.MaterialPath << "\n";
                    ss << "      Stack_" << i << "_MaxParticles: " << s.Config.MaxParticles << "\n";
                    ss << "      Stack_" << i << "_EmissionRate: " << s.Config.EmissionRate << "\n";
                    ss << "      Stack_" << i << "_Duration: " << s.Config.Duration << "\n";
                    ss << "      Stack_" << i << "_Looping: " << (s.Config.Looping ? "true" : "false") << "\n";
                }
                // Canonical versioned payload supersedes the legacy per-field adapter above.
                ss << (pec.AssetPath.empty() ? "      EffectData: " : "      EffectFallbackData: ")
                   << EncodeVFXSceneData(ParticleEffectSerializer::Serialize(pec.GetEffectDefinition()))
                   << "\n";
            }

            // CharacterVFXComponent
            if (entity.HasComponent<CharacterVFXComponent>()) {
                auto& cvfx = entity.GetComponent<CharacterVFXComponent>();
                ss << "    CharacterVFXComponent:\n";
                ss << "      Enabled: " << (cvfx.Enabled ? "true" : "false") << "\n";
                ss << "      FootstepMinSpeed: " << cvfx.Config.FootstepMinSpeed << "\n";
                ss << "      StrideLength: " << cvfx.Config.StrideLength << "\n";
                ss << "      FootSpread: " << cvfx.Config.FootSpread << "\n";
                ss << "      WalkPuffCount: " << cvfx.Config.WalkPuffCount << "\n";
                ss << "      RunPuffCount: " << cvfx.Config.RunPuffCount << "\n";
                ss << "      LandingImpactThreshold: " << cvfx.Config.LandingImpactThreshold << "\n";
                ss << "      HeavyLandingThreshold: " << cvfx.Config.HeavyLandingThreshold << "\n";
                ss << "      NormalLandingCount: " << cvfx.Config.NormalLandingCount << "\n";
                ss << "      HeavyLandingCount: " << cvfx.Config.HeavyLandingCount << "\n";
            }

            // UICanvasComponent
            if (entity.HasComponent<UICanvasComponent>()) {
                auto& uic = entity.GetComponent<UICanvasComponent>();
                if (uic.Document && uic.AssetPath.empty()) {
                    uic.AssetPath = "assets/ui/" + entity.GetName() + ".axleui";
                }
                if (uic.Document && !uic.AssetPath.empty()) {
                    std::filesystem::path fullDocPath = uic.AssetPath;
                    if (!std::filesystem::exists(fullDocPath)) {
                        fullDocPath = FileSystem::GetAssetDirectory() / uic.AssetPath;
                    }
                    if (uic.Document->IsDirty() || !std::filesystem::exists(fullDocPath)) {
                        if (fullDocPath.has_parent_path() && !std::filesystem::exists(fullDocPath.parent_path())) {
                            std::filesystem::create_directories(fullDocPath.parent_path());
                        }
                        uic.Document->SaveToFile(fullDocPath.string());
                    }
                }
                ss << "    UICanvasComponent:\n";
                ss << "      AssetPath: " << uic.AssetPath << "\n";
                if (uic.DataSourceEntityID != 0) {
                    ss << "      DataSourceEntityID: " << static_cast<uint64_t>(uic.DataSourceEntityID) << "\n";
                }
                ss << "      SortOrder: " << uic.SortOrder << "\n";
                ss << "      Visible: " << (uic.Visible ? "true" : "false") << "\n";
            }

            // Custom Entity Serializer Hook (for plugins e.g. SplineComponent, TerrainComponent)
            if (!m_Scene->SerializeExtensions(entity, ss)) return {};
            if (m_Context.CustomEntitySerializer) {
                m_Context.CustomEntitySerializer(entity, ss);
            }

            // Passive Extension Storage (unknown/plugin component sections preserved)
            if (entity.HasComponent<PassiveExtensionComponent>()) {
                const auto& pec = entity.GetComponent<PassiveExtensionComponent>();
                for (const auto& section : pec.Sections) {
                    ss << "    " << section.SectionName << ":\n";
                    for (const auto& l : section.Lines) {
                        ss << l << "\n";
                    }
                }
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

        m_ResourceOwnerPath = filepath.generic_string();
        bool success = DeserializeFromString(content);
        m_ResourceOwnerPath.clear();
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
        bool vfxReferenceLoaded = false;
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
            AudioListener,
            AudioSource,
            SkeletalMesh,
            Animator,
            CharacterController,
            ParticleEmitter,
            CharacterVFX,
            UICanvas,
            Custom
        };
        ComponentSection currentSection = ComponentSection::None;
        std::string currentCustomSectionName;
        std::vector<std::string> customSectionLines;

        auto flushCustomSection = [&]() {
            if (currentSection == ComponentSection::Custom && currentEntity && !currentCustomSectionName.empty()) {
                bool handled = false;
                if (m_Context.CustomSectionDeserializer) {
                    handled = m_Context.CustomSectionDeserializer(currentEntity, currentCustomSectionName, customSectionLines);
                }
                if (!handled) {
                    m_Scene->StorePassiveSection(currentEntity, currentCustomSectionName, customSectionLines);
                }
                customSectionLines.clear();
                currentCustomSectionName.clear();
            }
        };

        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
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
            } else if (trimmed == "ParticleEmitterComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::ParticleEmitter;
                vfxReferenceLoaded = false;
                if (!currentEntity.HasComponent<ParticleEmitterComponent>()) {
                    currentEntity.AddComponent<ParticleEmitterComponent>();
                }
            } else if (trimmed == "CharacterVFXComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::CharacterVFX;
                if (!currentEntity.HasComponent<CharacterVFXComponent>()) {
                    currentEntity.AddComponent<CharacterVFXComponent>();
                }
            } else if (trimmed == "UICanvasComponent:") {
                flushCustomSection();
                currentSection = ComponentSection::UICanvas;
                if (!currentEntity.HasComponent<UICanvasComponent>()) {
                    currentEntity.AddComponent<UICanvasComponent>();
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
                        } else if (trimmed.rfind("ShadowSlopeBias:", 0) == 0) {
                            dlc.ShadowSlopeBias = std::stof(Trim(trimmed.substr(16)));
                        } else if (trimmed.rfind("ShadowNormalBias:", 0) == 0) {
                            dlc.ShadowNormalBias = std::stof(Trim(trimmed.substr(17)));
                        } else if (trimmed.rfind("ShadowFilterRadius:", 0) == 0) {
                            dlc.ShadowFilterRadius = std::stof(Trim(trimmed.substr(19)));
                        } else if (trimmed.rfind("ShadowQuality:", 0) == 0) {
                            dlc.ShadowQuality = std::stoi(Trim(trimmed.substr(14)));
                        } else if (trimmed.rfind("CascadeSplitLambda:", 0) == 0) {
                            dlc.CascadeSplitLambda = std::stof(Trim(trimmed.substr(19)));
                        } else if (trimmed.rfind("MaxShadowDistance:", 0) == 0) {
                            dlc.MaxShadowDistance = std::stof(Trim(trimmed.substr(18)));
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
                        ReadMaterialComponentLine(mat,trimmed);
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
                    case ComponentSection::SkeletalMesh: {
                        if (!currentEntity.HasComponent<SkeletalMeshComponent>()) {
                            currentEntity.AddComponent<SkeletalMeshComponent>();
                        }
                        auto& smc = currentEntity.GetComponent<SkeletalMeshComponent>();
                        if (trimmed.rfind("AssetPath:", 0) == 0) {
                            smc.AssetPath = Trim(trimmed.substr(10));
                        } else if (trimmed.rfind("SkeletonPath:", 0) == 0) {
                            smc.SkeletonPath = Trim(trimmed.substr(13));
                        } else if (trimmed.rfind("RigFallbackData:", 0) == 0) {
                            smc.RigFallbackData = Trim(trimmed.substr(16));
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
                        if (trimmed.rfind("ControllerPath:", 0) == 0) {
                            ac.ControllerPath = Trim(trimmed.substr(15));
                        } else if (trimmed.rfind("IsUnique:", 0) == 0) {
                            ac.IsUnique = (Trim(trimmed.substr(9)) == "true");
                        } else if (trimmed.rfind("InlineFallbackData:", 0) == 0) {
                            ac.InlineFallbackData = Trim(trimmed.substr(19));
                        } else if (trimmed.rfind("Playing:", 0) == 0) {
                            ac.Playing = (Trim(trimmed.substr(8)) == "true");
                        } else if (trimmed.rfind("PlaybackSpeed:", 0) == 0) {
                            ac.PlaybackSpeed = std::stof(Trim(trimmed.substr(14)));
                        } else if (trimmed.rfind("Speed:", 0) == 0) {
                            ac.Speed = std::stof(Trim(trimmed.substr(6)));
                            ac.FloatOverrides["Speed"] = ac.Speed;
                        } else if (trimmed.rfind("IsGrounded:", 0) == 0) {
                            ac.IsGrounded = (Trim(trimmed.substr(11)) == "true");
                            ac.BoolOverrides["IsGrounded"] = ac.IsGrounded;
                        } else if (trimmed.rfind("VerticalVelocity:", 0) == 0) {
                            ac.VerticalVelocity = std::stof(Trim(trimmed.substr(17)));
                            ac.FloatOverrides["VerticalVelocity"] = ac.VerticalVelocity;
                        } else if (trimmed.rfind("FloatOverride:", 0) == 0) {
                            std::string rest = Trim(trimmed.substr(14));
                            auto sp = rest.find(' ');
                            if (sp != std::string::npos) {
                                std::string pName = rest.substr(0, sp);
                                float pVal = std::stof(Trim(rest.substr(sp + 1)));
                                ac.FloatOverrides[pName] = pVal;
                            }
                        } else if (trimmed.rfind("IntOverride:", 0) == 0) {
                            std::string rest = Trim(trimmed.substr(12));
                            auto sp = rest.find(' ');
                            if (sp != std::string::npos) {
                                std::string pName = rest.substr(0, sp);
                                int32_t pVal = std::stoi(Trim(rest.substr(sp + 1)));
                                ac.IntOverrides[pName] = pVal;
                            }
                        } else if (trimmed.rfind("BoolOverride:", 0) == 0) {
                            std::string rest = Trim(trimmed.substr(13));
                            auto sp = rest.find(' ');
                            if (sp != std::string::npos) {
                                std::string pName = rest.substr(0, sp);
                                bool pVal = (Trim(rest.substr(sp + 1)) == "true");
                                ac.BoolOverrides[pName] = pVal;
                            }
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
                    case ComponentSection::ParticleEmitter: {
                        if (!currentEntity.HasComponent<ParticleEmitterComponent>()) {
                            currentEntity.AddComponent<ParticleEmitterComponent>();
                        }
                        auto& pec = currentEntity.GetComponent<ParticleEmitterComponent>();
                        if (vfxReferenceLoaded && trimmed.rfind("Active:", 0) != 0 &&
                            trimmed.rfind("AutoPlay:", 0) != 0 && trimmed.rfind("Visible:", 0) != 0)
                            break;
                        if (trimmed.rfind("EffectData:", 0) == 0 ||
                            trimmed.rfind("EffectFallbackData:", 0) == 0) {
                            std::string text;
                            ParticleEffectDefinition effect;
                            const auto colon = trimmed.find(':');
                            if (!DecodeVFXSceneData(Trim(trimmed.substr(colon + 1)), text) ||
                                !ParticleEffectSerializer::Deserialize(text, effect))
                                return false;
                            pec.SetEffectDefinition(effect);
                        } else if (trimmed.rfind("AssetPath:", 0) == 0) {
                            pec.AssetPath = Trim(trimmed.substr(10));
                            if (!pec.AssetPath.empty()) {
                                vfxReferenceLoaded = pec.LoadFromAsset(pec.AssetPath);
                            }
                        } else if (trimmed.rfind("EmitterStackCount:", 0) == 0) {
                            try {
                                size_t count = std::stoul(Trim(trimmed.substr(18)));
                                if (count > 0 && pec.EmitterStacks.size() < count) {
                                    pec.EmitterStacks.resize(count);
                                }
                            } catch (...) {}
                        } else if (trimmed.rfind("Stack_", 0) == 0) {
                            size_t firstUnder = 6;
                            size_t secondUnder = trimmed.find('_', firstUnder);
                            if (secondUnder != std::string::npos) {
                                try {
                                    size_t idx = std::stoul(trimmed.substr(firstUnder, secondUnder - firstUnder));
                                    if (idx < pec.EmitterStacks.size()) {
                                        size_t colon = trimmed.find(':', secondUnder);
                                        if (colon != std::string::npos) {
                                            std::string prop = trimmed.substr(secondUnder + 1, colon - secondUnder - 1);
                                            std::string val = Trim(trimmed.substr(colon + 1));
                                            auto& s = pec.EmitterStacks[idx];
                                            if (prop == "Name") s.Name = val;
                                            else if (prop == "Enabled") s.Enabled = (val == "true");
                                            else if (prop == "Visible") s.Visible = (val == "true");
                                            else if (prop == "TexturePath") s.TexturePath = val;
                                            else if (prop == "NormalMapPath") s.NormalMapPath = val;
                                            else if (prop == "MaterialPath") s.MaterialPath = val;
                                            else if (prop == "MaxParticles") try { s.Config.MaxParticles = std::stoul(val); } catch (...) {}
                                            else if (prop == "EmissionRate") try { s.Config.EmissionRate = std::stof(val); } catch (...) {}
                                            else if (prop == "Duration") try { s.Config.Duration = std::stof(val); } catch (...) {}
                                            else if (prop == "Looping") s.Config.Looping = (val == "true");
                                        }
                                    }
                                } catch (...) {}
                            }
                        } else if (trimmed.rfind("Name:", 0) == 0) {
                            pec.Config.Name = Trim(trimmed.substr(5));
                        } else if (trimmed.rfind("Active:", 0) == 0) {
                            pec.Active = (Trim(trimmed.substr(7)) == "true");
                        } else if (trimmed.rfind("AutoPlay:", 0) == 0) {
                            pec.AutoPlay = (Trim(trimmed.substr(9)) == "true");
                        } else if (trimmed.rfind("Visible:", 0) == 0) {
                            pec.Visible = (Trim(trimmed.substr(8)) == "true");
                        } else if (trimmed.rfind("SimulationSpace:", 0) == 0) {
                            std::string spaceStr = Trim(trimmed.substr(16));
                            pec.SetSimulationSpace(spaceStr == "Local" ? ParticleSimulationSpace::Local : ParticleSimulationSpace::World);
                        } else if (trimmed.rfind("EmissionRate:", 0) == 0) {
                            try { pec.Config.EmissionRate = std::stof(Trim(trimmed.substr(13))); } catch (...) {}
                        } else if (trimmed.rfind("BurstCount:", 0) == 0) {
                            try { pec.Config.BurstCount = std::stoul(Trim(trimmed.substr(11))); } catch (...) {}
                        } else if (trimmed.rfind("BurstInterval:", 0) == 0) {
                            try { pec.Config.BurstInterval = std::stof(Trim(trimmed.substr(14))); } catch (...) {}
                        } else if (trimmed.rfind("MaxParticles:", 0) == 0) {
                            try { pec.Config.MaxParticles = std::stoul(Trim(trimmed.substr(13))); } catch (...) {}
                        } else if (trimmed.rfind("Lifetime:", 0) == 0) {
                            float minV = 1.0f, maxV = 2.0f;
                            sscanf(trimmed.c_str(), "Lifetime: [ %f , %f ]", &minV, &maxV);
                            pec.Config.Lifetime = FloatRange(minV, maxV);
                        } else if (trimmed.rfind("InitialSpeed:", 0) == 0) {
                            float minV = 2.0f, maxV = 5.0f;
                            sscanf(trimmed.c_str(), "InitialSpeed: [ %f , %f ]", &minV, &maxV);
                            pec.Config.InitialSpeed = FloatRange(minV, maxV);
                        } else if (trimmed.rfind("StartSize:", 0) == 0) {
                            float minV = 0.2f, maxV = 0.4f;
                            sscanf(trimmed.c_str(), "StartSize: [ %f , %f ]", &minV, &maxV);
                            pec.Config.StartSize = FloatRange(minV, maxV);
                        } else if (trimmed.rfind("EndSize:", 0) == 0) {
                            float minV = 0.8f, maxV = 1.2f;
                            sscanf(trimmed.c_str(), "EndSize: [ %f , %f ]", &minV, &maxV);
                            pec.Config.EndSize = FloatRange(minV, maxV);
                        } else if (trimmed.rfind("StartRotation:", 0) == 0) {
                            float minV = 0.0f, maxV = 6.283f;
                            sscanf(trimmed.c_str(), "StartRotation: [ %f , %f ]", &minV, &maxV);
                            pec.Config.StartRotation = FloatRange(minV, maxV);
                        } else if (trimmed.rfind("AngularVelocity:", 0) == 0) {
                            float minV = -1.5f, maxV = 1.5f;
                            sscanf(trimmed.c_str(), "AngularVelocity: [ %f , %f ]", &minV, &maxV);
                            pec.Config.AngularVelocity = FloatRange(minV, maxV);
                        } else if (trimmed.rfind("GravityModifier:", 0) == 0) {
                            try { pec.Config.GravityModifier = std::stof(Trim(trimmed.substr(16))); } catch (...) {}
                        } else if (trimmed.rfind("Drag:", 0) == 0) {
                            try { pec.Config.Drag = std::stof(Trim(trimmed.substr(5))); } catch (...) {}
                        } else if (trimmed.rfind("StartColor:", 0) == 0) {
                            float r = 1, g = 1, b = 1, a = 1;
                            sscanf(trimmed.c_str(), "StartColor: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                            pec.Config.ColorOverLifetime.StartColor = glm::vec4(r, g, b, a);
                        } else if (trimmed.rfind("EndColor:", 0) == 0) {
                            float r = 1, g = 1, b = 1, a = 0;
                            sscanf(trimmed.c_str(), "EndColor: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                            pec.Config.ColorOverLifetime.EndColor = glm::vec4(r, g, b, a);
                        } else if (trimmed.rfind("BlendMode:", 0) == 0) {
                            try { pec.Config.BlendMode = static_cast<ParticleBlendMode>(std::stoi(Trim(trimmed.substr(10)))); } catch (...) {}
                        } else if (trimmed.rfind("Orientation:", 0) == 0) {
                            try { pec.Config.Orientation = static_cast<ParticleOrientation>(std::stoi(Trim(trimmed.substr(12)))); } catch (...) {}
                        } else if (trimmed.rfind("ShapeType:", 0) == 0) {
                            try { pec.Config.Shape.Type = static_cast<ParticleEmitterShapeType>(std::stoi(Trim(trimmed.substr(10)))); } catch (...) {}
                        } else if (trimmed.rfind("ShapeRadius:", 0) == 0) {
                            try { pec.Config.Shape.Radius = std::stof(Trim(trimmed.substr(12))); } catch (...) {}
                        } else if (trimmed.rfind("ShapeConeAngle:", 0) == 0) {
                            try { pec.Config.Shape.ConeAngle = std::stof(Trim(trimmed.substr(15))); } catch (...) {}
                        } else if (trimmed.rfind("ShapeBoxExtents:", 0) == 0) {
                            float x = 1, y = 1, z = 1;
                            sscanf(trimmed.c_str(), "ShapeBoxExtents: [ %f , %f , %f ]", &x, &y, &z);
                            pec.Config.Shape.BoxExtents = glm::vec3(x, y, z);
                        } else if (trimmed.rfind("ShapeLineLength:", 0) == 0) {
                            try { pec.Config.Shape.LineLength = std::stof(Trim(trimmed.substr(16))); } catch (...) {}
                        } else if (trimmed.rfind("ShapeDirection:", 0) == 0) {
                            float x = 0, y = 1, z = 0;
                            sscanf(trimmed.c_str(), "ShapeDirection: [ %f , %f , %f ]", &x, &y, &z);
                            pec.Config.Shape.Direction = glm::vec3(x, y, z);
                        } else if (trimmed.rfind("ShapeSurfaceOnly:", 0) == 0) {
                            pec.Config.Shape.SurfaceOnly = (Trim(trimmed.substr(17)) == "true");
                        } else if (trimmed.rfind("TexturePath:", 0) == 0) {
                            pec.TexturePath = Trim(trimmed.substr(12));
                        } else if (trimmed.rfind("NormalMapPath:", 0) == 0) {
                            pec.NormalMapPath = Trim(trimmed.substr(14));
                        } else if (trimmed.rfind("TextureRows:", 0) == 0) {
                            try { pec.Config.TextureRows = std::stoul(Trim(trimmed.substr(12))); } catch (...) {}
                        } else if (trimmed.rfind("TextureCols:", 0) == 0) {
                            try { pec.Config.TextureCols = std::stoul(Trim(trimmed.substr(12))); } catch (...) {}
                        } else if (trimmed.rfind("TotalFrames:", 0) == 0) {
                            try { pec.Config.TotalFrames = std::stoul(Trim(trimmed.substr(12))); } catch (...) {}
                        } else if (trimmed.rfind("SubFrameBlending:", 0) == 0) {
                            pec.Config.SubFrameBlending = (Trim(trimmed.substr(17)) == "true");
                        } else if (trimmed.rfind("LightingEnabled:", 0) == 0) {
                            pec.Config.LightingEnabled = (Trim(trimmed.substr(16)) == "true");
                        } else if (trimmed.rfind("WrapLighting:", 0) == 0) {
                            try { pec.Config.WrapLighting = std::stof(Trim(trimmed.substr(13))); } catch (...) {}
                        } else if (trimmed.rfind("ScatteringFactor:", 0) == 0) {
                            try { pec.Config.ScatteringFactor = std::stof(Trim(trimmed.substr(17))); } catch (...) {}
                        } else if (trimmed.rfind("NormalMapStrength:", 0) == 0) {
                            try { pec.Config.NormalMapStrength = std::stof(Trim(trimmed.substr(18))); } catch (...) {}
                        } else if (trimmed.rfind("NearFadeEnabled:", 0) == 0) {
                            pec.Config.NearFadeEnabled = (Trim(trimmed.substr(16)) == "true");
                        } else if (trimmed.rfind("NearFadeStart:", 0) == 0) {
                            try { pec.Config.NearFadeStart = std::stof(Trim(trimmed.substr(14))); } catch (...) {}
                        } else if (trimmed.rfind("NearFadeEnd:", 0) == 0) {
                            try { pec.Config.NearFadeEnd = std::stof(Trim(trimmed.substr(12))); } catch (...) {}
                        } else if (trimmed.rfind("FarFadeEnabled:", 0) == 0) {
                            pec.Config.FarFadeEnabled = (Trim(trimmed.substr(15)) == "true");
                        } else if (trimmed.rfind("FarFadeStart:", 0) == 0) {
                            try { pec.Config.FarFadeStart = std::stof(Trim(trimmed.substr(13))); } catch (...) {}
                        } else if (trimmed.rfind("FarFadeEnd:", 0) == 0) {
                            try { pec.Config.FarFadeEnd = std::stof(Trim(trimmed.substr(11))); } catch (...) {}
                        } else if (trimmed.rfind("SoftnessDistance:", 0) == 0) {
                            try { pec.Config.SoftnessDistance = std::stof(Trim(trimmed.substr(17))); } catch (...) {}
                        } else if (trimmed.rfind("CurlNoiseEnabled:", 0) == 0) {
                            pec.Config.CurlNoise.Enabled = (Trim(trimmed.substr(17)) == "true");
                        } else if (trimmed.rfind("CurlNoiseStrength:", 0) == 0) {
                            try { pec.Config.CurlNoise.Strength = std::stof(Trim(trimmed.substr(18))); } catch (...) {}
                        } else if (trimmed.rfind("CurlNoiseFrequency:", 0) == 0) {
                            try { pec.Config.CurlNoise.Frequency = std::stof(Trim(trimmed.substr(19))); } catch (...) {}
                        } else if (trimmed.rfind("CollisionEnabled:", 0) == 0) {
                            pec.Config.Collision.Enabled = (Trim(trimmed.substr(17)) == "true");
                        } else if (trimmed.rfind("CollisionRestitution:", 0) == 0 ||
                                   trimmed.rfind("CollisionBounciness:", 0) == 0) {
                            try { pec.Config.Collision.Restitution = std::stof(Trim(trimmed.substr(trimmed.find(':') + 1))); } catch (...) {}
                        } else if (trimmed.rfind("CollisionFriction:", 0) == 0) {
                            try { pec.Config.Collision.Friction = std::stof(Trim(trimmed.substr(18))); } catch (...) {}
                        } else if (trimmed.rfind("CollisionParticleRadius:", 0) == 0) {
                            try { pec.Config.Collision.ParticleRadius = std::stof(Trim(trimmed.substr(24))); } catch (...) {}
                        }
                        pec.Initialize();
                        break;
                    }
                    case ComponentSection::CharacterVFX: {
                        if (!currentEntity.HasComponent<CharacterVFXComponent>()) {
                            currentEntity.AddComponent<CharacterVFXComponent>();
                        }
                        auto& cvfx = currentEntity.GetComponent<CharacterVFXComponent>();
                        if (trimmed.rfind("Enabled:", 0) == 0) {
                            cvfx.Enabled = (Trim(trimmed.substr(8)) == "true");
                        } else if (trimmed.rfind("FootstepMinSpeed:", 0) == 0) {
                            try { cvfx.Config.FootstepMinSpeed = std::stof(Trim(trimmed.substr(17))); } catch (...) {}
                        } else if (trimmed.rfind("StrideLength:", 0) == 0) {
                            try { cvfx.Config.StrideLength = std::stof(Trim(trimmed.substr(13))); } catch (...) {}
                        } else if (trimmed.rfind("FootSpread:", 0) == 0) {
                            try { cvfx.Config.FootSpread = std::stof(Trim(trimmed.substr(11))); } catch (...) {}
                        } else if (trimmed.rfind("WalkPuffCount:", 0) == 0) {
                            try { cvfx.Config.WalkPuffCount = std::stoul(Trim(trimmed.substr(14))); } catch (...) {}
                        } else if (trimmed.rfind("RunPuffCount:", 0) == 0) {
                            try { cvfx.Config.RunPuffCount = std::stoul(Trim(trimmed.substr(13))); } catch (...) {}
                        } else if (trimmed.rfind("LandingImpactThreshold:", 0) == 0) {
                            try { cvfx.Config.LandingImpactThreshold = std::stof(Trim(trimmed.substr(23))); } catch (...) {}
                        } else if (trimmed.rfind("HeavyLandingThreshold:", 0) == 0) {
                            try { cvfx.Config.HeavyLandingThreshold = std::stof(Trim(trimmed.substr(22))); } catch (...) {}
                        } else if (trimmed.rfind("NormalLandingCount:", 0) == 0) {
                            try { cvfx.Config.NormalLandingCount = std::stoul(Trim(trimmed.substr(19))); } catch (...) {}
                        } else if (trimmed.rfind("HeavyLandingCount:", 0) == 0) {
                            try { cvfx.Config.HeavyLandingCount = std::stoul(Trim(trimmed.substr(18))); } catch (...) {}
                        }
                        break;
                    }
                    case ComponentSection::UICanvas: {
                        if (!currentEntity.HasComponent<UICanvasComponent>()) {
                            currentEntity.AddComponent<UICanvasComponent>();
                        }
                        auto& uic = currentEntity.GetComponent<UICanvasComponent>();
                        if (trimmed.rfind("AssetPath:", 0) == 0) {
                            uic.AssetPath = Trim(trimmed.substr(10));
                            if (!uic.AssetPath.empty()) {
                                std::filesystem::path fullPath = uic.AssetPath;
                                if (!std::filesystem::exists(fullPath)) {
                                    fullPath = FileSystem::GetAssetDirectory() / uic.AssetPath;
                                }
                                if (std::filesystem::exists(fullPath)) {
                                    uic.Document = std::make_shared<UIDocument>();
                                    UIParser::ParseFile(fullPath.string(), *uic.Document);
                                }
                            }
                        } else if (trimmed.rfind("DataSourceEntityID:", 0) == 0) {
                            try { uic.DataSourceEntityID = std::stoull(Trim(trimmed.substr(19))); } catch (...) {}
                        } else if (trimmed.rfind("SortOrder:", 0) == 0) {
                            try { uic.SortOrder = std::stoi(Trim(trimmed.substr(10))); } catch (...) {}
                        } else if (trimmed.rfind("Visible:", 0) == 0) {
                            uic.Visible = (Trim(trimmed.substr(8)) == "true");
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

        // An unresolved identity stays unresolved; names cannot recover it.
        auto meshView = m_Scene->GetRegistry().view<MeshInstance3D>();
        for (auto entityID : meshView) ResolveMeshResources(meshView.get<MeshInstance3D>(entityID), &m_Context);

        // Resolve animator-only entities too; all resource reads go through FileSystem/VFS.
        auto& animationRegistry = m_Scene->GetRegistry();
        for (auto entityID : animationRegistry.view<SkeletalMeshComponent>()) {
            ResolveAnimationBinding(&animationRegistry.get<SkeletalMeshComponent>(entityID),
                animationRegistry.try_get<AnimatorComponent>(entityID), m_ResourceOwnerPath);
        }
        for (auto entityID : animationRegistry.view<AnimatorComponent>(entt::exclude<SkeletalMeshComponent>)) {
            ResolveAnimationBinding(nullptr, &animationRegistry.get<AnimatorComponent>(entityID), m_ResourceOwnerPath);
        }

        // Recreate Physics Bodies in PhysicsScene if active
        if (m_PhysicsScene) {
            auto rbView = m_Scene->GetRegistry().view<RigidBody3D, TransformComponent>();
            for (auto entityID : rbView) {
                auto& rb = rbView.get<RigidBody3D>(entityID);
                auto& tc = rbView.get<TransformComponent>(entityID);

                if (m_Scene->GetRegistry().all_of<CharacterControllerComponent>(entityID)) {
                    continue; // Skip static body creation; handled by CharacterController
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

        // Inline values saved with a shared resource represent the scene's latest
        // edits; apply them once before renderers synchronize from that resource.
        auto materialView = m_Scene->GetRegistry().view<MaterialComponent>();
        for (auto entity : materialView) {
            auto& material = materialView.get<MaterialComponent>(entity);
            if (material.MaterialAsset) material.SyncToAsset();
        }

        m_Scene->UpdateWorldTransforms();
        return true;
    }

}
