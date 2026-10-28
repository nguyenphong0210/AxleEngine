#pragma once
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/Components.hpp>
#include <Axle/Input.hpp>
#include <Axle/KeyCodes.hpp>
#include <Axle/Log.hpp>
#include <Axle/Vehicle.hpp>
#include <Axle/Scene.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

class RacingHUD : public Axle::ScriptableEntity {
public:
    // --- Live Inspector Tunable Properties ---
    bool Visible = true;                // Master HUD overlay toggle (Hotkey 'H')
    bool UseMph = false;                // Speed units (false: KM/H, true: MPH - Hotkey 'U')
    bool ShowTachometer = true;         // Analog circular RPM gauge & needle
    bool ShowSpeedometer = true;        // Digital numeric speed readout
    bool ShowGear = true;               // Central transmission gear badge (R, N, 1..6)
    bool ShowPedals = true;             // Throttle, Brake, Handbrake, Steering meters
    bool ShowGForce = true;             // Lateral / Longitudinal G-Force friction circle
    bool ShowWheelSlip = true;          // 4-tire grip/slip telemetry indicators
    float MaxRPM = 8000.0f;             // Maximum tachometer gauge display RPM
    float RedlineRPM = 6500.0f;         // Shift-up warning threshold RPM
    float HUDScale = 1.0f;              // Overall HUD UI scale factor

private:
    // Filtered Telemetry Data
    float m_SpeedKmh = 0.0f;
    float m_EngineRPM = 0.0f;
    int m_CurrentGear = 0;
    float m_Throttle = 0.0f;
    float m_Brake = 0.0f;
    float m_Steering = 0.0f;
    bool m_Handbrake = false;

    // Smoothed visual animations
    float m_SmoothedRPM = 1000.0f;
    float m_SmoothedSpeed = 0.0f;
    glm::vec2 m_SmoothedGForce{ 0.0f, 0.0f }; // x: lateral, y: longitudinal
    float m_ShiftFlashTimer = 0.0f;
    bool m_ShiftFlashState = false;

    // Acceleration derivation cache
    glm::vec3 m_PrevLinearVelocity{ 0.0f };
    bool m_HasPrevVelocity = false;

    // Wheel slip tracking (FL, FR, RL, RR)
    float m_WheelSlip[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float m_WheelSteer = 0.0f;

    // Hotkey debounce states
    bool m_HKeyWasPressed = false;
    bool m_UKeyWasPressed = false;

public:
    void OnCreate() override {
        AXLE_INFO("RacingHUD: Attached to entity '{0}' (Status: Active)", GetEntityName());
        if (!HasComponent<Axle::UICanvasComponent>()) {
            GetEntity().AddComponent<Axle::UICanvasComponent>("Editor/assets/ui/RacingHUD.axleui");
        }
    }

    void OnUpdate(Axle::Timestep ts) override {
        // Hotkey 'H': Toggle HUD Visibility
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::H)) {
            if (!m_HKeyWasPressed) {
                Visible = !Visible;
                AXLE_INFO("RacingHUD: Visibility toggled {0}", Visible ? "ON" : "OFF");
                if (HasComponent<Axle::UICanvasComponent>()) {
                    GetComponent<Axle::UICanvasComponent>().Visible = Visible;
                }
                m_HKeyWasPressed = true;
            }
        } else {
            m_HKeyWasPressed = false;
        }

