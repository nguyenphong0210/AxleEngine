#include <Axle/PluginSDK.hpp>
#include <AxleSpline/SplinePluginRuntime.hpp>
#include <AxleSpline/SplineComponent.hpp>
#include <AxleSpline/TrackGenerator.hpp>
#include <AxleSplineEditor/SplinePluginEditor.hpp>

#include <string>
#include <cmath>

namespace {

const Axle::SDK::EditorServices* s_Editor = nullptr;

void DrawSplineInspector(void*, Axle::SDK::EntityRef, void* value) {
    if (!s_Editor || !value) return;

    auto* comp = static_cast<Axle::Spline::SplineComponent*>(value);

    s_Editor->Text(s_Editor->User, "=== Spline Track Generator ===");
    s_Editor->DragFloat(s_Editor->User, "Track Width (m)", &comp->TrackWidth, 0.1f);
    s_Editor->DragFloat(s_Editor->User, "Step Distance (m)", &comp->StepDistance, 0.05f);
    s_Editor->DragFloat(s_Editor->User, "Texture Repeat V", &comp->TextureRepeatV, 0.01f);
    s_Editor->DragFloat(s_Editor->User, "Curb Width (m)", &comp->CurbWidth, 0.05f);
    s_Editor->DragFloat(s_Editor->User, "Curb Height (m)", &comp->CurbHeight, 0.02f);
    s_Editor->DragFloat(s_Editor->User, "Guardrail Height (m)", &comp->GuardrailHeight, 0.05f);
    s_Editor->DragFloat(s_Editor->User, "Guardrail Thickness (m)", &comp->GuardrailThickness, 0.02f);
    s_Editor->DragFloat(s_Editor->User, "Tire Friction", &comp->Friction, 0.05f);
    s_Editor->DragFloat(s_Editor->User, "Restitution", &comp->Restitution, 0.02f);

    // Diagnostics
    s_Editor->Text(s_Editor->User, "--- Diagnostics ---");
    if (comp->TrackWidth <= 0.0f || !std::isfinite(comp->TrackWidth)) {
        s_Editor->Text(s_Editor->User, "[ERROR] Track width must be strictly positive!");
    } else if (comp->StepDistance <= 0.0f || !std::isfinite(comp->StepDistance)) {
        s_Editor->Text(s_Editor->User, "[ERROR] Step distance must be strictly positive!");
    } else if (comp->Friction < 0.0f || !std::isfinite(comp->Friction)) {
        s_Editor->Text(s_Editor->User, "[ERROR] Friction cannot be negative!");
    } else {
        s_Editor->Text(s_Editor->User, "[OK] Spline configuration is valid.");
    }

    // Telemetry / Mesh Statistics
    s_Editor->Text(s_Editor->User, "--- Track Statistics ---");
    s_Editor->Text(s_Editor->User, ("Control Nodes: " + std::to_string(comp->Curve.GetNodeCount())).c_str());
    s_Editor->Text(s_Editor->User, ("Curve Length: " + std::to_string(static_cast<int>(comp->Curve.GetTotalLength())) + " m").c_str());
    s_Editor->Text(s_Editor->User, ("Vertex Count: " + std::to_string(comp->VertexCount)).c_str());
    s_Editor->Text(s_Editor->User, ("Triangle Count: " + std::to_string(comp->TriangleCount)).c_str());

    if (s_Editor->Button && s_Editor->Button(s_Editor->User, "Rebuild Track Geometry")) {
        comp->Curve.RebuildCache();
        comp->Dirty = true;
        auto mesh = Axle::Spline::TrackGenerator::GenerateTrackMesh(comp->Curve, *comp);
        comp->VertexCount = static_cast<Axle::u32>(mesh.GetVertexCount());
        comp->TriangleCount = static_cast<Axle::u32>(mesh.GetTriangleCount());
    }
}

} // namespace

#if defined(AXLE_PLUGIN_DESCRIPTOR) && defined(AXLE_PLUGIN_ID) && defined(AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_DESCRIPTOR(AxleSpline::kSplinePluginId, "SplineEditor")
#endif

AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    if (!host || host->struct_size != sizeof(*host) || host->abi_version != Axle::PluginRuntime::kPluginAbiVersion) {
        return false;
    }
    s_Editor = static_cast<const Axle::SDK::EditorServices*>(
        host->get_service(host->user_data, Axle::SDK::EditorService, sizeof(Axle::SDK::EditorServices)));
    if (!s_Editor || s_Editor != AxleEditorHost_GetV1()) {
        return false;
    }

    if (!s_Editor->RegisterInspector(s_Editor->User, host->owner_id, AxleSpline::kSplineTypeId, DrawSplineInspector, nullptr)) {
        return false;
    }
    return true;
}

AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept {
    s_Editor = nullptr;
}
