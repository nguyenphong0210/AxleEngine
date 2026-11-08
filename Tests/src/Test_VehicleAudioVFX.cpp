#include "AxleVehicle/VehicleComponents.hpp"
#include "AxleVehicle/VehicleAudioAdapter.hpp"
#include "AxleVehicle/VehicleVFXAdapter.hpp"

#include <iostream>
#include <cassert>
#include <cmath>

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "CHECK FAILED: " #expr " at " __FILE__ ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while (false)

using namespace AxleVehicle;

int main(int argc, char** argv) {
    std::cout << "[Test_VehicleAudioVFX] Starting PLG-18 vehicle audio and VFX integration verification..." << std::endl;

    // 1. Verify Audio Synthesis Across Engine RPM Range
    std::cout << "  - Testing engine RPM audio pitch synthesis..." << std::endl;
    {
        VehicleAudioConfig audioConfig;
        audioConfig.IdleRPM = 1000.0f;
        audioConfig.MaxRPM = 7000.0f;
        audioConfig.MinPitch = 0.6f;
        audioConfig.MaxPitch = 2.4f;
        audioConfig.MasterVolume = 0.8f;

        VehicleAudioAdapter audio(audioConfig);
        audio.Start();
        CHECK(audio.IsActive());

        ChassisRuntimeState chassis{};
        WheelRuntimeState wheels[4]{};

        // Idle RPM
        chassis.CurrentRPM = 1000.0f;
        chassis.Throttle = 0.0f;
        audio.Update(0.016f, chassis, wheels);
        CHECK(std::abs(audio.GetEnginePitch() - 0.6f) < 1e-4f);
        CHECK(std::abs(audio.GetEngineVolume() - (0.8f * 0.35f)) < 1e-4f);

        // Mid RPM (4000 RPM -> 50% fraction -> pitch = 0.6 + 1.8 * 0.5 = 1.5)
        chassis.CurrentRPM = 4000.0f;
        chassis.Throttle = 0.5f;
        audio.Update(0.016f, chassis, wheels);
        CHECK(std::abs(audio.GetEnginePitch() - 1.5f) < 1e-4f);
        CHECK(audio.GetEngineVolume() > 0.8f * 0.35f);

        // Redline RPM (7000 RPM -> pitch = 2.4)
        chassis.CurrentRPM = 7000.0f;
        chassis.Throttle = 1.0f;
        audio.Update(0.016f, chassis, wheels);
        CHECK(std::abs(audio.GetEnginePitch() - 2.4f) < 1e-4f);
        CHECK(std::abs(audio.GetEngineVolume() - 0.8f) < 1e-4f);

        // RPM clamping beyond redline
        chassis.CurrentRPM = 8500.0f;
        audio.Update(0.016f, chassis, wheels);
        CHECK(std::abs(audio.GetEnginePitch() - 2.4f) < 1e-4f);
    }

    // 2. Verify Tire Squeal Dynamics
    std::cout << "  - Testing tire squeal acoustic fade..." << std::endl;
    {
        VehicleAudioConfig audioConfig;
        audioConfig.TireSlipThreshold = 0.25f;

        VehicleAudioAdapter audio(audioConfig);
        audio.Start();

        ChassisRuntimeState chassis{};
        WheelRuntimeState wheels[4]{};

        // Normal driving without slip
        for (int i = 0; i < 4; ++i) {
            wheels[i].IsOnGround = true;
            wheels[i].LongitudinalSlip = 0.05f;
            wheels[i].LateralSlip = 0.02f;
        }
        audio.Update(0.1f, chassis, wheels);
        CHECK(audio.GetTireSquealVolume() == 0.0f);

        // Aggressive burnout / lateral slide on FL and FR
        wheels[0].LongitudinalSlip = 0.45f;
        wheels[1].LateralSlip = 0.35f;
        for (int step = 0; step < 10; ++step) {
            audio.Update(0.05f, chassis, wheels);
        }
        CHECK(audio.GetTireSquealVolume() > 0.5f);

        // Slide ends -> squeal fades out
        wheels[0].LongitudinalSlip = 0.05f;
        wheels[1].LateralSlip = 0.02f;
        for (int step = 0; step < 30; ++step) {
            audio.Update(0.05f, chassis, wheels);
        }
        CHECK(audio.GetTireSquealVolume() < 0.05f);

        // Airborne slip produces no tire squeal
        wheels[0].LongitudinalSlip = 0.8f;
        wheels[0].IsOnGround = false;
        audio.Update(0.05f, chassis, wheels);
        CHECK(audio.GetTireSquealVolume() == 0.0f);
    }

    // 3. Verify Audio Lifecycle & Missing Asset Handling
    std::cout << "  - Testing audio lifecycle and missing asset resilience..." << std::endl;
    {
        VehicleAudioConfig audioConfig;
        audioConfig.EngineAudioClipPath = "assets/audio/missing_engine_sound.wav"; // Missing asset

        VehicleAudioAdapter audio(audioConfig);
        audio.Start();
        CHECK(audio.IsActive());

        audio.Stop();
        CHECK(!audio.IsActive());
        CHECK(audio.GetEngineVolume() == 0.0f);

        audio.Reset();
        CHECK(audio.IsActive());
    }

    // 4. Verify VFX Wheel Skid Smoke
    std::cout << "  - Testing wheel burnout and skid VFX..." << std::endl;
    {
        VehicleVFXConfig vfxConfig;
        vfxConfig.BurnoutSlipThreshold = 0.30f;
        vfxConfig.SkidSlipThreshold = 0.25f;

        VehicleVFXAdapter vfx(vfxConfig);
        vfx.Start();
        CHECK(vfx.IsActive());

        ChassisRuntimeState chassis{};
        WheelRuntimeState wheels[4]{};

        for (int i = 0; i < 4; ++i) {
            wheels[i].IsOnGround = true;
            wheels[i].LongitudinalSlip = 0.1f;
            wheels[i].LateralSlip = 0.05f;
        }

        vfx.Update(0.016f, chassis, wheels);
        for (int i = 0; i < 4; ++i) {
            CHECK(!vfx.IsWheelSmokeActive(i));
        }

        // RL (wheel 2) and RR (wheel 3) burnout lockup
        wheels[2].LongitudinalSlip = 0.65f;
        wheels[3].LateralSlip = 0.40f;
        vfx.Update(0.016f, chassis, wheels);

        CHECK(!vfx.IsWheelSmokeActive(0));
        CHECK(!vfx.IsWheelSmokeActive(1));
        CHECK(vfx.IsWheelSmokeActive(2));
        CHECK(vfx.IsWheelSmokeActive(3));

        // Lift wheel into air -> stops smoke
        wheels[2].IsOnGround = false;
        vfx.Update(0.016f, chassis, wheels);
        CHECK(!vfx.IsWheelSmokeActive(2));
    }

    // 5. Verify VFX Exhaust Backfire on Throttle Lift-Off
    std::cout << "  - Testing exhaust backfire burst on high-RPM throttle lift-off..." << std::endl;
    {
        VehicleVFXConfig vfxConfig;
        vfxConfig.BackfireMinRPM = 5000.0f;
        vfxConfig.BackfireHighThrottleMin = 0.60f;
        vfxConfig.BackfireLiftThreshold = 0.15f;
        vfxConfig.BackfireCooldown = 0.40f;

        VehicleVFXAdapter vfx(vfxConfig);
        vfx.Start();

        ChassisRuntimeState chassis{};
        WheelRuntimeState wheels[4]{};

        // Cruise at full throttle, high RPM
        chassis.Throttle = 1.0f;
        chassis.CurrentRPM = 6200.0f;
        vfx.Update(0.05f, chassis, wheels);
        CHECK(!vfx.IsBackfireActive());
        CHECK(vfx.GetBackfireBurstCount() == 0);

        // Sudden lift-off!
        chassis.Throttle = 0.0f;
        vfx.Update(0.05f, chassis, wheels);
        CHECK(vfx.IsBackfireActive());
        CHECK(vfx.GetBackfireBurstCount() == 1);

        // Cooldown prevents immediate re-trigger
        chassis.Throttle = 1.0f;
        vfx.Update(0.05f, chassis, wheels);
        chassis.Throttle = 0.0f;
        vfx.Update(0.05f, chassis, wheels);
        CHECK(vfx.GetBackfireBurstCount() == 1); // Still 1 due to cooldown!

        // Advance past cooldown
        for (int i = 0; i < 10; ++i) {
            vfx.Update(0.05f, chassis, wheels); // 0.5s elapsed > 0.4s cooldown
        }
        CHECK(!vfx.IsBackfireActive());

        // Re-trigger after cooldown
        chassis.Throttle = 0.8f;
        vfx.Update(0.05f, chassis, wheels);
        chassis.Throttle = 0.0f;
        vfx.Update(0.05f, chassis, wheels);
        CHECK(vfx.IsBackfireActive());
        CHECK(vfx.GetBackfireBurstCount() == 2);
    }

    // 6. Verify VFX Lifecycle & Teardown
    std::cout << "  - Testing VFX lifecycle stop and reset..." << std::endl;
    {
        VehicleVFXAdapter vfx;
        vfx.Start();
        vfx.Stop();
        CHECK(!vfx.IsActive());
        for (int i = 0; i < 4; ++i) CHECK(!vfx.IsWheelSmokeActive(i));
        CHECK(!vfx.IsBackfireActive());

        vfx.Reset();
        CHECK(vfx.IsActive());
    }

    std::cout << "[Test_VehicleAudioVFX] ALL CHECKS PASSED!" << std::endl;
    return 0;
}
