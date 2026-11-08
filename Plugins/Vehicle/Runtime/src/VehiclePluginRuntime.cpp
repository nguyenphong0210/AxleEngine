#include <Axle/PluginSDK.hpp>
#include "AxleVehicle/VehicleComponents.hpp"
#include "AxleVehicle/VehicleCodecs.hpp"

#include <memory>
#include <string>

using namespace AxleVehicle;

namespace {

const Axle::SDK::RuntimeServices* s_Services = nullptr;
std::uint64_t s_Owner = 0;

struct ComponentContext {
    std::string Name;
};

void DestroyContext(void* ptr) {
    delete static_cast<ComponentContext*>(ptr);
}

// ---------------- Chassis Lifecycle ----------------

void* CreateChassis(void*, Axle::SDK::EntityRef, const char* text, std::uint32_t size) {
    auto comp = std::make_unique<VehicleChassisComponent>();
    if (size > 0) {
        if (!DeserializeChassis(std::string_view(text, size), *comp)) {
            return nullptr;
        }
    }
    return comp.release();
}

void DestroyChassis(void*, void* data) {
    delete static_cast<VehicleChassisComponent*>(data);
}

bool SerializeChassisHook(void*, const void* data, Axle::SDK::TextSink sink, void* user) {
    if (!data) return false;
    std::string text;
    if (!SerializeChassis(*static_cast<const VehicleChassisComponent*>(data), text)) {
        return false;
    }
    return sink(user, text.data(), static_cast<std::uint32_t>(text.size()));
}

void* CloneChassis(void*, const void* pointer, Axle::SDK::EntityRef, const Axle::SDK::EntityRemap* remap, std::uint32_t count) {
    if (!pointer) return nullptr;
    auto copy = std::make_unique<VehicleChassisComponent>(*static_cast<const VehicleChassisComponent*>(pointer));
    copy->Runtime.Reset();
    if (remap && count) {
        for (std::uint32_t w = 0; w < 4; ++w) {
            for (std::uint32_t i = 0; i < count; ++i) {
                if (copy->Config.WheelEntities[w] == remap[i].Source) {
                    copy->Config.WheelEntities[w] = remap[i].Destination;
                    break;
                }
            }
        }
    }
    return copy.release();
}

void StartChassis(void*, void* data, Axle::SDK::EntityRef) {
    if (!data) return;
    auto& comp = *static_cast<VehicleChassisComponent*>(data);
    comp.Runtime.Reset();
    ++comp.Runtime.StartCount;
}

void StopChassis(void*, void* data, Axle::SDK::EntityRef) {
    if (!data) return;
    auto& comp = *static_cast<VehicleChassisComponent*>(data);
    ++comp.Runtime.StopCount;
}

void UpdateChassis(void*, void* data, Axle::SDK::EntityRef entity, Axle::SDK::Phase phase, float dt) {
    if (!data || !s_Services) return;
    auto& comp = *static_cast<VehicleChassisComponent*>(data);
    ++comp.Runtime.UpdateCount;

    if (phase == Axle::SDK::Phase::FixedControls) {
        // Telemetry update placeholder for PLG-14; Jolt system integration follows in PLG-15
        float velocity[3]{};
        if (s_Services->GetLinearVelocity(s_Services->User, s_Owner, entity, velocity)) {
            const float speedMs = std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
            comp.Runtime.CurrentSpeedKmh = speedMs * 3.6f;
        }
    }
    if (phase == Axle::SDK::Phase::Late) {
        Axle::SDK::Transform transform{};
        if (s_Services->GetTransform(s_Services->User, s_Owner, entity, &transform)) {
            const float from[] = { transform.Position[0], transform.Position[1], transform.Position[2] };
            const float to[] = { transform.Position[0], transform.Position[1] + comp.Config.HalfExtents[1] * 2.0f, transform.Position[2] };
            const float color[] = { 0.1f, 0.8f, 1.0f, 1.0f };
            s_Services->DebugLine(s_Services->User, s_Owner, entity.Scene, from, to, color);
        }
    }
}

// ---------------- Wheel Lifecycle ----------------

void* CreateWheel(void*, Axle::SDK::EntityRef, const char* text, std::uint32_t size) {
    auto comp = std::make_unique<VehicleWheelComponent>();
    if (size > 0) {
        if (!DeserializeWheel(std::string_view(text, size), *comp)) {
            return nullptr;
        }
    }
    return comp.release();
}

void DestroyWheel(void*, void* data) {
    delete static_cast<VehicleWheelComponent*>(data);
}

bool SerializeWheelHook(void*, const void* data, Axle::SDK::TextSink sink, void* user) {
    if (!data) return false;
    std::string text;
    if (!SerializeWheel(*static_cast<const VehicleWheelComponent*>(data), text)) {
        return false;
    }
    return sink(user, text.data(), static_cast<std::uint32_t>(text.size()));
}

void* CloneWheel(void*, const void* pointer, Axle::SDK::EntityRef, const Axle::SDK::EntityRemap* remap, std::uint32_t count) {
    if (!pointer) return nullptr;
    auto copy = std::make_unique<VehicleWheelComponent>(*static_cast<const VehicleWheelComponent*>(pointer));
    copy->Runtime.Reset();
    if (remap && count) {
        for (std::uint32_t i = 0; i < count; ++i) {
            if (copy->Config.ChassisEntity == remap[i].Source) {
                copy->Config.ChassisEntity = remap[i].Destination;
                break;
            }
        }
    }
    return copy.release();
}

void StartWheel(void*, void* data, Axle::SDK::EntityRef) {
    if (!data) return;
    auto& comp = *static_cast<VehicleWheelComponent*>(data);
    comp.Runtime.Reset();
    ++comp.Runtime.StartCount;
}

void StopWheel(void*, void* data, Axle::SDK::EntityRef) {
    if (!data) return;
    auto& comp = *static_cast<VehicleWheelComponent*>(data);
    ++comp.Runtime.StopCount;
}

void UpdateWheel(void*, void* data, Axle::SDK::EntityRef entity, Axle::SDK::Phase phase, float dt) {
    if (!data || !s_Services) return;
    auto& comp = *static_cast<VehicleWheelComponent*>(data);
    ++comp.Runtime.UpdateCount;

    if (phase == Axle::SDK::Phase::FixedControls) {
        if (comp.Runtime.IsOnGround && comp.Config.Radius > 0.0f) {
            const float angVel = comp.Runtime.AngularVelocity;
            comp.Runtime.RotationAngle += angVel * dt;
            if (comp.Runtime.RotationAngle > 6.2831853f) comp.Runtime.RotationAngle -= 6.2831853f;
            else if (comp.Runtime.RotationAngle < -6.2831853f) comp.Runtime.RotationAngle += 6.2831853f;
        }
    }
}

} // namespace

