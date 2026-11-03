#include "VFXTestHarness.hpp"
#include <Axle/ParticleModules.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/ParticleEffectSerializer.hpp>
#include <Axle/ParticleEmitterSerializer.hpp>
#include <Axle/ParticleSystem.hpp>
#include <Axle/VFXEditorDocument.hpp>
#include <cmath>

namespace {
    bool TestModulePersistenceAndHistory() {
        using namespace Axle;
        VFXEditorDocument doc;
        const auto id = doc.AddEmitter("Authored", ParticleEmitterConfig::CreateNeutral());
        auto definition = doc.GetDefinition();
        auto& config = definition.Emitters[0].Config;
        config.Shape.Radius = 7;
        config.SizeOverLifetime.SetConstant(3);
        config.TextureCols = 4;
        config.TotalFrames = 4;
        doc.ReplaceDefinition(definition, "Edit disabled modules");
        config.Modules.Shape = true;
        doc.ReplaceDefinition(definition, "Enable shape");
        VFX_TEST_CHECK(doc.Undo() && !doc.GetDefinition().Emitters[0].Config.Modules.Shape,
                       "Module toggle has its own undo state");
        VFX_TEST_CHECK(doc.Redo() && doc.GetDefinition().Emitters[0].Config.Modules.Shape,
                       "Module toggle redo restores authored flag");
        ParticleEffectDefinition loaded;
        VFX_TEST_CHECK(ParticleEffectSerializer::Deserialize(ParticleEffectSerializer::Serialize(definition), loaded),
                       "Module state effect round trip");
        VFX_TEST_CHECK(ParticleEmitterConfigsEqual(config, loaded.Emitters[0].Config),
                       "Disabled module values and every enable flag survive serialization");
        ParticleEmitterConfig flat;
        std::string texture;
        VFX_TEST_CHECK(ParticleEmitterSerializer::Deserialize(ParticleEmitterSerializer::Serialize(config), flat, texture),
                       "Single-emitter codec also preserves module flags");
        VFX_TEST_CHECK(ParticleEmitterConfigsEqual(config, flat), "Shared codec parity");
        AssetBlock legacy;
        ParticleEmitterConfig old = ParticleEmitterConfig::CreateNeutral();
        VFX_TEST_CHECK(ParticleEffectSerializer::ReadEmitterConfig(legacy, old), "Legacy config loads");
        for (int i = int(ParticleModule::Emission); i <= int(ParticleModule::TextureSheet); ++i) {
            const auto module = static_cast<ParticleModule>(i);
            if (module == ParticleModule::Noise || module == ParticleModule::Collision) continue;
            VFX_TEST_CHECK(*GetParticleModuleToggle(old, module), "Missing legacy flags keep previous behavior");
        }
        auto copy = ParticleEmitterConfig::CreateNeutral();
        CopyParticleModule(copy, config, ParticleModule::Shape);
        VFX_TEST_CHECK(copy.Modules.Shape && copy.Shape.Radius == 7 && copy.TotalFrames == 1,
                       "Module paste only changes the selected module");
        ResetParticleModule(copy, ParticleModule::Shape);
        VFX_TEST_CHECK(!copy.Modules.Shape && copy.Shape.Radius == 1, "Module reset uses neutral defaults");
        return true;
    }

    bool TestModuleSimulation() {
        using namespace Axle;
        auto config = ParticleEmitterConfig::CreateNeutral();
        config.EmissionRate = 0;
        config.Lifetime = {10, 10};
        config.InitialSpeed = {1, 1};
        config.StartSize = {2, 2};
        config.EndSize = {6, 6};
        config.SizeOverLifetime.SetConstant(2);
        config.SpeedOverLifetime.SetConstant(3);
        config.AlphaOverLifetime.SetConstant(0.2f);
        config.ColorOverLifetime = ColorGradient(glm::vec4(1), glm::vec4(0, 0, 0, 0));
        config.AngularVelocity = {2, 2};
        config.ConstantAcceleration = {4, 0, 0};
        config.Shape.Type = ParticleEmitterShapeType::Sphere;
        config.Shape.Radius = 8;
        config.TextureCols = config.TotalFrames = 4;
        config.SubEmitters.emplace_back(SubEmitterTrigger::Birth, "child", 1, 1);
        ParticleSystem system(config);
        system.EmitBurst(1);
        VFX_TEST_CHECK(system.GetEventQueue().empty(), "Disabled sub emitters generate no events");
        auto& pool = system.GetPool();
        VFX_TEST_CHECK(glm::length(pool.GetPositions()[0]) == 0, "Disabled shape spawns at origin");
        system.Update(1);
        VFX_TEST_CHECK(std::abs(pool.GetPositions()[0].y - 1) < 0.001f && pool.GetPositions()[0].x == 0,
                       "Disabled speed and forces do not alter start velocity");
        VFX_TEST_CHECK(pool.GetSizes()[0] == 2 && pool.GetRotations()[0] == 0 && pool.GetColors()[0].a == 1,
                       "Disabled lifetime modules retain initial size, rotation and color");
        VFX_TEST_CHECK(pool.GetUVOffsetScales()[0] == glm::vec4(0, 0, 1, 1), "Disabled sheet uses complete texture");
        auto& live = system.GetConfig();
        live.Modules.Size = live.Modules.Speed = live.Modules.Color = live.Modules.Alpha = true;
        live.Modules.Rotation = live.Modules.Forces = live.Modules.TextureSheet = true;
        system.Update(1);
        VFX_TEST_CHECK(pool.GetSizes()[0] > 4 && pool.GetRotations()[0] == 2 && pool.GetColors()[0].a < 0.2f,
                       "Reenabled lifetime modules use retained authored values");
        VFX_TEST_CHECK(pool.GetPositions()[0].x > 10 && pool.GetUVOffsetScales()[0].z == 0.25f,
                       "Reenabled forces, speed and texture sheet affect live particles");
        live.Modules.TextureSheet = false;
        system.Update(0.1f);
        VFX_TEST_CHECK(pool.GetUVOffsetScales()[0] == glm::vec4(0, 0, 1, 1), "Turning sheet off clears stale UVs");
        live.Modules.SubEmitters = true;
        system.EmitBurst(1);
        VFX_TEST_CHECK(system.GetEventQueue().size() == 1, "Reenabled sub emitters dispatch events");
        return true;
    }

    bool TestDisabledEmissionDoesNotCatchUp() {
        using namespace Axle;
        auto config = ParticleEmitterConfig::CreateNeutral();
        config.Modules.Emission = false;
        config.EmissionRate = 10;
        config.BurstCount = 2;
        config.BurstInterval = 0.1f;
        config.Duration = 1;
        config.Bursts.emplace_back(0, 3, 3, 1, 0);
        ParticleSystem system(config);
        system.Update(1.25f);
        VFX_TEST_CHECK(system.GetActiveParticleCount() == 0 && system.GetEmitterTime() < 1,
                       "Disabled emission still advances bounded loop scheduling");
        system.GetConfig().Modules.Emission = true;
        system.Update(0.1f);
        VFX_TEST_CHECK(system.GetActiveParticleCount() <= 5, "Enabling emission does not replay skipped bursts");
        system.Reset();
        system.GetConfig().Modules.Emission = false;
        system.EmitBurst(1);
        VFX_TEST_CHECK(system.GetActiveParticleCount() == 1, "Explicit preview burst bypasses automatic emission");
        return true;
    }
}

bool RunVFXAuthoringUnitTests() {
    return TestModulePersistenceAndHistory() && TestModuleSimulation() && TestDisabledEmissionDoesNotCatchUp();
}
