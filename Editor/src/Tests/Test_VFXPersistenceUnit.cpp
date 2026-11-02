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

    bool TestReferenceVFXAssetsRoundTrip() {
        // 1. Author and save Campfire_v2.axle_vfx
        {
            Axle::ParticleEffectDefinition campfire;
            campfire.SchemaVersion = 2;
            campfire.Name = "Campfire_v2";
            campfire.Seed = 42;

            Axle::EmitterDefinition flame;
            flame.Id = "flame-emitter";
            flame.Name = "Flame";
            flame.Enabled = true;
            flame.Config.Name = "Flame";
            flame.Config.BlendMode = Axle::ParticleBlendMode::Additive;
            flame.Config.Shape.Type = Axle::ParticleEmitterShapeType::Cone;
            flame.Config.Shape.ConeAngle = 0.25f;
            flame.Config.Shape.Radius = 0.2f;
            flame.Config.EmissionRate = 50.0f;
            flame.Config.Lifetime = { 0.8f, 1.4f };
            flame.Config.InitialSpeed = { 2.0f, 4.0f };
            flame.Config.StartSize = { 0.3f, 0.5f };
            flame.Config.EndSize = { 0.1f, 0.2f };
            flame.Config.ColorOverLifetime = Axle::ColorGradient(
                glm::vec4(1.0f, 0.8f, 0.2f, 1.0f),
                glm::vec4(0.9f, 0.2f, 0.05f, 0.0f)
            );
            flame.Resources.TexturePath = "Assets/Textures/Particles/Flame.png";
            flame.Resources.UseTexture = true;
            Axle::SubEmitterConfig sub(Axle::SubEmitterTrigger::Death, "sparks-emitter", 3, 0.8f);
            sub.InheritColor = true;
            sub.InheritVelocity = true;
            flame.Config.SubEmitters.push_back(sub);
            campfire.Emitters.push_back(std::move(flame));

            Axle::EmitterDefinition smoke;
            smoke.Id = "smoke-emitter";
            smoke.Name = "Smoke";
            smoke.Enabled = true;
            smoke.Config.Name = "Smoke";
            smoke.Config.BlendMode = Axle::ParticleBlendMode::AlphaBlend;
            smoke.Config.Shape.Type = Axle::ParticleEmitterShapeType::Cone;
            smoke.Config.Shape.ConeAngle = 0.35f;
            smoke.Config.Shape.Radius = 0.3f;
            smoke.Config.EmissionRate = 25.0f;
            smoke.Config.Lifetime = { 2.0f, 3.5f };
            smoke.Config.InitialSpeed = { 1.0f, 2.2f };
            smoke.Config.StartSize = { 0.4f, 0.7f };
            smoke.Config.EndSize = { 1.8f, 2.5f };
            smoke.Config.Buoyancy = 1.5f;
            smoke.Config.CurlNoise.Strength = 0.8f;
            smoke.Config.CurlNoise.Frequency = 0.5f;
            smoke.Config.CurlNoise.Octaves = 2;
            smoke.Config.ColorOverLifetime = Axle::ColorGradient(
                glm::vec4(0.3f, 0.3f, 0.32f, 0.6f),
                glm::vec4(0.15f, 0.15f, 0.18f, 0.0f)
            );
            smoke.Resources.TexturePath = "Assets/Textures/Particles/Smoke.png";
            smoke.Resources.UseTexture = true;
            campfire.Emitters.push_back(std::move(smoke));

            Axle::EmitterDefinition sparks;
            sparks.Id = "sparks-emitter";
            sparks.Name = "Sparks";
            sparks.Enabled = true;
            sparks.TriggerOnly = true;
            sparks.Config.Name = "Sparks";
            sparks.Config.BlendMode = Axle::ParticleBlendMode::Additive;
            sparks.Config.Shape.Type = Axle::ParticleEmitterShapeType::Sphere;
            sparks.Config.Shape.Radius = 0.1f;
            sparks.Config.EmissionRate = 0.0f;
            sparks.Config.Lifetime = { 0.6f, 1.2f };
            sparks.Config.InitialSpeed = { 2.5f, 5.0f };
            sparks.Config.StartSize = { 0.05f, 0.08f };
            sparks.Config.EndSize = { 0.01f, 0.02f };
            sparks.Config.Collision.Enabled = true;
            sparks.Config.Collision.CollisionMode = Axle::ParticleCollisionMode::Floor;
            sparks.Config.Collision.FloorHeight = 0.0f;
            sparks.Config.Collision.Restitution = 0.65f;
            sparks.Config.Collision.Friction = 0.25f;
            sparks.Config.ColorOverLifetime = Axle::ColorGradient(
                glm::vec4(1.0f, 0.6f, 0.1f, 1.0f),
                glm::vec4(0.8f, 0.1f, 0.0f, 0.0f)
            );
            campfire.Emitters.push_back(std::move(sparks));

            Axle::VFXTests::ScopedTempDir fixtureDir;
            std::filesystem::path campfirePath = fixtureDir.Path / "Campfire_v2.axle_vfx";
            VFX_TEST_CHECK(Axle::ParticleEffectSerializer::SerializeToFile(campfirePath.string(), campfire), "Serialize Campfire_v2.axle_vfx");

            Axle::ParticleEffectDefinition loadedCampfire;
            VFX_TEST_CHECK(Axle::ParticleEffectSerializer::DeserializeFromFile(campfirePath.string(), loadedCampfire), "Deserialize Campfire_v2.axle_vfx");
            VFX_TEST_CHECK(loadedCampfire.Emitters.size() == 3, "Campfire has 3 emitters");
            std::vector<std::string> errors;
            VFX_TEST_CHECK(Axle::ValidateSubEmitterGraph(loadedCampfire, errors), "Campfire sub-emitter graph is valid DAG");
        }

        // 2. Author and save CollisionSparks.axle_vfx
        {
            Axle::ParticleEffectDefinition collisionSparks;
            collisionSparks.SchemaVersion = 2;
            collisionSparks.Name = "CollisionSparks";
            collisionSparks.Seed = 101;

            Axle::EmitterDefinition flash;
            flash.Id = "flash";
            flash.Name = "Flash";
            flash.Enabled = true;
            flash.Config.Name = "Flash";
            flash.Config.BlendMode = Axle::ParticleBlendMode::Additive;
            flash.Config.EmissionRate = 0.0f;
            flash.Config.Bursts.emplace_back(0.0f, 1, 1, 1, 0.0f, 1.0f);
            flash.Config.Lifetime = { 0.08f, 0.08f };
            flash.Config.StartSize = { 0.8f, 0.8f };
            flash.Config.EndSize = { 0.0f, 0.0f };
            collisionSparks.Emitters.push_back(std::move(flash));

            Axle::EmitterDefinition debris;
            debris.Id = "debris";
            debris.Name = "DebrisSparks";
            debris.Enabled = true;
            debris.Config.Name = "DebrisSparks";
            debris.Config.BlendMode = Axle::ParticleBlendMode::Additive;
            debris.Config.EmissionRate = 0.0f;
            debris.Config.Bursts.emplace_back(0.0f, 30, 50, 1, 0.0f, 1.0f);
            debris.Config.Lifetime = { 0.8f, 1.5f };
            debris.Config.InitialSpeed = { 3.0f, 7.0f };
            debris.Config.GravityModifier = 1.2f;
            debris.Config.Collision.Enabled = true;
            debris.Config.Collision.CollisionMode = Axle::ParticleCollisionMode::Floor;
            debris.Config.Collision.FloorHeight = 0.0f;
            debris.Config.Collision.Restitution = 0.7f;
            debris.Config.Collision.Friction = 0.3f;
            collisionSparks.Emitters.push_back(std::move(debris));

            Axle::VFXTests::ScopedTempDir fixtureDir;
            std::filesystem::path sparksPath = fixtureDir.Path / "CollisionSparks.axle_vfx";
            VFX_TEST_CHECK(Axle::ParticleEffectSerializer::SerializeToFile(sparksPath.string(), collisionSparks), "Serialize CollisionSparks.axle_vfx");

            Axle::ParticleEffectDefinition loadedSparks;
            VFX_TEST_CHECK(Axle::ParticleEffectSerializer::DeserializeFromFile(sparksPath.string(), loadedSparks), "Deserialize CollisionSparks.axle_vfx");
            VFX_TEST_CHECK(loadedSparks.Emitters.size() == 2, "CollisionSparks has 2 emitters");
        }

        // 3. Author and save VehicleBackfire.axle_vfx
        {
            Axle::ParticleEffectDefinition backfire;
            backfire.SchemaVersion = 2;
            backfire.Name = "VehicleBackfire";
            backfire.Seed = 202;

            Axle::EmitterDefinition flameJet;
            flameJet.Id = "flame-jet";
            flameJet.Name = "FlameJet";
            flameJet.Enabled = true;
            flameJet.Config.Name = "FlameJet";
            flameJet.Config.BlendMode = Axle::ParticleBlendMode::Additive;
            flameJet.Config.Shape.Type = Axle::ParticleEmitterShapeType::Cone;
            flameJet.Config.Shape.ConeAngle = 0.15f;
            flameJet.Config.EmissionRate = 0.0f;
            flameJet.Config.Bursts.emplace_back(0.0f, 25, 35, 1, 0.0f, 1.0f);
            flameJet.Config.Lifetime = { 0.1f, 0.2f };
            flameJet.Config.InitialSpeed = { 8.0f, 15.0f };
            backfire.Emitters.push_back(std::move(flameJet));

            Axle::EmitterDefinition smokePuff;
            smokePuff.Id = "smoke-puff";
            smokePuff.Name = "SmokePuff";
            smokePuff.Enabled = true;
            smokePuff.Config.Name = "SmokePuff";
            smokePuff.Config.BlendMode = Axle::ParticleBlendMode::AlphaBlend;
            smokePuff.Config.EmissionRate = 0.0f;
            smokePuff.Config.Bursts.emplace_back(0.05f, 15, 25, 1, 0.0f, 1.0f);
            smokePuff.Config.Lifetime = { 1.0f, 1.8f };
            smokePuff.Config.Drag = 0.4f;
            smokePuff.Config.Buoyancy = 0.5f;
            backfire.Emitters.push_back(std::move(smokePuff));

            Axle::VFXTests::ScopedTempDir fixtureDir;
            std::filesystem::path backfirePath = fixtureDir.Path / "VehicleBackfire.axle_vfx";
            VFX_TEST_CHECK(Axle::ParticleEffectSerializer::SerializeToFile(backfirePath.string(), backfire), "Serialize VehicleBackfire.axle_vfx");

            Axle::ParticleEffectDefinition loadedBackfire;
            VFX_TEST_CHECK(Axle::ParticleEffectSerializer::DeserializeFromFile(backfirePath.string(), loadedBackfire), "Deserialize VehicleBackfire.axle_vfx");
            VFX_TEST_CHECK(loadedBackfire.Emitters.size() == 2, "VehicleBackfire has 2 emitters");
        }

        return true;
    }
}

