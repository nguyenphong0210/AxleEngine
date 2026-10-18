#pragma once
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/Components.hpp>
#include <Axle/Input.hpp>
#include <Axle/KeyCodes.hpp>
#include <Axle/Log.hpp>
#include <Axle/Vehicle.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/Scene.hpp>
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

enum class VehicleCameraMode : int {
    ThirdPersonChase = 0,
    Cockpit = 1,
    Hood = 2,
    FreeOrbit = 3,
    Count = 4
};

class VehicleChaseCamera : public Axle::ScriptableEntity {
public:
    // --- Live Inspector Tunable Fields ---
    int CameraMode = 0;                 // 0: Third-Person Chase, 1: Cockpit Driver POV, 2: Hood/Bumper, 3: Free Orbit

    // Chase Camera Parameters
    int DistanceLevel = 1;              // 0: Near, 1: Medium, 2: Far
    float NearDistance = 5.2f;          // Close chase camera distance
    float MediumDistance = 7.2f;        // Medium chase camera distance (default)
    float FarDistance = 10.2f;          // Far chase camera distance
    float Distance = 7.2f;              // Orbit distance from chase target in meters
    float HeightOffset = 1.15f;         // Target focus point height above vehicle center
    float MinElevation = 0.35f;         // Minimum world Y height to prevent ground clipping
    float ElevationOffset = 1.45f;      // Vertical lift added to orbit position
    float MouseSensitivity = 0.25f;     // Mouse look rotation sensitivity
    float AutoAlignSpeed = 3.5f;        // Responsive auto-alignment speed behind vehicle
    float PositionLagSpeed = 9.0f;      // Spring-arm position lag interpolation rate
    float RotationLagSpeed = 8.0f;      // Camera look rotation smoothing rate

    // Dynamic FOV Expansion (Sense of Speed)
    float BaseFOV = 60.0f;              // Baseline camera field of view in degrees
    float SpeedFOVExpansion = 15.0f;    // Maximum FOV increase at top speed (e.g. 60° -> 75°)
    float MaxSpeedForFOV = 160.0f;      // Vehicle speed (km/h) for maximum FOV expansion

    // Cockpit / Interior Camera Parameters (Driver POV)
    float DriverHeadX = -0.38f;         // Lateral position (Left seat cockpit offset)
    float DriverHeadY = 0.95f;          // Eye level height above vehicle origin
    float DriverHeadZ = 0.15f;          // Longitudinal seating position
    bool EnableHeadInertia = true;      // G-force head tilt, lunge, and sway
    bool EnableVibration = true;        // Engine RPM vibration tremor

    // Hood / Bumper Camera Parameters
    float HoodOffsetX = 0.0f;           // Hood lateral center alignment
    float HoodOffsetY = 0.72f;          // Low front hood eye level
    float HoodOffsetZ = 1.85f;          // Front nose position ahead of windshield

    // Free Orbit Parameters
    float FreeOrbitDistance = 7.5f;     // Orbit distance for inspecting vehicle

    // Environment Collision Raycast
    bool EnableAntiClipping = true;     // Raycast query preventing wall/floor penetration

    // Mouse Capture / Input Mode (Unreal Engine & GTA V Style)
    bool ClickToLock = true;            // Click LMB in Viewport to capture/hide mouse
    bool UnlockOnEscape = true;         // Press Escape to release/show mouse
    bool UnlockOnShiftF1 = true;        // Press Shift+F1 (Unreal Engine PIE standard) to release/show mouse
    bool EnableRMBZoom = true;          // Hold RMB to zoom in / cinematic focus (GTA style)
    float RMBZoomFOV = 42.0f;           // Target FOV when holding RMB focus
    float AutoAlignDelay = 1.0f;        // Seconds to wait after mouse movement before auto-aligning

private:
    VehicleCameraMode m_CurrentMode = VehicleCameraMode::ThirdPersonChase;

    // Third-person orbit angles
    float m_OrbitYaw = 180.0f;
    float m_OrbitPitch = 14.0f;

    // Free-orbit angles
    float m_FreeOrbitYaw = 180.0f;
    float m_FreeOrbitPitch = 15.0f;

    // Cockpit look angles (glance left/right/mirrors)
    float m_CockpitYaw = 0.0f;
    float m_CockpitPitch = 0.0f;

