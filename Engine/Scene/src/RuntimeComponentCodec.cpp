#include <Axle/ComponentCodec.hpp>
#include <Axle/Components.hpp>

namespace Axle {

    template<typename T>
    static void RegisterSimpleCodec(ComponentCodecRegistry& codecs, std::string_view name) {
        codecs.RegisterCodec(std::make_unique<FunctionalComponentCodec<T, void(*)(const ComponentCopyContext&)>>(
            name,
            [](const ComponentCopyContext& ctx) {
                ctx.DstRegistry.emplace_or_replace<T>(ctx.DstEntity, ctx.SrcRegistry.get<T>(ctx.SrcEntity));
            }
        ));
    }

    void RegisterRuntimeComponentCodecs() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        auto& m_Codecs = ComponentCodecRegistry::Get();
        // Visual / Rendering
        RegisterSimpleCodec<MeshInstance3D>(m_Codecs, "MeshInstance3D");
        RegisterSimpleCodec<MaterialComponent>(m_Codecs, "MaterialComponent");
        RegisterSimpleCodec<DirectionalLightComponent>(m_Codecs, "DirectionalLightComponent");
        RegisterSimpleCodec<PointLightComponent>(m_Codecs, "PointLightComponent");
        RegisterSimpleCodec<SpotLightComponent>(m_Codecs, "SpotLightComponent");
        RegisterSimpleCodec<ReflectionProbeComponent>(m_Codecs, "ReflectionProbeComponent");
        RegisterSimpleCodec<RectLightComponent>(m_Codecs, "RectLightComponent");
        RegisterSimpleCodec<VolumetricFogComponent>(m_Codecs, "VolumetricFogComponent");
        RegisterSimpleCodec<DDGIProbeGridComponent>(m_Codecs, "DDGIProbeGridComponent");
        RegisterSimpleCodec<CameraComponent>(m_Codecs, "CameraComponent");
        RegisterSimpleCodec<SkyboxComponent>(m_Codecs, "SkyboxComponent");
        RegisterSimpleCodec<AssemblyComponent>(m_Codecs, "AssemblyComponent");

        // Physics
        RegisterSimpleCodec<CollisionShape3D>(m_Codecs, "CollisionShape3D");
        m_Codecs.RegisterCodec(std::make_unique<FunctionalComponentCodec<RigidBody3D, void(*)(const ComponentCopyContext&)>>(
            "RigidBody3D", [](const ComponentCopyContext& ctx) {
                auto copy = ctx.SrcRegistry.get<RigidBody3D>(ctx.SrcEntity);
                copy.RuntimeBodyID = {};
                ctx.DstRegistry.emplace_or_replace<RigidBody3D>(ctx.DstEntity, copy);
            }));

        // Native Script (pure state copy, runtime instance deferred)
        m_Codecs.RegisterCodec(std::make_unique<FunctionalComponentCodec<NativeScriptComponent, void(*)(const ComponentCopyContext&)>>(
            "NativeScriptComponent",
            [](const ComponentCopyContext& ctx) {
                const auto& srcNsc = ctx.SrcRegistry.get<NativeScriptComponent>(ctx.SrcEntity);
                auto& dstNsc = ctx.DstRegistry.emplace_or_replace<NativeScriptComponent>(ctx.DstEntity);
                dstNsc.ScriptName = srcNsc.ScriptName;
                dstNsc.FloatFields = srcNsc.FloatFields;
                dstNsc.IntFields = srcNsc.IntFields;
                dstNsc.BoolFields = srcNsc.BoolFields;
                dstNsc.Vec3Fields = srcNsc.Vec3Fields;
                dstNsc.StringFields = srcNsc.StringFields;
                dstNsc.EntityFields = srcNsc.EntityFields;
                dstNsc.InstantiateScript = srcNsc.InstantiateScript;
                dstNsc.DestroyScript = srcNsc.DestroyScript;
                dstNsc.Instance = nullptr;
            }
        ));

