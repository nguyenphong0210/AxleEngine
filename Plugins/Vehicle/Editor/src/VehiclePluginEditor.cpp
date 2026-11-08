#include <Axle/PluginSDK.hpp>
#include "AxleVehicle/VehicleComponents.hpp"
#include <string>

#if defined(_WIN32)
#define AXLE_MODULE_IMPORT extern "C" __declspec(dllimport)
#else
#define AXLE_MODULE_IMPORT extern "C"
#endif

AXLE_MODULE_IMPORT AxleVehicle::ChassisConfig* axle_vehicle_chassis_config(void*) noexcept;
AXLE_MODULE_IMPORT AxleVehicle::ChassisRuntimeState* axle_vehicle_chassis_runtime(void*) noexcept;
AXLE_MODULE_IMPORT AxleVehicle::WheelConfig* axle_vehicle_wheel_config(void*) noexcept;
AXLE_MODULE_IMPORT AxleVehicle::WheelRuntimeState* axle_vehicle_wheel_runtime(void*) noexcept;

namespace {

const Axle::SDK::EditorServices* s_Editor = nullptr;

void DrawChassisInspector(void*, Axle::SDK::EntityRef, void* value) {
    if (!s_Editor || !value) return;
    s_Editor->Text(s_Editor->User, "--- Vehicle Chassis ---");
    if (auto* config = axle_vehicle_chassis_config(value)) {
        s_Editor->DragFloat(s_Editor->User, "Mass (kg)", &config->Mass, 10.0f);
        s_Editor->DragFloat(s_Editor->User, "Max Torque (N.m)", &config->MaxEngineTorque, 10.0f);
        s_Editor->DragFloat(s_Editor->User, "Max Steer Angle (deg)", &config->MaxSteerAngle, 1.0f);
        s_Editor->DragFloat(s_Editor->User, "Max Brake Torque", &config->MaxBrakeTorque, 50.0f);
        s_Editor->DragFloat(s_Editor->User, "Max Handbrake Torque", &config->MaxHandbrakeTorque, 50.0f);
    }
    if (auto* runtime = axle_vehicle_chassis_runtime(value)) {
        s_Editor->Text(s_Editor->User, ("Speed (km/h): " + std::to_string(static_cast<int>(runtime->CurrentSpeedKmh))).c_str());
        s_Editor->Text(s_Editor->User, ("Engine RPM: " + std::to_string(static_cast<int>(runtime->CurrentRPM))).c_str());
        s_Editor->Text(s_Editor->User, ("Gear: " + std::to_string(runtime->CurrentGear)).c_str());
    }
}

void DrawWheelInspector(void*, Axle::SDK::EntityRef, void* value) {
    if (!s_Editor || !value) return;
    s_Editor->Text(s_Editor->User, "--- Vehicle Wheel ---");
    if (auto* config = axle_vehicle_wheel_config(value)) {
        s_Editor->DragFloat(s_Editor->User, "Radius (m)", &config->Radius, 0.01f);
        s_Editor->DragFloat(s_Editor->User, "Width (m)", &config->Width, 0.01f);
        s_Editor->DragFloat(s_Editor->User, "Suspension Min", &config->SuspensionMinLength, 0.01f);
        s_Editor->DragFloat(s_Editor->User, "Suspension Max", &config->SuspensionMaxLength, 0.01f);
        s_Editor->DragFloat(s_Editor->User, "Spring Freq (Hz)", &config->SpringFrequency, 0.1f);
        s_Editor->DragFloat(s_Editor->User, "Spring Damping", &config->SpringDamping, 0.05f);
    }
    if (auto* runtime = axle_vehicle_wheel_runtime(value)) {
        s_Editor->Text(s_Editor->User, runtime->IsOnGround ? "Status: Grounded" : "Status: Airborne");
        s_Editor->Text(s_Editor->User, ("Suspension: " + std::to_string(runtime->CurrentSuspensionLength) + " m").c_str());
    }
}

} // namespace

AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID, AXLE_PLUGIN_MODULE_NAME)

AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    if (!host) return false;
    s_Editor = static_cast<const Axle::SDK::EditorServices*>(
        host->get_service(host->user_data, Axle::SDK::EditorService, sizeof(Axle::SDK::EditorServices)));
    if (!s_Editor || s_Editor != AxleEditorHost_GetV1()) {
        return false;
    }

    if (!s_Editor->RegisterInspector(s_Editor->User, host->owner_id, AxleVehicle::kChassisTypeId, DrawChassisInspector, nullptr)) {
        return false;
    }
    if (!s_Editor->RegisterInspector(s_Editor->User, host->owner_id, AxleVehicle::kWheelTypeId, DrawWheelInspector, nullptr)) {
        return false;
    }
    return true;
}

AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept {
    s_Editor = nullptr;
}