static bool TestProductionEffectAdapters() {
    Axle::VFXTests::ScopedTempDir temp;
    auto effect = Axle::CreateDefaultParticleEffect("Adapter round trip");
    effect.Seed = 90210;
    auto& emitter = effect.Emitters[0];
    emitter.Id = "stable-primary";
    emitter.TriggerOnly = true;
    emitter.LocalTransform.Translation = {1, 2, 3};
    emitter.LocalTransform.Scale = {2, 3, 4};
    emitter.Resources = {"retained-disabled.png", "retained-normal.png", "test.axleasset", false, false};
    emitter.Config.Drag = 0.765f;
    emitter.Config.SizeOverLifetime.SetCubicBezier(0, 0.5f, 0.8f, 1);
    emitter.Config.SizeOverLifetime.P1.x = 0.08f;
    emitter.Config.SizeOverLifetime.P2.x = 0.91f;
    emitter.Config.SpeedOverLifetime.Keyframes = {{0, 0.25f}, {0.3f, 0.8f}, {1, 1}};
    emitter.Config.Bursts.emplace_back(0.2f, 3, 3, 2, 0.5f, 1.0f);
    auto file = temp.Path / "effect.axle_vfx";
    VFX_TEST_CHECK(Axle::ParticleEffectSerializer::SerializeToFile(file.string(), effect), "Write fixture");
    Axle::ParticleEmitterComponent component;
    VFX_TEST_CHECK(component.LoadFromAsset(file.string()), "Load through production component");
    VFX_TEST_CHECK(Axle::ParticleEffectSerializer::Serialize(component.GetEffectDefinition()) == Axle::ParticleEffectSerializer::Serialize(effect), "Component preserves all authored fields");
    VFX_TEST_CHECK(!component.System->IsEmitting(), "Trigger-only target does not emit continuously");
    VFX_TEST_CHECK(component.System->GetSeed() == Axle::HashEmitterSeed(effect.Seed, emitter.Id), "Seed follows effect and stable identity");
    auto scene = std::make_shared<Axle::Scene>();
    auto entity = scene->CreateEntity("Effect");
    entity.AddComponent<Axle::ParticleEmitterComponent>(component);
    auto sceneFile = temp.Path / "effect.axlescene";
    VFX_TEST_CHECK(Axle::SceneSerializer(scene.get()).Serialize(sceneFile), "Write scene");
    auto reopened = std::make_shared<Axle::Scene>();
    VFX_TEST_CHECK(Axle::SceneSerializer(reopened.get()).Deserialize(sceneFile), "Read scene");
    auto view = reopened->GetRegistry().view<Axle::ParticleEmitterComponent>();
    VFX_TEST_CHECK(view.begin() != view.end(), "Effect component restored");
    auto& loaded = view.get<Axle::ParticleEmitterComponent>(*view.begin());
    VFX_TEST_CHECK(Axle::ParticleEffectSerializer::Serialize(loaded.GetEffectDefinition()) == Axle::ParticleEffectSerializer::Serialize(effect), "Scene preserves complete definition and disabled resources");
    return true;
}

