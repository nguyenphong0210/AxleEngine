#include "RenderTestCommon.hpp"
#include <Axle/LightingService.hpp>
#include <Axle/ReflectionService.hpp>
#include <Axle/GIService.hpp>

bool RunLightingServiceUnitTests() {
    using namespace Axle;
    std::cout << "  [LightingServices] Running lighting/reflection/GI boundary tests...\n";
    RenderWorld world;
    world.DirectionalLight = RenderDirectionalLight{};
    world.PointLights.push_back(RenderPointLight{});
    world.SpotLights.push_back(RenderSpotLight{});
    world.RectLights.push_back(RenderRectLight{});
    world.ReflectionProbes.push_back(RenderReflectionProbe{1, {4,0,0}, {1,1,1}, {}, 5.0f, 1.0f, 0, true, "near_probe.axleasset"});
    world.ReflectionProbes.push_back(RenderReflectionProbe{2, {20,0,0}, {1,1,1}, {}, 5.0f, 1.0f, 0, true, "far_probe.axleasset"});
    SceneRenderSettings settings;

    LightingService lighting;
    auto frame = lighting.BuildFrame(world, settings, ShadowPublication{true, true, 11, 22});
    RENDER_CHECK(frame && frame.Status == LightingFrameStatus::Supported && frame.DirectionalCount == 1 &&
                 frame.PointCount == 1 && frame.SpotCount == 1 && frame.RectCount == 1 &&
                 frame.Shadows.CascadeGeneration == 11, "lighting service owns light counts and shadow publication metadata");
    RENDER_CHECK(frame.Sun.Illuminance == 100000.0f && frame.PointLights[0].Intensity == 1000.0f &&
                 frame.SpotLights[0].Intensity == 4000.0f && frame.RectLights[0].SignedIntensity == 10.0f,
        "lighting service owns physical unit conversion and CPU light packing");
    settings.EnableShadows = false;
    auto noShadows = lighting.BuildFrame(world, settings, ShadowPublication{true, true, 11, 22});
    RENDER_CHECK(!noShadows.Shadows.CascadeGeneration && noShadows.DirectionalCount == 1,
        "disabling shadows does not remove direct-light inputs");
    world.AdditionalDirectionalLights.push_back(RenderDirectionalLight{});
    world.AdditionalDirectionalLights.back().Intensity = 2.0f;
    const auto additional = lighting.BuildFrame(world, settings, ShadowPublication{true, true, 11, 22});
    RENDER_CHECK(additional.PointCount == 2 && additional.PointLights[0].Directional &&
                 additional.PointLights[0].Intensity == 7.0f,
        "additional directional lights use the service-owned directional packing contract");
    RenderWorld noSun;
    const auto fallbackSun = lighting.BuildFrame(noSun, settings);
    RENDER_CHECK(std::abs(glm::length(fallbackSun.Sun.Direction) - 1.0f) < 0.001f &&
                 fallbackSun.Sun.Illuminance == 0.0f,
        "lighting service preserves normalized fallback sun direction");
    RenderWorld capped;
    capped.AdditionalDirectionalLights.resize(MaxLightingPointLights + 1);
    const auto cappedFrame = lighting.BuildFrame(capped, settings);
    RENDER_CHECK(cappedFrame.DirectionalCount == MaxLightingPointLights &&
                 cappedFrame.PointCount == MaxLightingPointLights,
        "lighting service reports representable directional and point capacity");

    ReflectionService reflection;
    auto authored = reflection.Prepare(world, {3,0,0}, true, 7);
    RENDER_CHECK(authored.State == ReflectionState::AuthoredReady && authored.SelectedProbeEntityID == 1 &&
                 authored.CubemapPath == "near_probe.axleasset" &&
                 authored.PrefilterReady && authored.ParallaxCorrected,
        "reflection service selects nearest authored probe and reports ready state");
    RenderWorld noProbe;
    auto fallback = reflection.Prepare(noProbe, {}, true, 8);
    RENDER_CHECK(fallback.State == ReflectionState::Fallback && fallback.Fallback == ReflectionFallback::SSR,
        "reflection service exposes SSR fallback when no local probe exists");
    RenderWorld emptyNearest;
    emptyNearest.ReflectionProbes.push_back(RenderReflectionProbe{3, {1,0,0}, {1,1,1}, {}, 5.0f, 1.0f, 0, true, ""});
    emptyNearest.ReflectionProbes.push_back(RenderReflectionProbe{4, {8,0,0}, {1,1,1}, {}, 5.0f, 1.0f, 0, true, "far_authored.axleasset"});
    const auto authoredPastEmpty = reflection.Prepare(emptyNearest, {0,0,0}, true, 8);
    RENDER_CHECK(authoredPastEmpty.State == ReflectionState::AuthoredReady && authoredPastEmpty.SelectedProbeEntityID == 4,
        "reflection service ignores unauthored probes when selecting an authored generation");
    auto invalidation = reflection.EvaluateInvalidation({false, true, false, false, true, false});
    RENDER_CHECK(invalidation.Any() && invalidation.SceneRevisionChanged && invalidation.LightingChanged,
        "reflection invalidation tracks scene and lighting changes independently");

    GIService gi;
    auto disabled = gi.Prepare(world, settings, true, 9);
    RENDER_CHECK(disabled.Status == GIFrameStatus::Disabled && disabled.Fallback == GIFallback::IBL,
        "GI service is inert and falls back to IBL without a grid");
    world.DDGIProbeGrid = RenderDDGIProbeGrid{};
    settings.EnableDDGI = true;
    auto experimental = gi.Prepare(world, settings, true, 10);
    RENDER_CHECK(experimental.Status == GIFrameStatus::Experimental && experimental.UpdateScheduled &&
                 !experimental.IrradianceAvailable && !experimental.VisibilityAvailable,
        "GI service exposes experimental update boundary without pretending feedback is ready");
    auto noCompute = gi.Prepare(world, settings, false, 10);
    RENDER_CHECK(noCompute.Status == GIFrameStatus::Fallback && noCompute.Fallback == GIFallback::IBL,
        "GI service falls back to IBL when compute capability is unavailable");

    std::cout << "  [LightingServices] Lighting/reflection/GI boundary tests passed.\n";
    return true;
}
