#include <Axle/PluginSDK.hpp>
#include <cstdint>
#if defined(_WIN32)
#define MODULE_IMPORT extern "C" __declspec(dllimport)
#else
#define MODULE_IMPORT extern "C"
#endif
MODULE_IMPORT float* axle_spline_width(void*) noexcept;
MODULE_IMPORT float* axle_spline_step(void*) noexcept;
namespace {
const Axle::SDK::EditorServices* editor=nullptr;
void Draw(void*,Axle::SDK::EntityRef,void* value) {
    if(!editor||!value) return;
    editor->Text(editor->User,"Spline authored data and line preview");
    editor->DragFloat(editor->User,"Track width",axle_spline_width(value),0.1f);
    editor->DragFloat(editor->User,"Step distance",axle_spline_step(value),0.05f);
}
}
#if defined(AXLE_PLUGIN_DESCRIPTOR) && defined(AXLE_PLUGIN_ID) && defined(AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID,AXLE_PLUGIN_MODULE_NAME)
#endif
AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    try {
        if(!host||host->struct_size!=sizeof(*host)||host->abi_version!=Axle::PluginRuntime::kPluginAbiVersion||!host->get_service) return false;
        editor=static_cast<const Axle::SDK::EditorServices*>(host->get_service(host->user_data,Axle::SDK::EditorService,sizeof(Axle::SDK::EditorServices)));
        return editor&&editor==AxleEditorHost_GetV1()&&editor->RegisterInspector(editor->User,host->owner_id,"axle.spline/Spline",Draw,nullptr);
    } catch(...) { return false; }
}
AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept { editor=nullptr; }