static bool TestSharedReferenceVersusUniqueStorage() {
    Axle::VFXTests::ScopedTempDir temp;
    auto definition = Axle::CreateDefaultParticleEffect("Shared");
    auto asset = temp.Path / "shared.axle_vfx";
    VFX_TEST_CHECK(Axle::ParticleEffectSerializer::SerializeToFile(asset.string(), definition),
                   "Write shared asset");
    auto scene = std::make_shared<Axle::Scene>();
    auto entity = scene->CreateEntity("Reference");
    auto &component = entity.AddComponent<Axle::ParticleEmitterComponent>();
    VFX_TEST_CHECK(component.LoadFromAsset(asset.string()), "Bind reference");
    const auto referencedScene = Axle::SceneSerializer(scene.get()).SerializeToString();
    definition.Emitters[0].Config.EmissionRate = 321;
    VFX_TEST_CHECK(Axle::ParticleEffectSerializer::SerializeToFile(asset.string(), definition),
                   "Change shared source");
    auto loaded = std::make_shared<Axle::Scene>();
    VFX_TEST_CHECK(Axle::SceneSerializer(loaded.get()).DeserializeFromString(referencedScene),
                   "Reload reference scene");
    auto view = loaded->GetRegistry().view<Axle::ParticleEmitterComponent>();
    auto &reference = view.get<Axle::ParticleEmitterComponent>(*view.begin());
    VFX_TEST_CHECK(reference.Config.EmissionRate == 321,
                   "Reference follows current shared asset, not its old fallback snapshot");
    reference.AssetPath.clear();
    const auto uniqueScene = Axle::SceneSerializer(loaded.get()).SerializeToString();
    definition.Emitters[0].Config.EmissionRate = 456;
    VFX_TEST_CHECK(Axle::ParticleEffectSerializer::SerializeToFile(asset.string(), definition),
                   "Change shared source again");
    auto unique = std::make_shared<Axle::Scene>();
    VFX_TEST_CHECK(Axle::SceneSerializer(unique.get()).DeserializeFromString(uniqueScene),
                   "Reload unique scene");
    auto uniqueView = unique->GetRegistry().view<Axle::ParticleEmitterComponent>();
    VFX_TEST_CHECK(uniqueView.get<Axle::ParticleEmitterComponent>(*uniqueView.begin()).Config.EmissionRate ==
                       321,
                   "Make Unique retains authored values independently");
    std::filesystem::remove(asset);
    auto missing = std::make_shared<Axle::Scene>();
    VFX_TEST_CHECK(Axle::SceneSerializer(missing.get()).DeserializeFromString(referencedScene),
                   "Missing reference uses complete fallback definition");
    return true;
}

