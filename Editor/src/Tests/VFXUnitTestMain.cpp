#include "VFXTestHarness.hpp"
#include <iostream>
#include <vector>
#include <string>

// Test suite forward declarations
bool RunVFXDefinitionUnitTests();
bool RunVFXPersistenceUnitTests();
bool RunVFXDocumentUnitTests();
bool RunVFXPreviewIsolationUnitTests();
bool RunVFXPlaybackUnitTests();
bool RunVFXRenderPlanUnitTests();
bool RunVFXEventsAndMeshUnitTests();

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    std::cout << "\n========================================================\n"
              << "          AxleEngine VFX System CPU Unit Tests          \n"
              << "========================================================\n"
              << " [MODE] Headless / CPU-only (Zero GPU/Context allocations)\n"
              << "--------------------------------------------------------\n";

    struct TestCase {
        std::string Name;
        bool (*Function)();
    };

    std::vector<TestCase> testSuites = {
        { "VFX Effect Definition & Structure",  RunVFXDefinitionUnitTests },
        { "VFX Persistence & Legacy Migration", RunVFXPersistenceUnitTests },
        { "VFX Document Model & Commands",      RunVFXDocumentUnitTests },
        { "VFX Preview Isolation & Lifecycle",  RunVFXPreviewIsolationUnitTests },
        { "VFX Playback, Duration & RNG",       RunVFXPlaybackUnitTests },
        { "VFX Render Plan & GPU Packing",      RunVFXRenderPlanUnitTests },
        { "VFX Graph Events & Mesh Sampling",   RunVFXEventsAndMeshUnitTests },
    };

    size_t passedCount = 0;
    size_t totalCount = testSuites.size();

    for (const auto& suite : testSuites) {
        std::cout << "\n[RUN] >>> Suite: " << suite.Name << " ...\n";
        bool result = false;
        try {
            result = suite.Function();
        } catch (const std::exception& e) {
            std::cerr << "  [EXCEPTION] Unhandled exception in suite " << suite.Name << ": " << e.what() << "\n";
            result = false;
        } catch (...) {
            std::cerr << "  [EXCEPTION] Unknown exception in suite " << suite.Name << "\n";
            result = false;
        }

        if (result) {
            std::cout << "[PASS] <<< Suite: " << suite.Name << "\n";
            passedCount++;
        } else {
            std::cerr << "[FAIL] <<< Suite: " << suite.Name << "\n";
        }
    }

    std::cout << "\n========================================================\n"
              << " Unit Test Summary: " << passedCount << " / " << totalCount << " suites passed\n";

    if (passedCount == totalCount) {
        std::cout << " [ALL UNIT TESTS PASSED SUCCESSFULLY]\n"
                  << "========================================================\n\n";
        return 0;
    } else {
        std::cerr << " [UNIT TESTS FAILED]\n"
                  << "========================================================\n\n";
        return 1;
    }
}
