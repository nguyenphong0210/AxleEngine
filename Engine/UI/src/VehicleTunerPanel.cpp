#include <Axle/VehicleTunerPanel.hpp>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <imgui.h>
#include <algorithm>

namespace Axle {

    VehicleTunerPanel::VehicleTunerPanel(Vehicle* vehicle, PhysicsScene* physicsScene) {
        SetContext(vehicle, physicsScene);
    }

    void VehicleTunerPanel::SetContext(Vehicle* vehicle, PhysicsScene* physicsScene) {
        m_Vehicle = vehicle;
        m_PhysicsScene = physicsScene;
    }

    void VehicleTunerPanel::OnImGuiRender(bool* p_open) {
        if (p_open && !*p_open) return;
        if (!m_Vehicle) return;

        ImGui::SetNextWindowSize(ImVec2(380.0f, 520.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Vehicle Tuner", p_open)) {
            ImGui::End();
            return;
        }

        const auto telemetry = m_Vehicle->GetTelemetry();

        // Top Status Header: Speedometer & Tachometer
        ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f), "Speed: %.1f km/h (%.1f m/s)", 
                           telemetry.SpeedKmh, telemetry.SpeedMps);

        const char* gearStr = telemetry.CurrentGear < 0 ? "R" : (telemetry.CurrentGear == 0 ? "N" : "D");
        char gearBuf[16];
        if (telemetry.CurrentGear > 0) {
            snprintf(gearBuf, sizeof(gearBuf), "Gear: D%d", telemetry.CurrentGear);
        } else {
            snprintf(gearBuf, sizeof(gearBuf), "Gear: %s", gearStr);
        }

        ImGui::Text("Engine: %.0f RPM | %s", telemetry.EngineRPM, gearBuf);
        const float rpmRatio = std::clamp(telemetry.EngineRPM / 7000.0f, 0.0f, 1.0f);
        ImGui::ProgressBar(rpmRatio, ImVec2(-1.0f, 0.0f));

        ImGui::Spacing();
        ImGui::Separator();

