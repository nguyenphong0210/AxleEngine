#include <Axle/PluginSDK.hpp>
#include "AxleVehicle/VehicleComponents.hpp"
#include <string>
#include <cmath>

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

    s_Editor->Text(s_Editor->User, "=== Vehicle Chassis Configuration ===");
    auto* config = axle_vehicle_chassis_config(value);
    auto* runtime = axle_vehicle_chassis_runtime(value);

    if (config) {
        s_Editor->DragFloat(s_Editor->User, "Half Width (m)", &config->HalfExtents[0], 0.05f);
        s_Editor->DragFloat(s_Editor->User, "Half Height (m)", &config->HalfExtents[1], 0.05f);
        s_Editor->DragFloat(s_Editor->User, "Half Length (m)", &config->HalfExtents[2], 0.05f);
        s_Editor->DragFloat(s_Editor->User, "Chassis Mass (kg)", &config->Mass, 10.0f);
        s_Editor->DragFloat(s_Editor->User, "CoM Offset Y (m)", &config->CenterOfMassOffset[1], 0.02f);
        s_Editor->DragFloat(s_Editor->User, "Max Torque (N.m)", &config->MaxEngineTorque, 10.0f);
        s_Editor->DragFloat(s_Editor->User, "Min Idle RPM", &config->MinRPM, 50.0f);
        s_Editor->DragFloat(s_Editor->User, "Max Redline RPM", &config->MaxRPM, 100.0f);
        s_Editor->DragFloat(s_Editor->User, "Max Steer Angle (deg)", &config->MaxSteerAngle, 1.0f);
        s_Editor->DragFloat(s_Editor->User, "Service Brake Torque", &config->MaxBrakeTorque, 50.0f);
        s_Editor->DragFloat(s_Editor->User, "Handbrake Torque", &config->MaxHandbrakeTorque, 50.0f);

        // Configuration diagnostics
        s_Editor->Text(s_Editor->User, "--- Diagnostics ---");
        if (config->Mass <= 0.0f || !std::isfinite(config->Mass)) {
            s_Editor->Text(s_Editor->User, "[ERROR] Chassis mass must be strictly positive!");
        } else if (config->MinRPM >= config->MaxRPM) {
            s_Editor->Text(s_Editor->User, "[ERROR] Max RPM must be strictly greater than Min RPM!");
        } else if (config->MaxSteerAngle < 0.0f || config->MaxSteerAngle > 90.0f) {
            s_Editor->Text(s_Editor->User, "[ERROR] Steer angle must be in range [0, 90] degrees!");
        } else if (config->HalfExtents[0] <= 0.0f || config->HalfExtents[1] <= 0.0f || config->HalfExtents[2] <= 0.0f) {
            s_Editor->Text(s_Editor->User, "[ERROR] All chassis half-extents must be strictly positive!");
        } else {
            s_Editor->Text(s_Editor->User, "[OK] Chassis configuration is valid.");
        }
    }

    if (runtime) {
        s_Editor->Text(s_Editor->User, "=== Live Telemetry ===");
        s_Editor->Text(s_Editor->User, ("Vehicle Speed: " + std::to_string(static_cast<int>(runtime->CurrentSpeedKmh)) + " km/h").c_str());
        s_Editor->Text(s_Editor->User, ("Engine RPM: " + std::to_string(static_cast<int>(runtime->CurrentRPM))).c_str());
        s_Editor->Text(s_Editor->User, ("Current Gear: " + (runtime->CurrentGear < 0 ? "R" : (runtime->CurrentGear == 0 ? "N" : std::to_string(runtime->CurrentGear)))).c_str());
        s_Editor->Text(s_Editor->User, runtime->IsOnGround ? "Ground Contact: Grounded" : "Ground Contact: Airborne");

        if (s_Editor->Button && s_Editor->Button(s_Editor->User, "Reset Vehicle Dynamics")) {
            runtime->Reset();
        }
    }
}

