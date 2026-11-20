#include "RenderTestCommon.hpp"
#include <Axle/RenderContracts.hpp>
#include <Axle/ResolvedSurface.hpp>
#include <Axle/ShaderSurface.hpp>
#include <Axle/MaterialGraph.hpp>
#include <Axle/MaterialGraphHLSL.hpp>
#include <Axle/MaterialGraphProgram.hpp>
#include <Axle/MaterialGraphRuntime.hpp>
#include <Axle/MaterialSurfaceShader.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
    std::string LoadTextFile(const char* path) {
        std::ifstream file(path);
        if (!file.is_open()) return {};
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
}

bool RunM15AdvancedMaterialContractUnitTests() {
    using namespace Axle;
    std::cout << "[M15U9Contract] Running Milestone 15 Unit 9 Batch A (Charlie Sheen & Energy Conservation) Tests...\n";

    // M15-U9-01: Charlie sheen directional albedo vs numerical quadrature & white-furnace <= 1.0 layering
    {
        RENDER_CHECK(SheenContract::VerifyCharlieAlbedoAndEnergyConservation(),
            "Charlie directional albedo matches numerical quadrature and white-furnace layered reflectance <= 1.0");
        RENDER_CHECK(SheenContract::VerifyDirectVsIBLUniformHemisphereParity(),
            "Direct Charlie integral over uniform hemisphere matches IBL sheen response with base attenuation");
        RENDER_CHECK(SheenContract::VerifyGrazingRimAndRoughnessNarrowing(),
            "Charlie sheen produces stronger grazing rim response and narrows peak as roughness drops");
        RENDER_CHECK(SheenContract::VerifyDisabledAndEdgeBehavior(),
            "Disabled sheen, spec/env toggles, and grazing/below-horizon edges have zero leak and remain finite");

        // Verify CPU surface/RHI packing clamps SheenColor [0,1] and SheenRoughness [0.01,1]
        RHI::RHIMaterialDesc desc;
        desc.SheenEnabled = true;
        desc.SheenColor = glm::vec3(1.8f, -0.2f, 0.6f);
        desc.SheenRoughness = 0.001f;
        RHI::RHIMaterial mat(desc);
        const auto c = mat.BuildConstants();
        const auto resolved = MaterialResolver::Resolve(mat);
        const auto rc = resolved.ToMaterialConstants();
        RENDER_CHECK(std::abs(c.Sheen.x - 1.0f) < 1e-5f && c.Sheen.y == 0.0f &&
                     std::abs(c.Sheen.z - 0.6f) < 1e-5f && std::abs(c.Sheen.w - 0.01f) < 1e-5f,
            "RHIMaterial::BuildConstants clamps SheenColor to [0,1] and SheenRoughness to [0.01,1]");
        RENDER_CHECK(std::abs(rc.Sheen.x - 1.0f) < 1e-5f && rc.Sheen.y == 0.0f &&
                     std::abs(rc.Sheen.z - 0.6f) < 1e-5f && std::abs(rc.Sheen.w - 0.01f) < 1e-5f,
            "ResolvedSurface::ToMaterialConstants matches RHIMaterial Sheen clamping parity");

        const std::string pbrCommon = LoadTextFile(AXLE_PBR_COMMON_SHADER_PATH);
        const std::string forwardPBR = LoadTextFile(AXLE_FORWARD_PBR_SHADER_PATH);
        RENDER_CHECK(!pbrCommon.empty() && !forwardPBR.empty() &&
                     pbrCommon.find("EvaluateCharlieDirectionalAlbedo") != std::string::npos &&
                     pbrCommon.find("ApplyCharlieSheenDirect") != std::string::npos &&
                     pbrCommon.find("ApplyCharlieSheenArea") != std::string::npos &&
                     pbrCommon.find("ApplyCharlieSheenIBL") != std::string::npos &&
                     forwardPBR.find("ApplyCharlieSheenDirect") != std::string::npos &&
                     forwardPBR.find("ApplyCharlieSheenArea") != std::string::npos &&
                     forwardPBR.find("ApplyCharlieSheenIBL") != std::string::npos,
            "PBRCommon.hlsli and ForwardPBR.hlsl share energy-conserving Charlie Sheen direct/area/IBL helpers");
    }

    // M15-U9-02 & M15-U9-03: Beer-Lambert absorption & Cauchy dispersion contracts + documented policy
    {
        RENDER_CHECK(AbsorptionDispersionContract::VerifyBeerLambertExponentialAndClamps(),
            "Beer-Lambert transmittance matches exp(-sigma*d), clamps negative/zero inputs and caps grazing path <= 5x thickness");
        RENDER_CHECK(AbsorptionDispersionContract::VerifyCauchyWavelengthDispersion(),
            "Cauchy dispersion orders IOR(R) < IOR(G) < IOR(B) cleanly and stays neutral at IOR=1 or Dispersion=0");
        RENDER_CHECK(AbsorptionDispersionContract::VerifyOpaqueFallbackUnaffected(),
            "Non-refractive opaque materials emit zero absorption and dispersion impact");

        // Verify Dispersion forwards independently for clear dispersive glass (RefractionEnabled=true, AbsorptionEnabled=false)
        RHI::RHIMaterialDesc clearDisp;
        clearDisp.SurfaceType = SurfaceType::Transparent;
        clearDisp.RefractionEnabled = true;
        clearDisp.AbsorptionEnabled = false;
        clearDisp.Dispersion = 0.25f;
        RHI::RHIMaterial clearMat(clearDisp);
        const auto cc = clearMat.BuildConstants();
        const auto cr = MaterialResolver::Resolve(clearMat).ToMaterialConstants();
        RENDER_CHECK(cc.Absorption.x == 0.0f && cc.Absorption.y == 0.0f && cc.Absorption.z == 0.0f &&
                     std::abs(cc.Absorption.w - 0.25f) < 1e-5f && cr.Absorption == cc.Absorption,
            "Clear dispersive glass retains Cauchy dispersion without spurious absorption");

        const std::string pbrCommon = LoadTextFile(AXLE_PBR_COMMON_SHADER_PATH);
        const std::string forwardPBR = LoadTextFile(AXLE_FORWARD_PBR_SHADER_PATH);
        const std::string policyDoc = LoadTextFile(AXLE_REFRACTION_POLICY_DOC_PATH);
        RENDER_CHECK(pbrCommon.find("EvaluateBeerLambertTransmittance") != std::string::npos &&
                     pbrCommon.find("EvaluateCauchyIORs") != std::string::npos &&
                     pbrCommon.find("ProjectSnellRefractedUV") != std::string::npos &&
                     forwardPBR.find("EvaluateBeerLambertTransmittance") != std::string::npos &&
                     forwardPBR.find("EvaluateCauchyIORs") != std::string::npos,
            "ForwardPBR.hlsl and PBRCommon.hlsli implement documented Beer-Lambert & Cauchy helpers");
        RENDER_CHECK(!policyDoc.empty() &&
                     policyDoc.find("Beer-Lambert") != std::string::npos &&
                     policyDoc.find("Cauchy") != std::string::npos &&
                     policyDoc.find("single-bounce screen-space refraction limits") != std::string::npos,
            "REFRACTION_ABSORPTION_POLICY.md documents thickness/path policy and screen-space vs traced glass limits");
    }

    // M15-U9-04 & M15-U9-05: Screen-space Separable Subsurface Scattering contracts & diffuse/specular split
    {
        RENDER_CHECK(SubsurfaceContract::VerifyNormalizedKernelAndDiffusePreservation(),
            "Separable SSS 1D filter preserves uniform diffuse plateau and smooths step terminator asymmetrically by RGB scatter");
        RENDER_CHECK(SubsurfaceContract::VerifyDepthAndBoundaryRejection(),
            "Separable SSS bilateral weights reject silhouette depth jumps, creases, non-SSS and dissimilar SSS materials");
        RENDER_CHECK(SubsurfaceContract::VerifySpecularSplitRecombination(),
            "Separable SSS splits diffuse from crisp specular before blurring and recombines exactly");

        RHI::RHIMaterialDesc sssDesc;
        sssDesc.SubsurfaceEnabled = true;
        sssDesc.SubsurfaceColor = glm::vec3(1.4f, -0.1f, 0.25f);
        sssDesc.SubsurfaceRadius = 2.0f;
        RHI::RHIMaterial sssMat(sssDesc);
        const auto sc = sssMat.BuildConstants();
        const auto sr = MaterialResolver::Resolve(sssMat).ToMaterialConstants();
        RENDER_CHECK(std::abs(sc.Subsurface.x - 1.0f) < 1e-5f && sc.Subsurface.y == 0.0f &&
                     std::abs(sc.Subsurface.z - 0.25f) < 1e-5f && std::abs(sc.Subsurface.w - 2.0f) < 1e-5f &&
                     sr.Subsurface == sc.Subsurface,
            "RHIMaterial and ResolvedSurface clamp SubsurfaceColor [0,1] and pack SubsurfaceRadius in sync");

        const std::string forwardPBR = LoadTextFile(AXLE_FORWARD_PBR_SHADER_PATH);
        const std::string sssShader = LoadTextFile(AXLE_SSS_SHADER_PATH);
        const std::string sssPass = LoadTextFile(AXLE_SUBSURFACE_PASS_SOURCE_PATH);
        const std::string scenePasses = LoadTextFile(AXLE_SCENE_PASSES_SOURCE_PATH);
        RENDER_CHECK(!sssShader.empty() && !sssPass.empty() && !scenePasses.empty() &&
                     sssShader.find("PSBlurH") != std::string::npos &&
                     sssShader.find("PSBlurVComposite") != std::string::npos &&
                     sssShader.find("ComputeBoundaryWeight") != std::string::npos &&
                     sssPass.find("SceneRequiresSubsurfacePass") != std::string::npos &&
                     sssPass.find("SSSBlurH") != std::string::npos &&
                     sssPass.find("SSSBlurV") != std::string::npos &&
                     forwardPBR.find("PSSSSMain") != std::string::npos &&
                     forwardPBR.find("PSReflectionSSSMain") != std::string::npos,
            "SSS.hlsl, SubsurfaceScatteringPass, and ForwardPBR SSS MRT entry points exist and wire separable diffusion");

        const auto sssPos = scenePasses.find("\"SubsurfaceScatteringPass\"");
        const auto ssrPos = scenePasses.find("\"ScreenSpaceReflectionPass\"");
        RENDER_CHECK(sssPos != std::string::npos && ssrPos != std::string::npos && sssPos < ssrPos,
            "SubsurfaceScatteringPass executes before ScreenSpaceReflectionPass in scene frame pass schedule");
    }

    // M15-U9-07.1 & M15-U9-07.2: Material Graph advanced lowering, DXIL/SPIR-V entry points & explicit unsupported diagnostics
    {
        // 1. 13-pin PBRMasterOutput lowering & DXIL/SPIR-V compilation (Sheen + Subsurface)
        MaterialGraphDocument advDoc;
        advDoc.Identity = 90901ULL;
        advDoc.Name = "M_U9GraphAdv";
        advDoc.Parameters = {
            {"u_SheenTint", {MaterialValueType::Float3, {1.0f, 0.85f, 0.65f}}, {}},
            {"u_SheenRough", {MaterialValueType::Float, {0.32f}}, {}},
            {"u_SSSColor", {MaterialValueType::Float3, {1.0f, 0.25f, 0.08f}}, {}},
            {"u_SSSRad", {MaterialValueType::Float, {6.0f}}, {}}
        };
        const auto* paramDef = MaterialNodeRegistry::Builtins().Find("ParameterRef");
        const auto* masterDef = MaterialNodeRegistry::Builtins().Find("PBRMasterOutput");
        RENDER_CHECK(paramDef && masterDef && masterDef->Signatures.size() >= 2,
            "PBRMasterOutput exposes overload signature supporting advanced U9 material outputs");

        auto makeParamNode = [&](uint64_t id, const char* paramName, MaterialValueType type) {
            MaterialGraphNode n;
            n.ID = id;
            n.Type = "ParameterRef";
            n.Attributes = {{"parameter", std::string(paramName), {}}};
            n.Outputs = {{"value", type, {}, {}}};
            return n;
        };
        advDoc.Nodes.push_back(makeParamNode(10, "u_SheenTint", MaterialValueType::Float3));
        advDoc.Nodes.push_back(makeParamNode(11, "u_SheenRough", MaterialValueType::Float));
        advDoc.Nodes.push_back(makeParamNode(12, "u_SSSColor", MaterialValueType::Float3));
        advDoc.Nodes.push_back(makeParamNode(13, "u_SSSRad", MaterialValueType::Float));

        MaterialGraphNode master;
        master.ID = 1;
        master.Type = "PBRMasterOutput";
        for (const auto& pin : masterDef->Signatures[1].Inputs) {
            master.Inputs.push_back({pin.Name, *pin.Type, pin.Default, {}});
        }
        advDoc.Nodes.push_back(std::move(master));
        advDoc.Connections = {
            {{10, "value"}, {1, "SheenColor"}, {}},
            {{11, "value"}, {1, "SheenRoughness"}, {}},
            {{12, "value"}, {1, "SubsurfaceColor"}, {}},
            {{13, "value"}, {1, "SubsurfaceRadius"}, {}}
        };

        std::string serialized;
        MaterialGraphDiagnostic diag;
        RENDER_CHECK(MaterialGraph::Serialize(advDoc, serialized, &diag),
            "13-pin MaterialGraphDocument serializes cleanly");
        MaterialGraphDocument reparsed;
        RENDER_CHECK(MaterialGraph::Parse(serialized, reparsed, &diag),
            "13-pin MaterialGraphDocument round-trips cleanly through Parse");

        for (auto fmt : {RHI::RHIShaderFormat::DXIL, RHI::RHIShaderFormat::SPIRV}) {
            CompiledMaterialGraphProgram prog;
            RENDER_CHECK(MaterialGraphProgram::Compile(reparsed, fmt, prog, &diag),
                "13-pin Sheen+Subsurface MaterialGraph compiles cleanly for DXIL & SPIR-V");
            RENDER_CHECK(prog.Vertex.IsValid() && prog.Fragment.IsValid() && prog.NormalFragment.IsValid() &&
                         prog.ReflectionFragment.IsValid() && prog.SSSFragment.IsValid() &&
                         prog.ReflectionSSSFragment.IsValid(),
                "MaterialGraph compiles all 6 entry points (VS, PS, Normal, Reflection, SSS, ReflectionSSS)");
            RENDER_CHECK(prog.Graph.Warnings.empty() &&
                         prog.Graph.SurfaceFunction.find("surface.SheenColor =") != std::string::npos &&
                         prog.Graph.SurfaceFunction.find("surface.SubsurfaceRadius =") != std::string::npos &&
                         prog.Graph.SurfaceFunction.find("surface.FeatureFlags |=") != std::string::npos,
                "MaterialGraph lowers Sheen/Subsurface surface fields and sets FeatureFlags with zero warnings");
            RENDER_CHECK((prog.BindingPlan.SupportedFeatures & static_cast<uint32_t>(SurfaceFeature::Sheen)) != 0 &&
                         (prog.BindingPlan.SupportedFeatures & static_cast<uint32_t>(SurfaceFeature::Subsurface)) != 0,
                "MaterialGraph layout advertises Sheen and Subsurface support");
        }

        // 2. M15-U9-07.2: Explicit unsupported-feature diagnostics on both opaque graph & raw/graph ResolveShaderSurface
        auto opaqueOptDoc = reparsed;
        for (auto& inp : opaqueOptDoc.Nodes.back().Inputs) {
            if (inp.Name == "AbsorptionColor") inp.Default->Components = {1.5f, 0.2f, 0.1f};
            if (inp.Name == "Dispersion") inp.Default->Components = {0.35f};
        }
        MaterialHLSLSource warnedSource;
        RENDER_CHECK(MaterialGraphHLSL::Lower(opaqueOptDoc, warnedSource, &diag) && warnedSource.Warnings.size() >= 2,
            "Opaque MaterialGraph authoring AbsorptionColor/Dispersion emits explicit unsupported-feature lowering warnings");
        bool hasAbsWarn = false, hasDispWarn = false;
        for (const auto& w : warnedSource.Warnings) {
            hasAbsWarn |= w.Code == "MG_UNSUPPORTED_OPAQUE_ABSORPTION";
            hasDispWarn |= w.Code == "MG_UNSUPPORTED_OPAQUE_DISPERSION";
        }
        RENDER_CHECK(hasAbsWarn && hasDispWarn,
            "Opaque optical warnings carry explicit MG_UNSUPPORTED_OPAQUE_ABSORPTION and MG_UNSUPPORTED_OPAQUE_DISPERSION codes");

        MaterialGraphRuntime rt(90901ULL, MaterialGraphTarget{RHI::RHIShaderFormat::DXIL, false});
        RENDER_CHECK(rt.Reload(opaqueOptDoc) && rt.Ready(), "MaterialGraphRuntime builds generation with warnings");
        const auto rtRes = MaterialGraphRuntime::ResolveSurface(*rt.Ready());
        RENDER_CHECK(std::holds_alternative<ResolvedShaderSurface>(rtRes), "MaterialGraphRuntime resolves surface");
        const auto& rtSurf = std::get<ResolvedShaderSurface>(rtRes).Surface;
        RENDER_CHECK(rtSurf.Fallback &&
                     (rtSurf.UnsupportedFeatures & static_cast<uint32_t>(SurfaceFeature::Absorption)) != 0 &&
                     (rtSurf.UnsupportedFeatures & static_cast<uint32_t>(SurfaceFeature::Dispersion)) != 0 &&
                     rtSurf.Diagnostic.find("MG_UNSUPPORTED_OPAQUE") != std::string::npos,
            "MaterialGraphRuntime flags Fallback, UnsupportedFeatures bits, and explicit diagnostic when opaque optical features authored");

        // ResolveShaderSurface interface-capability check: graph plan supports Sheen/SSS, rejects Absorption/Dispersion
        CompiledMaterialGraphProgram cleanProg;
        RENDER_CHECK(MaterialGraphProgram::Compile(reparsed, RHI::RHIShaderFormat::DXIL, cleanProg, &diag),
                     "Clean 13-pin graph compiles for DXIL interface check");
        RHI::RHIMaterialDesc matReq;
        matReq.SheenEnabled = true;
        matReq.SheenColor = glm::vec3(1.0f, 0.8f, 0.7f);
        matReq.SheenRoughness = 0.4f;
        matReq.SubsurfaceEnabled = true;
        matReq.SubsurfaceColor = glm::vec3(1.0f, 0.3f, 0.2f);
        matReq.SubsurfaceRadius = 4.0f;
        EngineMaterialValues viewVals;
        const auto graphSupportedRes = ResolveShaderSurface(MaterialResolver::Resolve(RHI::RHIMaterial(matReq)),
            SurfaceEvaluation::MaterialGraph, 77u, cleanProg.BindingPlan, viewVals, {});
        RENDER_CHECK(std::holds_alternative<ResolvedShaderSurface>(graphSupportedRes) &&
                     std::get<ResolvedShaderSurface>(graphSupportedRes).Surface.UnsupportedFeatures == 0 &&
                     !std::get<ResolvedShaderSurface>(graphSupportedRes).Surface.Fallback,
            "Graph BindingPlan accepts Sheen+Subsurface cleanly without spurious unsupported fallback");

        matReq.AbsorptionEnabled = true;
        matReq.AbsorptionColor = glm::vec3(1.0f, 0.2f, 0.1f);
        matReq.Dispersion = 0.3f;
        const auto graphPartialRes = ResolveShaderSurface(MaterialResolver::Resolve(RHI::RHIMaterial(matReq)),
            SurfaceEvaluation::MaterialGraph, 78u, cleanProg.BindingPlan, viewVals, {});
        RENDER_CHECK(std::holds_alternative<ResolvedShaderSurface>(graphPartialRes), "Partial feature interface resolves");
        const auto& graphPartial = std::get<ResolvedShaderSurface>(graphPartialRes).Surface;
        RENDER_CHECK(graphPartial.Fallback &&
                     graphPartial.UnsupportedFeatures ==
                         (static_cast<uint32_t>(SurfaceFeature::Absorption) | static_cast<uint32_t>(SurfaceFeature::Dispersion)) &&
                     graphPartial.Diagnostic.find("Absorption") != std::string::npos &&
                     graphPartial.Diagnostic.find("Dispersion") != std::string::npos,
            "Graph BindingPlan names only genuinely unsupported optical controls [Absorption, Dispersion] in diagnostic");

        // Empty/raw custom shader plan names missing Sheen/Subsurface/Absorption/Dispersion
        MaterialBindingPlan rawPlan;
        const auto rawRes = ResolveShaderSurface(MaterialResolver::Resolve(RHI::RHIMaterial(matReq)),
            SurfaceEvaluation::CustomShader, 79u, rawPlan, viewVals, {});
        RENDER_CHECK(std::holds_alternative<ResolvedShaderSurface>(rawRes), "Raw plan resolves with diagnostics");
        const auto& rawSurf = std::get<ResolvedShaderSurface>(rawRes).Surface;
        RENDER_CHECK(rawSurf.Fallback &&
                     rawSurf.Diagnostic.find("Sheen") != std::string::npos &&
                     rawSurf.Diagnostic.find("Subsurface") != std::string::npos &&
                     rawSurf.Diagnostic.find("Absorption") != std::string::npos &&
                     rawSurf.Diagnostic.find("Dispersion") != std::string::npos,
            "Raw custom shader plan explicitly names unsupported Sheen/Subsurface/Absorption/Dispersion controls");
    }

    std::cout << "[M15U9Contract] M15 U9 Batch A, B, C & D contract tests passed.\n";
    return true;
}