        // Hotkey 'U': Toggle Speed Units (KM/H <-> MPH)
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::U)) {
            if (!m_UKeyWasPressed) {
                UseMph = !UseMph;
                m_UKeyWasPressed = true;
            }
        } else {
            m_UKeyWasPressed = false;
        }

        if (HasComponent<Axle::UICanvasComponent>()) {
            GetComponent<Axle::UICanvasComponent>().Visible = Visible;
        }

        if (!Visible) return;

        Axle::Scene* scene = GetEntity().GetScene();
        if (!scene) return;

        // Resolve Target Vehicle: Entity itself or primary scene vehicle
        Axle::Vehicle* vehicle = nullptr;
        Axle::VehicleComponent* vc = nullptr;

        if (HasComponent<Axle::VehicleComponent>()) {
            vc = &GetComponent<Axle::VehicleComponent>();
            vehicle = scene->GetRuntimeVehicle(GetEntity());
        }

        if (!vehicle) {
            vehicle = scene->GetPrimaryRuntimeVehicle();
            if (vehicle) {
                for (const auto& rv : scene->GetRuntimeVehicles()) {
                    if (rv.VehicleInstance.get() == vehicle) {
                        Axle::Entity carEnt{ rv.ChassisEntityID, scene };
                        if (carEnt.HasComponent<Axle::VehicleComponent>()) {
                            vc = &carEnt.GetComponent<Axle::VehicleComponent>();
                        }
                        break;
                    }
                }
            }
        }

        const float dt = ts.GetSeconds();

        // Extract physical telemetry
        if (vehicle) {
            const auto telemetry = vehicle->GetTelemetry();
            m_SpeedKmh = telemetry.SpeedKmh;
            m_EngineRPM = telemetry.EngineRPM;
            m_CurrentGear = telemetry.CurrentGear;

            // G-Force accelerometer derivation
            const glm::vec3 currentVelocity = telemetry.LinearVelocity;
            if (m_HasPrevVelocity && dt > 1e-4f) {
                const glm::vec3 worldAccel = (currentVelocity - m_PrevLinearVelocity) / dt;
                glm::vec3 fwd = telemetry.ForwardVector;
                if (glm::length(fwd) > 1e-3f) {
                    fwd = glm::normalize(fwd);
                    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
                    const float aLong = glm::dot(worldAccel, fwd);
                    const float aLat = glm::dot(worldAccel, right);
                    const glm::vec2 targetG(aLat / 9.81f, aLong / 9.81f);
                    m_SmoothedGForce = glm::mix(m_SmoothedGForce, targetG, std::min(1.0f, dt * 8.0f));
                }
            }
            m_PrevLinearVelocity = currentVelocity;
            m_HasPrevVelocity = true;

            // 4-Wheel slip & steering angle
            for (int i = 0; i < 4; ++i) {
                const float slip = std::max(std::abs(telemetry.Wheels[i].LongitudinalSlip), std::abs(telemetry.Wheels[i].LateralSlip));
                m_WheelSlip[i] = glm::mix(m_WheelSlip[i], slip, std::min(1.0f, dt * 15.0f));
            }
            m_WheelSteer = telemetry.Wheels[0].SteerAngle;
        }

        // Driver inputs from VehicleComponent
        if (vc) {
            m_Throttle = std::clamp(vc->Throttle, 0.0f, 1.0f);
            m_Brake = std::clamp(vc->Brake, 0.0f, 1.0f);
            m_Handbrake = vc->Handbrake;
            m_Steering = std::clamp(vc->Steering, -1.0f, 1.0f);
            if (vc->MaxRPM > 1000.0f) {
                MaxRPM = vc->MaxRPM + 1000.0f;
                RedlineRPM = vc->MaxRPM;
            }
        }

        // Smooth animations
        m_SmoothedRPM = glm::mix(m_SmoothedRPM, m_EngineRPM, std::min(1.0f, dt * 18.0f));
        m_SmoothedSpeed = glm::mix(m_SmoothedSpeed, m_SpeedKmh, std::min(1.0f, dt * 12.0f));

        // Redline Shift-Up Flash Strobe
        if (m_SmoothedRPM >= RedlineRPM) {
            m_ShiftFlashTimer += dt;
            if (m_ShiftFlashTimer >= 0.09f) {
                m_ShiftFlashState = !m_ShiftFlashState;
                m_ShiftFlashTimer = 0.0f;
            }
        } else {
            m_ShiftFlashState = false;
            m_ShiftFlashTimer = 0.0f;
        }

        // Publish to scene UIDataContext for declarative AxleUI (RacingHUD.axleui)
        auto& uiCtx = scene->GetUIDataContext();
        uiCtx.Set("Engine.FPS", dt > 1e-4f ? static_cast<int32_t>(std::round(1.0f / dt)) : 60);

        const float displaySpeed = UseMph ? (m_SmoothedSpeed * 0.621371f) : m_SmoothedSpeed;
        uiCtx.Set("Vehicle.SpeedKmh", static_cast<int32_t>(std::round(displaySpeed)));
        uiCtx.Set("Vehicle.SpeedUnit", std::string(UseMph ? "MPH" : "KM/H"));

        const float rpmNorm = (MaxRPM > 0.0f) ? std::clamp(m_SmoothedRPM / MaxRPM, 0.0f, 1.0f) : 0.0f;
        uiCtx.Set("Vehicle.RPMNormalized", rpmNorm);

        const bool isRedline = (m_SmoothedRPM >= RedlineRPM);
        uiCtx.Set("Vehicle.IsRedline", isRedline);

        std::string gearStr = "N";
        if (m_CurrentGear < 0) gearStr = "R";
        else if (m_CurrentGear > 0) gearStr = std::to_string(m_CurrentGear);
        uiCtx.Set("Vehicle.GearString", gearStr);

        uiCtx.Set("Vehicle.Throttle", m_Throttle);
        uiCtx.Set("Vehicle.Brake", m_Brake);
        uiCtx.Set("Vehicle.Handbrake", m_Handbrake);
        uiCtx.Set("Vehicle.Steering", m_Steering);

        char latBuf[32], longBuf[32];
        std::snprintf(latBuf, sizeof(latBuf), "%+.2f", m_SmoothedGForce.x);
        std::snprintf(longBuf, sizeof(longBuf), "%+.2f", m_SmoothedGForce.y);
        uiCtx.Set("Vehicle.LateralG", std::string(latBuf));
        uiCtx.Set("Vehicle.LongitudinalG", std::string(longBuf));
        uiCtx.Set("Vehicle.CameraMode", std::string("CHASE"));
    }

    void OnImGuiRender() override {
        if (!Visible) return;

        // When UICanvasComponent is attached, the HUD is rendered declaratively by the Canvas UI system!
        if (HasComponent<Axle::UICanvasComponent>()) return;

        if (!ImGui::GetCurrentContext()) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (!drawList) return;

        // Viewport boundaries
        const ImVec2 vMin = ImGui::GetWindowPos();
        const ImVec2 vSize = ImGui::GetWindowSize();
        if (vSize.x < 300.0f || vSize.y < 200.0f) return;

        const float scale = std::clamp(HUDScale, 0.6f, 2.0f);
        const float hudW = 410.0f * scale;
        const float hudH = 175.0f * scale;
        const float margin = 20.0f * scale;

        // Position HUD at Bottom-Right of Viewport (Classic Forza / Gran Turismo style)
        const ImVec2 hudMin(vMin.x + vSize.x - hudW - margin, vMin.y + vSize.y - hudH - margin);
        const ImVec2 hudMax(hudMin.x + hudW, hudMin.y + hudH);

        // 1. Semi-Transparent Carbon Glass Background Card
        drawList->AddRectFilled(hudMin, hudMax, IM_COL32(14, 18, 26, 210), 12.0f * scale);
        const ImU32 borderColor = (m_ShiftFlashState) ? IM_COL32(231, 76, 60, 255) : IM_COL32(45, 55, 75, 180);
        drawList->AddRect(hudMin, hudMax, borderColor, 12.0f * scale, 0, 1.5f * scale);

        // 2. Render Sub-Modules
        if (ShowGForce) {
            RenderGForceMeter(drawList, ImVec2(hudMin.x + 58.0f * scale, hudMin.y + 70.0f * scale), 34.0f * scale);
        }

        if (ShowWheelSlip) {
            RenderWheelTraction(drawList, ImVec2(hudMin.x + 125.0f * scale, hudMin.y + 68.0f * scale), scale);
        }

        if (ShowPedals) {
            RenderPedalBars(drawList, ImVec2(hudMin.x + 180.0f * scale, hudMin.y + 25.0f * scale), scale);
        }

        if (ShowTachometer) {
            RenderTachometerAndSpeed(drawList, ImVec2(hudMax.x - 105.0f * scale, hudMin.y + 90.0f * scale), 72.0f * scale);
        }
    }

    void OnDestroy() override {
        AXLE_INFO("RacingHUD: Detached from entity '{0}'", GetEntityName());
    }

