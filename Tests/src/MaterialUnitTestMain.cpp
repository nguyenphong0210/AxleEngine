#include "TestWorkspace.hpp"
#include "MaterialTestHarness.hpp"
#include <iostream>
#include <vector>
#include <string>

// Test suite forward declarations
bool RunMaterialSerializationUnitTests();
bool RunMaterialTextureSelectionUnitTests();
bool RunMaterialBindingPlanUnitTests();
bool RunMaterialProgramCacheUnitTests();
bool RunShaderDragDropUnitTests();

int main(int argc, char** argv) {
    Axle::Tests::InitializeWorkspace();
    (void)argc;
    (void)argv;

    std::cout << "\n========================================================\n"
              << "       AxleEngine Material System CPU Unit Tests         \n"
              << "========================================================\n"
              << " [MODE] Headless / CPU-only (Zero GPU/Context allocations)\n"
              << "--------------------------------------------------------\n";

    struct TestCase {
        std::string Name;
        bool (*Function)();
    };

    std::vector<TestCase> testSuites = {
        { "Material Serialization & Synchronization", RunMaterialSerializationUnitTests },
        { "Material Texture Role & Slot Selection",   RunMaterialTextureSelectionUnitTests },
        { "Material Binding Plan & Packing",          RunMaterialBindingPlanUnitTests },
        { "Material Program Cache & Generations",     RunMaterialProgramCacheUnitTests },
        { "Shader Asset Drag/Drop & Extension Decoding", RunShaderDragDropUnitTests },
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

