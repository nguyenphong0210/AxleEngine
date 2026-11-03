#include "VFXTestHarness.hpp"
#include <Axle/ParticleModules.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/ParticleEffectSerializer.hpp>
#include <Axle/ParticleEmitterSerializer.hpp>
#include <Axle/ParticleSystem.hpp>
#include <Axle/VFXEditorDocument.hpp>
#include <Axle/VFXEditorPanel.hpp>
#include <Axle/Scene.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstring>

namespace {
    bool TestModulePersistenceAndHistory() {
        using namespace Axle;
        VFXEditorDocument doc;
        const auto id = doc.AddEmitter("Authored", ParticleEmitterConfig::CreateNeutral());
        auto definition = doc.GetDefinition();
        auto& config = definition.Emitters[0].Config;
        config.StartColor = {0.4f, 0.7f, 0.9f, 0.8f};
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
        config.StartColor = {0.5f, 0.5f, 0.5f, 0.5f};
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
        config.SubEmitters.emplace_back(SubEmitterTrigger::Birth, "child", 1u, 1.0f);
        ParticleSystem system(config);
        system.EmitBurst(1);
        VFX_TEST_CHECK(system.GetEventQueue().empty(), "Disabled sub emitters generate no events");
        auto& pool = system.GetPool();
        VFX_TEST_CHECK(glm::length(pool.GetPositions()[0]) == 0, "Disabled shape spawns at origin");
        system.Update(1);
        VFX_TEST_CHECK(std::abs(pool.GetPositions()[0].y - 1) < 0.001f && pool.GetPositions()[0].x == 0,
                       "Disabled speed and forces do not alter start velocity");
        VFX_TEST_CHECK(pool.GetSizes()[0] == 2 && pool.GetRotations()[0] == 0 && pool.GetColors()[0] == glm::vec4(0.5f),
                       "Disabled lifetime modules retain initial size, rotation and color");
        VFX_TEST_CHECK(pool.GetUVOffsetScales()[0] == glm::vec4(0, 0, 1, 1), "Disabled sheet uses complete texture");
        auto& live = system.GetConfig();
        live.Modules.Size = live.Modules.Speed = live.Modules.Color = live.Modules.Alpha = true;
        live.Modules.Rotation = live.Modules.Forces = live.Modules.TextureSheet = true;
        system.Update(1);
        VFX_TEST_CHECK(pool.GetSizes()[0] > 4 && pool.GetRotations()[0] == 2 && pool.GetColors()[0].a < 0.1f,
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
        config.Bursts.emplace_back(0.0f, 3u, 3u, 1u, 0.0f);
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

    bool TestAuthoringCommandsAndGraph() {
        using namespace Axle;
        VFXEditorPanel panel;
        panel.AddEmitterStack("Parent");
        const auto parent = panel.GetCurrentEmitterStack()->Id;
        panel.GetDocument().BeginTransaction();
        panel.GetConfig().EmissionRate = 42;
        panel.AddEmitterStack("Child");
        const auto child = panel.GetCurrentEmitterStack()->Id;
        VFX_TEST_CHECK(panel.Undo() && panel.GetEmitterStackCount() == 1 && panel.GetConfig().EmissionRate == 42,
                       "Add emitter is a separate undo step from an active property drag");
        VFX_TEST_CHECK(panel.Undo() && panel.GetConfig().EmissionRate == 10, "Second undo restores the drag");
        VFX_TEST_CHECK(panel.Redo() && panel.Redo(), "Redo drag and add");
        panel.SelectEmitterStack(0);
        VFX_TEST_CHECK(panel.SetSubEmitterTarget(0, child), "Child chosen by authored ID");
        const auto links = panel.GetConfig().SubEmitters.size();
        VFX_TEST_CHECK(!panel.SetSubEmitterTarget(0, "missing") && !panel.SetSubEmitterTarget(0, parent),
                       "Missing and self targets are rejected before publication");
        VFX_TEST_CHECK(panel.GetConfig().SubEmitters.size() == links && panel.GetConfig().SubEmitters[0].TargetEmitterName == child,
                       "Rejected graph edit preserves the working effect");
        panel.SelectEmitterStack(1);
        VFX_TEST_CHECK(!panel.SetSubEmitterTarget(0, parent), "Cycle rejected before publication");
        VFX_TEST_CHECK(panel.GetDocument().RenameEmitter(child, "Renamed"), "Rename child");
        panel.SyncStacksFromDocument();
        VFX_TEST_CHECK(panel.GetConfig().Name == "Renamed", "Display and simulation emitter names stay consistent");
        VFX_TEST_CHECK(panel.DuplicateEmitterStack(1), "Explicit duplicate command");
        const auto duplicate = panel.GetCurrentEmitterStack()->Id;
        VFX_TEST_CHECK(duplicate != child && panel.GetCurrentEmitterStack()->Name == "Renamed (Copy)", "Duplicate has its own identity");
        VFX_TEST_CHECK(panel.ReorderEmitterStack(2, 0) && panel.GetCurrentEmitterStack()->Id == duplicate,
                       "Reorder retains selected emitter identity");
        return true;
    }

    bool TestSceneAuthoringAndBounds() {
        using namespace Axle;
        Scene scene;
        VFXEditorPanel panel;
        panel.SetSceneContext(&scene);
        auto empty = panel.CreateSceneInstance({2, 3, 4});
        VFX_TEST_CHECK(empty && empty.GetComponent<ParticleEmitterComponent>().EmitterStacks.empty(),
                       "Creating an empty scene instance does not synthesize an emitter");
        VFX_TEST_CHECK(empty.GetComponent<TransformComponent>().Translation == glm::vec3(2, 3, 4), "Chosen scene position is honored");
        panel.AddEmitterStack("Authored");
        auto cfg = panel.GetConfig();
        cfg.SimulationSpace = ParticleSimulationSpace::Local;
        cfg.InitialSpeed = {0, 0};
        cfg.StartSize = {2, 2};
        panel.SetEmitterConfig(cfg);
        auto* stack = panel.GetCurrentEmitterStack();
        stack->LocalTransform.Translation = {10, 0, 0};
        stack->LocalTransform.Scale = {2, 2, 2};
        panel.TriggerBurst(1);
        glm::vec3 minimum, maximum;
        VFX_TEST_CHECK(panel.GetPreviewBounds(minimum, maximum) && glm::length(minimum - (glm::vec3(10, 0, 0) - glm::vec3(std::sqrt(8.0f)))) < 0.001f && glm::length(maximum - (glm::vec3(10, 0, 0) + glm::vec3(std::sqrt(8.0f)))) < 0.001f,
                       "Live preview bounds include local transform and particle radius");
        stack->Visible = false;
        VFX_TEST_CHECK(!panel.GetPreviewBounds(minimum, maximum), "Hidden preview emitters do not affect framing");
        stack->Visible = true;
        panel.FramePreview();
        VFXTests::ScopedTempDir temp;
        const auto path = (temp.Path / "authored.axleasset").string();
        VFX_TEST_CHECK(panel.SaveDocumentAs(path), "Save authored effect for scene reference");
        auto instance = panel.CreateSceneInstance({4, 5, 6});
        auto& component = instance.GetComponent<ParticleEmitterComponent>();
        VFX_TEST_CHECK(component.AssetPath == path && component.GetEffectDefinition() == panel.GetDocument().GetDefinition(),
                       "Clean saved documents create referenced instances");
        VFX_TEST_CHECK(component.System.get() != panel.GetPreviewSystem(), "Scene instance owns independent simulation");
        panel.GetConfig().EmissionRate = 17;
        VFX_TEST_CHECK(panel.ApplyToSelectedEntity() && component.AssetPath.empty() && component.Config.EmissionRate == 17,
                       "Apply working copy explicitly detaches reference and transfers edits");
        panel.Undo();
        VFX_TEST_CHECK(component.Config.EmissionRate == 17, "Editor undo does not rewind scene simulation or inline data");
        cfg = panel.GetConfig();
        cfg.PlayOnAwake = false;
        panel.SetEmitterConfig(cfg);
        auto dormant = panel.CreateSceneInstance();
        auto& dormantEffect = dormant.GetComponent<ParticleEmitterComponent>();
        dormantEffect.Update(0.5f);
        VFX_TEST_CHECK(!dormantEffect.System->IsEmitting() && dormantEffect.System->GetActiveParticleCount() == 0,
                       "Placing an effect honors authored Play on Awake instead of forcing Play");
        VFX_TEST_CHECK(panel.ApplyToSelectedEntity() && !dormantEffect.System->IsEmitting(), "Applying also retains authored autoplay behavior");
        dormantEffect.Play();
        dormantEffect.Update(0.1f);
        VFX_TEST_CHECK(dormantEffect.System->GetActiveParticleCount() > 0, "An explicit scene Play still starts the dormant effect");
        return true;
    }

    bool TestEditorLayoutWithoutGPU() {
        using namespace Axle;
        auto* previous = ImGui::GetCurrentContext();
        auto* context = ImGui::CreateContext();
        struct Cleanup { ImGuiContext* Context; ImGuiContext* Previous;
            ~Cleanup() { ImGui::DestroyContext(Context); ImGui::SetCurrentContext(Previous); }
        } cleanup{context, previous};
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.DeltaTime = 1.0f / 60;
        io.DisplaySize = ImVec2(1600, 1000);
        unsigned char* pixels; int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.Fonts->SetTexID((ImTextureID)1);
        VFXEditorPanel panel;
        panel.SetOpen(true);
        for (int frame = 0; frame < 8; ++frame) {
            if (frame == 1) {
                panel.AddEmitterStack("Authored");
                ParticleEmitterConfig config;
                config.CurlNoise.Enabled = config.Collision.Enabled = true;
                panel.SetEmitterConfig(config);
            }
            // Open all module bodies to catch ImGui ID/disabled/group scope mistakes.
            if (frame >= 2) for (auto* window : context->Windows) {
                if (!std::strstr(window->Name, "VFX_Shuriken_Inspector")) continue;
                const auto id = ImHashStr(panel.GetCurrentEmitterStack()->Id.c_str(), 0, window->ID);
                for (int i = int(ParticleModule::Main); i <= int(ParticleModule::Renderer); ++i) {
                    const auto moduleId = ImHashData(&i, sizeof(i), id);
                    window->StateStorage.SetInt(ImHashStr(GetParticleModuleName(static_cast<ParticleModule>(i)), 0, moduleId), 1);
                }
            }
            if (frame == 6) {
                // Switch away from Effect; document dialogs must not depend on the visible pane.
                for (int i = 0; i < context->TabBars.GetBufSize(); ++i) {
                    auto* tabs = context->TabBars.GetByIndex(i);
                    if (tabs->Tabs.Size == 3) tabs->NextSelectedTabId = tabs->Tabs[2].ID;
                }
                panel.NewDocument("Replacement");
            }
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(frame < 5 ? 1280.0f : 480.0f, 780));
            bool open = frame != 7;
            panel.OnImGuiRender(&open);
            if (frame >= 6) {
                const auto* modal = ImGui::FindWindowByName("Unsaved Effect");
                VFX_TEST_CHECK(modal && modal->Active, "Unsaved dialog remains reachable with Effect tab hidden");
                VFX_TEST_CHECK(open && panel.IsOpen(), "Closing a dirty editor preserves its unsaved document");
            }
            ImGui::Render();
            VFX_TEST_CHECK(ImGui::GetDrawData()->Valid && ImGui::GetDrawData()->TotalVtxCount > 0, "Wide and narrow editor layouts produce UI draw data");
            VFX_TEST_CHECK(context->ErrorCountCurrentFrame == 0, "No ImGui scope or ID errors in the editor");
            for (auto* list : ImGui::GetDrawData()->CmdLists) for (const auto& vertex : list->VtxBuffer)
                VFX_TEST_CHECK(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y), "UI layout emits finite vertices");
            VFX_TEST_CHECK(panel.GetEmitterStackCount() == (frame == 0 ? 0 : 1), "Drawing the editor never inserts example effects");
        }
        return true;
    }
}

bool RunVFXAuthoringUnitTests() {
    return TestModulePersistenceAndHistory() && TestModuleSimulation() && TestDisabledEmissionDoesNotCatchUp()
        && TestAuthoringCommandsAndGraph() && TestSceneAuthoringAndBounds() && TestEditorLayoutWithoutGPU();
}
