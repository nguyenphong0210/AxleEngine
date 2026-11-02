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
}

bool RunVFXPreviewIsolationUnitTests() {
    std::cout << "  - Running TestBaselineSystemIsolation ...\n";
    VFX_TEST_CHECK(TestBaselineSystemIsolation(), "TestBaselineSystemIsolation failed");
    std::cout << "  - Running TestSelectionIdempotenceAndPreservation ...\n";
    VFX_TEST_CHECK(TestSelectionIdempotenceAndPreservation(), "TestSelectionIdempotenceAndPreservation failed");
    return true;
}