// ---------------- Narrow Accessors for Editor/Diagnostics ----------------

AXLE_PLUGIN_EXPORT AxleVehicle::ChassisConfig* axle_vehicle_chassis_config(void* value) noexcept {
    return value ? &static_cast<VehicleChassisComponent*>(value)->Config : nullptr;
}

AXLE_PLUGIN_EXPORT AxleVehicle::ChassisRuntimeState* axle_vehicle_chassis_runtime(void* value) noexcept {
    return value ? &static_cast<VehicleChassisComponent*>(value)->Runtime : nullptr;
}

AXLE_PLUGIN_EXPORT AxleVehicle::WheelConfig* axle_vehicle_wheel_config(void* value) noexcept {
    return value ? &static_cast<VehicleWheelComponent*>(value)->Config : nullptr;
}

AXLE_PLUGIN_EXPORT AxleVehicle::WheelRuntimeState* axle_vehicle_wheel_runtime(void* value) noexcept {
    return value ? &static_cast<VehicleWheelComponent*>(value)->Runtime : nullptr;
}

// ---------------- Plugin Handshake ----------------

AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID, AXLE_PLUGIN_MODULE_NAME)

AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    if (!host || host->struct_size != sizeof(*host) || host->abi_version != Axle::PluginRuntime::kPluginAbiVersion) {
        return false;
    }
    s_Services = static_cast<const Axle::SDK::RuntimeServices*>(
        host->get_service(host->user_data, Axle::SDK::RuntimeService, sizeof(Axle::SDK::RuntimeServices)));
    if (!s_Services || s_Services != AxleRuntimeHost_GetV1()) {
        return false;
    }
    s_Owner = host->owner_id;

    // Register Chassis
    {
        auto ctx = std::make_unique<ComponentContext>(ComponentContext{"ChassisContext"});
        Axle::SDK::ComponentType chassis{};
        chassis.StructSize = sizeof(chassis);
        chassis.TypeId = kChassisTypeId;
        chassis.Schema = kChassisSchemaVersion;
        chassis.Required = true;
        chassis.ContainsEntityReferences = true;
        chassis.User = ctx.get();
        chassis.DestroyUser = DestroyContext;
        chassis.Create = CreateChassis;
        chassis.Destroy = DestroyChassis;
        chassis.Serialize = SerializeChassisHook;
        chassis.Clone = CloneChassis;
        chassis.Start = StartChassis;
        chassis.Stop = StopChassis;
        chassis.Update = UpdateChassis;

        if (!s_Services->RegisterComponent(s_Services->User, s_Owner, &chassis)) {
            return false;
        }
        ctx.release(); // Transferred to host
    }

    // Register Wheel
    {
        auto ctx = std::make_unique<ComponentContext>(ComponentContext{"WheelContext"});
        Axle::SDK::ComponentType wheel{};
        wheel.StructSize = sizeof(wheel);
        wheel.TypeId = kWheelTypeId;
        wheel.Schema = kWheelSchemaVersion;
        wheel.Required = true;
        wheel.ContainsEntityReferences = true;
        wheel.User = ctx.get();
        wheel.DestroyUser = DestroyContext;
        wheel.Create = CreateWheel;
        wheel.Destroy = DestroyWheel;
        wheel.Serialize = SerializeWheelHook;
        wheel.Clone = CloneWheel;
        wheel.Start = StartWheel;
        wheel.Stop = StopWheel;
        wheel.Update = UpdateWheel;

        if (!s_Services->RegisterComponent(s_Services->User, s_Owner, &wheel)) {
            return false;
        }
        ctx.release(); // Transferred to host
    }

    return true;
}

AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept {
    s_Services = nullptr;
    s_Owner = 0;
}
