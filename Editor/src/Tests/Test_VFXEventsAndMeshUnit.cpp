#include "VFXTestHarness.hpp"
#include <Axle/VFXCommon.hpp>
#include <Axle/ParticleEffectDefinition.hpp>

namespace {
    bool TestBaselineEventsAndMesh() {
        // Baseline test for sub-emitter triggers
        Axle::SubEmitterTrigger birth = Axle::SubEmitterTrigger::Birth;
        Axle::SubEmitterTrigger collision = Axle::SubEmitterTrigger::Collision;
        Axle::SubEmitterTrigger death = Axle::SubEmitterTrigger::Death;
        VFX_TEST_CHECK(static_cast<int>(birth) == 0, "Birth trigger index");
        VFX_TEST_CHECK(static_cast<int>(collision) == 1, "Collision trigger index");
        VFX_TEST_CHECK(static_cast<int>(death) == 2, "Death trigger index");
        return true;
    }

    bool TestSubEmitterGraphValidation() {
        // 1. Valid linear chain A -> B -> C
        Axle::ParticleEffectDefinition def;
        def.SchemaVersion = 2;
        def.Name = "ValidChain";

        Axle::EmitterDefinition eA; eA.Id = "eA"; eA.Name = "EmitterA";
        eA.Config.SubEmitters.emplace_back(Axle::SubEmitterTrigger::Death, "eB", 5, 1.0f);

        Axle::EmitterDefinition eB; eB.Id = "eB"; eB.Name = "EmitterB";
        eB.Config.SubEmitters.emplace_back(Axle::SubEmitterTrigger::Collision, "eC", 10, 1.0f);

        Axle::EmitterDefinition eC; eC.Id = "eC"; eC.Name = "EmitterC";

        def.Emitters = { eA, eB, eC };

        std::vector<std::string> errors;
        VFX_TEST_CHECK(Axle::ValidateSubEmitterGraph(def, errors), "Valid DAG should pass validation");
        VFX_TEST_CHECK(errors.empty(), "No errors for valid DAG");

        // 2. Self-referencing link A -> A
        Axle::ParticleEffectDefinition selfDef = def;
        selfDef.Emitters[0].Config.SubEmitters[0].TargetEmitterName = "eA";
        errors.clear();
        VFX_TEST_CHECK(!Axle::ValidateSubEmitterGraph(selfDef, errors), "Self-referencing emitter must fail");
        VFX_TEST_CHECK(!errors.empty(), "Error reported for self-reference");

        // 3. Non-existent target A -> NonExistent
        Axle::ParticleEffectDefinition missingDef = def;
        missingDef.Emitters[0].Config.SubEmitters[0].TargetEmitterName = "Ghost";
        errors.clear();
        VFX_TEST_CHECK(!Axle::ValidateSubEmitterGraph(missingDef, errors), "Missing target must fail");
        VFX_TEST_CHECK(!errors.empty(), "Error reported for missing target");

        // 4. Direct cycle A -> B -> A
        Axle::ParticleEffectDefinition cycleDef = def;
        cycleDef.Emitters[1].Config.SubEmitters[0].TargetEmitterName = "eA";
        errors.clear();
        VFX_TEST_CHECK(!Axle::ValidateSubEmitterGraph(cycleDef, errors), "Direct cycle A->B->A must fail");
        VFX_TEST_CHECK(!errors.empty(), "Error reported for cycle");

        // 5. Transitive cycle A -> B -> C -> A
        Axle::ParticleEffectDefinition transDef = def;
        transDef.Emitters[2].Config.SubEmitters.emplace_back(Axle::SubEmitterTrigger::Birth, "eA", 2, 1.0f);
        errors.clear();
        VFX_TEST_CHECK(!Axle::ValidateSubEmitterGraph(transDef, errors), "Transitive cycle A->B->C->A must fail");
        VFX_TEST_CHECK(!errors.empty(), "Error reported for transitive cycle");

        return true;
    }
}

bool RunVFXEventsAndMeshUnitTests() {
    std::cout << "  - Running TestBaselineEventsAndMesh ...\n";
    VFX_TEST_CHECK(TestBaselineEventsAndMesh(), "TestBaselineEventsAndMesh failed");
    std::cout << "  - Running TestSubEmitterGraphValidation ...\n";
    VFX_TEST_CHECK(TestSubEmitterGraphValidation(), "TestSubEmitterGraphValidation failed");
    return true;
}
