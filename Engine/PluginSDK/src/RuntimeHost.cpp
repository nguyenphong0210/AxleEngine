#include <Axle/PluginSDK.hpp>
#include <string>
#include <cstring>
#include <thread>

namespace {
Axle::SDK::RuntimeServices RevokedServices() {
    Axle::SDK::RuntimeServices api{};
    api.StructSize = sizeof(api); api.Version = Axle::SDK::Version; api.SdkFingerprint = AXLE_PLUGIN_HOST_SDK;
    api.Address = [](void*, std::uint64_t, Axle::SDK::ServiceAddress, std::uint64_t) -> const void* { return nullptr; };
    api.RegisterComponent = [](void*, std::uint64_t, const Axle::SDK::ComponentType*) { return false; };
    api.GetTransform = [](void*, std::uint64_t, Axle::SDK::EntityRef, Axle::SDK::Transform*) { return false; };
    api.SetTransform = [](void*, std::uint64_t, Axle::SDK::EntityRef, const Axle::SDK::Transform*) { return false; };
    api.GetLinearVelocity = [](void*, std::uint64_t, Axle::SDK::EntityRef, float*) { return false; };
    api.SetLinearVelocity = [](void*, std::uint64_t, Axle::SDK::EntityRef, const float*) { return false; };
    api.SetComponent = [](void*, std::uint64_t, Axle::SDK::EntityRef, const char*, const char*, std::uint32_t) { return false; };
    api.GetComponent = [](void*, std::uint64_t, Axle::SDK::EntityRef, const char*) -> void* { return nullptr; };
    api.RemoveComponent = [](void*, std::uint64_t, Axle::SDK::EntityRef, const char*) { return false; };
    api.SubmitJob = [](void*, std::uint64_t, std::uint64_t, void(*)(void*), void(*)(void*), void*, void(*)(void*)) { return false; };
    api.DebugLine = [](void*, std::uint64_t, std::uint64_t, const float*, const float*, const float*) { return false; };
    api.Log = [](void*, std::uint64_t, const char*) {};
    return api;
}
Axle::SDK::RuntimeServices services = RevokedServices();
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
        !candidate->RegisterComponent || !candidate->GetTransform || !candidate->SetTransform || !candidate->GetLinearVelocity || !candidate->SetLinearVelocity || !candidate->SetComponent ||
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
    services = RevokedServices(); sdk.clear(); owner = {};
    return true;
}
