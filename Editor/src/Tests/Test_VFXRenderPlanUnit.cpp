#include "VFXTestHarness.hpp"
#include <Axle/ParticleRenderer.hpp>

namespace {
    bool TestBaselineParticleVertexStride() {
        // Verify ParticleInstanceVertex stride matches 84 bytes as documented
        VFX_TEST_CHECK(sizeof(Axle::ParticleInstanceVertex) == 84, "ParticleInstanceVertex must be exactly 84 bytes");
        return true;
    }
}

bool RunVFXRenderPlanUnitTests() {
    std::cout << "  - Running TestBaselineParticleVertexStride ...\n";
    VFX_TEST_CHECK(TestBaselineParticleVertexStride(), "TestBaselineParticleVertexStride failed");
    return true;
}