        // Action Buttons
        if (ImGui::Button("Reset to Start")) {
            m_Vehicle->Reset(glm::vec3(0.0f, 1.2f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip Upright")) {
            glm::vec3 currentPos = m_Vehicle->GetPosition();
            currentPos.y += 1.0f; // Lift slightly
            glm::vec3 fwd = m_Vehicle->GetForwardVector();
            float yaw = std::atan2(-fwd.x, -fwd.z);
            glm::quat uprightRot = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            m_Vehicle->Reset(currentPos, uprightRot);
        }

        ImGui::Spacing();

        // 1. Live Wheel Telemetry Table
        if (ImGui::CollapsingHeader("Wheel Telemetry (4 Wheels)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("WheelsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Wheel");
                ImGui::TableSetupColumn("Contact");
                ImGui::TableSetupColumn("Susp (m)");
                ImGui::TableSetupColumn("Steer (deg)");
                ImGui::TableSetupColumn("Ang Vel");
                ImGui::TableHeadersRow();

                const char* names[4] = { "FL", "FR", "RL", "RR" };
                for (u32 i = 0; i < 4; ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", names[i]);

                    ImGui::TableSetColumnIndex(1);
                    if (telemetry.Wheels[i].HasContact) {
                        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "ON");
                    } else {
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "AIR");
                    }

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.3f", telemetry.Wheels[i].SuspensionLength);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.1f", glm::degrees(telemetry.Wheels[i].SteerAngle));

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%.1f", telemetry.Wheels[i].AngularVelocity);
                }
                ImGui::EndTable();
            }
        }

        // 2. Powertrain & Engine Live Tuning
        auto* constraint = m_Vehicle->GetConstraint();
        if (constraint) {
            auto* controller = static_cast<JPH::WheeledVehicleController*>(constraint->GetController());
            if (controller) {
                if (ImGui::CollapsingHeader("Powertrain Tuning", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& engine = controller->GetEngine();
                    ImGui::SliderFloat("Peak Torque (N.m)", &engine.mMaxTorque, 200.0f, 2500.0f, "%.0f");
                    ImGui::SliderFloat("Max Redline RPM", &engine.mMaxRPM, 4000.0f, 10000.0f, "%.0f");
                    ImGui::SliderFloat("Idle RPM", &engine.mMinRPM, 500.0f, 2000.0f, "%.0f");

                    auto& transmission = controller->GetTransmission();
                    ImGui::SliderFloat("Clutch Strength", &transmission.mClutchStrength, 10.0f, 100.0f, "%.0f");
                    ImGui::SliderFloat("Shift Delay (s)", &transmission.mSwitchTime, 0.05f, 0.5f, "%.2f");
                }

                // 3. Suspension Springs & Dampers Live Tuning
                if (ImGui::CollapsingHeader("Suspension Tuning", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& wheels = constraint->GetWheels();
                    if (!wheels.empty()) {
                        auto* w0 = wheels[0];
                        auto* s0 = const_cast<JPH::WheelSettingsWV*>(static_cast<const JPH::WheelSettingsWV*>(w0->GetSettings()));

                        float springFreq = s0->mSuspensionSpring.mFrequency;
                        if (ImGui::SliderFloat("Spring Frequency (Hz)", &springFreq, 0.5f, 6.0f, "%.2f")) {
                            for (auto* w : wheels) {
                                auto* ws = const_cast<JPH::WheelSettingsWV*>(static_cast<const JPH::WheelSettingsWV*>(w->GetSettings()));
                                ws->mSuspensionSpring.mFrequency = springFreq;
                            }
                        }

                        float springDamping = s0->mSuspensionSpring.mDamping;
                        if (ImGui::SliderFloat("Damping Ratio", &springDamping, 0.1f, 2.0f, "%.2f")) {
                            for (auto* w : wheels) {
                                auto* ws = const_cast<JPH::WheelSettingsWV*>(static_cast<const JPH::WheelSettingsWV*>(w->GetSettings()));
                                ws->mSuspensionSpring.mDamping = springDamping;
                            }
                        }

                        float maxSteerDeg = glm::degrees(s0->mMaxSteerAngle);
                        if (ImGui::SliderFloat("Max Steer Angle (deg)", &maxSteerDeg, 10.0f, 50.0f, "%.1f")) {
                            // Apply only to front wheels (FL = 0, FR = 1)
                            for (u32 i = 0; i < 2 && i < wheels.size(); ++i) {
                                auto* ws = const_cast<JPH::WheelSettingsWV*>(static_cast<const JPH::WheelSettingsWV*>(wheels[i]->GetSettings()));
                                ws->mMaxSteerAngle = glm::radians(maxSteerDeg);
                            }
                        }

                        float maxBrake = s0->mMaxBrakeTorque;
                        if (ImGui::SliderFloat("Brake Torque (N.m)", &maxBrake, 500.0f, 8000.0f, "%.0f")) {
                            for (auto* w : wheels) {
                                auto* ws = const_cast<JPH::WheelSettingsWV*>(static_cast<const JPH::WheelSettingsWV*>(w->GetSettings()));
                                ws->mMaxBrakeTorque = maxBrake;
                            }
                        }

                        auto* s2 = static_cast<const JPH::WheelSettingsWV*>(wheels[2]->GetSettings());
                        float maxHandBrake = s2->mMaxHandBrakeTorque;
                        if (ImGui::SliderFloat("Handbrake Torque (N.m)", &maxHandBrake, 1000.0f, 12000.0f, "%.0f")) {
                            for (u32 i = 2; i < 4 && i < wheels.size(); ++i) {
                                auto* ws = const_cast<JPH::WheelSettingsWV*>(static_cast<const JPH::WheelSettingsWV*>(wheels[i]->GetSettings()));
                                ws->mMaxHandBrakeTorque = maxHandBrake;
                            }
                        }
                    }
                }
            }
        }

        ImGui::End();
    }

}
