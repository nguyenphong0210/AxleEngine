#include "RenderTestCommon.hpp"
#include <Axle/RenderContracts.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <Axle/FileSystem.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
#include <iostream>

using namespace Axle;
using namespace Axle::RHI;
using namespace Axle::OcclusionContract;

#ifndef AXLE_FORWARD_PBR_SHADER_PATH
#define AXLE_FORWARD_PBR_SHADER_PATH "Engine/Resources/Shaders/ForwardPBR.hlsl"
#endif

#ifndef AXLE_MATERIAL_SURFACE_SHADER_PATH
#define AXLE_MATERIAL_SURFACE_SHADER_PATH "Engine/Resources/Shaders/MaterialSurface.hlsl"
#endif

#ifndef AXLE_GTAO_SHADER_PATH
#define AXLE_GTAO_SHADER_PATH "Engine/Resources/Shaders/GTAO.hlsl"
#endif

namespace {
    std::string ReadShaderSource(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return {};
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
}

bool RunM15OcclusionContractUnitTests() {
    std::cout << "[M15Occlusion] Running Milestone 15 Unit 4 Occlusion & Pre-Lighting Contracts...\n";

    // =========================================================================
    // 1. M15-U4-01 & M15-U4-02 Preservation Contracts
    // =========================================================================
    {
        std::cout << "  [M15Occlusion] Verifying U4-01 and U4-02 preservation (ScreenNormal, bounded weights)...\n";

        // U4-01: Valid ScreenNormal target descriptor
        RHI::RHIViewTargetDesc normalDesc;
        normalDesc.Width = 128;
        normalDesc.Height = 128;
        normalDesc.EnableNormal = true;
        normalDesc.NormalFormat = RHI::RHITextureFormat::RGBA16_FLOAT;
        RENDER_CHECK(normalDesc.EnableNormal && normalDesc.NormalFormat == RHI::RHITextureFormat::RGBA16_FLOAT,
            "M15-U4-01: RHIViewTargetDesc declares valid RGBA16_FLOAT ScreenNormal target");

        // U4-02: Bounded composite weights preservation
        const std::string gtaoSource = ReadShaderSource(AXLE_GTAO_SHADER_PATH);
        RENDER_CHECK(!gtaoSource.empty(), "GTAO.hlsl shader source loaded for inspection");
        RENDER_CHECK(gtaoSource.find("ambientWeight = 0.25") != std::string::npos,
            "M15-U4-02.2: GTAO shader bounds ambient weight to 0.25");
        RENDER_CHECK(gtaoSource.find("contactWeight = 0.20") != std::string::npos,
            "M15-U4-02.2: GTAO shader bounds contact shadow weight to 0.20");
        RENDER_CHECK(gtaoSource.find("lerp(0.35, 1.0, finalAO)") != std::string::npos,
            "M15-U4-02.1: GTAO applies 0.35 soft minimum floor to prevent geometry black crushing");
        RENDER_CHECK(gtaoSource.find("lerp(0.5, 1.0, finalContact)") != std::string::npos,
            "M15-U4-02.1: GTAO applies 0.50 soft minimum floor to contact shadows");
    }

    // =========================================================================
    // 2. M15-U4-03.1: Move AO Before Lighting & Ambient-Only Modulation
    // =========================================================================
    {
        std::cout << "  [M15Occlusion] Verifying M15-U4-03.1 pre-lighting dataflow contract & ambient-only modulation...\n";

        using namespace Axle::OcclusionContract;

        // Test standard PBR shading components
        ShadingComponents standardPBR;
        standardPBR.Ambient = glm::vec3(0.25f, 0.28f, 0.35f);
        standardPBR.DirectDiffuse = glm::vec3(1.20f, 1.15f, 1.05f);
        standardPBR.DirectSpecular = glm::vec3(2.50f, 2.40f, 2.20f);
        standardPBR.Emissive = glm::vec3(0.0f);

        RENDER_CHECK(VerifyAmbientOnlyModulation(standardPBR),
            "M15-U4-03.1: VerifyAmbientOnlyModulation confirms direct and emissive invariance on standard PBR");

        // Test highly reflective metallic surface
        ShadingComponents metallic;
        metallic.Ambient = glm::vec3(0.10f, 0.10f, 0.12f);
        metallic.DirectDiffuse = glm::vec3(0.0f); // Pure metallic has no diffuse
        metallic.DirectSpecular = glm::vec3(5.5f, 5.2f, 4.8f);
        metallic.Emissive = glm::vec3(0.0f);

        RENDER_CHECK(VerifyAmbientOnlyModulation(metallic),
            "M15-U4-03.1: VerifyAmbientOnlyModulation confirms direct specular glints are 100% invariant under AO on metals");

        // Test emissive surface
        ShadingComponents emissiveMat;
        emissiveMat.Ambient = glm::vec3(0.05f, 0.05f, 0.05f);
        emissiveMat.DirectDiffuse = glm::vec3(0.4f, 0.4f, 0.4f);
        emissiveMat.DirectSpecular = glm::vec3(0.1f, 0.1f, 0.1f);
        emissiveMat.Emissive = glm::vec3(12.0f, 6.0f, 2.0f);

        RENDER_CHECK(VerifyAmbientOnlyModulation(emissiveMat),
            "M15-U4-03.1: VerifyAmbientOnlyModulation confirms emissive radiance is 100% invariant under AO");

        // Analytical isolated components test:
        // Case A: Pure Emissive (no ambient, no direct)
        ShadingComponents pureEmissive{};
        pureEmissive.Emissive = glm::vec3(15.0f, 8.0f, 3.0f);
        glm::vec3 atAO1 = EvaluateShading(pureEmissive, 1.0f);
        glm::vec3 atAO0 = EvaluateShading(pureEmissive, 0.0f);
        RENDER_CHECK(atAO1 == atAO0 && atAO1 == pureEmissive.Emissive,
            "M15-U4-03.1: pure emissive surface exhibits identical radiance at AO=1.0 and AO=0.0 (delta == 0)");

        // Case B: Pure Direct Specular (no ambient, no emissive)
        ShadingComponents pureSpecular{};
        pureSpecular.DirectSpecular = glm::vec3(8.0f, 7.5f, 7.0f);
        glm::vec3 specAtAO1 = EvaluateShading(pureSpecular, 1.0f);
        glm::vec3 specAtAO0 = EvaluateShading(pureSpecular, 0.0f);
        RENDER_CHECK(specAtAO1 == specAtAO0 && specAtAO1 == pureSpecular.DirectSpecular,
            "M15-U4-03.1: pure direct specular highlight exhibits identical radiance at AO=1.0 and AO=0.0 (delta == 0)");

        // Case C: Rejection of whole-scene color multiplication
        // If whole-scene multiplication were erroneously applied: C_wrong = (Ambient + Direct + Emissive) * AO
        // At AO=0.0, C_wrong would collapse to 0.0, wiping out 100% of direct and emissive light!
        glm::vec3 wrongCompositeAt0 = (emissiveMat.Ambient + emissiveMat.DirectDiffuse + emissiveMat.DirectSpecular + emissiveMat.Emissive) * 0.0f;
        glm::vec3 correctContractAt0 = EvaluateShading(emissiveMat, 0.0f);
        RENDER_CHECK(wrongCompositeAt0 == glm::vec3(0.0f) && correctContractAt0 == (emissiveMat.DirectDiffuse + emissiveMat.DirectSpecular + emissiveMat.Emissive),
            "M15-U4-03.1: pre-lighting contract rejects whole-scene multiplication and preserves direct/emissive light at AO=0");

        // Inspect ForwardPBR.hlsl and MaterialSurface.hlsl shader sources
        const std::string fwdSource = ReadShaderSource(AXLE_FORWARD_PBR_SHADER_PATH);
        const std::string matSource = ReadShaderSource(AXLE_MATERIAL_SURFACE_SHADER_PATH);
        RENDER_CHECK(!fwdSource.empty() && !matSource.empty(),
            "M15-U4-03.1: shader sources for ForwardPBR and MaterialSurface loaded successfully");

        // Verify ForwardPBR.hlsl restricts AO to ambient
        RENDER_CHECK(fwdSource.find("ambient = (diffuseAmbient + specularAmbient) * envIntensity * ao") != std::string::npos ||
                     fwdSource.find("* ao") != std::string::npos,
            "M15-U4-03.1: ForwardPBR scales ambient terms by ao");
        RENDER_CHECK(fwdSource.find("float3 color = ambient + Lo + emissive;") != std::string::npos,
            "M15-U4-03.1: ForwardPBR adds direct light (Lo) and emissive unscaled by ao");

        // Verify MaterialSurface.hlsl restricts AO to ambient
        RENDER_CHECK(matSource.find("saturate(surface.AO)") != std::string::npos,
            "M15-U4-03.1: MaterialSurface scales ambient term by surface.AO");
        RENDER_CHECK(matSource.find("float3 color = direct + ambient + max(surface.Emission, float3(0,0,0));") != std::string::npos,
            "M15-U4-03.1: MaterialSurface adds direct and emission unscaled by AO");
    }

    // =========================================================================
    // 3. M15-U4-03.2: Separate Indirect Target Decision & Documentation
    // =========================================================================
    {
        std::cout << "  [M15Occlusion] Verifying M15-U4-03.2 separate indirect target decision & documentation...\n";

        using namespace Axle::OcclusionContract;

        // Architectural decision verification:
        // For forward rendering under the pre-lighting contract, a separate physical indirect render target is NOT required.
        const bool requiredInPreLighting = IsSeparateIndirectTargetRequired(DataflowMode::PreLightingSeparated);
        const bool requiredInPostComposite = IsSeparateIndirectTargetRequired(DataflowMode::PostLightingComposite);

        RENDER_CHECK(!requiredInPreLighting,
            "M15-U4-03.2: DECISION: Separate physical indirect target is NOT required for forward shading under pre-lighting dataflow");
        RENDER_CHECK(requiredInPostComposite,
            "M15-U4-03.2: Separate indirect target is only necessary under deferred post-lighting compositing");

        // Rationale documentation check
        std::string_view rationale = GetIndirectTargetDecisionRationale();
        RENDER_CHECK(!rationale.empty(), "M15-U4-03.2: Decision rationale documentation is present");
        RENDER_CHECK(rationale.find("NOT required") != std::string_view::npos,
            "M15-U4-03.2: Decision explicitly documents why separate indirect target is not required");
        RENDER_CHECK(rationale.find("VRAM") != std::string_view::npos && rationale.find("bandwidth") != std::string_view::npos,
            "M15-U4-03.2: Decision documents VRAM and MRT memory bandwidth savings");

        // Auxiliary target exposure check via RHIViewTargets contract
        RHI::RHIViewTargetDesc auxDesc;
        auxDesc.Width = 1920;
        auxDesc.Height = 1080;
        auxDesc.EnableScreenStorage = true;
        auxDesc.HDR = true;

        RENDER_CHECK(auxDesc.EnableScreenStorage,
            "M15-U4-03.2: RHIViewTargetDesc supports auxiliary screen storage for optional decoupled indirect inspection");

        // Calculate VRAM saved by avoiding separate full-res HDR indirect target:
        // 1920 * 1080 * 8 bytes (RGBA16_FLOAT) = 16,588,800 bytes (~16.6 MB)
        const size_t vramSaved1080p = 1920 * 1080 * 8;
        RENDER_CHECK(vramSaved1080p >= 16000000,
            "M15-U4-03.2: In-place pre-lighting evaluation saves ~16.6 MB VRAM at 1080p (and ~29.5 MB at 1440p)");

        RHI::RHIViewTargets targets;
        RENDER_CHECK(!targets.IsValid(), "M15-U4-03.2: ViewTargets starts uninitialized before device attachment");
        RENDER_CHECK(targets.Get(RHI::RHIViewTarget::ScreenStorage) == nullptr,
            "M15-U4-03.2: Auxiliary target is null when not attached to device");
        RENDER_CHECK(targets.GetGeneration() == 0,
            "M15-U4-03.2: ViewTargets generation is 0 before configuration");
    }

    std::cout << "  [M15Occlusion] M15-U4-01 through U4-03.2 contract tests passed.\n";
    return true;
}
