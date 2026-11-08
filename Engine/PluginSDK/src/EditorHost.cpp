#include <Axle/PluginSDK.hpp>
#include <thread>
namespace {
Axle::SDK::EditorServices RevokedServices() {
    Axle::SDK::EditorServices api{}; api.StructSize = sizeof(api); api.Version = Axle::SDK::Version;
    api.Context = [](void*) -> const void* { return nullptr; };
    api.RegisterInspector = [](void*, std::uint64_t, const char*, Axle::SDK::InspectorDraw, void*) { return false; };
    api.Text = [](void*, const char*) {};
    api.DragFloat = [](void*, const char*, float*, float) { return false; };
    api.Button = [](void*, const char*) { return false; };
    return api;
}
Axle::SDK::EditorServices services = RevokedServices();
std::thread::id owner;
}
extern "C" const Axle::SDK::EditorServices* AxleEditorHost_GetV1() noexcept { return &services; }
extern "C" const char* AxleEditorHost_SdkFingerprint() noexcept { return AXLE_PLUGIN_HOST_SDK; }
extern "C" bool AxleEditorHost_BindV1(const Axle::SDK::EditorServices* candidate) noexcept {
    if (!candidate || candidate->StructSize != sizeof(services) || candidate->Version != Axle::SDK::Version ||
        !candidate->User || !candidate->Context || !candidate->RegisterInspector || !candidate->Text ||
        !candidate->DragFloat || !candidate->Button || services.User) return false;
    if (!AxleRuntimeHost_GetV1()->User) return false;
    services = *candidate; owner = std::this_thread::get_id(); return true;
}
extern "C" bool AxleEditorHost_UnbindV1(void* user) noexcept {
    if (!user || services.User != user || owner != std::this_thread::get_id()) return false;
    services = RevokedServices(); owner = {}; return true;
}
