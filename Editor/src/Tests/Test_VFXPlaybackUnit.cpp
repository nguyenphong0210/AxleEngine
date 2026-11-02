#include "VFXTestHarness.hpp"
#include <Axle/ParticleSystem.hpp>

namespace {
    bool TestBaselinePlayback() {
        Axle::ParticleEmitterConfig cfg;
        cfg.MaxParticles = 100;
        cfg.EmissionRate = 20.0f;
        cfg.Duration = 2.0f;
        cfg.Looping = false;

        Axle::ParticleSystem system(cfg);
        system.SetEmitting(true);
        system.Update(0.5f);
        VFX_TEST_CHECK(system.GetActiveParticleCount() > 0, "Playback active particles");
        return true;
    }

    bool TestDurationAndNonLoopingDrain() {
        Axle::ParticleEmitterConfig cfg;
        cfg.MaxParticles = 500;
        cfg.EmissionRate = 50.0f;
        cfg.Duration = 1.0f;
        cfg.Looping = false;
        cfg.Lifetime = { 2.0f, 2.0f }; // Long particle lifetime relative to duration

        Axle::ParticleSystem system(cfg);
        system.SetEmitting(true);

        // Within duration: emitting
        system.Update(0.4f);
        uint32_t count1 = system.GetActiveParticleCount();
        VFX_TEST_CHECK(count1 > 0, "Particles spawned during active duration");

        // Pass duration boundary: t = 1.2s
        system.Update(0.8f);
        uint32_t count2 = system.GetActiveParticleCount();
        VFX_TEST_CHECK(count2 >= count1, "Particles continue to live and drain after duration");

        // Advance simulation past duration: no new emissions occur
        system.Update(0.5f); // t = 1.7s
        uint32_t count3 = system.GetActiveParticleCount();
        VFX_TEST_CHECK(count3 <= count2, "No new emissions after duration");

        // Drain beyond particle lifetime: t = 3.5s (> 1.0s duration + 2.0s lifetime)
        system.Update(1.8f);
        uint32_t count4 = system.GetActiveParticleCount();
        VFX_TEST_CHECK(count4 == 0, "All particles drained naturally to 0 after duration + lifetime");

        return true;
    }

    bool TestLoopRollover() {
        Axle::ParticleEmitterConfig cfg;
        cfg.MaxParticles = 500;
        cfg.EmissionRate = 0.0f;
        cfg.Duration = 1.0f;
        cfg.Looping = true;
        cfg.Lifetime = { 3.0f, 3.0f }; // particles live for 3s (across multiple loops)

        // Burst at t=0 of 10 particles
        cfg.Bursts.push_back(Axle::ParticleBurst(0.0f, 10, 10, 1, 0.0f));

        Axle::ParticleSystem system(cfg);
        system.SetEmitting(true);

        // First step: fires t=0 burst
        system.Update(0.5f);
        VFX_TEST_CHECK(system.GetActiveParticleCount() == 10, "Cycle 1 burst fired 10 particles");

        // Step within same cycle: no extra burst
        system.Update(0.4f);
        VFX_TEST_CHECK(system.GetActiveParticleCount() == 10, "No duplicate burst within cycle");

        // Step crossing loop boundary (0.9s -> 1.1s, wraps to 0.1s): cycle 2 rollover
        system.Update(0.2f);
        VFX_TEST_CHECK(system.GetActiveParticleCount() == 20, "Cycle 2 rollover fired burst, preserving cycle 1 particles");

        return true;
    }

    bool TestBoundedBurstCatchup() {
        Axle::ParticleEmitterConfig cfg;
        cfg.MaxParticles = 1000;
        cfg.EmissionRate = 0.0f;
        cfg.Duration = 10.0f;
        cfg.Looping = false;
        cfg.Lifetime = { 10.0f, 10.0f };

        // Extremely fast interval with infinite cycles: interval 0.001s
        cfg.Bursts.push_back(Axle::ParticleBurst(0.0f, 1, 1, 0, 0.001f));

        Axle::ParticleSystem system(cfg);
        system.SetEmitting(true);

        // Huge time jump of 5 seconds: must be bounded by MAX_BURST_EVENTS_PER_TICK (64)
        system.Update(5.0f);
        VFX_TEST_CHECK(system.GetActiveParticleCount() == 64, "Bounded burst catch-up capped at 64 events");

        return true;
    }

