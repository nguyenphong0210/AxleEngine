#include "VFXTestHarness.hpp"
#include <Axle/ParticleSystem.hpp>
#include <Axle/VFXEditorPanel.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>

namespace {
    bool TestBaselineSystemIsolation() {
        Axle::ParticleEmitterConfig cfg;
        cfg.MaxParticles = 100;
        cfg.EmissionRate = 50.0f;

        Axle::ParticleSystem systemA(cfg);
        Axle::ParticleSystem systemB(cfg);

        systemA.SetEmitting(true);
        systemA.Update(0.1f);

        VFX_TEST_CHECK(systemA.GetActiveParticleCount() > 0, "System A should have emitted particles");
        VFX_TEST_CHECK(systemB.GetActiveParticleCount() == 0, "System B must remain unaffected by System A");
        return true;
    }

    bool TestSelectionIdempotenceAndPreservation() {
        auto scene = std::make_shared<Axle::Scene>();
        auto entity = scene->CreateEntity("CampfireEffect");
        auto& pec = entity.AddComponent<Axle::ParticleEmitterComponent>();
        pec.Config.Name = "Flame";
        pec.Config.MaxParticles = 128;
        pec.Config.EmissionRate = 30.0f;
        pec.EmitterStacks.clear();
        pec.EmitterStacks.emplace_back("Flame", pec.Config);

        Axle::ParticleEmitterConfig sparksCfg;
        sparksCfg.Name = "Sparks";
        sparksCfg.MaxParticles = 64;
        sparksCfg.EmissionRate = 15.0f;
        pec.EmitterStacks.emplace_back("Sparks", sparksCfg);

        Axle::VFXEditorPanel panel;
        panel.SetSceneContext(scene.get());
        panel.SetSelectedEntity(entity);

        VFX_TEST_CHECK(panel.GetEmitterStackCount() == 2, "Panel should have loaded 2 emitter stacks");
        VFX_TEST_CHECK(panel.GetSelectedStackIndex() == 0, "Initial stack index should be 0");

        // Select second stack and set scrubber time
        panel.SelectEmitterStack(1);
        panel.SetPlaybackScrubberTime(1.75f);
        VFX_TEST_CHECK(panel.GetSelectedStackIndex() == 1, "Selected stack index should be 1");
        VFX_TEST_APPROX_EQUAL(panel.GetPlaybackScrubberTime(), 1.75f, 0.001f, "Scrubber time should be 1.75");

        // Simulate subsequent frame selection calls in EditorLayer with the same entity
        panel.SetSelectedEntity(entity);
        panel.SetSelectedEntity(entity);

        // Verify selection and scrubber time were NOT reset
        VFX_TEST_CHECK(panel.GetSelectedStackIndex() == 1, "Selected stack index must remain 1 after repeated SetSelectedEntity");
        VFX_TEST_APPROX_EQUAL(panel.GetPlaybackScrubberTime(), 1.75f, 0.001f, "Scrubber time must be preserved after repeated SetSelectedEntity");
        VFX_TEST_CHECK(panel.GetEmitterStackCount() == 2, "Stack count must remain 2");

        // Test scene context detach
        panel.SetSceneContext(nullptr);
        VFX_TEST_CHECK(!panel.GetSelectedEntity(), "Selected entity should be detached when scene context cleared");
        VFX_TEST_CHECK(panel.GetEmitterStackCount() == 2, "Working stacks must be preserved even after scene detach");

        return true;
    }

    bool TestPreviewRuntimeIsolation() {
        // 1. ParticleEmitterStack copy independence
        Axle::ParticleEmitterConfig cfg;
        cfg.MaxParticles = 100;
        cfg.EmissionRate = 50.0f;
        Axle::ParticleEmitterStack stack1("TestStack", cfg);
        stack1.Initialize();
        Axle::ParticleEmitterStack stack2 = stack1; // copy constructor
        VFX_TEST_CHECK(stack1.System != nullptr, "Stack1 system must exist");
        VFX_TEST_CHECK(stack2.System != nullptr, "Stack2 system must exist");
        VFX_TEST_CHECK(stack1.System != stack2.System, "Stack copy must instantiate distinct ParticleSystem");

        // 2. ParticleEmitterComponent copy independence
        Axle::ParticleEmitterComponent pec1(cfg);
        Axle::ParticleEmitterComponent pec2 = pec1; // copy constructor
        VFX_TEST_CHECK(pec1.System != nullptr, "pec1 system must exist");
        VFX_TEST_CHECK(pec2.System != nullptr, "pec2 system must exist");
        VFX_TEST_CHECK(pec1.System != pec2.System, "Component copy must instantiate distinct primary ParticleSystem");
        VFX_TEST_CHECK(pec1.EmitterStacks[0].System != pec2.EmitterStacks[0].System, "Component copy must instantiate distinct stack ParticleSystem");

        // 3. VFXEditorPanel preview system independence from scene entity
        auto scene = std::make_shared<Axle::Scene>();
        auto entity = scene->CreateEntity("VFXEntity");
        auto& entityPec = entity.AddComponent<Axle::ParticleEmitterComponent>(cfg);
        Axle::VFXEditorPanel panel;
        panel.SetSceneContext(scene.get());
        panel.SetSelectedEntity(entity);

        VFX_TEST_CHECK(panel.GetPreviewSystem() != nullptr, "Preview system must exist");
        VFX_TEST_CHECK(panel.GetPreviewSystem() != entityPec.System.get(), "Preview system must be independent from scene entity system");

        // 4. Updating preview does not advance or mutate scene entity system
        panel.GetPreviewSystem()->SetEmitting(true);
        panel.GetPreviewSystem()->Update(0.1f);
        VFX_TEST_CHECK(panel.GetPreviewSystem()->GetActiveParticleCount() > 0, "Preview system should have particles");
        VFX_TEST_CHECK(entityPec.System->GetActiveParticleCount() == 0, "Scene entity system must remain at 0 particles");

        return true;
    }
}

bool RunVFXPreviewIsolationUnitTests() {
    std::cout << "  - Running TestBaselineSystemIsolation ...\n";
    VFX_TEST_CHECK(TestBaselineSystemIsolation(), "TestBaselineSystemIsolation failed");
    std::cout << "  - Running TestSelectionIdempotenceAndPreservation ...\n";
    VFX_TEST_CHECK(TestSelectionIdempotenceAndPreservation(), "TestSelectionIdempotenceAndPreservation failed");
    std::cout << "  - Running TestPreviewRuntimeIsolation ...\n";
    VFX_TEST_CHECK(TestPreviewRuntimeIsolation(), "TestPreviewRuntimeIsolation failed");
    return true;
}
