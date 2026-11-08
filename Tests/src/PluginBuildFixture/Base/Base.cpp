#include <Axle/PluginSDK.hpp>
#include "Value.hpp"
#include <string>
namespace {
void* Create(void*, Axle::SDK::EntityRef, const char* text, std::uint32_t count) { return new FixtureBase{count ? std::stoi(std::string(text, count)) : 0, false}; }
void Destroy(void*, void* value) { delete static_cast<FixtureBase*>(value); }
bool Serialize(void*, const void* value, Axle::SDK::TextSink sink, void* user) {
    const auto text = std::to_string(static_cast<const FixtureBase*>(value)->Value); return sink(user, text.data(), static_cast<std::uint32_t>(text.size()));
}
void* Clone(void*, const void* value, Axle::SDK::EntityRef, const Axle::SDK::EntityRemap*, std::uint32_t) { return new FixtureBase{static_cast<const FixtureBase*>(value)->Value, false}; }
void Start(void*, void* value, Axle::SDK::EntityRef) { static_cast<FixtureBase*>(value)->Started = true; }
void Stop(void*, void* value, Axle::SDK::EntityRef) { static_cast<FixtureBase*>(value)->Started = false; }
void Update(void*, void* value, Axle::SDK::EntityRef, Axle::SDK::Phase phase, float) { if (phase == Axle::SDK::Phase::FixedControls) ++static_cast<FixtureBase*>(value)->Value; }
}
AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID, AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    const auto* services = static_cast<const Axle::SDK::RuntimeServices*>(host->get_service(host->user_data, Axle::SDK::RuntimeService, sizeof(Axle::SDK::RuntimeServices)));
    const Axle::SDK::ComponentType type{sizeof(type), "custom.plugin/ZBase", 1, true, false, nullptr, nullptr, Create, Destroy, Serialize, Clone, Start, Stop, Update};
    return services && services->RegisterComponent(services->User, host->owner_id, &type);
}
AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept {}
AXLE_PLUGIN_EXPORT const void* ax_fixture_base_runtime_table() noexcept { return AxleRuntimeHost_GetV1(); }