static bool TestLegacyCodecRetainsCompleteCurves() {
    Axle::ParticleEmitterConfig source;
    source.SizeOverLifetime.SetEaseIn(0, 1);
    source.SizeOverLifetime.P1.x = 0.05f;
    source.SpeedOverLifetime.SetEaseOut(0, 1);
    source.AlphaOverLifetime.Keyframes = {{0, 0}, {0.2f, 0.8f}, {1, 1}};
    auto text = Axle::ParticleEmitterSerializer::Serialize(source);
    Axle::ParticleEmitterConfig restored;
    std::string texture;
    VFX_TEST_CHECK(Axle::ParticleEmitterSerializer::Deserialize(text, restored, texture),
                   "Read legacy flat container");
    VFX_TEST_CHECK(Axle::ParticleEmitterConfigsEqual(source, restored),
                   "Legacy API shares the complete module codec");
    auto modern = Axle::CreateDefaultParticleEffect();
    modern.Emitters.push_back(modern.Emitters.front());
    modern.Emitters.back().Id = "second";
    VFX_TEST_CHECK(!Axle::ParticleEmitterSerializer::Deserialize(
                       Axle::ParticleEffectSerializer::Serialize(modern), restored, texture),
                   "Single-emitter API cannot silently truncate modern effects");
    return true;
}

