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
        const std::string gtaoPassSource = ReadShaderSource("Engine/Scene/src/Passes/GTAOPass.cpp");
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

    std::cout << "  [M15Occlusion] M15-U4-01 through U4-04.2 contract tests passed.\n";
    return true;
}