    // Mouse tracking & auto-alignment cooldown
    glm::vec2 m_LastMousePos{ 0.0f, 0.0f };
    bool m_FirstMouseUpdate = true;
    bool m_WasDragging = false;
    float m_AutoAlignTimer = 10.0f;

    // Smoothed spring-arm world position & velocity
    glm::vec3 m_SmoothedPos{ 0.0f };
    glm::quat m_SmoothedRot{ 1.0f, 0.0f, 0.0f, 0.0f };
    bool m_HasInitialPose = false;

    // G-force inertia tracking
    glm::vec3 m_PrevVehicleVelocity{ 0.0f };
    glm::vec3 m_HeadInertiaPos{ 0.0f };
    glm::vec3 m_HeadInertiaRot{ 0.0f }; // pitch, yaw, roll in radians

    // Engine vibration timer
    float m_VibeTimer = 0.0f;

    // Mode cycle hotkey debounce (Key V)
    bool m_VKeyWasPressed = false;

    // Distance cycle hotkey debounce (Key C)
    bool m_CKeyWasPressed = false;

    // Smooth transition interpolation between camera modes
    bool m_IsTransitioning = false;
    float m_TransitionTimer = 0.0f;
    const float m_TransitionDuration = 0.35f;
    glm::vec3 m_TransitionStartPos{ 0.0f };
    glm::quat m_TransitionStartRot{ 1.0f, 0.0f, 0.0f, 0.0f };

public:
    void OnCreate() override {
        AXLE_INFO("VehicleChaseCamera: Attached to entity '{0}' (Mode: {1})", GetEntityName(), GetModeName(m_CurrentMode));
        if (HasComponent<Axle::TransformComponent>()) {
            const auto& tc = GetComponent<Axle::TransformComponent>();
            m_SmoothedPos = tc.Translation;
            m_SmoothedRot = tc.Rotation;
            m_HasInitialPose = true;

            glm::vec3 fwd = tc.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
            if (glm::length(fwd) > 1e-4f) {
                fwd = glm::normalize(fwd);
                m_OrbitPitch = std::clamp(glm::degrees(std::asin(std::clamp(fwd.y, -0.999f, 0.999f))), -45.0f, 75.0f);
                m_OrbitYaw = glm::degrees(std::atan2(-fwd.x, -fwd.z));
                m_FreeOrbitYaw = m_OrbitYaw;
                m_FreeOrbitPitch = m_OrbitPitch;
            }
        }
        if (HasComponent<Axle::CameraComponent>()) {
            BaseFOV = GetComponent<Axle::CameraComponent>().FOV;
        }
    }