static bool TestDisabledModuleStateIsAuthored() {
    auto effect = Axle::CreateDefaultParticleEffect();
    auto &config = effect.Emitters[0].Config;
    config.LightingEnabled = false;
    config.WrapLighting = 0.73f;
    config.ScatteringFactor = 0.61f;
    config.NormalMapStrength = 1.7f;
    config.CurlNoise.Enabled = false;
    config.CurlNoise.Lacunarity = 3.4f;
    config.CurlNoise.Persistence = 0.2f;
    config.CurlNoise.ScrollSpeed = {1, 2, 3};
    config.Collision.Enabled = false;
    config.Collision.FloorHeight = -4;
    config.Collision.Planes.emplace_back(glm::vec3(0, 1, 0), -2.0f);
    config.Collision.Boxes.emplace_back(glm::vec3(-2), glm::vec3(2));
    config.Wind = {0, 0, 0};
    config.WindInfluence = 0.81f;
    config.BurstCount = 0;
    config.BurstInterval = 0.9f;
    config.Shape.Type = Axle::ParticleEmitterShapeType::Point;
    config.Shape.MeshPath = "retained.obj";
    config.Shape.ConeAngle = 0.25f;
    config.Shape.BoxExtents = {2, 3, 4};
    config.Shape.LineLength = 3;
    config.NearFadeEnabled = false;
    config.NearFadeStart = 3;
    config.NearFadeEnd = 4;
    config.FarFadeEnabled = false;
    config.FarFadeStart = 60;
    config.FarFadeEnd = 80;
    config.ForceFields = {Axle::ParticleForceField::CreateWind({1, 0, 0}, 4, 0.3f, 2, 0.5f),
                          Axle::ParticleForceField::CreateVortex({1, 2, 3}, {0, 1, 0}, 5, 2, 3, 4)};
    config.MaterialAsset = 1234567890123456789ull;
    Axle::ParticleEffectDefinition restored;
    VFX_TEST_CHECK(Axle::ParticleEffectSerializer::Deserialize(
                       Axle::ParticleEffectSerializer::Serialize(effect), restored),
                   "Disabled modules deserialize");
    const auto &loaded = restored.Emitters[0].Config;
    VFX_TEST_CHECK(loaded.WrapLighting == config.WrapLighting &&
                       loaded.CurlNoise.ScrollSpeed == config.CurlNoise.ScrollSpeed,
                   "Disabled lighting/noise values retained");
    VFX_TEST_CHECK(loaded.Collision.Planes.size() == 1 && loaded.Collision.Boxes.size() == 1,
                   "Disabled collision geometry retained");
    VFX_TEST_CHECK(loaded.Shape.MeshPath == config.Shape.MeshPath &&
                       loaded.Shape.BoxExtents == config.Shape.BoxExtents,
                   "Inactive shape parameters retained");
    VFX_TEST_CHECK(loaded.ForceFields.size() == 2 && loaded.ForceFields[1].RadialSuction == 3,
                   "Full force-field modules retained");
    VFX_TEST_CHECK(loaded.MaterialAsset == config.MaterialAsset &&
                       loaded.BurstInterval == config.BurstInterval,
                   "Full-width identity and disabled burst timing retained");
    VFX_TEST_CHECK(Axle::ParticleEmitterConfigsEqual(config, loaded), "Complete canonical state matches");
    return true;
}

bool RunVFXPersistenceUnitTests() {
    VFX_TEST_CHECK(TestDisabledModuleStateIsAuthored(), "Disabled module persistence");
    VFX_TEST_CHECK(TestLegacyCodecRetainsCompleteCurves(), "Legacy module codec regression");
    VFX_TEST_CHECK(TestSharedReferenceVersusUniqueStorage(), "Shared/unique storage regression");
    VFX_TEST_CHECK(TestProductionEffectAdapters(), "Production adapters failed");
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
    std::cout << "  - Running TestReferenceVFXAssetsRoundTrip ...\n";
    VFX_TEST_CHECK(TestReferenceVFXAssetsRoundTrip(), "TestReferenceVFXAssetsRoundTrip failed");
    return true;
}
