#include "RenderTestCommon.hpp"
#include <Axle/CascadedShadowMap.hpp>
#include <Axle/LightingService.hpp>
#include <Axle/LocalShadowMap.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {
bool FiniteMatrix(const glm::mat4& matrix) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(matrix[c][r])) return false;
    return true;
}
}

bool RunM15PrimaryLightingReferenceUnitTests() {
    using namespace Axle;
    std::cout << "  [M15PrimaryLightingReference] Running units and shadow contracts...\n";

    RenderWorld world;
    world.DirectionalLight = RenderDirectionalLight{};
    world.DirectionalLight->Direction = {0.4f, 1.0f, 0.3f};
    world.DirectionalLight->Intensity = 1.0f;
    RenderPointLight point;
    point.EntityID = 10; point.Position = {1.0f, 2.0f, 1.0f}; point.Radius = 12.0f; point.Intensity = 2.0f;
    world.PointLights.push_back(point);
    RenderSpotLight spot;
    spot.EntityID = 20; spot.Position = {-1.0f, 3.0f, 1.0f}; spot.Direction = {0.0f, -1.0f, 0.0f};
    spot.Range = 18.0f; spot.Intensity = 2.0f; spot.InnerAngle = 20.0f; spot.OuterAngle = 35.0f;
    world.SpotLights.push_back(spot);

    SceneRenderSettings settings;
    settings.EnableShadows = true;
    const RHI::RHIResourceHandle cascade{1, 2, 3}, localHandle{1, 4, 5};
    LightingService lighting;
    const auto frame = lighting.BuildFrame(world, settings, {true, true, cascade, localHandle});
    RENDER_CHECK(frame.Status == LightingFrameStatus::Supported && frame.DirectionalCount == 1 &&
                 frame.PointCount == 1 && frame.SpotCount == 1 && frame.Sun.Illuminance == 100000.0f &&
                 frame.PointLights[0].Intensity == 2000.0f && frame.SpotLights[0].Intensity == 1000.0f &&
                 frame.Shadows.CascadeGeneration == cascade && frame.Shadows.LocalGeneration == localHandle,
        "directional, point and spot units preserve the frozen calibration and shadow publication");
    const auto fallback = lighting.BuildFrame(world, settings, {});
    RENDER_CHECK(fallback.Status == LightingFrameStatus::Fallback && fallback.Sun.Illuminance == 100000.0f &&
                 fallback.PointCount == 1 && fallback.SpotCount == 1 &&
                 fallback.Diagnostic.find("shadow_publication_unavailable") != std::string::npos,
        "missing shadow publication keeps direct light inputs and reports an explicit fallback");
    settings.EnableShadows = false;
    const auto disabled = lighting.BuildFrame(world, settings, {true, true, cascade, localHandle});
    RENDER_CHECK(disabled.Status == LightingFrameStatus::Supported && !disabled.Shadows.CascadeGeneration &&
                 !disabled.Shadows.LocalGeneration && disabled.PointCount == 1 && disabled.SpotCount == 1,
        "disabling shadows does not remove directional, point or spot lighting");

    // Directional-light CSM: right-handed camera, zero-to-one depth, monotonic
    // splits and finite matrices, including the invalid-input clearing policy.
    CascadedShadowMapSettings csmSettings;
    csmSettings.CascadeCount = 4; csmSettings.Resolution = 1024; csmSettings.NearClip = 0.1f;
    csmSettings.FarClip = 200.0f; csmSettings.MaxDistance = 60.0f; csmSettings.SplitLambda = 0.65f;
    CascadedShadowMap csm(csmSettings);
    const auto view = glm::lookAtRH(glm::vec3(7, 8, 12), glm::vec3(0), glm::vec3(0, 1, 0));
    const auto projection = glm::perspectiveRH_ZO(glm::radians(55.0f), 1.0f, 0.1f, 200.0f);
    std::string error;
    RENDER_CHECK(csm.Update(view, projection, glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f)), &error) && error.empty() &&
                 csm.GetFrame().Valid && csm.GetFrame().CascadeCount == 4,
        "directional CSM publishes a valid four-cascade frame");
    float previous = csmSettings.NearClip;
    for (uint32_t i = 0; i < 4; ++i) {
        const auto& cascadeData = csm.GetCascade(i);
        RENDER_CHECK(cascadeData.NearDistance >= previous && cascadeData.FarDistance > cascadeData.NearDistance &&
                     cascadeData.FarDistance <= csmSettings.MaxDistance && cascadeData.TexelWorldSize > 0.0f &&
                     cascadeData.DepthRange > 0.0f && FiniteMatrix(cascadeData.LightViewProjection),
            "directional cascade split and light matrix remain finite and ordered");
        previous = cascadeData.FarDistance;
    }
    RENDER_CHECK(std::abs(previous - csmSettings.MaxDistance) < 1e-4f,
        "directional cascade coverage ends exactly at the authored shadow distance");
    RENDER_CHECK(!csm.Update(view, projection, glm::vec3(0.0f), &error) && !csm.GetFrame().Valid &&
                 csm.GetFrame().CascadeCount == 0 && !error.empty(),
        "invalid directional light direction clears CSM and returns an explicit error");

    // Point/spot local shadows: deterministic selection, ZO face projections,
    // atlas bounds and invalid-setting fallback.
    settings.EnableShadows = true; settings.LocalShadowResolution = 256;
    settings.MaxShadowedPointLights = 2; settings.MaxShadowedSpotLights = 4;
    LocalShadowFrame localFrame;
    RENDER_CHECK(BuildLocalShadowFrame(world, glm::vec3(0), settings, localFrame) &&
                 localFrame.Views.size() == 7 && localFrame.PointViews[0] >= 0 && localFrame.SpotViews[0] >= 0 &&
                 localFrame.AtlasSize == 1024,
        "point and spot lights publish bounded local shadow views");
    const int firstFace = localFrame.PointViews[0];
    const glm::vec3 axes[]{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (int face = 0; face < 6; ++face) {
        const auto& shadowView = localFrame.Views[static_cast<size_t>(firstFace + face)];
        const auto clip = shadowView.Matrix * glm::vec4(point.Position + axes[face] * 2.0f, 1.0f);
        RENDER_CHECK(clip.w > 0.0f && std::abs(clip.x) < 1e-5f && std::abs(clip.y) < 1e-5f &&
                     clip.z > 0.0f && clip.z < clip.w && shadowView.X + shadowView.Size <= localFrame.AtlasSize &&
                     shadowView.Y + shadowView.Size <= localFrame.AtlasSize,
            "point shadow cubemap face maps its axis center into the zero-to-one atlas");
    }
    const auto& spotView = localFrame.Views[static_cast<size_t>(localFrame.SpotViews[0])];
    const auto spotClip = spotView.Matrix * glm::vec4(spot.Position + glm::vec3(0, -2, 0), 1.0f);
    RENDER_CHECK(spotClip.w > 0.0f && std::abs(spotClip.x) < 1e-5f && std::abs(spotClip.y) < 1e-5f &&
                 spotClip.z > 0.0f && spotClip.z < spotClip.w,
        "spot shadow direction maps its cone center into zero-to-one depth");
    settings.LocalShadowResolution = 100;
    RENDER_CHECK(!BuildLocalShadowFrame(world, glm::vec3(0), settings, localFrame) && localFrame.Views.empty() &&
                 localFrame.Diagnostic.find("LOCAL_SHADOW_SETTINGS") != std::string::npos,
        "invalid local shadow resolution disables point/spot shadows with a diagnostic");

    std::ifstream forward(AXLE_FORWARD_PBR_SHADER_PATH);
    const std::string forwardSource{std::istreambuf_iterator<char>(forward), std::istreambuf_iterator<char>()};
    std::ifstream localShader(AXLE_LOCAL_SHADOW_SHADER_PATH);
    const std::string localSource{std::istreambuf_iterator<char>(localShader), std::istreambuf_iterator<char>()};
    RENDER_CHECK(forwardSource.find("SunShadowVisibility(input.WorldPos, N, sunDir, shadowDepth)") != std::string::npos &&
                 forwardSource.find("PointShadowVisibility(p,pLight.PositionAndRadius.xyz,input.WorldPos,N,L)") != std::string::npos &&
                 forwardSource.find("LocalShadowVisibility(u_LocalShadowViews[4+s/4][s%4],input.WorldPos,N,L)") != std::string::npos,
        "directional, point and spot direct-light branches consume their matching shadow visibility");
    RENDER_CHECK(localSource.find("SampleCmpLevelZero") != std::string::npos &&
                 localSource.find("return 1;") != std::string::npos &&
                 localSource.find("clamp(uv+float2(x,y)*texel*u_LocalShadowBias[view].w") != std::string::npos &&
                 localSource.find("int first=u_LocalShadowViews[index/4][index%4]") != std::string::npos,
        "local shadow receiver uses comparison sampling, clamped PCF and explicit unsupported fallback");

    std::cout << "  [M15PrimaryLightingReference] Units and shadow contracts passed.\n";
    return true;
}
