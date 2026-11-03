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

    bool TestStackEditPreservationOnApply() {
        auto scene = std::make_shared<Axle::Scene>();
        auto entity = scene->CreateEntity("CampfireVFX");
        auto& pec = entity.AddComponent<Axle::ParticleEmitterComponent>();
        pec.Config.Name = "BaseFlame";
        pec.Config.EmissionRate = 25.0f;
        pec.EmitterStacks.clear();
        pec.EmitterStacks.emplace_back("BaseFlame", pec.Config);

        Axle::ParticleEmitterConfig smokeCfg;
        smokeCfg.Name = "SmokePlume";
        smokeCfg.EmissionRate = 10.0f;
        pec.EmitterStacks.emplace_back("SmokePlume", smokeCfg);
        pec.SyncPrimary();
        pec.Initialize();

        Axle::VFXEditorPanel panel;
        panel.SetSceneContext(scene.get());
        panel.SetSelectedEntity(entity);

        // Select the secondary stack (SmokePlume at index 1)
        panel.SelectEmitterStack(1);
        VFX_TEST_CHECK(panel.GetSelectedStackIndex() == 1, "Selected stack must be 1");
        VFX_TEST_CHECK(panel.GetConfig().Name == "SmokePlume", "Config should match selected stack");

        // Mutate the secondary stack's emission rate
        panel.GetConfig().EmissionRate = 88.0f;

        // Apply back to entity (simulating the apply button logic)
        auto* stack1 = panel.GetCurrentEmitterStack();
        VFX_TEST_CHECK(stack1 != nullptr, "Selected stack pointer must exist");
        panel.SelectEmitterStack(1); // triggers SyncCurrentStackConfig()

        // Manually trigger the updated Apply logic on pec
        pec.EmitterStacks.clear();
        for (size_t i = 0; i < panel.GetEmitterStackCount(); ++i) {
            auto* s = panel.GetEmitterStackCount() > i ? panel.GetCurrentEmitterStack() : nullptr; // or get stack
        }
        // Let's copy from panel stacks
        for (size_t i = 0; i < 2; ++i) {
            panel.SelectEmitterStack(i);
            Axle::ParticleEmitterStack sceneStack(panel.GetConfig().Name, panel.GetConfig());
            pec.EmitterStacks.push_back(std::move(sceneStack));
        }
        pec.SyncPrimary();

        // Verify: Primary stack (index 0) was preserved and NOT overwritten by index 1
        VFX_TEST_CHECK(pec.EmitterStacks[0].Config.Name == "BaseFlame", "Primary stack name must remain BaseFlame");
        VFX_TEST_APPROX_EQUAL(pec.EmitterStacks[0].Config.EmissionRate, 25.0f, 0.001f, "Primary stack rate must remain 25.0");
        VFX_TEST_APPROX_EQUAL(pec.Config.EmissionRate, 25.0f, 0.001f, "Primary pec.Config must remain 25.0, not secondary rate");

        // Verify: Secondary stack received the modified rate
        VFX_TEST_CHECK(pec.EmitterStacks[1].Config.Name == "SmokePlume", "Secondary stack name must be SmokePlume");
        VFX_TEST_APPROX_EQUAL(pec.EmitterStacks[1].Config.EmissionRate, 88.0f, 0.001f, "Secondary stack rate must be 88.0");

        return true;
    }

    bool TestPlayPreservesAuthoredDisabledState() {
        Axle::ParticleEmitterComponent pec;
        pec.EmitterStacks.clear();

        Axle::ParticleEmitterConfig flameCfg;
        flameCfg.Name = "Flame";
        Axle::ParticleEmitterStack flame("Flame", flameCfg);
        flame.Enabled = true;
        flame.Initialize();
        pec.EmitterStacks.push_back(std::move(flame));

        Axle::ParticleEmitterConfig embersCfg;
        embersCfg.Name = "Embers";
        Axle::ParticleEmitterStack embers("Embers", embersCfg);
        embers.Enabled = false; // Authored as disabled!
        embers.Initialize();
        pec.EmitterStacks.push_back(std::move(embers));

        pec.SyncPrimary();

        // 1. Calling Play() must NOT set embers.Enabled = true!
        pec.Play();
        VFX_TEST_CHECK(pec.Active == true, "Component should be active");
        VFX_TEST_CHECK(pec.EmitterStacks[0].Enabled == true, "Flame stack must remain enabled");
        VFX_TEST_CHECK(pec.EmitterStacks[1].Enabled == false, "Embers stack must RETAIN authored disabled state after Play()");

        VFX_TEST_CHECK(pec.EmitterStacks[0].System->IsEmitting() == true, "Enabled stack system must emit");
        VFX_TEST_CHECK(pec.EmitterStacks[1].System->IsEmitting() == false, "Disabled stack system must NOT emit");

        // 2. Calling Stop() must NOT reset authored flame.Enabled to false
        pec.Stop();
        VFX_TEST_CHECK(pec.Active == false, "Component should be inactive");
        VFX_TEST_CHECK(pec.EmitterStacks[0].Enabled == true, "Flame stack must retain enabled authored state after Stop()");
        VFX_TEST_CHECK(pec.EmitterStacks[1].Enabled == false, "Embers stack must retain disabled authored state after Stop()");

        return true;
    }
}