private:
    void RenderTachometerAndSpeed(ImDrawList* drawList, const ImVec2& center, float radius) {
        const float scale = radius / 72.0f;
        constexpr float pi = 3.1415926535f;

        // 270-degree sweep from 135 deg (3*pi/4) to 405 deg (9*pi/4)
        const float startAngle = 135.0f * (pi / 180.0f);
        const float endAngle = 405.0f * (pi / 180.0f);
        const float totalSweep = endAngle - startAngle;

        // Background track arc
        drawList->PathClear();
        drawList->PathArcTo(center, radius, startAngle, endAngle, 48);
        drawList->PathStroke(IM_COL32(35, 42, 58, 200), 0, 8.0f * scale);

        // Redline section track highlight
        const float redlineFraction = std::clamp(RedlineRPM / MaxRPM, 0.0f, 1.0f);
        const float redlineAngle = startAngle + redlineFraction * totalSweep;
        drawList->PathClear();
        drawList->PathArcTo(center, radius, redlineAngle, endAngle, 20);
        drawList->PathStroke(IM_COL32(180, 40, 40, 160), 0, 8.0f * scale);

        // Active RPM progress arc
        const float rpmFraction = std::clamp(m_SmoothedRPM / MaxRPM, 0.0f, 1.0f);
        const float currentAngle = startAngle + rpmFraction * totalSweep;

        ImU32 arcColor = IM_COL32(30, 200, 245, 255); // Cyan
        if (m_SmoothedRPM >= RedlineRPM) {
            arcColor = m_ShiftFlashState ? IM_COL32(255, 255, 255, 255) : IM_COL32(231, 76, 60, 255); // Red / White Strobe
        } else if (rpmFraction > 0.65f) {
            arcColor = IM_COL32(241, 196, 15, 255); // Amber / Yellow
        }

        drawList->PathClear();
        drawList->PathArcTo(center, radius, startAngle, currentAngle, 48);
        drawList->PathStroke(arcColor, 0, 8.0f * scale);

        // Analog Needle
        const float needleLen = radius - 6.0f * scale;
        const ImVec2 needleTip(center.x + std::cos(currentAngle) * needleLen, center.y + std::sin(currentAngle) * needleLen);
        const ImVec2 needleBase1(center.x + std::cos(currentAngle + 1.57f) * (4.0f * scale), center.y + std::sin(currentAngle + 1.57f) * (4.0f * scale));
        const ImVec2 needleBase2(center.x + std::cos(currentAngle - 1.57f) * (4.0f * scale), center.y + std::sin(currentAngle - 1.57f) * (4.0f * scale));
        drawList->AddTriangleFilled(needleTip, needleBase1, needleBase2, arcColor);

        // Center Chrome Hub
        drawList->AddCircleFilled(center, 12.0f * scale, IM_COL32(20, 26, 38, 255));
        drawList->AddCircle(center, 12.0f * scale, IM_COL32(140, 155, 180, 220), 24, 2.0f * scale);

        // Gear Indicator (Centered above hub)
        if (ShowGear) {
            char gearStr[8];
            ImU32 gearColor = IM_COL32(255, 255, 255, 255);
            if (m_CurrentGear == -1) {
                snprintf(gearStr, sizeof(gearStr), "R");
                gearColor = IM_COL32(231, 76, 60, 255); // Red
            } else if (m_CurrentGear == 0) {
                snprintf(gearStr, sizeof(gearStr), "N");
                gearColor = IM_COL32(241, 196, 15, 255); // Amber
            } else {
                snprintf(gearStr, sizeof(gearStr), "%d", m_CurrentGear);
                if (m_SmoothedRPM >= RedlineRPM) gearColor = IM_COL32(231, 76, 60, 255);
            }

            ImVec2 gTextSize = ImGui::CalcTextSize(gearStr);
            ImVec2 gearPos(center.x - gTextSize.x * 0.5f, center.y - 34.0f * scale);
            drawList->AddRectFilled(ImVec2(center.x - 14.0f * scale, center.y - 38.0f * scale),
                                    ImVec2(center.x + 14.0f * scale, center.y - 16.0f * scale),
                                    IM_COL32(22, 28, 40, 220), 4.0f * scale);
            drawList->AddText(gearPos, gearColor, gearStr);
        }

        // Digital Speedometer (Centered below hub)
        if (ShowSpeedometer) {
            const float dispSpeed = UseMph ? (m_SmoothedSpeed * 0.621371f) : m_SmoothedSpeed;
            char speedStr[16];
            snprintf(speedStr, sizeof(speedStr), "%.0f", dispSpeed);
            ImVec2 sTextSize = ImGui::CalcTextSize(speedStr);
            ImVec2 speedPos(center.x - sTextSize.x * 0.5f, center.y + 16.0f * scale);
            drawList->AddText(speedPos, IM_COL32(240, 245, 255, 255), speedStr);

            const char* unitStr = UseMph ? "MPH" : "KM/H";
            ImVec2 uTextSize = ImGui::CalcTextSize(unitStr);
            ImVec2 unitPos(center.x - uTextSize.x * 0.5f, center.y + 34.0f * scale);
            drawList->AddText(unitPos, IM_COL32(0, 210, 180, 240), unitStr);
        }
    }

    void RenderPedalBars(ImDrawList* drawList, const ImVec2& origin, float scale) {
        const float barW = 12.0f * scale;
        const float barH = 75.0f * scale;
        const float spacing = 7.0f * scale;

        // 1. Throttle Bar (Green)
        ImVec2 tMin = origin;
        ImVec2 tMax = ImVec2(tMin.x + barW, tMin.y + barH);
        drawList->AddRectFilled(tMin, tMax, IM_COL32(25, 35, 45, 180), 3.0f * scale);
        const float tFillH = barH * m_Throttle;
        if (tFillH > 1.0f) {
            drawList->AddRectFilled(ImVec2(tMin.x, tMax.y - tFillH), tMax, IM_COL32(46, 204, 113, 255), 3.0f * scale);
        }
        drawList->AddText(ImVec2(tMin.x - 2.0f, tMax.y + 4.0f * scale), IM_COL32(160, 175, 190, 220), "THR");

        // 2. Brake Bar (Red)
        ImVec2 bMin = ImVec2(tMax.x + spacing, tMin.y);
        ImVec2 bMax = ImVec2(bMin.x + barW, bMin.y + barH);
        drawList->AddRectFilled(bMin, bMax, IM_COL32(25, 35, 45, 180), 3.0f * scale);
        const float bFillH = barH * m_Brake;
        if (bFillH > 1.0f) {
            drawList->AddRectFilled(ImVec2(bMin.x, bMax.y - bFillH), bMax, IM_COL32(231, 76, 60, 255), 3.0f * scale);
        }
        drawList->AddText(ImVec2(bMin.x - 2.0f, bMax.y + 4.0f * scale), IM_COL32(160, 175, 190, 220), "BRK");

        // 3. Handbrake Indicator
        ImVec2 hbPos = ImVec2(bMax.x + spacing, tMin.y + 12.0f * scale);
        const ImU32 hbColor = m_Handbrake ? IM_COL32(241, 196, 15, 255) : IM_COL32(60, 70, 85, 150);
        drawList->AddCircleFilled(ImVec2(hbPos.x + 9.0f * scale, hbPos.y + 9.0f * scale), 9.0f * scale, hbColor);
        drawList->AddText(ImVec2(hbPos.x + 5.0f * scale, hbPos.y + 2.0f * scale), m_Handbrake ? IM_COL32(20, 20, 20, 255) : IM_COL32(120, 130, 140, 200), "P");
        drawList->AddText(ImVec2(hbPos.x + 2.0f * scale, tMax.y + 4.0f * scale), IM_COL32(160, 175, 190, 220), "HB");

        // 4. Horizontal Steering Deviation Bar
        const float steerBarW = (barW * 2.0f + spacing * 2.0f + 18.0f * scale);
        const ImVec2 sBarMin(tMin.x, tMax.y + 24.0f * scale);
        const ImVec2 sBarMax(sBarMin.x + steerBarW, sBarMin.y + 7.0f * scale);
        drawList->AddRectFilled(sBarMin, sBarMax, IM_COL32(25, 35, 45, 180), 3.0f * scale);
        const float centerSteerX = sBarMin.x + steerBarW * 0.5f;
        const float steerOffsetX = (steerBarW * 0.5f - 2.0f) * m_Steering;
        drawList->AddLine(ImVec2(centerSteerX, sBarMin.y), ImVec2(centerSteerX, sBarMax.y), IM_COL32(120, 130, 140, 180));
        drawList->AddRectFilled(ImVec2(centerSteerX + steerOffsetX - 3.0f * scale, sBarMin.y),
                                ImVec2(centerSteerX + steerOffsetX + 3.0f * scale, sBarMax.y),
                                IM_COL32(30, 200, 245, 255), 2.0f * scale);
    }

    void RenderGForceMeter(ImDrawList* drawList, const ImVec2& center, float radius) {
        const float scale = radius / 34.0f;

        // Concentric Rings: 1.0G (inner) and 2.0G (outer)
        drawList->AddCircle(center, radius * 0.5f, IM_COL32(50, 65, 85, 160), 28, 1.0f * scale);
        drawList->AddCircle(center, radius, IM_COL32(70, 85, 110, 200), 28, 1.5f * scale);

        // Crosshairs
        drawList->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), IM_COL32(50, 65, 85, 150));
        drawList->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), IM_COL32(50, 65, 85, 150));

        // Accelerometer Dot
        const float maxDisplayG = 2.0f;
        const float dotX = center.x + std::clamp(m_SmoothedGForce.x / maxDisplayG, -1.0f, 1.0f) * radius;
        const float dotY = center.y - std::clamp(m_SmoothedGForce.y / maxDisplayG, -1.0f, 1.0f) * radius;
        drawList->AddCircleFilled(ImVec2(dotX, dotY), 4.5f * scale, IM_COL32(241, 196, 15, 255));
        drawList->AddCircle(ImVec2(dotX, dotY), 6.5f * scale, IM_COL32(241, 196, 15, 150), 16, 1.0f * scale);

        // Numeric Readout below
        char gStr[32];
        snprintf(gStr, sizeof(gStr), "%.2fG", glm::length(m_SmoothedGForce));
        ImVec2 gSize = ImGui::CalcTextSize(gStr);
        drawList->AddText(ImVec2(center.x - gSize.x * 0.5f, center.y + radius + 6.0f * scale), IM_COL32(200, 215, 230, 220), gStr);
        drawList->AddText(ImVec2(center.x - 18.0f * scale, center.y - radius - 16.0f * scale), IM_COL32(140, 155, 175, 200), "G-FORCE");
    }

    void RenderWheelTraction(ImDrawList* drawList, const ImVec2& center, float scale) {
        const float tireW = 7.0f * scale;
        const float tireH = 14.0f * scale;
        const float trackW = 16.0f * scale;
        const float wheelbaseH = 24.0f * scale;

        const ImVec2 offsets[4] = {
            ImVec2(-trackW, -wheelbaseH), // FL
            ImVec2( trackW, -wheelbaseH), // FR
            ImVec2(-trackW,  wheelbaseH), // RL
            ImVec2( trackW,  wheelbaseH)  // RR
        };

        // Chassis outline skeleton
        drawList->AddRect(ImVec2(center.x - trackW * 0.7f, center.y - wheelbaseH * 0.8f),
                          ImVec2(center.x + trackW * 0.7f, center.y + wheelbaseH * 0.8f),
                          IM_COL32(50, 60, 75, 150), 3.0f * scale);

        for (int i = 0; i < 4; ++i) {
            const ImVec2 tPos(center.x + offsets[i].x, center.y + offsets[i].y);
            ImU32 tireColor = IM_COL32(46, 204, 113, 220); // Green (Grip)
            if (m_WheelSlip[i] > 0.45f) {
                tireColor = IM_COL32(231, 76, 60, 255);     // Red (Skid / Spin)
            } else if (m_WheelSlip[i] > 0.18f) {
                tireColor = IM_COL32(241, 196, 15, 240);    // Yellow (Slip)
            }

            drawList->AddRectFilled(ImVec2(tPos.x - tireW * 0.5f, tPos.y - tireH * 0.5f),
                                    ImVec2(tPos.x + tireW * 0.5f, tPos.y + tireH * 0.5f),
                                    tireColor, 2.0f * scale);
        }
        drawList->AddText(ImVec2(center.x - 14.0f * scale, center.y + wheelbaseH + 12.0f * scale), IM_COL32(140, 155, 175, 200), "TIRES");
    }

    std::string GetEntityName() const {
        return HasComponent<Axle::TagComponent>() ? GetComponent<Axle::TagComponent>().Tag : "Racing HUD";
    }

