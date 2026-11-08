#include <Axle/PluginSDK.hpp>
extern "C" __declspec(dllimport) float* axle_@MODULE@_speed(void*) noexcept;
namespace {
const Axle::SDK::EditorServices* editor = nullptr;
void Draw(void*, Axle::SDK::EntityRef, void* value) {
    editor->Text(editor->User, "@PLUGIN_NAME@ Mover");
    if (auto* speed = axle_@MODULE@_speed(value)) editor->DragFloat(editor->User, "Speed", speed, 0.1f);
}
}
AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID, AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    if (!host) return false;
    editor = static_cast<const Axle::SDK::EditorServices*>(host->get_service(host->user_data, Axle::SDK::EditorService, sizeof(Axle::SDK::EditorServices)));
    return editor && editor == AxleEditorHost_GetV1() && editor->RegisterInspector(editor->User, host->owner_id, "@PLUGIN_ID@/Mover", Draw, nullptr);
}
AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept { editor = nullptr; }
