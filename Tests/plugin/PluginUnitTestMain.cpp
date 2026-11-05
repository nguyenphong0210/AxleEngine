#include "PluginTestCommon.hpp"
#include <iostream>
#include <string_view>

bool RunPluginBaselineUnitTests();

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test-failure") {
        std::cout << "[AxlePluginUnitTests] Running intentional self-test failure probe...\n";
        std::cout << "AXLE_TEST_EXPECTED_FAILURE plugin-probe\n";
        return Axle::PluginTests::Check(false, "Deliberate test harness failure probe (--self-test-failure)", __FILE__, __LINE__) ? 0 : 1;
    }

    std::cout << "========================================================\n";
    std::cout << " AxlePluginUnitTests: Plugin baseline & retirement suite\n";
    std::cout << "========================================================\n";

    if (!RunPluginBaselineUnitTests()) {
        std::cerr << "[AxlePluginUnitTests] RunPluginBaselineUnitTests FAILED!\n";
        return 1;
    }

    std::cout << "========================================================\n";
    std::cout << " All AxlePluginUnitTests passed successfully.\n";
    std::cout << "========================================================\n";
    return 0;
}
