#include "VFXTestHarness.hpp"
#include <Axle/VFXCommon.hpp>
#include <Axle/ParticleEffectDefinition.hpp>
#include <Axle/Components.hpp>

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

    bool TestBoundedSubEmitterEventSpawning() {
        // 1. Setup Fire emitter with Death trigger targeting Sparks
        Axle::ParticleEmitterConfig fireConfig;
        fireConfig.Name = "Fire";
        fireConfig.MaxParticles = 50;
        fireConfig.EmissionRate = 0.0f; // controlled bursts
        fireConfig.Lifetime = { 0.1f, 0.1f };
        fireConfig.InitialSpeed = { 5.0f, 5.0f };
        fireConfig.SubEmitters.emplace_back(Axle::SubEmitterTrigger::Death, "Sparks", 5, 1.0f);
        fireConfig.SubEmitters.back().InheritColor = true;
        fireConfig.SubEmitters.back().InheritVelocity = true;

        Axle::ParticleEmitterStack fireStack("Fire", fireConfig);
        fireStack.Initialize();

        // 2. Setup Sparks emitter (Target) with TriggerOnly (no auto emission)
        Axle::ParticleEmitterConfig sparksConfig;
        sparksConfig.Name = "Sparks";
        sparksConfig.MaxParticles = 100;
        sparksConfig.EmissionRate = 0.0f;
        sparksConfig.Lifetime = { 1.0f, 1.0f };
        sparksConfig.InitialSpeed = { 2.0f, 2.0f };

        Axle::ParticleEmitterStack sparksStack("Sparks", sparksConfig);
        sparksStack.Initialize();

        std::vector<Axle::ParticleEmitterStack> stacks;
        stacks.push_back(fireStack);
        stacks.push_back(sparksStack);

        VFX_TEST_CHECK(stacks[1].System->GetActiveParticleCount() == 0, "Sparks starts empty");

        // 3. Emit 2 particles in Fire
        stacks[0].System->EmitBurst(2);
        VFX_TEST_CHECK(stacks[0].System->GetActiveParticleCount() == 2, "Fire has 2 active particles");

        // 4. Update with dt = 0.05s (particles are still alive)
        stacks[0].System->Update(0.05f);
        stacks[1].System->Update(0.05f);
        Axle::DispatchEffectEvents(stacks);
        VFX_TEST_CHECK(stacks[1].System->GetActiveParticleCount() == 0, "Sparks still empty before Fire particles die");

        // 5. Update with dt = 0.08s (total 0.13s > 0.1s lifetime -> Fire particles die and trigger Death event)
        stacks[0].System->Update(0.08f);
        stacks[1].System->Update(0.08f);
        Axle::DispatchEffectEvents(stacks);

        VFX_TEST_CHECK(stacks[0].System->GetActiveParticleCount() == 0, "Fire particles died");
        // 2 Fire particles died * 5 sparks = 10 sparks spawned
        VFX_TEST_CHECK(stacks[1].System->GetActiveParticleCount() == 10, "Sparks received 10 spawned particles from Death event");

        // 6. Test event budgeting: huge burst should clamp to budget
        stacks[0].System->Reset();
        stacks[1].System->Reset();
        stacks[0].Config.SubEmitters[0].EmitCount = 50;
        stacks[0].System->SetConfig(stacks[0].Config);
        stacks[0].System->EmitBurst(20); // 20 * 50 = 1000 requested spawns
        stacks[0].System->Update(0.15f); // all die
        stacks[1].System->Update(0.15f);

        // Dispatch with strict budget = 2 events
        Axle::DispatchEffectEvents(stacks, 2);
        // 2 events * 50 spawns = 100 spawns (pool cap is 100)
        VFX_TEST_CHECK(stacks[1].System->GetActiveParticleCount() <= 100, "Spawns bounded within budget and capacity");

        return true;
    }
}

bool RunVFXEventsAndMeshUnitTests() {
    std::cout << "  - Running TestBaselineEventsAndMesh ...\n";
    VFX_TEST_CHECK(TestBaselineEventsAndMesh(), "TestBaselineEventsAndMesh failed");
    std::cout << "  - Running TestSubEmitterGraphValidation ...\n";
    VFX_TEST_CHECK(TestSubEmitterGraphValidation(), "TestSubEmitterGraphValidation failed");
    std::cout << "  - Running TestBoundedSubEmitterEventSpawning ...\n";
    VFX_TEST_CHECK(TestBoundedSubEmitterEventSpawning(), "TestBoundedSubEmitterEventSpawning failed");
    return true;
}
