#include "VFXTestHarness.hpp"
#include <Axle/VFXCommon.hpp>
#include <Axle/ParticlePool.hpp>

namespace {
    bool TestBaselineVFXTypes() {
        Axle::ParticlePool pool(100);
        VFX_TEST_CHECK(pool.GetCapacity() == 100, "Pool capacity initialization");
        VFX_TEST_CHECK(pool.GetActiveCount() == 0, "Initial pool count should be zero");
        return true;
    }
}

bool RunVFXDefinitionUnitTests() {
    std::cout << "  - Running TestBaselineVFXTypes ...\n";
    VFX_TEST_CHECK(TestBaselineVFXTypes(), "TestBaselineVFXTypes failed");
    return true;
}
