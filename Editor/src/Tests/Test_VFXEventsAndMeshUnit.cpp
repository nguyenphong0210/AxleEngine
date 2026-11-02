#include "VFXTestHarness.hpp"
#include <Axle/VFXCommon.hpp>

namespace {
    bool TestBaselineEventsAndMesh() {
        // Baseline test for sub-emitter triggers
        Axle::SubEmitterTrigger birth = Axle::SubEmitterTrigger::Birth;
        Axle::SubEmitterTrigger collision = Axle::SubEmitterTrigger::Collision;
        Axle::SubEmitterTrigger death = Axle::SubEmitterTrigger::Death;
        VFX_TEST_CHECK(static_cast<int>(birth) == 0, "Birth trigger index");
        VFX_TEST_CHECK(static_cast<int>(collision) == 1, "Collision trigger index");
        VFX_TEST_CHECK(static_cast<int>(death) == 2, "Death trigger index");
        return true;
    }
}

bool RunVFXEventsAndMeshUnitTests() {
    std::cout << "  - Running TestBaselineEventsAndMesh ...\n";
    VFX_TEST_CHECK(TestBaselineEventsAndMesh(), "TestBaselineEventsAndMesh failed");
    return true;
}
