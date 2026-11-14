#include "RenderTestCommon.hpp"
#include <iostream>
#include <string_view>

bool RunRenderContractsUnitTests();
bool RunRenderBackendPolicyUnitTests();
bool RunRenderPrimitiveIdentityUnitTests();
bool RunRenderWorldExtractionUnitTests();
bool RunRenderServiceUnitTests();
bool RunViewContextUnitTests();
bool RunRenderPassUnitTests();
bool RunResolvedSurfaceUnitTests();
bool RunLightingServiceUnitTests();
namespace Axle::RenderTests { bool RunRenderSnapshotQueueUnitTests(); }
namespace Axle::RenderTests { bool RunRenderWorkerUnitTests(); }
namespace Axle::RenderTests { bool RunRenderParallelRecordingUnitTests(); }
namespace Axle::RenderTests { bool RunRenderTransientGraphUnitTests(); }

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test-failure") {
        std::cout << "[AxleRenderUnitTests] Running intentional self-test failure probe...\n";
        std::cout << "AXLE_TEST_EXPECTED_FAILURE render-probe\n";
        return Axle::RenderTests::Check(false, "Deliberate test harness failure probe (--self-test-failure)", __FILE__, __LINE__) ? 0 : 1;
    }

    std::cout << "========================================================\n";
    std::cout << " AxleRenderUnitTests: Device-free CPU contracts suite\n";
    std::cout << "========================================================\n";

    if (!RunRenderContractsUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunRenderContractsUnitTests FAILED!\n";
        return 1;
    }

    if (!RunRenderBackendPolicyUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunRenderBackendPolicyUnitTests FAILED!\n";
        return 1;
    }

    if (!RunRenderPrimitiveIdentityUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunRenderPrimitiveIdentityUnitTests FAILED!\n";
        return 1;
    }

    if (!RunRenderWorldExtractionUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunRenderWorldExtractionUnitTests FAILED!\n";
        return 1;
    }

    if (!RunRenderServiceUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunRenderServiceUnitTests FAILED!\n";
        return 1;
    }

    if (!RunViewContextUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunViewContextUnitTests FAILED!\n";
        return 1;
    }

    if (!RunRenderPassUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunRenderPassUnitTests FAILED!\n";
        return 1;
    }

    if (!RunResolvedSurfaceUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunResolvedSurfaceUnitTests FAILED!\n";
        return 1;
    }

    if (!RunLightingServiceUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunLightingServiceUnitTests FAILED!\n";
        return 1;
    }

    if (!Axle::RenderTests::RunRenderSnapshotQueueUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunRenderSnapshotQueueUnitTests FAILED!\n";
        return 1;
    }

    if (!Axle::RenderTests::RunRenderWorkerUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunRenderWorkerUnitTests FAILED!\n";
        return 1;
    }

    if (!Axle::RenderTests::RunRenderParallelRecordingUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunRenderParallelRecordingUnitTests FAILED!\n";
        return 1;
    }

    if (!Axle::RenderTests::RunRenderTransientGraphUnitTests()) {
        std::cerr << "[AxleRenderUnitTests] RunRenderTransientGraphUnitTests FAILED!\n";
        return 1;
    }

    std::cout << "========================================================\n";
    std::cout << " All AxleRenderUnitTests passed successfully.\n";
    std::cout << "========================================================\n";
    return 0;
}

