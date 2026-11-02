#include "MaterialTestHarness.hpp"
#include <Axle/CustomMaterialProgram.hpp>
#include <Axle/RHISceneRenderer.hpp>

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

    bool TestCustomPipelineKeyHashAndEquality() {
        RHISceneRenderer::CustomPipelineKey keyBase;
        keyBase.GenerationID = 101;
        keyBase.ColorFormat = nri::Format::RGBA8_UNORM;
        keyBase.DepthFormat = nri::Format::D32_SFLOAT;
        keyBase.SampleCount = 1;
        keyBase.SurfaceType = Axle::SurfaceType::Opaque;
        keyBase.CullMode = Axle::CullMode::Back;
        keyBase.DepthWrite = true;

        RHISceneRenderer::CustomPipelineKey keyIdentical = keyBase;
        MAT_TEST_CHECK(keyBase == keyIdentical, "Identical pipeline keys must compare equal");

        RHISceneRenderer::CustomPipelineKeyHash hasher;
        MAT_TEST_CHECK(hasher(keyBase) == hasher(keyIdentical), "Identical pipeline keys must hash to same value");

        // Generation difference
        RHISceneRenderer::CustomPipelineKey keyGen = keyBase;
        keyGen.GenerationID = 102;
        MAT_TEST_CHECK(keyBase != keyGen, "Different generations must yield distinct keys");
        MAT_TEST_CHECK(hasher(keyBase) != hasher(keyGen), "Different generations must yield different hash");

        // Color format difference (SDR vs HDR)
        RHISceneRenderer::CustomPipelineKey keyHdr = keyBase;
        keyHdr.ColorFormat = nri::Format::RGBA16_SFLOAT;
        MAT_TEST_CHECK(keyBase != keyHdr, "SDR vs HDR formats must yield distinct keys");
        MAT_TEST_CHECK(hasher(keyBase) != hasher(keyHdr), "SDR vs HDR formats must yield different hash");

        // Sample count difference (MSAA 1 vs 4)
        RHISceneRenderer::CustomPipelineKey keyMsaa = keyBase;
        keyMsaa.SampleCount = 4;
        MAT_TEST_CHECK(keyBase != keyMsaa, "Sample counts 1 vs 4 must yield distinct keys");
        MAT_TEST_CHECK(hasher(keyBase) != hasher(keyMsaa), "Sample counts 1 vs 4 must yield different hash");

        // Surface type difference (Opaque vs Transparent vs Additive)
        RHISceneRenderer::CustomPipelineKey keyTrans = keyBase;
        keyTrans.SurfaceType = Axle::SurfaceType::Transparent;
        keyTrans.DepthWrite = false;
        MAT_TEST_CHECK(keyBase != keyTrans, "Opaque vs Transparent must yield distinct keys");

        RHISceneRenderer::CustomPipelineKey keyAdd = keyBase;
        keyAdd.SurfaceType = Axle::SurfaceType::Additive;
        keyAdd.DepthWrite = false;
        MAT_TEST_CHECK(keyTrans != keyAdd, "Transparent vs Additive must yield distinct keys");

        // Cull mode difference
        RHISceneRenderer::CustomPipelineKey keyCullNone = keyBase;
        keyCullNone.CullMode = Axle::CullMode::None;
        MAT_TEST_CHECK(keyBase != keyCullNone, "Cull mode differences must yield distinct keys");

        return true;
    }

} // namespace

bool RunMaterialProgramCacheUnitTests() {
    std::cout << "  - Running TestProgramCacheSequenceAndGenerations ...\n";
    if (!TestProgramCacheSequenceAndGenerations()) return false;

    std::cout << "  - Running TestCustomPipelineKeyHashAndEquality ...\n";
    if (!TestCustomPipelineKeyHashAndEquality()) return false;

    return true;
}
