#include "VFXTestHarness.hpp"
#include <Axle/ParticleSystem.hpp>

namespace {
    bool TestBaselineSystemIsolation() {
        Axle::ParticleEmitterConfig cfg;
        cfg.MaxParticles = 100;
        cfg.EmissionRate = 50.0f;

        Axle::ParticleSystem systemA(cfg);
        Axle::ParticleSystem systemB(cfg);

        systemA.SetEmitting(true);
        systemA.Update(0.1f);

        VFX_TEST_CHECK(systemA.GetActiveParticleCount() > 0, "System A should have emitted particles");
        VFX_TEST_CHECK(systemB.GetActiveParticleCount() == 0, "System B must remain unaffected by System A");
        return true;
    }
}

bool RunVFXPreviewIsolationUnitTests() {
    std::cout << "  - Running TestBaselineSystemIsolation ...\n";
    VFX_TEST_CHECK(TestBaselineSystemIsolation(), "TestBaselineSystemIsolation failed");
    return true;
}