        // Audio
        RegisterSimpleCodec<AudioListenerComponent>(m_Codecs, "AudioListenerComponent");
        m_Codecs.RegisterCodec(std::make_unique<FunctionalComponentCodec<AudioSourceComponent, void(*)(const ComponentCopyContext&)>>(
            "AudioSourceComponent",
            [](const ComponentCopyContext& ctx) {
                const auto& srcAsc = ctx.SrcRegistry.get<AudioSourceComponent>(ctx.SrcEntity);
                auto& dstAsc = ctx.DstRegistry.emplace_or_replace<AudioSourceComponent>(ctx.DstEntity);
                dstAsc.AssetPath = srcAsc.AssetPath;
                dstAsc.Volume = srcAsc.Volume;
                dstAsc.Pitch = srcAsc.Pitch;
                dstAsc.Loop = srcAsc.Loop;
                dstAsc.PlayOnAwake = srcAsc.PlayOnAwake;
                dstAsc.Spatialized = srcAsc.Spatialized;
                dstAsc.Attenuation = srcAsc.Attenuation;
                dstAsc.MinDistance = srcAsc.MinDistance;
                dstAsc.MaxDistance = srcAsc.MaxDistance;
                dstAsc.RollOff = srcAsc.RollOff;
                dstAsc.InnerAngle = srcAsc.InnerAngle;
                dstAsc.OuterAngle = srcAsc.OuterAngle;
                dstAsc.OuterGain = srcAsc.OuterGain;
                dstAsc.DopplerFactor = srcAsc.DopplerFactor;
            }
        ));

        // Animation & Skeleton
        RegisterSimpleCodec<SkeletalMeshComponent>(m_Codecs, "SkeletalMeshComponent");
        m_Codecs.RegisterCodec(std::make_unique<FunctionalComponentCodec<AnimatorComponent, void(*)(const ComponentCopyContext&)>>(
            "AnimatorComponent",
            [](const ComponentCopyContext& ctx) {
                const auto& srcAc = ctx.SrcRegistry.get<AnimatorComponent>(ctx.SrcEntity);
                auto& dstAc = ctx.DstRegistry.emplace_or_replace<AnimatorComponent>(ctx.DstEntity, srcAc);
                if (dstAc.AnimatorInstance) {
                    dstAc.AnimatorInstance->Reset();
                    dstAc.AnimatorInstance->Update(0.0f);
                }
            }
        ));

        // Character Controller
        m_Codecs.RegisterCodec(std::make_unique<FunctionalComponentCodec<CharacterControllerComponent, void(*)(const ComponentCopyContext&)>>(
            "CharacterControllerComponent",
            [](const ComponentCopyContext& ctx) {
                auto cccCopy = ctx.SrcRegistry.get<CharacterControllerComponent>(ctx.SrcEntity);
                cccCopy.RuntimeController.reset();
                ctx.DstRegistry.emplace_or_replace<CharacterControllerComponent>(ctx.DstEntity, cccCopy);
            }
        ));

        // Particles & VFX
        m_Codecs.RegisterCodec(std::make_unique<FunctionalComponentCodec<ParticleEmitterComponent, void(*)(const ComponentCopyContext&)>>(
            "ParticleEmitterComponent",
            [](const ComponentCopyContext& ctx) {
                auto pecCopy = ctx.SrcRegistry.get<ParticleEmitterComponent>(ctx.SrcEntity);
                pecCopy.Initialize();
                ctx.DstRegistry.emplace_or_replace<ParticleEmitterComponent>(ctx.DstEntity, pecCopy);
            }
        ));

        m_Codecs.RegisterCodec(std::make_unique<FunctionalComponentCodec<CharacterVFXComponent, void(*)(const ComponentCopyContext&)>>(
            "CharacterVFXComponent",
            [](const ComponentCopyContext& ctx) {
                auto cvfxCopy = ctx.SrcRegistry.get<CharacterVFXComponent>(ctx.SrcEntity);
                cvfxCopy.RuntimeVFX.reset();
                ctx.DstRegistry.emplace_or_replace<CharacterVFXComponent>(ctx.DstEntity, cvfxCopy);
            }
        ));

        // UI 2D
        RegisterSimpleCodec<UICanvasComponent>(m_Codecs, "UICanvasComponent");

        // Passive Extension Storage (unknown/plugin sections preserved across duplication & copy)

    }

}
