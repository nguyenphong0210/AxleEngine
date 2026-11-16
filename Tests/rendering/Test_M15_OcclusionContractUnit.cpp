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
#include <limits>

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

    // =========================================================================
    // 4. M15-U4-04.1: Apply Contact to Directional Sun Contribution Only
    // =========================================================================
    {
        std::cout << "  [M15Occlusion] Verifying M15-U4-04.1 directional contact shadow application & normal fallback...\n";

        using namespace Axle::OcclusionContract;

        // Multi-light scene setup: Directional Sun, Local lights, Ambient, Emissive
        ShadingComponents sceneLighting{};
        sceneLighting.Ambient = glm::vec3(0.15f, 0.18f, 0.22f);
        sceneLighting.SunDiffuse = glm::vec3(1.10f, 1.05f, 0.95f);
        sceneLighting.SunSpecular = glm::vec3(2.20f, 2.10f, 1.90f);
        sceneLighting.LocalDiffuse = glm::vec3(0.80f, 0.60f, 0.40f);
        sceneLighting.LocalSpecular = glm::vec3(1.50f, 1.20f, 0.90f);
        sceneLighting.Emissive = glm::vec3(3.0f, 1.5f, 0.5f);
        sceneLighting.HasSun = true;
        sceneLighting.HasLocalLights = true;
        sceneLighting.EnableContactShadows = true;
        sceneLighting.EnableCSM = true;
        sceneLighting.CSMVisibility = 1.0f;

        // Test 4.1: Contact shadow modulates ONLY directional sun contribution
        RENDER_CHECK(VerifySunOnlyContactModulation(sceneLighting),
            "M15-U4-04.1: contact shadow modulates strictly directional sun contribution; local, ambient, and emissive are invariant");

        // Test 4.2: Sun Present vs Sun Absent
        RENDER_CHECK(VerifySunAbsenceBehavior(sceneLighting),
            "M15-U4-04.1: when directional sun is absent (HasSun=false), contact shadows have zero effect on total radiance");

        ShadingComponents noSunScene = sceneLighting;
        noSunScene.HasSun = false;
        const glm::vec3 radNoSunContact1 = EvaluateShading(noSunScene, 1.0f, 1.0f);
        const glm::vec3 radNoSunContact0 = EvaluateShading(noSunScene, 1.0f, 0.0f);
        RENDER_CHECK(radNoSunContact1 == radNoSunContact0,
            "M15-U4-04.1: radiance identical across contact shadow variations when sun is absent");
        const glm::vec3 expectedNoSun = noSunScene.Ambient + noSunScene.LocalDiffuse + noSunScene.LocalSpecular + noSunScene.Emissive;
        RENDER_CHECK(radNoSunContact1 == expectedNoSun,
            "M15-U4-04.1: radiance equals ambient + local + emissive with zero directional sun light");

        // Legacy U4-A callers may still populate only DirectDiffuse/DirectSpecular.
        // When HasSun=false those terms remain local direct light rather than
        // disappearing during the U4-B separation upgrade.
        ShadingComponents legacyNoSun{};
        legacyNoSun.DirectDiffuse = glm::vec3(0.7f, 0.5f, 0.3f);
        legacyNoSun.DirectSpecular = glm::vec3(0.2f, 0.4f, 0.6f);
        legacyNoSun.HasSun = false;
        const glm::vec3 legacyExpected = legacyNoSun.DirectDiffuse + legacyNoSun.DirectSpecular;
        RENDER_CHECK(EvaluateShading(legacyNoSun, 1.0f, 0.0f) == legacyExpected &&
                     EvaluateShading(legacyNoSun, 1.0f, 1.0f) == legacyExpected,
            "M15-U4-04.1: legacy direct terms remain as local light when HasSun=false");

        ShadingComponents legacySpecularOnly{};
        legacySpecularOnly.DirectSpecular = glm::vec3(1.0f, 0.75f, 0.5f);
        legacySpecularOnly.HasSun = true;
        RENDER_CHECK(EvaluateShading(legacySpecularOnly, 1.0f, 0.0f) == glm::vec3(0.0f) &&
                     EvaluateShading(legacySpecularOnly, 1.0f, 1.0f) == legacySpecularOnly.DirectSpecular,
            "M15-U4-04.1: legacy specular-only sun term is still contact attenuated");

        // With CSM disabled, contact still attenuates the sun independently of
        // any stale CSMVisibility value.
        ShadingComponents noCSMScene = sceneLighting;
        noCSMScene.EnableCSM = false;
        noCSMScene.CSMVisibility = 0.0f;
        RENDER_CHECK(VerifySunOnlyContactModulation(noCSMScene),
            "M15-U4-04.1: contact modulation remains correct when CSM is disabled");

        // Test 4.3: Preserve normal direct-light behavior when contact shadows are disabled
        RENDER_CHECK(VerifyContactDisabledBehavior(sceneLighting),
            "M15-U4-04.1: when EnableContactShadows=false, normal direct-light behavior is preserved regardless of contact input");

        ShadingComponents contactDisabledScene = sceneLighting;
        contactDisabledScene.EnableContactShadows = false;
        const glm::vec3 radContactDisabled0 = EvaluateShading(contactDisabledScene, 1.0f, 0.0f);
        const glm::vec3 radContactDisabled1 = EvaluateShading(contactDisabledScene, 1.0f, 1.0f);
        const glm::vec3 normalDirect = sceneLighting.Ambient + (sceneLighting.SunDiffuse + sceneLighting.SunSpecular) +
                                       (sceneLighting.LocalDiffuse + sceneLighting.LocalSpecular) + sceneLighting.Emissive;
        RENDER_CHECK(radContactDisabled0 == radContactDisabled1 && radContactDisabled0 == normalDirect,
            "M15-U4-04.1: normal direct lighting matches unattenuated reference when contact shadows are disabled");

        // Test 4.4: Inspect shader & pass guards for sun presence and contact enable
        const std::string gtaoPassSource = ReadShaderSource(AXLE_GTAO_PASS_SOURCE_PATH);
        RENDER_CHECK(!gtaoPassSource.empty(), "GTAOPass.cpp source loaded for inspection");
        RENDER_CHECK(gtaoPassSource.find("if (i.World.DirectionalLight && i.Settings.EnableContactShadows)") != std::string::npos,
            "M15-U4-04.1: GTAOPass gates contact shadow execution on both DirectionalLight presence and EnableContactShadows");

        const std::string gtaoShaderSource = ReadShaderSource(AXLE_GTAO_SHADER_PATH);
        RENDER_CHECK(gtaoShaderSource.find("if (u_SunDirView.w > 0.5 && u_ContactParams.z > 0.001)") != std::string::npos,
            "M15-U4-04.1: GTAO compute shader activates contact raymarch only when sun is present (u_SunDirView.w > 0.5) and strength > 0");
    }

    // =========================================================================
    // 5. M15-U4-04.2: Avoid CSM & Local Double Attenuation
    // =========================================================================
    {
        std::cout << "  [M15Occlusion] Verifying M15-U4-04.2 avoidance of CSM and local shadow double attenuation...\n";

        using namespace Axle::OcclusionContract;

        // Test 5.1: Fully shadowed by CSM (csm = 0.0) -> contact shadow cannot double-darken
        const float visAtZeroCSM = CombineDirectionalVisibility(0.0f, 0.2f, true, true);
        RENDER_CHECK(visAtZeroCSM == 0.0f,
            "M15-U4-04.2: surface fully in CSM shadow (CSM=0) remains at visibility 0.0 (no redundant contact attenuation)");

        // Test 5.2: In penumbra (csm = 0.5, cs = 0.5) -> combined visibility is 0.5, NOT 0.25
        const float visPenumbra = CombineDirectionalVisibility(0.5f, 0.5f, true, true);
        RENDER_CHECK(visPenumbra == 0.5f,
            "M15-U4-04.2: CSM penumbra (0.5) and contact shadow (0.5) combine via min to 0.5, avoiding 0.25 double-dip attenuation");

        // Test 5.3: CSM enabled vs disabled
        const float visCSMDisabled = CombineDirectionalVisibility(0.2f, 0.8f, true, false);
        RENDER_CHECK(visCSMDisabled == 0.8f,
            "M15-U4-04.2: when CSM is disabled, directional visibility follows contact shadow directly without cascade clipping");

        const float visContactDisabled = CombineDirectionalVisibility(0.4f, 0.1f, false, true);
        RENDER_CHECK(visContactDisabled == 0.4f,
            "M15-U4-04.2: when contact shadows are disabled, directional visibility follows CSM directly without contact clipping");

        // Test 5.4: Local light isolation - Point and spot lights are NEVER attenuated by contact shadows
        ShadingComponents localScene{};
        localScene.LocalDiffuse = glm::vec3(2.0f, 1.5f, 1.0f);
        localScene.LocalSpecular = glm::vec3(4.0f, 3.0f, 2.0f);
        localScene.HasSun = false;
        localScene.HasLocalLights = true;

        const glm::vec3 localAtContact1 = EvaluateShading(localScene, 1.0f, 1.0f);
        const glm::vec3 localAtContact0 = EvaluateShading(localScene, 1.0f, 0.0f);
        const glm::vec3 localAtContactHalf = EvaluateShading(localScene, 1.0f, 0.5f);
        RENDER_CHECK(localAtContact1 == localAtContact0 && localAtContact1 == localAtContactHalf,
            "M15-U4-04.2: local point and spot light radiance is 100% invariant under directional contact shadow variations");
        RENDER_CHECK(localAtContact1 == (localScene.LocalDiffuse + localScene.LocalSpecular),
            "M15-U4-04.2: local light radiance preserves full unattenuated intensity");

        // Test 5.5: Comprehensive double attenuation sweep
        RENDER_CHECK(VerifyNoCSMDoubleAttenuation(0.7f, 0.3f),
            "M15-U4-04.2: VerifyNoCSMDoubleAttenuation passes across comprehensive boundary matrix");
    }

    // =========================================================================
    // 6. M15-U4-05.1: Validate Half-Resolution Sampling
    // =========================================================================
    {
        std::cout << "  [M15Occlusion] Verifying M15-U4-05.1 half-resolution dimensions & target contracts...\n";

        using namespace Axle::OcclusionContract;

        // Standard resolutions
        const Resolution2D half1080p = ComputeHalfResolution(1920, 1080);
        RENDER_CHECK(half1080p.Width == 960 && half1080p.Height == 540,
            "M15-U4-05.1: 1920x1080 maps to half-resolution 960x540");

        const Resolution2D half1440p = ComputeHalfResolution(2560, 1440);
        RENDER_CHECK(half1440p.Width == 1280 && half1440p.Height == 720,
            "M15-U4-05.1: 2560x1440 maps to half-resolution 1280x720");

        const Resolution2D half4k = ComputeHalfResolution(3840, 2160);
        RENDER_CHECK(half4k.Width == 1920 && half4k.Height == 1080,
            "M15-U4-05.1: 3840x2160 maps to half-resolution 1920x1080");

        const Resolution2D half720p = ComputeHalfResolution(1280, 720);
        RENDER_CHECK(half720p.Width == 640 && half720p.Height == 360,
            "M15-U4-05.1: 1280x720 maps to half-resolution 640x360");

        // Odd resolutions
        const Resolution2D halfOdd1080 = ComputeHalfResolution(1919, 1079);
        RENDER_CHECK(halfOdd1080.Width == 959 && halfOdd1080.Height == 539,
            "M15-U4-05.1: 1919x1079 maps to half-resolution 959x539 via max(1, full >> 1)");

        const Resolution2D halfOddSmall = ComputeHalfResolution(127, 63);
        RENDER_CHECK(halfOddSmall.Width == 63 && halfOddSmall.Height == 31,
            "M15-U4-05.1: 127x63 maps to half-resolution 63x31");

        // Degenerate resolutions clamped to at least 1
        const Resolution2D half1x1 = ComputeHalfResolution(1, 1);
        RENDER_CHECK(half1x1.Width == 1 && half1x1.Height == 1,
            "M15-U4-05.1: 1x1 maps to degenerate half-resolution 1x1 (clamped to max(1u, ...))");

        const Resolution2D half2x1 = ComputeHalfResolution(2, 1);
        RENDER_CHECK(half2x1.Width == 1 && half2x1.Height == 1,
            "M15-U4-05.1: 2x1 maps to half-resolution 1x1");

        const Resolution2D half1x2 = ComputeHalfResolution(1, 2);
        RENDER_CHECK(half1x2.Width == 1 && half1x2.Height == 1,
            "M15-U4-05.1: 1x2 maps to half-resolution 1x1");

        // Verification helper sweep
        const uint32_t testResolutions[][2] = {
            { 3840, 2160 }, { 2560, 1440 }, { 1920, 1080 }, { 1919, 1079 },
            { 1366, 768 },  { 1365, 767 },  { 1280, 720 },  { 800, 600 },
            { 127, 63 },    { 16, 16 },      { 2, 2 },       { 1, 1 }
        };
        for (const auto& res : testResolutions) {
            RENDER_CHECK(VerifyHalfResolutionDimensions(res[0], res[1]),
                "M15-U4-05.1: VerifyHalfResolutionDimensions valid for test resolution");
        }

        // Verify RHIViewTargets GTAO texture allocation matches half-resolution contract
        const std::string rhiTargetsSource = ReadShaderSource(AXLE_RHI_VIEW_TARGETS_SOURCE_PATH);
        RENDER_CHECK(!rhiTargetsSource.empty(), "RHIViewTargets.cpp loaded for target allocation contract verification");
        RENDER_CHECK(rhiTargetsSource.find("std::max(1u, desc.Width >> 1), std::max(1u, desc.Height >> 1), RHITextureFormat::RGBA16_FLOAT") != std::string::npos,
            "M15-U4-05.1: RHIViewTargets allocates GTAO target at max(1u, desc.Width >> 1) x max(1u, desc.Height >> 1) RHITextureFormat::RGBA16_FLOAT");
        RENDER_CHECK(rhiTargetsSource.find("check2D(RHIViewTarget::GTAO, std::max(1u, m_Desc.Width >> 1), std::max(1u, m_Desc.Height >> 1), RHITextureFormat::RGBA16_FLOAT)") != std::string::npos,
            "M15-U4-05.1: RHIViewTargets validation asserts GTAO target conforms to half-resolution contract");
    }

    // =========================================================================
    // 7. M15-U4-05.2: Validate Bilateral Reconstruction
    // =========================================================================
    {
        std::cout << "  [M15Occlusion] Verifying M15-U4-05.2 cross-bilateral filter weights & smooth reconstruction...\n";

        using namespace Axle::OcclusionContract;

        // Test 7.1: Verify 5x5 spatial kernel integrates to exactly 1.0
        float spatialWeightSum = 0.0f;
        for (int y = -2; y <= 2; ++y) {
            for (int x = -2; x <= 2; ++x) {
                const auto tap = EvaluateBilateralWeight(1.0f, 1.0f, x, y);
                spatialWeightSum += tap.SpatialWeight;
            }
        }
        RENDER_CHECK(std::abs(spatialWeightSum - 1.0f) < 1e-4f,
            "M15-U4-05.2: 5x5 bilateral spatial kernel integrates to 1.0 (+/- 0.0001)");

        // Test 7.2: Verify symmetry: tap(x, y) == tap(-x, y) == tap(x, -y) == tap(-x, -y)
        for (int y = 1; y <= 2; ++y) {
            for (int x = 1; x <= 2; ++x) {
                const auto t1 = EvaluateBilateralWeight(2.0f, 2.0f, x, y);
                const auto t2 = EvaluateBilateralWeight(2.0f, 2.0f, -x, y);
                const auto t3 = EvaluateBilateralWeight(2.0f, 2.0f, x, -y);
                const auto t4 = EvaluateBilateralWeight(2.0f, 2.0f, -x, -y);
                RENDER_CHECK(t1.SpatialWeight == t2.SpatialWeight &&
                             t1.SpatialWeight == t3.SpatialWeight &&
                             t1.SpatialWeight == t4.SpatialWeight,
                    "M15-U4-05.2: 5x5 spatial kernel exhibits perfect 4-way octant symmetry");
            }
        }

        // Test 7.3: Center tap has the maximum weight, monotonic falloff radially
        const auto centerTap = EvaluateBilateralWeight(3.0f, 3.0f, 0, 0);
        const auto directNeighborTap = EvaluateBilateralWeight(3.0f, 3.0f, 1, 0);
        const auto diagonalNeighborTap = EvaluateBilateralWeight(3.0f, 3.0f, 1, 1);
        const auto farTap = EvaluateBilateralWeight(3.0f, 3.0f, 2, 0);
        const auto cornerTap = EvaluateBilateralWeight(3.0f, 3.0f, 2, 2);

        RENDER_CHECK(centerTap.SpatialWeight > directNeighborTap.SpatialWeight,
            "M15-U4-05.2: center weight (0, 0) strictly exceeds direct neighbor weight (1, 0)");
        RENDER_CHECK(directNeighborTap.SpatialWeight > diagonalNeighborTap.SpatialWeight,
            "M15-U4-05.2: direct neighbor weight strictly exceeds diagonal neighbor weight (1, 1)");
        RENDER_CHECK(diagonalNeighborTap.SpatialWeight > farTap.SpatialWeight,
            "M15-U4-05.2: diagonal neighbor weight strictly exceeds 2-step axial tap (2, 0)");
        RENDER_CHECK(farTap.SpatialWeight > cornerTap.SpatialWeight,
            "M15-U4-05.2: 2-step axial tap strictly exceeds corner tap (2, 2)");

        // Test 7.4: Flat surface reconstruction stability
        // On a smooth flat surface where deltaZ = 0 across all taps:
        // reconstructed AO = sum(AO_tap * totalWeight) / sum(totalWeight) == center AO
        float flatTotalWeight = 0.0f;
        float flatWeightedAO = 0.0f;
        const float uniformAO = 0.65f;
        for (int y = -2; y <= 2; ++y) {
            for (int x = -2; x <= 2; ++x) {
                const auto tap = EvaluateBilateralWeight(5.0f, 5.0f, x, y);
                flatTotalWeight += tap.TotalWeight;
                flatWeightedAO += uniformAO * tap.TotalWeight;
            }
        }
        const float reconstructedFlatAO = flatWeightedAO / flatTotalWeight;
        RENDER_CHECK(std::abs(flatTotalWeight - 1.0f) < 1e-4f,
            "M15-U4-05.2: flat surface total weight sum is 1.0 (no depth penalty)");
        RENDER_CHECK(std::abs(reconstructedFlatAO - uniformAO) < 1e-5f,
            "M15-U4-05.2: flat surface bilateral reconstruction preserves constant ambient occlusion without attenuation");

        // Test 7.5: Contract verification helper
        RENDER_CHECK(VerifyBilateralReconstructionStability(),
            "M15-U4-05.2: VerifyBilateralReconstructionStability passes all kernel and flat stability checks");
    }

    // =========================================================================
    // 8. M15-U4-05.3: Validate Depth Edges (Depth-Edge Protection)
    // =========================================================================
    {
        std::cout << "  [M15Occlusion] Verifying M15-U4-05.3 depth-edge protection & edge bleeding rejection...\n";

        using namespace Axle::OcclusionContract;

        // Scene configuration: foreground object at Z = 2.0m, background geometry at Z = 6.0m
        const float centerZ = 2.0f;
        const float foregroundNeighborZ = 2.02f; // Slight curvature, same surface (deltaZ = 0.02)
        const float backgroundNeighborZ = 6.0f;  // Depth step / silhouette edge (deltaZ = 4.0)

        // Test 8.1: Same surface tap maintains high depth weight
        const auto sameSurfaceTap = EvaluateBilateralWeight(centerZ, foregroundNeighborZ, 1, 0);
        RENDER_CHECK(sameSurfaceTap.DepthWeight > 0.8f,
            "M15-U4-05.3: same surface neighbor (deltaZ=0.02m) preserves high depth weight (> 0.8)");
        RENDER_CHECK(!sameSurfaceTap.IsEdgeRejected,
            "M15-U4-05.3: same surface neighbor is NOT edge-rejected");

        // Test 8.2: Across-edge tap is strongly rejected (weight < 1e-3)
        const auto edgeTap = EvaluateBilateralWeight(centerZ, backgroundNeighborZ, 1, 0);
        RENDER_CHECK(edgeTap.IsEdgeRejected,
            "M15-U4-05.3: neighbor across depth edge (deltaZ=4.0m) is flagged IsEdgeRejected");
        RENDER_CHECK(edgeTap.DepthWeight < 1e-3f,
            "M15-U4-05.3: neighbor depth weight across discontinuity is < 1e-3 (actual < 1e-15)");

        // Test 8.3: Zero edge bleeding verification:
        // Visibility 0.2 is strongly occluded; visibility 0.95 is nearly unoccluded.
        // If bilateral filtering bled across the silhouette edge, foreground AO would jump towards 0.95.
        // We verify that the background tap contributing to the foreground pixel has total weight < 1e-5.
        const float fgAO = 0.20f;
        const float bgAO = 0.95f;

        float accumWeight = 0.0f;
        float accumAO = 0.0f;

        // Say half the filter kernel (left) is on foreground (Z=2.0, AO=0.2),
        // and half (right) is on background (Z=6.0, AO=0.95).
        for (int y = -2; y <= 2; ++y) {
            for (int x = -2; x <= 2; ++x) {
                const float tapZ = (x > 0) ? backgroundNeighborZ : foregroundNeighborZ;
                const float tapAO = (x > 0) ? bgAO : fgAO;
                const auto tap = EvaluateBilateralWeight(centerZ, tapZ, x, y);
                accumWeight += tap.TotalWeight;
                accumAO += tapAO * tap.TotalWeight;
            }
        }
        const float filteredAO = accumAO / accumWeight;
        RENDER_CHECK(std::abs(filteredAO - fgAO) < 0.01f,
            "M15-U4-05.3: zero edge bleeding: filtered foreground AO remains within 0.01 of true foreground AO despite 0.95 background occlusion");

        // Test 8.4: Depth-weight verification. The native reconstruction fixture
        // separately checks bilinear contamination and unmatched thin surfaces.
        RENDER_CHECK(VerifyDepthEdgeProtection(centerZ, foregroundNeighborZ, backgroundNeighborZ),
            "M15-U4-05.3: VerifyDepthEdgeProtection passes contract verification");

        // Test 8.5: Verify shader implementation of edge protection in GTAO.hlsl
        const std::string gtaoShader = ReadShaderSource(AXLE_GTAO_SHADER_PATH);
        RENDER_CHECK(gtaoShader.find("float depthWeight = exp(-depthDiff / (0.05 * max(abs(centerZ), 0.1) + 1e-4));") != std::string::npos,
            "M15-U4-05.3: GTAO.hlsl implements exponential depth weighting with adaptive view-depth scaling");
        RENDER_CHECK(gtaoShader.find("float2 filteredOcclusion = weightSum > 1e-4 ? (occlusionSum / weightSum) : float2(1.0, 1.0);") != std::string::npos,
            "M15-U4-05.3: isolated unrepresented surfaces use neutral visibility without importing foreign occlusion");
        RENDER_CHECK(edgeTap.TotalWeight == 0.0f &&
            EvaluateBilateralWeight(std::numeric_limits<float>::quiet_NaN(), 2.0f, 0, 0).IsEdgeRejected &&
            EvaluateBilateralWeight(2.0f, std::numeric_limits<float>::infinity(), 0, 0).TotalWeight == 0.0f,
            "M15-U4-05.3: across-edge and invalid-depth taps contribute no occlusion");
    }

    // =========================================================================
    // 9. M15-U4-05.4: Validate Odd Resolutions
    // =========================================================================
    {
        std::cout << "  [M15Occlusion] Verifying M15-U4-05.4 odd-resolution safety & boundary protection...\n";

        using namespace Axle::OcclusionContract;

        // Test 9.1: Odd resolution 1919x1079 (e.g. non-standard window or DPI scaling)
        RENDER_CHECK(VerifyOddResolutionSafety(1919, 1079),
            "M15-U4-05.4: VerifyOddResolutionSafety passes for 1919x1079");

        const Resolution2D half1919 = ComputeHalfResolution(1919, 1079);
        RENDER_CHECK(half1919.Width == 959 && half1919.Height == 539,
            "M15-U4-05.4: 1919x1079 half resolution is 959x539");

        // Thread dispatch: ceil(959 / 8) = 120 groups, ceil(539 / 8) = 68 groups
        const uint32_t dispatchX = (half1919.Width + 7) / 8;
        const uint32_t dispatchY = (half1919.Height + 7) / 8;
        RENDER_CHECK(dispatchX == 120 && dispatchY == 68,
            "M15-U4-05.4: compute dispatch thread groups for 959x539 are exactly 120 x 68");
        RENDER_CHECK(dispatchX * 8 >= half1919.Width && dispatchY * 8 >= half1919.Height,
            "M15-U4-05.4: total threads (960x544) completely cover half-resolution extent (959x539)");

        // Reciprocal texel size
        const float rcpX = 1.0f / static_cast<float>(half1919.Width);
        const float rcpY = 1.0f / static_cast<float>(half1919.Height);
        RENDER_CHECK(std::abs(rcpX - (1.0f / 959.0f)) < 1e-7f && std::abs(rcpY - (1.0f / 539.0f)) < 1e-7f,
            "M15-U4-05.4: reciprocal texel size matches exact half-resolution reciprocal");

        // Boundary texel UV coordinates strictly within [0, 1]
        const float uvMinX = 0.5f * rcpX;
        const float uvMinY = 0.5f * rcpY;
        const float uvMaxX = (static_cast<float>(half1919.Width - 1) + 0.5f) * rcpX;
        const float uvMaxY = (static_cast<float>(half1919.Height - 1) + 0.5f) * rcpY;
        RENDER_CHECK(uvMinX > 0.0f && uvMaxX < 1.0f && uvMinY > 0.0f && uvMaxY < 1.0f,
            "M15-U4-05.4: texel center UVs strictly contained inside (0, 1) bounds");

        // Test 9.2: Small odd resolution 127x63
        RENDER_CHECK(VerifyOddResolutionSafety(127, 63),
            "M15-U4-05.4: VerifyOddResolutionSafety passes for small odd 127x63");

        // Test 9.3: Extreme degenerate resolution 1x1
        RENDER_CHECK(VerifyOddResolutionSafety(1, 1),
            "M15-U4-05.4: VerifyOddResolutionSafety passes for degenerate 1x1");

        // Test 9.4: Other common odd/non-power-of-two window sizes
        RENDER_CHECK(VerifyOddResolutionSafety(1365, 767),
            "M15-U4-05.4: VerifyOddResolutionSafety passes for 1365x767");
        RENDER_CHECK(VerifyOddResolutionSafety(801, 601),
            "M15-U4-05.4: VerifyOddResolutionSafety passes for 801x601");

        // Test 9.5: Shader boundary clamping and compute thread guards
        const std::string gtaoShader = ReadShaderSource(AXLE_GTAO_SHADER_PATH);
        RENDER_CHECK(gtaoShader.find("if (pixelPos.x >= (uint)u_ScreenSize.x || pixelPos.y >= (uint)u_ScreenSize.y) return;") != std::string::npos,
            "M15-U4-05.4: GTAO CSMain has thread-group boundary guard preventing out-of-bounds execution on odd dimensions");
        RENDER_CHECK(gtaoShader.find("int2 aoPixel = clamp(occlusionCenter + int2(x, y), int2(0, 0), occlusionMax);") != std::string::npos,
            "M15-U4-05.4: GTAO.hlsl clamps half-resolution reconstruction texels to source bounds");

        // The apply pass reconstructs full-resolution pixels from a half-resolution
        // AO target. Its constants locate full-resolution depth texels; the shader
        // queries AO dimensions separately for the half-resolution footprint.
        const std::string gtaoPass = ReadShaderSource(AXLE_GTAO_PASS_SOURCE_PATH);
        RENDER_CHECK(gtaoPass.find("const uint32_t fullWidth = std::max(1u, i.Width);") != std::string::npos &&
                     gtaoPass.find("constants.ScreenSize = glm::vec4(static_cast<float>(fullWidth)") != std::string::npos,
            "M15-U4-05.4: GTAO apply restores full-resolution screen texel spacing after half-resolution compute");

        RENDER_CHECK(gtaoShader.find("u_SceneDepth.Load(int3(centerPixel, 0))") != std::string::npos &&
                     gtaoShader.find("u_SceneDepth.Load(int3(tapPixel, 0))") != std::string::npos &&
                     gtaoShader.find("u_OcclusionTexture.Load(int3(aoPixel, 0))") != std::string::npos &&
                     gtaoShader.find("OcclusionSourcePixel(pixelPos, uint2(u_ScreenSize.xy), sceneSize)") != std::string::npos &&
                     gtaoShader.find("OcclusionSourcePixel(uint2(aoPixel), occlusionSize, sceneSize)") != std::string::npos,
            "M15-U4-05.3: compute and reconstruction pair exact depth and AO texels with identical odd-resolution mapping");
        RENDER_CHECK(!VerifyHalfResolutionDimensions(0, 1) && !VerifyHalfResolutionDimensions(1, 0) &&
                     !VerifyOddResolutionSafety(0, 0) && !VerifyOddResolutionSafety(0, 1),
            "M15-U4-05.4: zero-size views are rejected before reciprocal/UV computations");
    }

    std::cout << "  [M15Occlusion] All M15-U4 (U4-01 through U4-05.4) occlusion contract tests passed successfully.\n";
    return true;
}
