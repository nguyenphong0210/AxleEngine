#include <AxleTerrain/TerrainEditor.hpp>
#include <AxleTerrain/TerrainMesh.hpp>
#include <AxleTerrain/TerrainCollider.hpp>
#include <AxleTerrain/TerrainSplineCarver.hpp>
#include <AxleSpline/SplineComponent.hpp>
#include <AxleSpline/TrackGenerator.hpp>
#include <Axle/Components.hpp>
#include <Axle/Log.hpp>

#include <imgui.h>
#include <algorithm>
#include <filesystem>

namespace Axle::Terrain {

    void TerrainEditor::RebuildTerrain(TerrainComponent& tc, PhysicsScene* physicsScene, Entity entity) {
        if (tc.UseChunkedLOD) {
            TerrainMesh::InitializeChunks(tc.Data, tc.Chunks, tc.ChunkSize);
            TerrainMesh::BuildAllChunksParallel(tc.Data, tc.Chunks);
            TerrainMesh::UploadAllChunksGPU(tc.Chunks);
        } else {
            tc.CombinedVAO = TerrainMesh::BuildCombinedVAO(tc.Data);
            if (entity && entity.HasComponent<MeshInstance3D>()) {
                entity.GetComponent<MeshInstance3D>().VAO = tc.CombinedVAO;
            }
        }

        // Rebuild Jolt Physics HeightField
        if (physicsScene && tc.GeneratePhysics) {
            tc.PhysicsBodyID = TerrainCollider::UpdateTerrainCollider(
                tc.PhysicsBodyID,
                tc.Data,
                *physicsScene,
                tc.Friction,
                tc.Restitution
            );
        }
    }