    bool TestDeterministicSeededSpawning() {
        Axle::ParticleEmitterConfig cfg;
        cfg.MaxParticles = 100;
        cfg.EmissionRate = 20.0f;
        cfg.Duration = 5.0f;
        cfg.Looping = true;
        cfg.Shape.Type = Axle::ParticleEmitterShapeType::Sphere;
        cfg.Shape.Radius = 2.0f;

        Axle::ParticleSystem system(cfg);
        system.SetSeed(4242);
        system.SetEmitting(true);

        for (int i = 0; i < 5; ++i) {
            system.Update(0.1f);
        }

        uint32_t count1 = system.GetActiveParticleCount();
        VFX_TEST_CHECK(count1 > 0, "Spawned particles in run 1");
        glm::vec3 pos1 = system.GetPool().GetPositions()[0];
        glm::vec3 vel1 = system.GetPool().GetVelocities()[0];

        // Reset and re-simulate same sequence
        system.Reset();
        for (int i = 0; i < 5; ++i) {
            system.Update(0.1f);
        }

        uint32_t count2 = system.GetActiveParticleCount();
        glm::vec3 pos2 = system.GetPool().GetPositions()[0];
        glm::vec3 vel2 = system.GetPool().GetVelocities()[0];

        VFX_TEST_CHECK(count1 == count2, "Particle counts match after reset and replay");
        VFX_TEST_CHECK(pos1 == pos2, "Particle positions match after reset and replay");
        VFX_TEST_CHECK(vel1 == vel2, "Particle velocities match after reset and replay");

        return true;
    }

    bool TestRNGIsolationAcrossInstances() {
        Axle::ParticleEmitterConfig cfg;
        cfg.MaxParticles = 100;
        cfg.EmissionRate = 20.0f;
        cfg.Shape.Type = Axle::ParticleEmitterShapeType::Box;

        Axle::ParticleSystem sysA(cfg);
        sysA.SetSeed(100);

        Axle::ParticleSystem sysB(cfg);
        sysB.SetSeed(200);

        Axle::ParticleSystem sysA_isolated(cfg);
        sysA_isolated.SetSeed(100);

        // Interleaved execution of A and B
        for (int i = 0; i < 5; ++i) {
            sysA.Update(0.1f);
            sysB.Update(0.1f);
        }

        // Isolated execution of A_isolated alone
        for (int i = 0; i < 5; ++i) {
            sysA_isolated.Update(0.1f);
        }

        VFX_TEST_CHECK(sysA.GetActiveParticleCount() == sysA_isolated.GetActiveParticleCount(), "Interleaved execution preserves count");
        VFX_TEST_CHECK(sysA.GetPool().GetPositions()[0] == sysA_isolated.GetPool().GetPositions()[0], "Interleaved execution preserves positions");
        VFX_TEST_CHECK(sysA.GetPool().GetVelocities()[0] == sysA_isolated.GetPool().GetVelocities()[0], "Interleaved execution preserves velocities");

        // Verify distinct seeds generate distinct streams
        VFX_TEST_CHECK(Axle::HashEmitterSeed(100, "Flame") != Axle::HashEmitterSeed(100, "Smoke"), "HashEmitterSeed generates distinct seeds for distinct emitters");
        VFX_TEST_CHECK(sysA.GetPool().GetPositions()[0] != sysB.GetPool().GetPositions()[0], "Different seeds yield different random streams");

        return true;
    }
}

bool RunVFXPlaybackUnitTests() {
    std::cout << "  - Running TestBaselinePlayback ...\n";
    VFX_TEST_CHECK(TestBaselinePlayback(), "TestBaselinePlayback failed");

    std::cout << "  - Running TestDurationAndNonLoopingDrain ...\n";
    VFX_TEST_CHECK(TestDurationAndNonLoopingDrain(), "TestDurationAndNonLoopingDrain failed");

    std::cout << "  - Running TestLoopRollover ...\n";
    VFX_TEST_CHECK(TestLoopRollover(), "TestLoopRollover failed");

    std::cout << "  - Running TestBoundedBurstCatchup ...\n";
    VFX_TEST_CHECK(TestBoundedBurstCatchup(), "TestBoundedBurstCatchup failed");

    std::cout << "  - Running TestDeterministicSeededSpawning ...\n";
    VFX_TEST_CHECK(TestDeterministicSeededSpawning(), "TestDeterministicSeededSpawning failed");

    std::cout << "  - Running TestRNGIsolationAcrossInstances ...\n";
    VFX_TEST_CHECK(TestRNGIsolationAcrossInstances(), "TestRNGIsolationAcrossInstances failed");

    return true;
}
