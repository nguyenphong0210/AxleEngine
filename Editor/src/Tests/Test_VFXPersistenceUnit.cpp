#include "VFXTestHarness.hpp"
#include <Axle/ParticleEmitterSerializer.hpp>
#include <Axle/ParticleEffectSerializer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>

namespace {
    bool TestBaselineEmitterSerializer() {
        Axle::ParticleEmitterConfig config;
        config.Name = "TestEmitter";
        config.MaxParticles = 500;
        config.EmissionRate = 25.0f;

        Axle::VFXTests::ScopedTempDir tempDir;
        auto path = tempDir.Path / "test_legacy.axleasset";
        VFX_TEST_CHECK(Axle::ParticleEmitterSerializer::SerializeToFile(path.string(), config), "Legacy serializer save");

        Axle::ParticleEmitterConfig loaded;
        std::string outTexture;
        VFX_TEST_CHECK(Axle::ParticleEmitterSerializer::DeserializeFromFile(path.string(), loaded, outTexture), "Legacy serializer load");
        VFX_TEST_CHECK(loaded.Name == "TestEmitter", "Name preserved");
        VFX_TEST_CHECK(loaded.MaxParticles == 500, "MaxParticles preserved");
        VFX_TEST_APPROX_EQUAL(loaded.EmissionRate, 25.0f, 0.001f, "EmissionRate preserved");
        return true;
    }

    bool TestVFXEffectRoundTrip() {
        Axle::ParticleEffectDefinition effect;
        effect.SchemaVersion = 2;
        effect.Name = "CampfireEffect";
        effect.Seed = 1337;

        // Emitter 1: Flame
        Axle::EmitterDefinition flame;
        flame.Id = "flame-0001";
        flame.Name = "Flame";
        flame.Enabled = true;
        flame.TriggerOnly = false;
        flame.LocalTransform.Translation = glm::vec3(0.0f, 0.5f, 0.0f);
        flame.Config.Name = "Flame";
        flame.Config.MaxParticles = 400;
        flame.Config.EmissionRate = 60.0f;
        flame.Config.Duration = 5.0f;
        flame.Config.Looping = true;
        flame.Resources.TexturePath = "vfx/textures/flame.png";
        flame.Resources.UseTexture = true;
        flame.Config.Bursts.emplace_back(1.0f, 20, 40, 2, 0.5f, 0.9f);
        effect.Emitters.push_back(std::move(flame));

        // Emitter 2: Sparks (TriggerOnly)
        Axle::EmitterDefinition sparks;
        sparks.Id = "sparks-0002";
        sparks.Name = "Sparks";
        sparks.Enabled = true;
        sparks.TriggerOnly = true;
        sparks.LocalTransform.Translation = glm::vec3(0.0f, 1.0f, 0.0f);
        sparks.Config.Name = "Sparks";
        sparks.Config.MaxParticles = 150;
        sparks.Config.EmissionRate = 0.0f; // trigger only
        sparks.Config.Duration = 2.0f;
        sparks.Config.Looping = false;
        sparks.Config.Collision.Enabled = true;
        sparks.Config.Collision.Restitution = 0.6f;
        effect.Emitters.push_back(std::move(sparks));

        // Serialize to file
        Axle::VFXTests::ScopedTempDir tempDir;
        auto filePath = tempDir.Path / "campfire.axleasset";
        std::vector<Axle::VFXDiagnostic> diags;
        bool saved = Axle::ParticleEffectSerializer::SerializeToFile(filePath.string(), effect, &diags);
        VFX_TEST_CHECK(saved, "ParticleEffectSerializer::SerializeToFile should succeed");

        // Deserialize from file
        Axle::ParticleEffectDefinition loaded;
        bool loadedOk = Axle::ParticleEffectSerializer::DeserializeFromFile(filePath.string(), loaded, &diags);
        VFX_TEST_CHECK(loadedOk, "ParticleEffectSerializer::DeserializeFromFile should succeed");

        // Verify roundtrip equivalence
        VFX_TEST_CHECK(loaded.SchemaVersion == 2, "Loaded SchemaVersion must be 2");
        VFX_TEST_CHECK(loaded.Name == "CampfireEffect", "Loaded Name must match");
        VFX_TEST_CHECK(loaded.Seed == 1337, "Loaded Seed must match");
        VFX_TEST_CHECK(loaded.Emitters.size() == 2, "Loaded Emitters count must be 2");

        VFX_TEST_CHECK(loaded.Emitters[0].Id == "flame-0001", "Emitter 0 ID must match");
        VFX_TEST_CHECK(loaded.Emitters[0].Name == "Flame", "Emitter 0 Name must match");
        VFX_TEST_CHECK(loaded.Emitters[0].Enabled == true, "Emitter 0 Enabled must match");
        VFX_TEST_CHECK(loaded.Emitters[0].TriggerOnly == false, "Emitter 0 TriggerOnly must match");
        VFX_TEST_CHECK(loaded.Emitters[0].Resources.TexturePath == "vfx/textures/flame.png", "Emitter 0 TexturePath must match");
        VFX_TEST_CHECK(loaded.Emitters[0].Config.MaxParticles == 400, "Emitter 0 MaxParticles must match");
        VFX_TEST_CHECK(loaded.Emitters[0].Config.Bursts.size() == 1, "Emitter 0 Bursts size must match");

        VFX_TEST_CHECK(loaded.Emitters[1].Id == "sparks-0002", "Emitter 1 ID must match");
        VFX_TEST_CHECK(loaded.Emitters[1].Name == "Sparks", "Emitter 1 Name must match");
        VFX_TEST_CHECK(loaded.Emitters[1].TriggerOnly == true, "Emitter 1 TriggerOnly must match");
        VFX_TEST_CHECK(loaded.Emitters[1].Config.Collision.Enabled == true, "Emitter 1 Collision.Enabled must match");

        VFX_TEST_CHECK(loaded == effect, "operator== must report loaded effect identical to source");

        return true;
    }

