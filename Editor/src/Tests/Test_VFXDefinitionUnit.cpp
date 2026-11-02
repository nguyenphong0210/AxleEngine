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

    bool TestCurvesAndGradientsContract() {
        // 1. FloatCurve evaluation
        Axle::FloatCurve linearCurve;
        linearCurve.SetLinear(2.0f, 0.0f);
        VFX_TEST_CHECK(std::abs(linearCurve.Evaluate(0.0f) - 2.0f) < 1e-3f, "Linear curve at t=0");
        VFX_TEST_CHECK(std::abs(linearCurve.Evaluate(0.5f) - 1.0f) < 1e-3f, "Linear curve at t=0.5");
        VFX_TEST_CHECK(std::abs(linearCurve.Evaluate(1.0f) - 0.0f) < 1e-3f, "Linear curve at t=1.0");

        Axle::FloatCurve constantCurve;
        constantCurve.SetConstant(0.75f);
        VFX_TEST_CHECK(std::abs(constantCurve.Evaluate(0.0f) - 0.75f) < 1e-3f, "Constant curve at t=0");
        VFX_TEST_CHECK(std::abs(constantCurve.Evaluate(0.8f) - 0.75f) < 1e-3f, "Constant curve at t=0.8");

        Axle::FloatCurve bellCurve;
        bellCurve.SetBell(0.0f, 1.0f, 0.0f);
        VFX_TEST_CHECK(std::abs(bellCurve.Evaluate(0.0f) - 0.0f) < 1e-3f, "Bell curve at t=0");
        VFX_TEST_CHECK(std::abs(bellCurve.Evaluate(1.0f) - 0.0f) < 1e-3f, "Bell curve at t=1");
        VFX_TEST_CHECK(bellCurve.Evaluate(0.5f) > 0.6f, "Bell curve peak near t=0.5");

        // 2. ColorGradient evaluation & key management
        Axle::ColorGradient grad;
        grad.SetFlamePreset();
        VFX_TEST_CHECK(!grad.ColorKeys.empty(), "Flame preset has color keys");
        VFX_TEST_CHECK(!grad.AlphaKeys.empty(), "Flame preset has alpha keys");

        // Evaluate t=0 should match yellow start and alpha 0
        glm::vec4 c0 = grad.Evaluate(0.0f);
        VFX_TEST_CHECK(c0.r > 0.8f && c0.g > 0.8f, "Flame t=0 color is yellowish");
        VFX_TEST_CHECK(std::abs(c0.a - 0.0f) < 1e-3f, "Flame t=0 alpha is 0.0");

        // Evaluate t=0.1 should have alpha 1.0
        glm::vec4 c1 = grad.Evaluate(0.1f);
        VFX_TEST_CHECK(std::abs(c1.a - 1.0f) < 1e-3f, "Flame t=0.1 alpha is 1.0");

        // Adding crossed keys and sorting
        Axle::ColorGradient customGrad;
        customGrad.AddColorKey(0.8f, glm::vec3(0.0f, 0.0f, 1.0f)); // Blue at 0.8
        customGrad.AddColorKey(0.2f, glm::vec3(1.0f, 0.0f, 0.0f)); // Red at 0.2
        VFX_TEST_CHECK(customGrad.ColorKeys.front().Time < customGrad.ColorKeys.back().Time, "Keys sorted after addition");

        customGrad.AddAlphaKey(0.7f, 0.3f);
        customGrad.AddAlphaKey(0.1f, 0.9f);
        VFX_TEST_CHECK(customGrad.AlphaKeys.front().Time < customGrad.AlphaKeys.back().Time, "Alpha keys sorted");

        // Evaluate custom gradient between keys
        glm::vec4 midCol = customGrad.Evaluate(0.2f);
        VFX_TEST_CHECK(std::abs(midCol.r - 1.0f) < 1e-3f, "Evaluated exact key color");

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
    std::cout << "  - Running TestCurvesAndGradientsContract ...\n";
    VFX_TEST_CHECK(TestCurvesAndGradientsContract(), "TestCurvesAndGradientsContract failed");
    return true;
}
