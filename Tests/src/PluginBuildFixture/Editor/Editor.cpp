#include <Axle/PluginSDK.hpp>
extern "C" __declspec(dllimport) float* ax_fixture_speed(void*) noexcept;
namespace {
const Axle::SDK::EditorServices* editor = nullptr;
int draws = 0;
void Draw(void*, Axle::SDK::EntityRef, void* component) {
    ++draws;
    editor->Text(editor->User, "Custom plugin mover");
    editor->DragFloat(editor->User, "Speed", ax_fixture_speed(component), 0.1f);
}
}
AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID, AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    if (!host) return false;
    editor = static_cast<const Axle::SDK::EditorServices*>(host->get_service(host->user_data, Axle::SDK::EditorService, sizeof(Axle::SDK::EditorServices)));
    return editor && editor == AxleEditorHost_GetV1() && editor->Context(editor->User) &&
        editor->RegisterInspector(editor->User, host->owner_id, "custom.plugin/Mover", Draw, nullptr);
}
AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept { editor = nullptr; }
AXLE_PLUGIN_EXPORT const void* ax_fixture_editor_context() noexcept { return editor ? editor->Context(editor->User) : nullptr; }
AXLE_PLUGIN_EXPORT const void* ax_fixture_editor_runtime_table() noexcept { return AxleRuntimeHost_GetV1(); }
AXLE_PLUGIN_EXPORT int ax_fixture_editor_draws() noexcept { return draws; }
