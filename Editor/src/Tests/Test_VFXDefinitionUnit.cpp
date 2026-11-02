#include "VFXTestHarness.hpp"
#include <Axle/ParticleEffectDefinition.hpp>
#include <Axle/ParticlePool.hpp>

namespace {
    bool TestBaselineVFXTypes() {
        Axle::ParticlePool pool(100);
        VFX_TEST_CHECK(pool.GetCapacity() == 100, "Pool capacity initialization");
        VFX_TEST_CHECK(pool.GetActiveCount() == 0, "Initial pool count should be zero");
        return true;
    }

    bool TestDefaultEffectDefinition() {
        auto def = Axle::CreateDefaultParticleEffect("Campfire");
        VFX_TEST_CHECK(def.SchemaVersion == 2, "Schema version should be 2");
        VFX_TEST_CHECK(def.Name == "Campfire", "Name preserved");
        VFX_TEST_CHECK(def.Emitters.size() == 1, "Default effect has 1 emitter");
        VFX_TEST_CHECK(!def.Emitters[0].Id.empty(), "Default emitter must have a non-empty ID");

        std::vector<std::string> errors;
        VFX_TEST_CHECK(Axle::ValidateParticleEffectDefinition(def, errors), "Default effect must pass validation");
        VFX_TEST_CHECK(errors.empty(), "No validation errors expected");
        return true;
    }

    bool TestDefinitionValidation() {
        // 1. Valid multi-emitter campfire fixture
        Axle::ParticleEffectDefinition campfire;
        campfire.SchemaVersion = 2;
        campfire.Name = "Campfire_Multi";
        campfire.Seed = 42;

        Axle::EmitterDefinition flame;
        flame.Id = "flame-001";
        flame.Name = "Flame";
        flame.Config = Axle::ParticleEmitterConfig::CreateFire();

        Axle::EmitterDefinition smoke;
        smoke.Id = "smoke-002";
        smoke.Name = "Smoke";
        smoke.Config = Axle::ParticleEmitterConfig::CreateSmoke();

        Axle::EmitterDefinition sparks;
        sparks.Id = "sparks-003";
        sparks.Name = "Sparks";
        sparks.TriggerOnly = true;
        sparks.Config = Axle::ParticleEmitterConfig::CreateSparks();

        campfire.Emitters.push_back(flame);
        campfire.Emitters.push_back(smoke);
        campfire.Emitters.push_back(sparks);

        std::vector<std::string> errors;
        VFX_TEST_CHECK(Axle::ValidateParticleEffectDefinition(campfire, errors), "Campfire fixture must be valid");
        VFX_TEST_CHECK(campfire.FindEmitter("flame-001") != nullptr, "Lookup flame-001");
        VFX_TEST_CHECK(campfire.FindEmitter("sparks-003") != nullptr, "Lookup sparks-003");
        VFX_TEST_CHECK(campfire.FindEmitter("nonexistent") == nullptr, "Lookup nonexistent is null");

        // 2. Duplicate ID detection
        Axle::ParticleEffectDefinition dupDef = campfire;
        dupDef.Emitters[1].Id = "flame-001"; // Duplicate ID!
        VFX_TEST_CHECK(!Axle::ValidateParticleEffectDefinition(dupDef, errors), "Duplicate IDs must fail validation");
        VFX_TEST_CHECK(!errors.empty(), "Duplicate ID error reported");

        // 3. Zero emitters failure
        Axle::ParticleEffectDefinition emptyDef;
        emptyDef.Emitters.clear();
        VFX_TEST_CHECK(!Axle::ValidateParticleEffectDefinition(emptyDef, errors), "Empty effect must fail validation");

        // 4. Invalid transform quaternion
        Axle::ParticleEffectDefinition badRotDef = campfire;
        badRotDef.Emitters[0].LocalTransform.Rotation = glm::quat(0.0f, 0.0f, 0.0f, 0.0f); // zero norm!
        VFX_TEST_CHECK(!Axle::ValidateParticleEffectDefinition(badRotDef, errors), "Zero quaternion must fail validation");

        // 5. Unsupported schema version
        Axle::ParticleEffectDefinition badVersionDef = campfire;
        badVersionDef.SchemaVersion = 99;
        VFX_TEST_CHECK(!Axle::ValidateParticleEffectDefinition(badVersionDef, errors), "Future schema version 99 must fail validation");

        return true;
    }
}

bool RunVFXDefinitionUnitTests() {
    std::cout << "  - Running TestBaselineVFXTypes ...\n";
    VFX_TEST_CHECK(TestBaselineVFXTypes(), "TestBaselineVFXTypes failed");
    std::cout << "  - Running TestDefaultEffectDefinition ...\n";
    VFX_TEST_CHECK(TestDefaultEffectDefinition(), "TestDefaultEffectDefinition failed");
    std::cout << "  - Running TestDefinitionValidation ...\n";
    VFX_TEST_CHECK(TestDefinitionValidation(), "TestDefinitionValidation failed");
    return true;
}
