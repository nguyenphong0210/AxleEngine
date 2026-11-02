#include "MaterialTestHarness.hpp"
#include <Axle/CustomMaterialProgram.hpp>

using namespace Axle;
using namespace Axle::MaterialTests;

namespace {

    bool TestProgramCacheSequenceAndGenerations() {
        CustomProgramCache cache;
        int builderInvocations = 0;

        // Injected candidate builder with controllable mock outcomes
        std::string nextOutcome = "G1_SUCCESS";
        std::shared_ptr<int> g1Lifetime = std::make_shared<int>(42);
        std::shared_ptr<int> g2Lifetime = std::make_shared<int>(84);

        cache.SetCandidateBuilder([&](const CustomProgramKey& key, const std::string& fingerprint) -> CandidateBuildResult {
            builderInvocations++;
            CandidateBuildResult res;

            if (nextOutcome == "G1_SUCCESS") {
                res.Success = true;
                auto gen = std::make_shared<CustomReadyGeneration>();
                gen->DependencyFingerprint = fingerprint;
                gen->LifetimeTracker = g1Lifetime;
                res.Generation = gen;
            } else if (nextOutcome == "COMPILER_ERROR") {
                res.Success = false;
                res.Diagnostic = "HLSL compilation syntax error at line 42";
            } else if (nextOutcome == "BACKEND_ERROR") {
                res.Success = false;
                res.Diagnostic = "Backend layout root descriptor allocation failure";
            } else if (nextOutcome == "G2_SUCCESS") {
                res.Success = true;
                auto gen = std::make_shared<CustomReadyGeneration>();
                gen->DependencyFingerprint = fingerprint;
                gen->LifetimeTracker = g2Lifetime;
                res.Generation = gen;
            } else if (nextOutcome == "REQUEST_B_FAIL") {
                res.Success = false;
                res.Diagnostic = "Shader B failed to compile";
            }

            return res;
        });

        CustomProgramKey keyA{ "shaders/CarShader.hlsl", "VSMain", "PSMain", RHI::RHIType::Direct3D12, 1 };

        // 1. First request A, fingerprint F1, success G1
        nextOutcome = "G1_SUCCESS";
        int prevCalls = builderInvocations;
        auto sel1 = cache.SelectProgram(true, keyA.ShaderPath, keyA, "F1");
        MAT_TEST_CHECK(builderInvocations == prevCalls + 1, "First request must invoke builder once");
        MAT_TEST_CHECK(sel1.Kind == ProgramSelectionKind::Custom, "Expected Custom program selected");
        MAT_TEST_CHECK(sel1.Generation != nullptr, "Generation must not be null");
        uint64_t g1ID = sel1.Generation->GenerationID;
        MAT_TEST_CHECK(g1ID > 0, "Valid generation ID required");
        MAT_TEST_CHECK(sel1.Diagnostic.empty(), "Diagnostic should be empty on success");

        // Retain an in-flight draw ownership reference to G1
        std::shared_ptr<const CustomReadyGeneration> inFlightDrawA1 = sel1.Generation;

        // 2. Same A, F1 -> 0 builder calls added, returns G1
        prevCalls = builderInvocations;
        auto sel2 = cache.SelectProgram(true, keyA.ShaderPath, keyA, "F1");
        MAT_TEST_CHECK(builderInvocations == prevCalls, "Same A and F1 must suppress builder call");
        MAT_TEST_CHECK(sel2.Generation->GenerationID == g1ID, "Must return same G1 generation");

        // 3. A, F2 compiler failure -> 1 builder call added, retains G1 with error diagnostic
        nextOutcome = "COMPILER_ERROR";
        prevCalls = builderInvocations;
        auto sel3 = cache.SelectProgram(true, keyA.ShaderPath, keyA, "F2");
        MAT_TEST_CHECK(builderInvocations == prevCalls + 1, "New fingerprint F2 must invoke builder");
        MAT_TEST_CHECK(sel3.Kind == ProgramSelectionKind::Custom, "Must retain working G1 on compile failure");
        MAT_TEST_CHECK(sel3.Generation->GenerationID == g1ID, "Must retain G1");
        MAT_TEST_CHECK(sel3.Diagnostic.find("syntax error") != std::string::npos, "Diagnostic must expose compile error");

        // 4. Same A, F2 -> 0 builder calls added, returns G1 with same error
        prevCalls = builderInvocations;
        auto sel4 = cache.SelectProgram(true, keyA.ShaderPath, keyA, "F2");
        MAT_TEST_CHECK(builderInvocations == prevCalls, "Unchanged failed fingerprint must suppress repeated calls");
        MAT_TEST_CHECK(sel4.Generation->GenerationID == g1ID, "Must return G1");
        MAT_TEST_CHECK(!sel4.Diagnostic.empty(), "Must retain error diagnostic");

        // 5. A, F3 backend failure -> 1 builder call added, retains G1 with error
        nextOutcome = "BACKEND_ERROR";
        prevCalls = builderInvocations;
        auto sel5 = cache.SelectProgram(true, keyA.ShaderPath, keyA, "F3");
        MAT_TEST_CHECK(builderInvocations == prevCalls + 1, "New fingerprint F3 must invoke builder");
        MAT_TEST_CHECK(sel5.Generation->GenerationID == g1ID, "Must retain G1");
        MAT_TEST_CHECK(sel5.Diagnostic.find("Backend layout") != std::string::npos, "Diagnostic must expose backend error");

        // 6. A, F4 success G2 -> 1 builder call added, switches to G2
        nextOutcome = "G2_SUCCESS";
        prevCalls = builderInvocations;
        auto sel6 = cache.SelectProgram(true, keyA.ShaderPath, keyA, "F4");
        MAT_TEST_CHECK(builderInvocations == prevCalls + 1, "Fingerprint F4 must invoke builder");
        MAT_TEST_CHECK(sel6.Generation->GenerationID != g1ID, "Must switch to new generation G2");
        MAT_TEST_CHECK(sel6.Diagnostic.empty(), "Diagnostic must be cleared on success");

        // 7. Verify in-flight draw ownership: inFlightDrawA1 still holds G1!
        MAT_TEST_CHECK(inFlightDrawA1->GenerationID == g1ID, "In-flight draw must still hold G1");
        MAT_TEST_CHECK(g1Lifetime.use_count() >= 2, "G1 must remain alive while draw holds reference");

        // 8. Request B, first compilation fails -> 1 builder call added, selects Built-in; never G2 from A!
        CustomProgramKey keyB{ "shaders/OtherShader.hlsl", "VSMain", "PSMain", RHI::RHIType::Direct3D12, 1 };
        nextOutcome = "REQUEST_B_FAIL";
        prevCalls = builderInvocations;
        auto sel7 = cache.SelectProgram(true, keyB.ShaderPath, keyB, "FB1");
        MAT_TEST_CHECK(builderInvocations == prevCalls + 1, "Request B must invoke builder");
        MAT_TEST_CHECK(sel7.Kind == ProgramSelectionKind::BuiltIn, "Failed first attempt must fall back to Built-in");
        MAT_TEST_CHECK(sel7.Generation == nullptr, "Must NEVER reuse generation from shader A!");
        MAT_TEST_CHECK(!sel7.Diagnostic.empty(), "Must expose diagnostic for Shader B");

        // 9. Disable custom mode -> 0 builder calls added, selects Built-in
        prevCalls = builderInvocations;
        auto sel8 = cache.SelectProgram(false, keyA.ShaderPath, keyA, "F5");
        MAT_TEST_CHECK(builderInvocations == prevCalls, "Disabled custom mode must not invoke builder");
        MAT_TEST_CHECK(sel8.Kind == ProgramSelectionKind::BuiltIn, "Must select Built-in when custom disabled");

        // 10. Missing / empty shader path -> selects Built-in with diagnostic
        auto sel9 = cache.SelectProgram(true, "", keyA, "");
        MAT_TEST_CHECK(sel9.Kind == ProgramSelectionKind::BuiltIn, "Empty path must select Built-in");
        MAT_TEST_CHECK(!sel9.Diagnostic.empty(), "Must provide diagnostic for empty path");

        return true;
    }

} // namespace

bool RunMaterialProgramCacheUnitTests() {
    std::cout << "  - Running TestProgramCacheSequenceAndGenerations ...\n";
    if (!TestProgramCacheSequenceAndGenerations()) return false;

    return true;
}