    bool TestLegacyV1Migration() {
        // Save a legacy single-emitter v1 asset
        Axle::ParticleEmitterConfig v1Config;
        v1Config.Name = "LegacySmoke";
        v1Config.MaxParticles = 256;
        v1Config.EmissionRate = 18.0f;
        std::string v1Tex = "vfx/smoke.png";

        Axle::VFXTests::ScopedTempDir tempDir;
        auto filePath = tempDir.Path / "legacy_smoke.axleasset";
        bool saved = Axle::ParticleEmitterSerializer::SerializeToFile(filePath.string(), v1Config, v1Tex);
        VFX_TEST_CHECK(saved, "Legacy v1 file must save successfully");

        // Read using v2 ParticleEffectSerializer
        Axle::ParticleEffectDefinition migrated;
        std::vector<Axle::VFXDiagnostic> diags;
        bool ok = Axle::ParticleEffectSerializer::DeserializeFromFile(filePath.string(), migrated, &diags);
        VFX_TEST_CHECK(ok, "ParticleEffectSerializer should seamlessly migrate legacy v1 asset");

        VFX_TEST_CHECK(migrated.SchemaVersion == 2, "Migrated effect schemaVersion must be upgraded to 2");
        VFX_TEST_CHECK(migrated.Name == "LegacySmoke", "Migrated effect name matches legacy config");
        VFX_TEST_CHECK(migrated.Emitters.size() == 1, "Migrated effect must contain exactly 1 emitter");
        VFX_TEST_CHECK(migrated.Emitters[0].Id == "legacy-0001", "Migrated emitter ID must be deterministic");
        VFX_TEST_CHECK(migrated.Emitters[0].Config.MaxParticles == 256, "MaxParticles matches");
        VFX_TEST_APPROX_EQUAL(migrated.Emitters[0].Config.EmissionRate, 18.0f, 0.001f, "EmissionRate matches");
        VFX_TEST_CHECK(migrated.Emitters[0].Resources.TexturePath == "vfx/smoke.png", "Texture path migrated");

        return true;
    }

    bool TestTransactionalSaveAndErrorPreservation() {
        Axle::ParticleEffectDefinition validEffect = Axle::CreateDefaultParticleEffect("Baseline");
        Axle::ParticleEffectDefinition targetEffect = validEffect;

        // Malformed DSL should fail and preserve targetEffect
        std::string badDsl = "InvalidRootBlock { bad syntax {";
        std::vector<Axle::VFXDiagnostic> diags;
        bool res1 = Axle::ParticleEffectSerializer::Deserialize(badDsl, targetEffect, &diags);
        VFX_TEST_CHECK(!res1, "Deserializing invalid DSL must return false");
        VFX_TEST_CHECK(targetEffect == validEffect, "Target effect must be left untouched on deserialization failure");
        VFX_TEST_CHECK(!diags.empty(), "Diagnostics must report the error");

        // Duplicate emitter IDs should fail validation and preserve targetEffect
        Axle::ParticleEffectDefinition dupEffect;
        dupEffect.SchemaVersion = 2;
        dupEffect.Name = "DupTest";
        Axle::EmitterDefinition e1; e1.Id = "dup-id"; e1.Name = "E1";
        Axle::EmitterDefinition e2; e2.Id = "dup-id"; e2.Name = "E2"; // duplicate ID!
        dupEffect.Emitters.push_back(e1);
        dupEffect.Emitters.push_back(e2);
        std::string dupDsl = Axle::ParticleEffectSerializer::Serialize(dupEffect);

        diags.clear();
        bool res2 = Axle::ParticleEffectSerializer::Deserialize(dupDsl, targetEffect, &diags);
        VFX_TEST_CHECK(!res2, "Deserializing duplicate emitter IDs must fail validation");
        VFX_TEST_CHECK(targetEffect == validEffect, "Target effect must remain untouched");

        return true;
    }

