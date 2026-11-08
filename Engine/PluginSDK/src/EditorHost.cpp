#include <Axle/PluginSDK.hpp>
#include <thread>
namespace {
Axle::SDK::EditorServices services{};
std::thread::id owner;
}
extern "C" const Axle::SDK::EditorServices* AxleEditorHost_GetV1() noexcept { return &services; }
extern "C" bool AxleEditorHost_BindV1(const Axle::SDK::EditorServices* candidate) noexcept {
    if (!candidate || candidate->StructSize != sizeof(services) || candidate->Version != Axle::SDK::Version ||
        !candidate->User || !candidate->Context || !candidate->RegisterInspector || !candidate->Text ||
        !candidate->DragFloat || !candidate->Button || services.User) return false;
    if (!AxleRuntimeHost_GetV1()->User) return false;
    services = *candidate; owner = std::this_thread::get_id(); return true;
}
extern "C" bool AxleEditorHost_UnbindV1(void* user) noexcept {
    if (!user || services.User != user || owner != std::this_thread::get_id()) return false;
    services = {}; owner = {}; return true;
}