    void OnUpdate(Axle::Timestep ts) override {
        Axle::Scene* scene = GetEntity().GetScene();
        if (!scene) return;

        // 1. Sync camera mode from Inspector slider or hotkey
        if (CameraMode != static_cast<int>(m_CurrentMode)) {
            SetCameraMode(static_cast<VehicleCameraMode>(std::clamp(CameraMode, 0, 3)));
        }

        // Hotkey 'V': Seamless cycle camera modes (Chase -> Cockpit -> Hood -> Free Orbit -> Chase)
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::V)) {
            if (!m_VKeyWasPressed) {
                int nextMode = (static_cast<int>(m_CurrentMode) + 1) % static_cast<int>(VehicleCameraMode::Count);
                SetCameraMode(static_cast<VehicleCameraMode>(nextMode));
                m_VKeyWasPressed = true;
            }
        } else {
            m_VKeyWasPressed = false;
        }

        // Hotkey 'C': Seamless cycle camera distance presets (Near -> Medium -> Far -> Near)
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::C)) {
            if (!m_CKeyWasPressed) {
                CycleDistancePreset();
                m_CKeyWasPressed = true;
            }
        } else {
            m_CKeyWasPressed = false;
        }

        // 2. Find target player vehicle in the scene
        Axle::Entity target;
        Axle::Vehicle* runtimeVehicle = scene->GetPrimaryRuntimeVehicle();
        if (runtimeVehicle) {
            for (const auto& rv : scene->GetRuntimeVehicles()) {
                if (rv.VehicleInstance.get() == runtimeVehicle) {
                    target = Axle::Entity{ rv.ChassisEntityID, scene };
                    break;
                }
            }
        }

        if (!target) {
            auto vehicleView = scene->GetRegistry().view<Axle::VehicleComponent>();
            for (auto entityID : vehicleView) {
                target = Axle::Entity{ entityID, scene };
                runtimeVehicle = scene->GetRuntimeVehicle(target);
                break;
            }
        }
        if (!target) return;

        // 3. Extract vehicle physical telemetry & world transforms
        glm::vec3 targetPos{ 0.0f };
        glm::quat targetRot{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 targetFwd{ 0.0f, 0.0f, 1.0f };
        glm::vec3 targetVelocity{ 0.0f };
        float targetSpeedKmh = 0.0f;
        float engineRPM = 1000.0f;
        float throttleInput = 0.0f;
        Axle::BodyID vehicleBodyID;

        if (target.HasComponent<Axle::TransformComponent>()) {
            const auto& tc = target.GetComponent<Axle::TransformComponent>();
            targetPos = tc.Translation;
            targetRot = tc.Rotation;
            targetFwd = tc.Rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        }

        if (runtimeVehicle) {
            targetPos = runtimeVehicle->GetPosition();
            targetFwd = runtimeVehicle->GetForwardVector();
            const auto telemetry = runtimeVehicle->GetTelemetry();
            targetVelocity = telemetry.LinearVelocity;
            targetSpeedKmh = telemetry.SpeedKmh;
            engineRPM = telemetry.EngineRPM;
            vehicleBodyID = runtimeVehicle->GetChassisBodyID();
        }

        if (target.HasComponent<Axle::VehicleComponent>()) {
            const auto& vc = target.GetComponent<Axle::VehicleComponent>();
            throttleInput = vc.Throttle;
            if (!runtimeVehicle) {
                targetSpeedKmh = vc.CurrentSpeedKmh;
                engineRPM = vc.CurrentRPM;
            }
        }

        // 4. Mouse Capture & Look Processing (Unreal Engine & GTA V Style)
        const auto [mouseX, mouseY] = Axle::Input::GetMousePosition();
        const glm::vec2 currentMousePos{ mouseX, mouseY };

        if (m_FirstMouseUpdate) {
            m_LastMousePos = currentMousePos;
            m_FirstMouseUpdate = false;
        }

        bool isCursorLocked = (Axle::Input::GetCursorMode() == Axle::CursorMode::Locked);

        const bool isShiftDown = Axle::Input::IsKeyPressed(Axle::KeyCode::LeftShift) || Axle::Input::IsKeyPressed(Axle::KeyCode::RightShift);
        const bool isF1Down = Axle::Input::IsKeyPressed(Axle::KeyCode::F1);
        const bool isEscapeDown = Axle::Input::IsKeyPressed(Axle::KeyCode::Escape);

        // Unreal Engine & GTA style:
        // 1. Click LMB inside Viewport captures mouse cursor into game mode
        if (ClickToLock && !isCursorLocked) {
            if (Axle::Input::IsViewportHovered() && Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonLeft)) {
                Axle::Input::SetCursorMode(Axle::CursorMode::Locked);
                isCursorLocked = true;
                m_LastMousePos = currentMousePos;
                m_AutoAlignTimer = 10.0f;
            }
        } 
        // 2. Escape or Shift+F1 (Unreal Engine PIE standard) releases cursor to Editor UI
        else if (isCursorLocked) {
            const bool requestUnlock = (UnlockOnEscape && isEscapeDown) || (UnlockOnShiftF1 && isShiftDown && isF1Down);
            if (requestUnlock) {
                Axle::Input::SetCursorMode(Axle::CursorMode::Normal);
                isCursorLocked = false;
            }
        }

        // When cursor is locked, any mouse movement free-looks / orbits around the car (GTA V style)
        // If ClickToLock is disabled, fallback to legacy RMB drag
        const bool isFreeLooking = isCursorLocked || (!ClickToLock && Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight));
        glm::vec2 mouseDelta{ 0.0f, 0.0f };
        if (isFreeLooking) {
            if (!m_WasDragging) {
                m_LastMousePos = currentMousePos;
                m_WasDragging = true;
            }
            mouseDelta = currentMousePos - m_LastMousePos;
        } else {
            m_WasDragging = false;
        }
        m_LastMousePos = currentMousePos;

        // Auto-align timer: resets when player actively moves mouse
        if (glm::length(mouseDelta) > 0.02f) {
            m_AutoAlignTimer = 0.0f;
        } else {
            m_AutoAlignTimer += ts.GetSeconds();
        }

        const bool isHoldingRMB = Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight);

        // 5. Compute Mode-Specific Target Transform & Dynamic FOV
        glm::vec3 targetModePos{ 0.0f };
        glm::quat targetModeRot{ 1.0f, 0.0f, 0.0f, 0.0f };
        float targetFOV = BaseFOV;

        const glm::vec3 lookTarget = targetPos + glm::vec3(0.0f, HeightOffset, 0.0f);

        switch (m_CurrentMode) {
            // =========================================================================
            // MODE 0: SPRING-ARM THIRD-PERSON CHASE CAMERA
            // =========================================================================
            case VehicleCameraMode::ThirdPersonChase: {
                // Dynamic FOV expansion proportional to vehicle speed (with GTA V RMB Focus Zoom)
                const float speedRatio = std::clamp(targetSpeedKmh / MaxSpeedForFOV, 0.0f, 1.0f);
                targetFOV = BaseFOV + speedRatio * SpeedFOVExpansion;
                if (isCursorLocked && EnableRMBZoom && isHoldingRMB) {
                    targetFOV = RMBZoomFOV;
                }

                // Mouse look orbit (GTA V style: active when free-looking and moving mouse)
                if (isFreeLooking && glm::length(mouseDelta) > 0.0f) {
                    m_OrbitYaw -= mouseDelta.x * MouseSensitivity;
                    m_OrbitPitch -= mouseDelta.y * MouseSensitivity;
                    m_OrbitPitch = std::clamp(m_OrbitPitch, -30.0f, 75.0f);
                }

                while (m_OrbitYaw < -180.0f) m_OrbitYaw += 360.0f;
                while (m_OrbitYaw > 180.0f) m_OrbitYaw -= 360.0f;

                // Auto-alignment behind car when driving forward (GTA V style: blends back when mouse is idle)
                const bool canAutoAlign = (m_AutoAlignTimer >= AutoAlignDelay);
                if (canAutoAlign && targetSpeedKmh > 1.5f && AutoAlignSpeed > 0.0f) {
                    const float targetYaw = glm::degrees(std::atan2(-targetFwd.x, -targetFwd.z));
                    float diff = targetYaw - m_OrbitYaw;
                    while (diff < -180.0f) diff += 360.0f;
                    while (diff > 180.0f) diff -= 360.0f;
                    m_OrbitYaw += diff * std::min(1.0f, ts.GetSeconds() * AutoAlignSpeed);
                    while (m_OrbitYaw < -180.0f) m_OrbitYaw += 360.0f;
                    while (m_OrbitYaw > 180.0f) m_OrbitYaw -= 360.0f;
                }

                // Ideal spherical orbit arm position
                const float radYaw = glm::radians(m_OrbitYaw);
                const float radPitch = glm::radians(m_OrbitPitch);

                glm::vec3 idealPos;
                idealPos.x = lookTarget.x + std::sin(radYaw) * std::cos(radPitch) * Distance;
                idealPos.y = lookTarget.y + std::sin(radPitch) * Distance + ElevationOffset;
                idealPos.z = lookTarget.z + std::cos(radYaw) * std::cos(radPitch) * Distance;

                if (!m_HasInitialPose) {
                    m_SmoothedPos = idealPos;
                    m_HasInitialPose = true;
                } else {
                    // Spring-arm position lag interpolation
                    m_SmoothedPos = glm::mix(m_SmoothedPos, idealPos, std::min(1.0f, ts.GetSeconds() * PositionLagSpeed));
                }

                // Environment Raycast Collision Detection (Anti-Clipping)
                if (EnableAntiClipping) {
                    Axle::PhysicsScene* physicsScene = scene->GetPhysicsScene();
                    if (physicsScene) {
                        glm::vec3 rayDir = m_SmoothedPos - lookTarget;
                        const float rayDist = glm::length(rayDir);
                        if (rayDist > 0.01f) {
                            rayDir /= rayDist;
                            Axle::RayCastHit hit;
                            if (physicsScene->CastRay(lookTarget, rayDir, rayDist, hit)) {
                                if (hit.HitBodyID != vehicleBodyID) {
                                    const float safeDist = std::max(0.8f, hit.Distance - 0.35f);
                                    m_SmoothedPos = lookTarget + rayDir * safeDist;
                                }
                            }
                        }
                    }
                }
                m_SmoothedPos.y = std::max(MinElevation, m_SmoothedPos.y);

                // Look direction towards vehicle target
                const glm::vec3 lookDir = glm::normalize(lookTarget - m_SmoothedPos);
                const float pitch = glm::degrees(std::asin(std::clamp(lookDir.y, -0.999f, 0.999f)));
                const float yaw = glm::degrees(std::atan2(-lookDir.x, -lookDir.z));

                targetModePos = m_SmoothedPos;
                targetModeRot = glm::quat(glm::vec3(glm::radians(pitch), glm::radians(yaw), 0.0f));
                break;
            }

            // =========================================================================
            // MODE 1: COCKPIT / INTERIOR FIRST-PERSON CAMERA (DRIVER POV)
            // =========================================================================
            case VehicleCameraMode::Cockpit: {
                targetFOV = BaseFOV + 8.0f; // Wide cockpit field of view
                if (isCursorLocked && EnableRMBZoom && isHoldingRMB) {
                    targetFOV = RMBZoomFOV;
                }

                // Glance look (left/right/mirrors)
                if (isFreeLooking && glm::length(mouseDelta) > 0.0f) {
                    m_CockpitYaw -= mouseDelta.x * MouseSensitivity;
                    m_CockpitPitch -= mouseDelta.y * MouseSensitivity;
                    m_CockpitYaw = std::clamp(m_CockpitYaw, -85.0f, 85.0f);
                    m_CockpitPitch = std::clamp(m_CockpitPitch, -35.0f, 35.0f);
                } else if (m_AutoAlignTimer >= AutoAlignDelay) {
                    m_CockpitYaw = glm::mix(m_CockpitYaw, 0.0f, std::min(1.0f, ts.GetSeconds() * 4.0f));
                    m_CockpitPitch = glm::mix(m_CockpitPitch, 0.0f, std::min(1.0f, ts.GetSeconds() * 4.0f));
                }

                // G-Force Head Inertia Lag
                if (EnableHeadInertia && ts.GetSeconds() > 1e-4f) {
                    const glm::vec3 worldAccel = (targetVelocity - m_PrevVehicleVelocity) / ts.GetSeconds();
                    glm::vec3 localAccel = glm::inverse(targetRot) * worldAccel;
                    localAccel = glm::clamp(localAccel, glm::vec3(-40.0f), glm::vec3(40.0f));

                    // Accel push back (-Z), braking lunge (+Z), centrifugal lateral sway (-X), roll
                    const glm::vec3 targetInertiaPos(
                        -localAccel.x * 0.0022f,
                        -localAccel.y * 0.0012f,
                        -localAccel.z * 0.0030f
                    );
                    const glm::vec3 targetInertiaRot(
                        -localAccel.z * 0.0010f,
                        0.0f,
                        localAccel.x * 0.0018f
                    );

                    m_HeadInertiaPos = glm::mix(m_HeadInertiaPos, targetInertiaPos, std::min(1.0f, ts.GetSeconds() * 12.0f));
                    m_HeadInertiaRot = glm::mix(m_HeadInertiaRot, targetInertiaRot, std::min(1.0f, ts.GetSeconds() * 12.0f));
                }

                // Engine Vibration Tremor
                glm::vec3 vibeOffset{ 0.0f };
                if (EnableVibration) {
                    m_VibeTimer += ts.GetSeconds();
                    const float vibeFreq = std::clamp(engineRPM / 60.0f, 15.0f, 120.0f) * 6.28318f;
                    const float vibeAmp = 0.0012f + throttleInput * 0.0020f;
                    vibeOffset = glm::vec3(
                        std::sin(m_VibeTimer * vibeFreq * 1.3f) * 0.4f,
                        std::sin(m_VibeTimer * vibeFreq) * 1.0f,
                        std::cos(m_VibeTimer * vibeFreq * 0.8f) * 0.5f
                    ) * vibeAmp;
                }

                const glm::vec3 localHead = glm::vec3(DriverHeadX, DriverHeadY, DriverHeadZ) + m_HeadInertiaPos + vibeOffset;
                targetModePos = targetPos + targetRot * localHead;

                const glm::quat glanceRot = glm::quat(glm::vec3(
                    glm::radians(m_CockpitPitch) + m_HeadInertiaRot.x,
                    glm::radians(m_CockpitYaw) + m_HeadInertiaRot.y,
                    m_HeadInertiaRot.z
                ));
                targetModeRot = targetRot * glanceRot;
                break;
            }

            // =========================================================================
            // MODE 2: HOOD / BUMPER CAMERA
            // =========================================================================
            case VehicleCameraMode::Hood: {
                // High sense of speed FOV
                const float speedRatio = std::clamp(targetSpeedKmh / MaxSpeedForFOV, 0.0f, 1.0f);
                targetFOV = BaseFOV + speedRatio * (SpeedFOVExpansion * 1.15f);
                if (isCursorLocked && EnableRMBZoom && isHoldingRMB) {
                    targetFOV = RMBZoomFOV;
                }

                const glm::vec3 hoodOffset(HoodOffsetX, HoodOffsetY, HoodOffsetZ);
                targetModePos = targetPos + targetRot * hoodOffset;

                // Slight downward tilt (-2.5 deg) for direct pavement visibility
                const glm::quat tiltRot = glm::quat(glm::vec3(glm::radians(-2.5f), 0.0f, 0.0f));
                targetModeRot = targetRot * tiltRot;
                break;
            }

            // =========================================================================
            // MODE 3: FREE ORBIT / REPLAY CAMERA
            // =========================================================================
            case VehicleCameraMode::FreeOrbit: {
                targetFOV = BaseFOV;

                // Mouse wheel zoom or keyboard (+ / - / PageUp / PageDown)
                const float scroll = Axle::Input::GetMouseScrollY();
                if (std::abs(scroll) > 0.01f) {
                    FreeOrbitDistance = std::clamp(FreeOrbitDistance - scroll * 1.25f, 2.0f, 30.0f);
                }
                if (Axle::Input::IsKeyPressed(Axle::KeyCode::Equal) || Axle::Input::IsKeyPressed(Axle::KeyCode::PageUp)) {
                    FreeOrbitDistance = std::clamp(FreeOrbitDistance - ts.GetSeconds() * 8.0f, 2.0f, 30.0f);
                }
                if (Axle::Input::IsKeyPressed(Axle::KeyCode::Minus) || Axle::Input::IsKeyPressed(Axle::KeyCode::PageDown)) {
                    FreeOrbitDistance = std::clamp(FreeOrbitDistance + ts.GetSeconds() * 8.0f, 2.0f, 30.0f);
                }

                // 360-degree free orbit without snapping
                if (isFreeLooking && glm::length(mouseDelta) > 0.0f) {
                    m_FreeOrbitYaw -= mouseDelta.x * MouseSensitivity;
                    m_FreeOrbitPitch -= mouseDelta.y * MouseSensitivity;
                    m_FreeOrbitPitch = std::clamp(m_FreeOrbitPitch, -45.0f, 85.0f);
                }

                while (m_FreeOrbitYaw < -180.0f) m_FreeOrbitYaw += 360.0f;
                while (m_FreeOrbitYaw > 180.0f) m_FreeOrbitYaw -= 360.0f;

                const float radYaw = glm::radians(m_FreeOrbitYaw);
                const float radPitch = glm::radians(m_FreeOrbitPitch);

                targetModePos.x = lookTarget.x + std::sin(radYaw) * std::cos(radPitch) * FreeOrbitDistance;
                targetModePos.y = std::max(MinElevation, lookTarget.y + std::sin(radPitch) * FreeOrbitDistance + ElevationOffset * 0.5f);
                targetModePos.z = lookTarget.z + std::cos(radYaw) * std::cos(radPitch) * FreeOrbitDistance;

                const glm::vec3 lookDir = glm::normalize(lookTarget - targetModePos);
                const float pitch = glm::degrees(std::asin(std::clamp(lookDir.y, -0.999f, 0.999f)));
                const float yaw = glm::degrees(std::atan2(-lookDir.x, -lookDir.z));
                targetModeRot = glm::quat(glm::vec3(glm::radians(pitch), glm::radians(yaw), 0.0f));
                break;
            }

            default:
                break;
        }

        // 6. Seamless Transition Interpolation between camera modes
        glm::vec3 finalPos = targetModePos;
        glm::quat finalRot = targetModeRot;

        if (m_IsTransitioning) {
            m_TransitionTimer += ts.GetSeconds();
            const float progress = std::clamp(m_TransitionTimer / m_TransitionDuration, 0.0f, 1.0f);
            const float smoothT = progress * progress * (3.0f - 2.0f * progress); // Hermite smoothstep
            finalPos = glm::mix(m_TransitionStartPos, targetModePos, smoothT);
            finalRot = glm::slerp(m_TransitionStartRot, targetModeRot, smoothT);
            if (progress >= 1.0f) {
                m_IsTransitioning = false;
            }
        }

        // 7. Update Camera Entity Transform & Live Dynamic FOV
        if (HasComponent<Axle::TransformComponent>()) {
            auto& tc = GetComponent<Axle::TransformComponent>();
            tc.Translation = finalPos;
            tc.Rotation = finalRot;
        }

        if (HasComponent<Axle::CameraComponent>()) {
            auto& cam = GetComponent<Axle::CameraComponent>();
            cam.FOV = glm::mix(cam.FOV, targetFOV, std::min(1.0f, ts.GetSeconds() * 6.0f));
        }

        // 8. Acoustic Insulation (Interior Cockpit vs Exterior Roar)
        if (target && target.HasComponent<Axle::VehicleAudioComponent>()) {
            auto& vac = target.GetComponent<Axle::VehicleAudioComponent>();
            const float targetVol = (m_CurrentMode == VehicleCameraMode::Cockpit) ? 0.55f : 0.85f;
            float newVol = glm::mix(vac.EngineMasterVolume, targetVol, std::min(1.0f, ts.GetSeconds() * 8.0f));
            vac.SetEngineMasterVolume(newVol);
        }

        // 9. Update history for next frame acceleration derivative
        m_PrevVehicleVelocity = targetVelocity;
    }

    void OnDestroy() override {
        if (Axle::Input::GetCursorMode() == Axle::CursorMode::Locked) {
            Axle::Input::SetCursorMode(Axle::CursorMode::Normal);
        }
        AXLE_INFO("VehicleChaseCamera: Detached from entity '{0}'", GetEntityName());
    }

    void SetCameraMode(VehicleCameraMode newMode) {
        if (m_CurrentMode == newMode) return;
        if (HasComponent<Axle::TransformComponent>()) {
            const auto& tc = GetComponent<Axle::TransformComponent>();
            m_TransitionStartPos = tc.Translation;
            m_TransitionStartRot = tc.Rotation;
            m_IsTransitioning = true;
            m_TransitionTimer = 0.0f;
        }
        m_CurrentMode = newMode;
        CameraMode = static_cast<int>(newMode);
        AXLE_INFO("VehicleChaseCamera: Switched to camera mode {0}", GetModeName(newMode));
    }

    void CycleDistancePreset() {
        DistanceLevel = (DistanceLevel + 1) % 3;
        ApplyDistancePreset(DistanceLevel);
    }

    void ApplyDistancePreset(int level) {
        DistanceLevel = std::clamp(level, 0, 2);
        switch (DistanceLevel) {
            case 0: // Near (Gần)
                Distance = NearDistance;
                ElevationOffset = 1.15f;
                HeightOffset = 1.05f;
                AXLE_INFO("VehicleChaseCamera: Distance set to Near ({0:.1f}m)", Distance);
                break;
            case 1: // Medium (Vừa)
                Distance = MediumDistance;
                ElevationOffset = 1.45f;
                HeightOffset = 1.15f;
                AXLE_INFO("VehicleChaseCamera: Distance set to Medium ({0:.1f}m)", Distance);
                break;
            case 2: // Far (Xa)
                Distance = FarDistance;
                ElevationOffset = 1.95f;
                HeightOffset = 1.35f;
                AXLE_INFO("VehicleChaseCamera: Distance set to Far ({0:.1f}m)", Distance);
                break;
        }
        if (m_CurrentMode != VehicleCameraMode::ThirdPersonChase) {
            SetCameraMode(VehicleCameraMode::ThirdPersonChase);
        }
    }

    VehicleCameraMode GetCameraMode() const { return m_CurrentMode; }
    bool IsTransitioning() const { return m_IsTransitioning; }

    static const char* GetModeName(VehicleCameraMode mode) {
        switch (mode) {
            case VehicleCameraMode::ThirdPersonChase: return "Third-Person Chase";
            case VehicleCameraMode::Cockpit:          return "Cockpit (Driver POV)";
            case VehicleCameraMode::Hood:             return "Hood / Bumper";
            case VehicleCameraMode::FreeOrbit:        return "Free Orbit / Replay";
            default:                                  return "Unknown";
        }
    }

    std::string GetEntityName() const {
        return HasComponent<Axle::TagComponent>() ? GetComponent<Axle::TagComponent>().Tag : "Camera";
    }

    AXLE_SCRIPT_FIELDS(VehicleChaseCamera,
        AXLE_FIELD(ClickToLock, "Click LMB in Viewport to capture/hide mouse"),
        AXLE_FIELD(UnlockOnEscape, "Press Escape to release/show mouse"),
        AXLE_FIELD(UnlockOnShiftF1, "Press Shift+F1 (UE PIE standard) to release/show mouse"),
        AXLE_FIELD(EnableRMBZoom, "Hold RMB to zoom in / cinematic focus (GTA style)"),
        AXLE_FIELD(RMBZoomFOV, 20.0f, 60.0f, "Target FOV when holding RMB focus"),
        AXLE_FIELD(AutoAlignDelay, 0.0f, 5.0f, "Seconds to wait before auto-aligning behind vehicle"),
        AXLE_FIELD(DistanceLevel, 0, 2, "Distance Preset (0: Near, 1: Medium, 2: Far)"),
        AXLE_FIELD(NearDistance, 2.0f, 15.0f, "Near distance preset in meters"),
        AXLE_FIELD(MediumDistance, 3.0f, 20.0f, "Medium distance preset in meters"),
        AXLE_FIELD(FarDistance, 5.0f, 30.0f, "Far distance preset in meters"),
        AXLE_FIELD(CameraMode, 0, 3, "Camera Mode (0: Chase, 1: Cockpit, 2: Hood, 3: Orbit)"),
        AXLE_FIELD(Distance, 2.0f, 30.0f, "Distance from chase target"),
        AXLE_FIELD(HeightOffset, -2.0f, 5.0f, "Height offset of focus point above target"),
        AXLE_FIELD(MinElevation, 0.0f, 5.0f, "Minimum camera height above ground"),
        AXLE_FIELD(ElevationOffset, 0.0f, 5.0f, "Vertical lift added to orbit position"),
        AXLE_FIELD(MouseSensitivity, 0.05f, 2.0f, "Mouse drag rotation sensitivity"),
        AXLE_FIELD(AutoAlignSpeed, 0.0f, 15.0f, "Speed to auto-align behind vehicle when driving"),
        AXLE_FIELD(PositionLagSpeed, 1.0f, 25.0f, "Spring-arm position lag interpolation rate"),
        AXLE_FIELD(RotationLagSpeed, 1.0f, 25.0f, "Camera look rotation smoothing rate"),
        AXLE_FIELD(BaseFOV, 30.0f, 120.0f, "Baseline field of view in degrees"),
        AXLE_FIELD(SpeedFOVExpansion, 0.0f, 30.0f, "Max dynamic FOV expansion at high speed"),
        AXLE_FIELD(MaxSpeedForFOV, 50.0f, 300.0f, "Speed (km/h) for maximum dynamic FOV expansion"),
        AXLE_FIELD(DriverHeadX, -2.0f, 2.0f, "Cockpit lateral eye position"),
        AXLE_FIELD(DriverHeadY, 0.0f, 3.0f, "Cockpit eye level height"),
        AXLE_FIELD(DriverHeadZ, -2.0f, 2.0f, "Cockpit longitudinal position"),
        AXLE_FIELD(EnableHeadInertia, "Enable driver head G-force inertia lag"),
        AXLE_FIELD(EnableVibration, "Enable engine RPM vibration tremor"),
        AXLE_FIELD(HoodOffsetX, -2.0f, 2.0f, "Hood camera lateral offset"),
        AXLE_FIELD(HoodOffsetY, 0.0f, 3.0f, "Hood camera vertical offset"),
        AXLE_FIELD(HoodOffsetZ, 0.0f, 5.0f, "Hood camera longitudinal offset"),
        AXLE_FIELD(FreeOrbitDistance, 2.0f, 30.0f, "Orbit distance in Free Orbit mode"),
        AXLE_FIELD(EnableAntiClipping, "Enable physics raycast camera collision prevention")
    )
};

AXLE_REGISTER_SCRIPT(VehicleChaseCamera);
