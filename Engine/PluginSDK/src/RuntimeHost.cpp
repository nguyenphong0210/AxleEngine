#include <Axle/PluginSDK.hpp>
#include <string>
#include <cstring>
#include <thread>

namespace {
Axle::SDK::RuntimeServices services{};
std::string sdk;
std::thread::id owner;
}
extern "C" const Axle::SDK::RuntimeServices* AxleRuntimeHost_GetV1() noexcept {
    return &services;
}
extern "C" const char* AxleRuntimeHost_SdkFingerprint() noexcept { return AXLE_PLUGIN_HOST_SDK; }
extern "C" const char* AxleRuntimeHost_ToolchainFingerprint() noexcept { return AXLE_PLUGIN_HOST_TOOLCHAIN; }
extern "C" bool AxleRuntimeHost_BindV1(const Axle::SDK::RuntimeServices* candidate) noexcept {
    if (!candidate || candidate->StructSize != sizeof(services) || candidate->Version != Axle::SDK::Version ||
        !candidate->SdkFingerprint || std::strcmp(candidate->SdkFingerprint, AXLE_PLUGIN_HOST_SDK) != 0 || !candidate->User || !candidate->Address ||
        !candidate->RegisterComponent || !candidate->GetTransform || !candidate->SetTransform || !candidate->SetComponent ||
        !candidate->GetComponent || !candidate->RemoveComponent || !candidate->SubmitJob || !candidate->DebugLine || !candidate->Log || services.User) return false;
    try {
        sdk = candidate->SdkFingerprint;
        services = *candidate;
        services.SdkFingerprint = sdk.c_str();
        owner = std::this_thread::get_id();
        return true;
    } catch (...) { return false; }
}
extern "C" bool AxleRuntimeHost_UnbindV1(void* user) noexcept {
    if (!user || services.User != user || owner != std::this_thread::get_id()) return false;
    services = {}; sdk.clear(); owner = {};
    return true;
}
