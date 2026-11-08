#include <Axle/PluginSDK.hpp>
#include <AxleTerrainEditor/TerrainPluginEditor.hpp>
#include <AxleTerrainEditor/TerrainEditor.hpp>
#include <AxleTerrain/TerrainComponent.hpp>
#include <AxleTerrain/TerrainMesh.hpp>
#include <string>

namespace {

const Axle::SDK::EditorServices* s_Editor = nullptr;

void DrawTerrainInspector(void*, Axle::SDK::EntityRef, void* component) {
    if (!s_Editor || !component) return;

    auto* tc = static_cast<Axle::Terrain::TerrainComponent*>(component);
    if (!tc) return;

    s_Editor->Text(s_Editor->User, "=== Off-Road Terrain Component ===");
    s_Editor->Text(s_Editor->User, ("Resolution: " + std::to_string(tc->Data.GetResolutionX()) + " x " + std::to_string(tc->Data.GetResolutionZ())).c_str());
    s_Editor->Text(s_Editor->User, ("Dimensions: " + std::to_string(static_cast<int>(tc->Data.GetWidth())) + "m x " + std::to_string(static_cast<int>(tc->Data.GetLength())) + "m").c_str());
    s_Editor->Text(s_Editor->User, ("Spacing: " + std::to_string(tc->Data.GetSpacingX()) + "m").c_str());

    s_Editor->DragFloat(s_Editor->User, "Friction", &tc->Friction, 0.05f);
    s_Editor->DragFloat(s_Editor->User, "Restitution", &tc->Restitution, 0.05f);

    if (s_Editor->Button && s_Editor->Button(s_Editor->User, "Generate Rolling Hills")) {
        tc->Data.GenerateHills(0.025f, 10.0f, 3);
        tc->Data.AutoGenerateSplatmap(32.0f, 18.0f);
    }
    if (s_Editor->Button && s_Editor->Button(s_Editor->User, "Generate Flat Plane")) {
        tc->Data.GenerateFlat(0.0f);
        tc->Data.AutoGenerateSplatmap();
    }
}

} // namespace

#if defined(AXLE_PLUGIN_DESCRIPTOR) && defined(AXLE_PLUGIN_ID) && defined(AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID, AXLE_PLUGIN_MODULE_NAME)
#endif

AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    if (!host) return false;
    s_Editor = static_cast<const Axle::SDK::EditorServices*>(
        host->get_service(host->user_data, Axle::SDK::EditorService, sizeof(Axle::SDK::EditorServices)));
    if (!s_Editor || s_Editor != AxleEditorHost_GetV1()) {
        return false;
    }

    if (!s_Editor->RegisterInspector(s_Editor->User, host->owner_id, Axle::Terrain::kTerrainComponentTypeId, DrawTerrainInspector, nullptr)) {
        return false;
    }

    return true;
}

AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept {
    s_Editor = nullptr;
}