public:
    AXLE_SCRIPT_FIELDS(RacingHUD,
        AXLE_FIELD(Visible, "Master HUD visibility toggle (Hotkey 'H')"),
        AXLE_FIELD(UseMph, "Display speed in MPH instead of KM/H (Hotkey 'U')"),
        AXLE_FIELD(ShowTachometer, "Display analog tachometer RPM arc & needle"),
        AXLE_FIELD(ShowSpeedometer, "Display digital speed readout"),
        AXLE_FIELD(ShowGear, "Display current transmission gear (R, N, 1..6)"),
        AXLE_FIELD(ShowPedals, "Display Throttle, Brake, Handbrake, Steering meters"),
        AXLE_FIELD(ShowGForce, "Display G-Force accelerometer friction circle"),
        AXLE_FIELD(ShowWheelSlip, "Display 4-tire grip/slip telemetry indicators"),
        AXLE_FIELD(MaxRPM, 5000.0f, 12000.0f, "Maximum gauge display RPM"),
        AXLE_FIELD(RedlineRPM, 4000.0f, 11000.0f, "Redline shift-up warning RPM"),
        AXLE_FIELD(HUDScale, 0.6f, 2.0f, "Overall HUD scaling factor")
    )
};

AXLE_REGISTER_SCRIPT(RacingHUD);