void DrawWheelInspector(void*, Axle::SDK::EntityRef, void* value) {
    if (!s_Editor || !value) return;

    s_Editor->Text(s_Editor->User, "=== Vehicle Wheel Configuration ===");
    auto* config = axle_vehicle_wheel_config(value);
    auto* runtime = axle_vehicle_wheel_runtime(value);

    if (config) {
        static const char* s_WheelNames[4] = { "0 (Front Left)", "1 (Front Right)", "2 (Rear Left)", "3 (Rear Right)" };
        const char* wheelName = (config->WheelIndex < 4) ? s_WheelNames[config->WheelIndex] : "Unknown";
        s_Editor->Text(s_Editor->User, ("Assigned Role: " + std::string(wheelName)).c_str());

        s_Editor->DragFloat(s_Editor->User, "Tire Radius (m)", &config->Radius, 0.01f);
        s_Editor->DragFloat(s_Editor->User, "Tire Width (m)", &config->Width, 0.01f);
        s_Editor->DragFloat(s_Editor->User, "Suspension Min Length (m)", &config->SuspensionMinLength, 0.01f);
        s_Editor->DragFloat(s_Editor->User, "Suspension Max Length (m)", &config->SuspensionMaxLength, 0.01f);
        s_Editor->DragFloat(s_Editor->User, "Spring Frequency (Hz)", &config->SpringFrequency, 0.1f);
        s_Editor->DragFloat(s_Editor->User, "Spring Damping Ratio", &config->SpringDamping, 0.05f);
        s_Editor->DragFloat(s_Editor->User, "Service Brake Ratio", &config->BrakeRatio, 0.05f);
        s_Editor->DragFloat(s_Editor->User, "Handbrake Ratio", &config->HandbrakeRatio, 0.05f);

        // Configuration diagnostics
        s_Editor->Text(s_Editor->User, "--- Diagnostics ---");
        if (config->Radius <= 0.0f || !std::isfinite(config->Radius)) {
            s_Editor->Text(s_Editor->User, "[ERROR] Tire radius must be strictly positive!");
        } else if (config->SuspensionMinLength >= config->SuspensionMaxLength) {
            s_Editor->Text(s_Editor->User, "[ERROR] Suspension min length must be strictly less than max length!");
        } else if (config->WheelIndex > 3) {
            s_Editor->Text(s_Editor->User, "[ERROR] Wheel index must be 0, 1, 2, or 3!");
        } else if (config->BrakeRatio < 0.0f || config->BrakeRatio > 1.0f) {
            s_Editor->Text(s_Editor->User, "[ERROR] Brake ratio must be within [0.0, 1.0]!");
        } else {
            s_Editor->Text(s_Editor->User, "[OK] Wheel configuration is valid.");
        }
    }

    if (runtime) {
        s_Editor->Text(s_Editor->User, "=== Live Wheel Telemetry ===");
        s_Editor->Text(s_Editor->User, runtime->IsOnGround ? "Wheel Contact: Grounded" : "Wheel Contact: Airborne");
        s_Editor->Text(s_Editor->User, ("Suspension Length: " + std::to_string(runtime->CurrentSuspensionLength) + " m").c_str());
        s_Editor->Text(s_Editor->User, ("Steer Angle: " + std::to_string(runtime->SteerAngle * 57.2957795f) + " deg").c_str());
        s_Editor->Text(s_Editor->User, ("Angular Velocity: " + std::to_string(runtime->AngularVelocity) + " rad/s").c_str());
        s_Editor->Text(s_Editor->User, ("Longitudinal Slip: " + std::to_string(runtime->LongitudinalSlip)).c_str());
        s_Editor->Text(s_Editor->User, ("Lateral Slip: " + std::to_string(runtime->LateralSlip)).c_str());
    }
}

} // namespace

#if defined(AXLE_PLUGIN_DESCRIPTOR) && defined(AXLE_PLUGIN_ID) && defined(AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID, AXLE_PLUGIN_MODULE_NAME)
#endif

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
