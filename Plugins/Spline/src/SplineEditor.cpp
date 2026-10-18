#include <AxleSpline/SplineEditor.hpp>
#include <AxleSpline/TrackGenerator.hpp>
#include <Axle/Components.hpp>
#include <Axle/Log.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstdio>

namespace Axle::Spline {

    int SplineEditor::s_SelectedNodeIndex = 0;

    void SplineEditor::DrawInspector(Entity entity, PhysicsScene* physicsScene) {
        if (!entity.HasComponent<SplineComponent>()) return;

        auto& sc = entity.GetComponent<SplineComponent>();
        auto& curve = sc.Curve;

        ImGui::PushID("SplineComponentInspector");

        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen
            | ImGuiTreeNodeFlags_Framed
            | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_AllowOverlap
            | ImGuiTreeNodeFlags_FramePadding;

        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4.0f, 4.0f });
        float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
        ImGui::Separator();
        bool open = ImGui::TreeNodeEx("##SplineTrackHeader", treeNodeFlags, "🛣️ Spline Track Generator");
        ImGui::PopStyleVar();

        if (open) {
            bool needsRebuild = false;

            // --- SECTION 1: PRESETS & CURVE TYPE ---
            ImGui::Spacing();
            ImGui::TextDisabled("Curve Settings:");

            const char* typeNames[] = { "Catmull-Rom (Smooth Passing)", "Cubic Bézier (Handles)", "Linear (Polygon)" };
            int currentType = static_cast<int>(curve.GetSplineType());
            if (ImGui::Combo("Curve Algorithm", &currentType, typeNames, IM_ARRAYSIZE(typeNames))) {
                curve.SetSplineType(static_cast<SplineType>(currentType));
                needsRebuild = true;
            }

            bool loop = curve.IsLoop();
            if (ImGui::Checkbox("Closed Loop (Circuit Track)", &loop)) {
                curve.SetLoop(loop);
                needsRebuild = true;
            }

            // Quick Presets
            ImGui::SameLine();
            if (ImGui::Button("Track Presets...")) {
                ImGui::OpenPopup("TrackPresetsPopup");
            }
            if (ImGui::BeginPopup("TrackPresetsPopup")) {
                if (ImGui::MenuItem("Oval Speedway (4 Nodes)")) {
                    curve.ClearNodes();
                    curve.SetLoop(true);
                    curve.AddNode({ glm::vec3(-40.0f, 0.0f, -20.0f), 0.0f, 1.0f });
                    curve.AddNode({ glm::vec3(40.0f, 0.0f, -20.0f), 15.0f, 1.0f }); // 15 deg bank
                    curve.AddNode({ glm::vec3(40.0f, 0.0f, 20.0f), 15.0f, 1.0f });
                    curve.AddNode({ glm::vec3(-40.0f, 0.0f, 20.0f), 0.0f, 1.0f });
                    s_SelectedNodeIndex = 0;
                    needsRebuild = true;
                }
                if (ImGui::MenuItem("Figure-8 Overpass (8 Nodes)")) {
                    curve.ClearNodes();
                    curve.SetLoop(true);
                    curve.AddNode({ glm::vec3(0.0f, 4.0f, 0.0f), 0.0f, 1.0f });     // Bridge
                    curve.AddNode({ glm::vec3(30.0f, 2.0f, -20.0f), 20.0f, 1.0f });
                    curve.AddNode({ glm::vec3(50.0f, 0.0f, 0.0f), 15.0f, 1.0f });
                    curve.AddNode({ glm::vec3(30.0f, 0.0f, 20.0f), 10.0f, 1.0f });
                    curve.AddNode({ glm::vec3(0.0f, 0.0f, 0.0f), 0.0f, 1.0f });     // Tunnel underpass
                    curve.AddNode({ glm::vec3(-30.0f, 0.0f, -20.0f), -10.0f, 1.0f });
                    curve.AddNode({ glm::vec3(-50.0f, 0.0f, 0.0f), -15.0f, 1.0f });
                    curve.AddNode({ glm::vec3(-30.0f, 2.0f, 20.0f), -20.0f, 1.0f });
                    s_SelectedNodeIndex = 0;
                    needsRebuild = true;
                }
                if (ImGui::MenuItem("Clear All Nodes")) {
                    curve.ClearNodes();
                    s_SelectedNodeIndex = -1;
                    needsRebuild = true;
                }
                ImGui::EndPopup();
            }

            // Telemetry Card
            ImGui::Spacing();
            ImGui::BeginChild("SplineTelemetry", ImVec2(0, 50), true);
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Track Length: %.1f m", curve.GetTotalLength());
            ImGui::SameLine();
            ImGui::Text("| Nodes: %zu | Vertices: %u | Triangles: %u",
                curve.GetNodeCount(), sc.VertexCount, sc.TriangleCount);
            ImGui::EndChild();

            // --- SECTION 2: TRACK EXTRUSION PROFILE ---
            ImGui::Spacing();
            ImGui::TextDisabled("Extrusion Profile:");
            if (ImGui::DragFloat("Track Width (m)", &sc.TrackWidth, 0.1f, 1.0f, 40.0f, "%.1f")) needsRebuild = true;
            if (ImGui::DragFloat("Step Distance (m)", &sc.StepDistance, 0.05f, 0.2f, 5.0f, "%.2f")) needsRebuild = true;
            if (ImGui::DragFloat("Texture Repeat V", &sc.TextureRepeatV, 0.01f, 0.01f, 2.0f, "%.2f")) needsRebuild = true;

            // Curbs
            if (ImGui::Checkbox("Generate Curbs / Rumble Strips", &sc.GenerateCurbs)) needsRebuild = true;
            if (sc.GenerateCurbs) {
                ImGui::Indent();
                if (ImGui::DragFloat("Curb Width", &sc.CurbWidth, 0.05f, 0.1f, 2.0f, "%.2f m")) needsRebuild = true;
                if (ImGui::DragFloat("Curb Height", &sc.CurbHeight, 0.01f, 0.01f, 0.5f, "%.2f m")) needsRebuild = true;
                ImGui::Unindent();
            }

            // Guardrails
            if (ImGui::Checkbox("Generate Guardrails / Barriers", &sc.GenerateGuardrails)) needsRebuild = true;
            if (sc.GenerateGuardrails) {
                ImGui::Indent();
                if (ImGui::DragFloat("Barrier Height", &sc.GuardrailHeight, 0.05f, 0.2f, 3.0f, "%.2f m")) needsRebuild = true;
                if (ImGui::DragFloat("Barrier Thickness", &sc.GuardrailThickness, 0.01f, 0.05f, 0.5f, "%.2f m")) needsRebuild = true;
                ImGui::Unindent();
            }

            // Physics
            ImGui::Spacing();
            ImGui::TextDisabled("Physics Simulation:");
            if (ImGui::Checkbox("Generate Jolt MeshShape Collider", &sc.GeneratePhysics)) needsRebuild = true;
            if (sc.GeneratePhysics) {
                ImGui::Indent();
                if (ImGui::SliderFloat("Tire Grip Friction", &sc.Friction, 0.1f, 2.0f, "%.2f")) needsRebuild = true;
                if (ImGui::SliderFloat("Restitution (Bounciness)", &sc.Restitution, 0.0f, 1.0f, "%.2f")) needsRebuild = true;
                ImGui::Unindent();
            }

            // --- SECTION 3: CONTROL POINTS (NODES) LIST ---
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Control Points (%zu nodes):", curve.GetNodeCount());

            auto& nodes = curve.GetNodes();
            int nodeToDelete = -1;

            for (size_t i = 0; i < nodes.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));

                bool isSelected = (static_cast<int>(i) == s_SelectedNodeIndex);
                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.45f, 0.85f, 0.8f));
                }

                char headerLabel[64];
                snprintf(headerLabel, sizeof(headerLabel), "Node #%zu: (%.1f, %.1f, %.1f) - Bank: %.1f deg",
                    i, nodes[i].Position.x, nodes[i].Position.y, nodes[i].Position.z, nodes[i].Roll);

                bool nodeOpen = ImGui::TreeNodeEx("NodeItem", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", headerLabel);

                if (ImGui::IsItemClicked()) {
                    s_SelectedNodeIndex = static_cast<int>(i);
                }

                if (isSelected) {
                    ImGui::PopStyleColor();
                }

                if (nodeOpen) {
                    if (ImGui::DragFloat3("Position", glm::value_ptr(nodes[i].Position), 0.1f)) needsRebuild = true;
                    if (ImGui::SliderFloat("Banking (Roll)", &nodes[i].Roll, -85.0f, 85.0f, "%.1f deg")) needsRebuild = true;
                    if (ImGui::DragFloat("Scale", &nodes[i].Scale, 0.05f, 0.2f, 4.0f, "%.2f")) needsRebuild = true;

                    if (curve.GetSplineType() == SplineType::CubicBezier) {
                        if (ImGui::DragFloat3("Handle In", glm::value_ptr(nodes[i].HandleIn), 0.1f)) needsRebuild = true;
                        if (ImGui::DragFloat3("Handle Out", glm::value_ptr(nodes[i].HandleOut), 0.1f)) needsRebuild = true;
                    }

                    if (ImGui::Button("Delete This Node")) {
                        nodeToDelete = static_cast<int>(i);
                    }
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            if (nodeToDelete >= 0) {
                curve.RemoveNode(nodeToDelete);
                if (s_SelectedNodeIndex >= static_cast<int>(curve.GetNodeCount())) {
                    s_SelectedNodeIndex = static_cast<int>(curve.GetNodeCount()) - 1;
                }
                needsRebuild = true;
            }

            // Add Node Button
            ImGui::Spacing();
            if (ImGui::Button("+ Append Node at End", ImVec2(-1, 26))) {
                glm::vec3 newPos(0.0f);
                if (nodes.empty()) {
                    newPos = glm::vec3(0.0f, 0.0f, 0.0f);
                } else if (nodes.size() == 1) {
                    newPos = nodes[0].Position + glm::vec3(0.0f, 0.0f, 15.0f);
                } else {
                    glm::vec3 lastDir = glm::normalize(nodes.back().Position - nodes[nodes.size() - 2].Position);
                    newPos = nodes.back().Position + lastDir * 20.0f;
                }
                curve.AddNode({ newPos, 0.0f, 1.0f });
                s_SelectedNodeIndex = static_cast<int>(nodes.size() - 1);
                needsRebuild = true;
            }

            // Bake Mesh Button
            ImGui::Spacing();
            if (ImGui::Button("⚡ Rebuild Track Mesh & Physics", ImVec2(-1, 30)) || needsRebuild) {
                curve.RebuildCache();
                sc.Dirty = true;

                auto mesh = TrackGenerator::GenerateTrackMesh(curve, sc);
                sc.TrackVAO = TrackGenerator::BuildVAO(mesh);
                sc.VertexCount = static_cast<u32>(mesh.GetVertexCount());
                sc.TriangleCount = static_cast<u32>(mesh.GetTriangleCount());

                if (entity.HasComponent<MeshInstance3D>()) {
                    entity.GetComponent<MeshInstance3D>().VAO = sc.TrackVAO;
                } else if (sc.TrackVAO) {
                    entity.AddComponent<MeshInstance3D>(sc.TrackVAO, 1, 4.0f,
                        glm::vec4(0.2f, 0.2f, 0.2f, 1.0f), glm::vec4(0.3f, 0.3f, 0.3f, 1.0f));
                }

                if (physicsScene && sc.GeneratePhysics) {
                    TrackGenerator::DestroyPhysicsCollider(sc.RuntimeBodyID, *physicsScene);
                    glm::mat4 wt = entity.HasComponent<TransformComponent>()
                        ? entity.GetComponent<TransformComponent>().GetLocalTransform()
                        : glm::mat4(1.0f);
                    sc.RuntimeBodyID = TrackGenerator::CreatePhysicsCollider(mesh, sc, *physicsScene, wt);
                }
            }

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    void SplineEditor::DrawViewportGizmo(
        Entity entity,
        const glm::mat4& viewMatrix,
        const glm::mat4& projectionMatrix,
        const glm::vec2& viewportMin,
        const glm::vec2& viewportMax,
        int gizmoOperation,
        PhysicsScene* physicsScene
    ) {
        if (!entity.HasComponent<SplineComponent>()) return;

        auto& sc = entity.GetComponent<SplineComponent>();
        auto& curve = sc.Curve;

        float totalLength = curve.GetTotalLength();
        if (totalLength < 0.1f) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (!drawList) return;

        glm::mat4 entityWorld = entity.HasComponent<TransformComponent>()
            ? entity.GetComponent<TransformComponent>().GetLocalTransform()
            : glm::mat4(1.0f);

        glm::mat4 viewProj = projectionMatrix * viewMatrix;

        auto ProjectToScreen = [&](const glm::vec3& worldPos, ImVec2& outPos) -> bool {
            glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.001f) return false;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
            float screenX = viewportMin.x + (ndc.x * 0.5f + 0.5f) * (viewportMax.x - viewportMin.x);
            float screenY = viewportMin.y + (-ndc.y * 0.5f + 0.5f) * (viewportMax.y - viewportMin.y);
            outPos = ImVec2(screenX, screenY);
            return true;
        };

        // 1. Draw 3D Spline center path and lateral borders
        u32 sampleCount = std::clamp(static_cast<u32>(totalLength * 0.5f), 30u, 300u);
        ImVec2 prevCenter, prevLeft, prevRight;
        bool hasPrev = false;

        float halfWidth = sc.TrackWidth * 0.5f;

        for (u32 i = 0; i <= sampleCount; ++i) {
            float dist = (static_cast<float>(i) / static_cast<float>(sampleCount)) * totalLength;
            SplineFrame frame = curve.SampleFrame(dist);

            glm::vec3 centerWorld = glm::vec3(entityWorld * glm::vec4(frame.Position, 1.0f));
            glm::vec3 leftWorld = glm::vec3(entityWorld * glm::vec4(frame.Position - (frame.Scale * halfWidth) * frame.Binormal, 1.0f));
            glm::vec3 rightWorld = glm::vec3(entityWorld * glm::vec4(frame.Position + (frame.Scale * halfWidth) * frame.Binormal, 1.0f));

            ImVec2 curCenter, curLeft, curRight;
            bool okCenter = ProjectToScreen(centerWorld, curCenter);
            bool okLeft = ProjectToScreen(leftWorld, curLeft);
            bool okRight = ProjectToScreen(rightWorld, curRight);

            if (hasPrev && okCenter) {
                // Centerline in bright cyan
                drawList->AddLine(prevCenter, curCenter, IM_COL32(0, 230, 255, 230), 2.5f);
                if (okLeft) {
                    drawList->AddLine(prevLeft, curLeft, IM_COL32(255, 215, 0, 160), 1.5f);
                }
                if (okRight) {
                    drawList->AddLine(prevRight, curRight, IM_COL32(255, 215, 0, 160), 1.5f);
                }
            }

            prevCenter = curCenter;
            prevLeft = curLeft;
            prevRight = curRight;
            hasPrev = okCenter;
        }

        // 2. Draw Control Point nodes
        auto& nodes = curve.GetNodes();
        for (size_t i = 0; i < nodes.size(); ++i) {
            glm::vec3 worldPos = glm::vec3(entityWorld * glm::vec4(nodes[i].Position, 1.0f));
            ImVec2 screenPos;
            if (ProjectToScreen(worldPos, screenPos)) {
                bool isSelected = (static_cast<int>(i) == s_SelectedNodeIndex);
                ImU32 color = isSelected ? IM_COL32(255, 60, 60, 255) : IM_COL32(255, 230, 40, 255);
                float radius = isSelected ? 8.0f : 5.5f;

                drawList->AddCircleFilled(screenPos, radius, color);
                drawList->AddCircle(screenPos, radius + 1.0f, IM_COL32(0, 0, 0, 255), 0, 1.5f);

                char nodeText[16];
                snprintf(nodeText, sizeof(nodeText), "%zu", i);
                drawList->AddText(ImVec2(screenPos.x + radius + 4.0f, screenPos.y - 7.0f),
                    IM_COL32(255, 255, 255, 255), nodeText);
            }
        }

        // 3. ImGuizmo manipulation on Selected Node
        if (s_SelectedNodeIndex >= 0 && s_SelectedNodeIndex < static_cast<int>(nodes.size())) {
            auto& node = nodes[s_SelectedNodeIndex];
            glm::vec3 worldNodePos = glm::vec3(entityWorld * glm::vec4(node.Position, 1.0f));

            glm::mat4 nodeTransform = glm::translate(glm::mat4(1.0f), worldNodePos);

            ImGuizmo::SetDrawlist(drawList);
            ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y);

            if (ImGuizmo::Manipulate(
                glm::value_ptr(viewMatrix),
                glm::value_ptr(projectionMatrix),
                static_cast<ImGuizmo::OPERATION>(gizmoOperation),
                ImGuizmo::MODE::WORLD,
                glm::value_ptr(nodeTransform)
            )) {
                glm::mat4 invEntity = glm::inverse(entityWorld);
                node.Position = glm::vec3(invEntity * nodeTransform[3]);
                curve.RebuildCache();
                sc.Dirty = true;

                // Real-time track mesh & physics regeneration
                auto mesh = TrackGenerator::GenerateTrackMesh(curve, sc);
                sc.TrackVAO = TrackGenerator::BuildVAO(mesh);
                sc.VertexCount = static_cast<u32>(mesh.GetVertexCount());
                sc.TriangleCount = static_cast<u32>(mesh.GetTriangleCount());

                if (entity.HasComponent<MeshInstance3D>()) {
                    entity.GetComponent<MeshInstance3D>().VAO = sc.TrackVAO;
                }

                if (physicsScene && sc.GeneratePhysics && !sc.RuntimeBodyID.IsInvalid()) {
                    TrackGenerator::DestroyPhysicsCollider(sc.RuntimeBodyID, *physicsScene);
                    sc.RuntimeBodyID = TrackGenerator::CreatePhysicsCollider(mesh, sc, *physicsScene, entityWorld);
                }
            }
        }
    }

}
