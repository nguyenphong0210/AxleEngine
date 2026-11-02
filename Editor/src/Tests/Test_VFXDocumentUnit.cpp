#include "VFXTestHarness.hpp"
#include <Axle/ParticleEmitter.hpp>
#include <Axle/VFXCommon.hpp>

namespace {
    bool TestBaselineDocumentContract() {
        // Baseline test for document model contract
        Axle::ParticleEmitterConfig cfg;
        cfg.Name = "DocumentInitial";
        VFX_TEST_CHECK(cfg.Name == "DocumentInitial", "Baseline document contract");
        return true;
    }
}

bool RunVFXDocumentUnitTests() {
    std::cout << "  - Running TestBaselineDocumentContract ...\n";
    VFX_TEST_CHECK(TestBaselineDocumentContract(), "TestBaselineDocumentContract failed");
    return true;
}