    bool TestSceneVFXRoundTrip() {
        auto scene = std::make_shared<Axle::Scene>();
        auto entity = scene->CreateEntity("CampfireSceneEntity");
        auto& pec = entity.AddComponent<Axle::ParticleEmitterComponent>();
        pec.Config.Name = "CampfireBase";
        pec.Config.MaxParticles = 500;
        pec.Config.EmissionRate = 45.0f;
        pec.EmitterStacks.clear();

        // Stack 0
        Axle::ParticleEmitterStack s0("FlameStack", pec.Config);
        s0.TexturePath = "vfx/flame.png";
        pec.EmitterStacks.push_back(std::move(s0));

        // Stack 1
        Axle::ParticleEmitterConfig s1Cfg;
        s1Cfg.Name = "SmokeStack";
        s1Cfg.MaxParticles = 200;
        s1Cfg.EmissionRate = 12.0f;
        Axle::ParticleEmitterStack s1("SmokeStack", s1Cfg);
        s1.TexturePath = "vfx/smoke.png";
        pec.EmitterStacks.push_back(std::move(s1));

        pec.SyncPrimary();
        pec.Initialize();

        // Second entity: referenced asset
        auto refEntity = scene->CreateEntity("ReferencedVFX");
        auto& refPec = refEntity.AddComponent<Axle::ParticleEmitterComponent>();
        refPec.AssetPath = "vfx/prefabs/torch.axleasset";

        // Serialize scene
        Axle::SceneSerializer serializer(scene.get());
        std::string sceneData = serializer.SerializeToString();
        VFX_TEST_CHECK(!sceneData.empty(), "Scene serialization must succeed");

        // Deserialize scene
        auto loadedScene = std::make_shared<Axle::Scene>();
        Axle::SceneSerializer deserializer(loadedScene.get());
        bool deserialized = deserializer.DeserializeFromString(sceneData);
        VFX_TEST_CHECK(deserialized, "Scene deserialization must succeed");

        Axle::Entity loadedEnt{};
        Axle::Entity loadedRef{};
        auto tagView = loadedScene->GetRegistry().view<Axle::TagComponent>();
        for (auto entityID : tagView) {
            const auto& tag = tagView.get<Axle::TagComponent>(entityID);
            if (tag.Tag == "CampfireSceneEntity") {
                loadedEnt = Axle::Entity{ entityID, loadedScene.get() };
            } else if (tag.Tag == "ReferencedVFX") {
                loadedRef = Axle::Entity{ entityID, loadedScene.get() };
            }
        }

        VFX_TEST_CHECK(loadedEnt, "Entity CampfireSceneEntity must exist");
        VFX_TEST_CHECK(loadedEnt.HasComponent<Axle::ParticleEmitterComponent>(), "Must have ParticleEmitterComponent");
        auto& lPec = loadedEnt.GetComponent<Axle::ParticleEmitterComponent>();
        VFX_TEST_CHECK(lPec.EmitterStacks.size() == 2, "Loaded entity must preserve 2 emitter stacks");
        VFX_TEST_CHECK(lPec.EmitterStacks[0].Name == "FlameStack", "Stack 0 Name must match");
        VFX_TEST_CHECK(lPec.EmitterStacks[0].TexturePath == "vfx/flame.png", "Stack 0 TexturePath must match");
        VFX_TEST_CHECK(lPec.EmitterStacks[1].Name == "SmokeStack", "Stack 1 Name must match");
        VFX_TEST_CHECK(lPec.EmitterStacks[1].TexturePath == "vfx/smoke.png", "Stack 1 TexturePath must match");
        VFX_TEST_APPROX_EQUAL(lPec.EmitterStacks[1].Config.EmissionRate, 12.0f, 0.001f, "Stack 1 EmissionRate must match");

        VFX_TEST_CHECK(loadedRef, "Referenced entity must exist");
        auto& lRefPec = loadedRef.GetComponent<Axle::ParticleEmitterComponent>();
        VFX_TEST_CHECK(lRefPec.AssetPath == "vfx/prefabs/torch.axleasset", "Referenced AssetPath must be preserved");

        return true;
    }
}

bool RunVFXPersistenceUnitTests() {
    std::cout << "  - Running TestBaselineEmitterSerializer ...\n";
    VFX_TEST_CHECK(TestBaselineEmitterSerializer(), "TestBaselineEmitterSerializer failed");
    std::cout << "  - Running TestVFXEffectRoundTrip ...\n";
    VFX_TEST_CHECK(TestVFXEffectRoundTrip(), "TestVFXEffectRoundTrip failed");
    std::cout << "  - Running TestLegacyV1Migration ...\n";
    VFX_TEST_CHECK(TestLegacyV1Migration(), "TestLegacyV1Migration failed");
    std::cout << "  - Running TestTransactionalSaveAndErrorPreservation ...\n";
    VFX_TEST_CHECK(TestTransactionalSaveAndErrorPreservation(), "TestTransactionalSaveAndErrorPreservation failed");
    std::cout << "  - Running TestSceneVFXRoundTrip ...\n";
    VFX_TEST_CHECK(TestSceneVFXRoundTrip(), "TestSceneVFXRoundTrip failed");
    return true;
}
