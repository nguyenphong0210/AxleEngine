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
}

bool RunVFXPlaybackUnitTests() {
    std::cout << "  - Running TestBaselinePlayback ...\n";
    VFX_TEST_CHECK(TestBaselinePlayback(), "TestBaselinePlayback failed");
    return true;
}
