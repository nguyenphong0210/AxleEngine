#include <Axle/PluginSDK.hpp>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
namespace {
const Axle::SDK::RuntimeServices* services = nullptr;
std::uint64_t owner = 0;
struct Mover { std::string Label = "Project mover"; float Speed = 2.0f; };
void* Create(void*, Axle::SDK::EntityRef, const char* bytes, std::uint32_t size) {
    try {
        auto value = std::make_unique<Mover>();
        if (size) {
            std::istringstream input(std::string(bytes, size));
            if (!(input >> value->Speed >> std::quoted(value->Label)) || !std::isfinite(value->Speed)) return nullptr;
            input >> std::ws; if (!input.eof()) return nullptr;
        }
        return value.release();
    } catch (...) { return nullptr; }
}
void Destroy(void*, void* value) { delete static_cast<Mover*>(value); }
bool Serialize(void*, const void* pointer, Axle::SDK::TextSink sink, void* user) {
    try {
        const auto& value = *static_cast<const Mover*>(pointer);
        std::ostringstream output; output << std::setprecision(9) << value.Speed << ' ' << std::quoted(value.Label);
        const auto text = output.str(); return sink(user, text.data(), static_cast<std::uint32_t>(text.size()));
    } catch (...) { return false; }
}
void* Clone(void*, const void* value, Axle::SDK::EntityRef, const Axle::SDK::EntityRemap*, std::uint32_t) {
    try { return new Mover(*static_cast<const Mover*>(value)); } catch (...) { return nullptr; }
}
void Update(void*, void* pointer, Axle::SDK::EntityRef entity, Axle::SDK::Phase phase, float seconds) {
    if (phase != Axle::SDK::Phase::Frame || !services) return;
    Axle::SDK::Transform transform{};
    if (services->GetTransform(services->User, owner, entity, &transform)) {
        transform.Position[0] += static_cast<Mover*>(pointer)->Speed * seconds;
        services->SetTransform(services->User, owner, entity, &transform);
        const float to[] = {transform.Position[0], transform.Position[1] + 1.0f, transform.Position[2]};
        const float color[] = {0.2f, 1.0f, 0.3f, 1.0f};
        services->DebugLine(services->User, owner, entity.Scene, transform.Position, to, color);
    }
}
}
// The Editor imports this narrow accessor; it never owns the Runtime payload.
AXLE_PLUGIN_EXPORT float* axle_@MODULE@_speed(void* value) noexcept { return value ? &static_cast<Mover*>(value)->Speed : nullptr; }
AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID, AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    if (!host) return false;
    services = static_cast<const Axle::SDK::RuntimeServices*>(host->get_service(host->user_data, Axle::SDK::RuntimeService, sizeof(Axle::SDK::RuntimeServices)));
    if (!services || services != AxleRuntimeHost_GetV1()) return false;
    owner = host->owner_id;
    Axle::SDK::ComponentType type{};
    type.StructSize = sizeof(type); type.TypeId = "@PLUGIN_ID@/Mover"; type.Schema = 1; type.Required = true;
    type.Create = Create; type.Destroy = Destroy; type.Serialize = Serialize; type.Clone = Clone; type.Update = Update;
    return services->RegisterComponent(services->User, owner, &type);
}
AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept { services = nullptr; owner = 0; }