static bool TestPanelDocumentIntegration() {
    Axle::VFXEditorPanel panel;
    VFX_TEST_CHECK(panel.GetEmitterStackCount() == 0 && panel.GetPreviewSystem() == nullptr, "Panel starts empty");
    panel.AddEmitterStack("First");
    auto first = panel.GetDocument().GetDefinition().Emitters[0].Id;
    auto firstConfig = panel.GetConfig();
    Axle::ParticleEmitterConfig second;
    second.Name = "Secondary";
    second.EmissionRate = 12;
    panel.AddEmitterStack("Secondary", second);
    VFX_TEST_CHECK(panel.GetDocument().IsDirty(), "Panel add marks document dirty");
    VFX_TEST_CHECK(panel.Undo() && panel.GetEmitterStackCount() == 1, "Panel undo removes added emitter");
    VFX_TEST_CHECK(panel.Redo() && panel.GetEmitterStackCount() == 2, "Panel redo restores added emitter");
    panel.RemoveEmitterStack(1);
    VFX_TEST_CHECK(panel.GetConfig().Name == firstConfig.Name,
                   "Delete selected emitter does not overwrite survivor");
    VFX_TEST_CHECK(panel.GetDocument().GetDefinition().Emitters[0].Id == first,
                   "Surviving ID remains stable");
    Axle::VFXTests::ScopedTempDir temp;
    auto path = (temp.Path / "panel.axle_vfx").string();
    VFX_TEST_CHECK(panel.SaveDocumentAs(path), "Panel save");
    VFX_TEST_CHECK(!panel.GetDocument().IsDirty(), "Save marks document clean");
    auto before = panel.GetDocument().GetDefinition();
    panel.GetConfig().Drag += 0.33f;
    panel.SelectEmitterStack(0);
    VFX_TEST_CHECK(panel.GetDocument().IsDirty(), "Module edit is dirty");
    VFX_TEST_CHECK(panel.Undo() && panel.GetDocument().GetDefinition() == before,
                   "Panel undo restores module values");
    VFX_TEST_CHECK(panel.SaveDocument() && panel.OpenDocument(path), "Panel save/reopen");
    VFX_TEST_CHECK(panel.GetDocument().GetDefinition() == before,
                   "Panel open/save round trip preserves identity");
    auto scene = std::make_shared<Axle::Scene>();
    auto sceneEntity = scene->CreateEntity("Other effect");
    sceneEntity.AddComponent<Axle::ParticleEmitterComponent>();
    panel.SetSceneContext(scene.get());
    panel.SetSelectedEntity(sceneEntity);
    VFX_TEST_CHECK(panel.GetDocument().GetDefinition() == before,
                   "Scene polling cannot replace an opened asset document");
    scene.reset();
    panel.SetSceneContext(nullptr);
    VFX_TEST_CHECK(!panel.GetSelectedEntity(), "Scene detach does not dereference a destroyed registry");
    return true;
}

static bool TestPausedSeekAndLoopBoundary() {
    Axle::VFXEditorPanel direct, seek;
    direct.AddEmitterStack("Fixture");
    auto cfg = direct.GetConfig();
    cfg.Duration = 1.0f;
    cfg.Looping = true;
    direct.SetEmitterConfig(cfg);
    seek.GetDocument().LoadDefinition(direct.GetDocument().GetDefinition());
    seek.SyncStacksFromDocument();
    direct.PausePreview();
    seek.PausePreview();
    for (int i = 0; i < 60; ++i)
        direct.Step();
    seek.SeekTo(1.0f);
    VFX_TEST_APPROX_EQUAL(seek.GetPlaybackScrubberTime(), 1.0f, 0.0001f,
                          "Seek finishes exactly at looping duration");
    VFX_TEST_CHECK(direct.GetPreviewSystem()->GetPool().GetActiveCount() ==
                       seek.GetPreviewSystem()->GetPool().GetActiveCount(),
                   "Paused seek emits exactly as stepped simulation");
    const auto &a = direct.GetPreviewSystem()->GetPool();
    const auto &b = seek.GetPreviewSystem()->GetPool();
    for (uint32_t i = 0; i < a.GetActiveCount(); ++i)
        VFX_TEST_CHECK(a.GetPositions()[i] == b.GetPositions()[i], "Seek preserves deterministic positions");
    return true;
}

bool RunVFXPreviewIsolationUnitTests() {
    VFX_TEST_CHECK(TestPausedSeekAndLoopBoundary(), "Transport regression");
    VFX_TEST_CHECK(TestPanelDocumentIntegration(), "Panel document regression failed");
    std::cout << "  - Running TestBaselineSystemIsolation ...\n";
    VFX_TEST_CHECK(TestBaselineSystemIsolation(), "TestBaselineSystemIsolation failed");
    std::cout << "  - Running TestSelectionIdempotenceAndPreservation ...\n";
    VFX_TEST_CHECK(TestSelectionIdempotenceAndPreservation(), "TestSelectionIdempotenceAndPreservation failed");
    std::cout << "  - Running TestPreviewRuntimeIsolation ...\n";
    VFX_TEST_CHECK(TestPreviewRuntimeIsolation(), "TestPreviewRuntimeIsolation failed");
    std::cout << "  - Running TestStackEditPreservationOnApply ...\n";
    VFX_TEST_CHECK(TestStackEditPreservationOnApply(), "TestStackEditPreservationOnApply failed");
    std::cout << "  - Running TestPlayPreservesAuthoredDisabledState ...\n";
    VFX_TEST_CHECK(TestPlayPreservesAuthoredDisabledState(), "TestPlayPreservesAuthoredDisabledState failed");
    return true;
}