    void TerrainEditor::DrawInspector(Entity entity, PhysicsScene* physicsScene, Scene* scene) {
        if (!entity.HasComponent<TerrainComponent>()) return;

        auto& tc = entity.GetComponent<TerrainComponent>();
        auto& data = tc.Data;

        ImGui::PushID("TerrainComponentInspector");

        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen
            | ImGuiTreeNodeFlags_Framed
            | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_AllowOverlap
            | ImGuiTreeNodeFlags_FramePadding;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4.0f, 4.0f });
        ImGui::Separator();
        bool open = ImGui::TreeNodeEx("##TerrainHeader", treeNodeFlags, "🏔️ Off-Road Terrain & Sculptor");
        ImGui::PopStyleVar();

        if (open) {
            bool needsRebuild = false;

            // --- SECTION 1: GRID DIMENSIONS & METRICS ---
            ImGui::Spacing();
            ImGui::TextDisabled("Grid Dimensions:");
            ImGui::Text("Resolution: %u x %u (%u vertices)", data.GetResolutionX(), data.GetResolutionZ(), data.GetResolutionX() * data.GetResolutionZ());
            ImGui::Text("Size: %.1f m x %.1f m (Spacing: %.2f m)", data.GetWidth(), data.GetLength(), data.GetSpacingX());

            // --- SECTION 2: PROCEDURAL GENERATION PRESETS ---
            ImGui::Spacing();
            ImGui::TextDisabled("Procedural Terrain Presets:");
            if (ImGui::Button("🏔️ Rolling Hills")) {
                data.GenerateHills(0.025f, 10.0f, 3);
                data.AutoGenerateSplatmap(32.0f, 18.0f);
                needsRebuild = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("🟩 Flat Plane")) {
                data.GenerateFlat(0.0f);
                data.AutoGenerateSplatmap();
                needsRebuild = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("📐 Slope Ramp")) {
                data.GenerateRamp(0.0f, 25.0f, false);
                data.AutoGenerateSplatmap();
                needsRebuild = true;
            }

            if (ImGui::Button("🎨 Re-Compute Auto-Splatmap (Slope-Based)")) {
                data.AutoGenerateSplatmap(32.0f, 18.0f);
                needsRebuild = true;
            }

            // --- SECTION 3: INTERACTIVE SCULPTING BRUSH ---
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Interactive Sculpting Tools:");

            ImGui::Checkbox("✏️ Enable Viewport Sculpting", &tc.SculptingActive);

            if (tc.SculptingActive) {
                const char* modes[] = { "Raise (+)", "Lower (-)", "Smooth (~)", "Flatten (=)", "Paint Texture (🎨)" };
                int curMode = static_cast<int>(tc.Sculpt.Mode);
                if (ImGui::Combo("Brush Mode", &curMode, modes, IM_ARRAYSIZE(modes))) {
                    tc.Sculpt.Mode = static_cast<BrushMode>(curMode);
                }

                ImGui::SliderFloat("Brush Radius (m)", &tc.Sculpt.Radius, 1.0f, 60.0f, "%.1f m");
                ImGui::SliderFloat("Brush Strength", &tc.Sculpt.Strength, 0.5f, 30.0f, "%.1f");

                if (tc.Sculpt.Mode == BrushMode::Flatten) {
                    ImGui::SliderFloat("Target Height", &tc.Sculpt.TargetHeight, -30.0f, 100.0f, "%.1f m");
                }

                if (tc.Sculpt.Mode == BrushMode::PaintTexture) {
                    const char* layers[] = { "Layer 0: Grass (Green)", "Layer 1: Dirt (Brown)", "Layer 2: Gravel (Grey)", "Layer 3: Rock (Dark)" };
                    int curLayer = static_cast<int>(tc.Sculpt.PaintLayer);
                    if (ImGui::Combo("Paint Layer", &curLayer, layers, IM_ARRAYSIZE(layers))) {
                        tc.Sculpt.PaintLayer = static_cast<u32>(curLayer);
                    }
                }

                const char* falloffs[] = { "Linear", "Smoothstep (Soft)", "Spherical (Round)" };
                int curFalloff = static_cast<int>(tc.Sculpt.Falloff);
                if (ImGui::Combo("Brush Falloff", &curFalloff, falloffs, IM_ARRAYSIZE(falloffs))) {
                    tc.Sculpt.Falloff = static_cast<BrushFalloff>(curFalloff);
                }
            }

            // --- SECTION 4: SPLINE ROAD CARVING & SNAPPING ---
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Spline Track Integration:");

            if (scene) {
                auto splineView = scene->GetRegistry().view<Axle::Spline::SplineComponent>();
                if (splineView.begin() != splineView.end()) {
                    if (ImGui::Button("🛣️ Carve Road Under Spline Tracks")) {
                        for (auto splineEntity : splineView) {
                            auto& sc = splineView.get<Axle::Spline::SplineComponent>(splineEntity);
                            TerrainSplineCarver::CarveSplineRoad(data, sc.Curve);
                        }
                        needsRebuild = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("📍 Snap Spline Tracks to Ground")) {
                        for (auto splineEntity : splineView) {
                            auto& sc = splineView.get<Axle::Spline::SplineComponent>(splineEntity);
                            TerrainSplineCarver::SnapSplineToTerrain(sc, data);
                            auto mesh = Axle::Spline::TrackGenerator::GenerateTrackMesh(sc.Curve, sc);
                            sc.TrackVAO = Axle::Spline::TrackGenerator::BuildVAO(mesh);
                            if (scene->GetRegistry().any_of<MeshInstance3D>(splineEntity)) {
                                scene->GetRegistry().get<MeshInstance3D>(splineEntity).VAO = sc.TrackVAO;
                            }
                        }
                    }
                } else {
                    ImGui::TextDisabled("(No SplineComponent entities found in scene)");
                }
            }

            // --- SECTION 5: PHYSICS & SERIALIZATION ---
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Physics & Storage:");

            if (ImGui::Checkbox("Generate Jolt Physics Collider", &tc.GeneratePhysics)) {
                needsRebuild = true;
            }
            if (ImGui::SliderFloat("Friction", &tc.Friction, 0.1f, 1.5f, "%.2f")) {
                needsRebuild = true;
            }
            if (ImGui::SliderFloat("Restitution", &tc.Restitution, 0.0f, 1.0f, "%.2f")) {
                needsRebuild = true;
            }

            if (ImGui::Button("🔄 Rebuild All (Mesh + Physics)")) {
                needsRebuild = true;
            }

            static char terrainFilePath[128] = "assets/terrain/SandboxTerrain.axleterrain";
            ImGui::InputText("File Path", terrainFilePath, IM_ARRAYSIZE(terrainFilePath));
            if (ImGui::Button("💾 Save Terrain")) {
                std::filesystem::create_directories(std::filesystem::path(terrainFilePath).parent_path());
                if (data.SaveToFile(terrainFilePath)) {
                    AXLE_INFO("Terrain successfully saved to: {0}", terrainFilePath);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("📂 Load Terrain")) {
                if (data.LoadFromFile(terrainFilePath)) {
                    AXLE_INFO("Terrain successfully loaded from: {0}", terrainFilePath);
                    needsRebuild = true;
                }
            }

            if (needsRebuild) {
                RebuildTerrain(tc, physicsScene, entity);
            }

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

}
