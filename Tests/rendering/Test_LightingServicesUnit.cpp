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
    const RHI::RHIResourceHandle cascade{1, 11, 1}, local{1, 22, 1};
    auto frame = lighting.BuildFrame(world, settings, ShadowPublication{true, true, cascade, local});
    RENDER_CHECK(frame && frame.Status == LightingFrameStatus::Supported && frame.DirectionalCount == 1 &&
                 frame.PointCount == 1 && frame.SpotCount == 1 && frame.RectCount == 1 &&
                 frame.Shadows.CascadeGeneration == cascade, "lighting service owns light counts and exact shadow publication metadata");
    RENDER_CHECK(frame.Sun.Illuminance == 100000.0f && frame.PointLights[0].Intensity == 1000.0f &&
                 frame.SpotLights[0].Intensity == 4000.0f && frame.RectLights[0].SignedIntensity == 10.0f,
        "lighting service owns physical unit conversion and CPU light packing");
    settings.EnableShadows = false;
    auto noShadows = lighting.BuildFrame(world, settings, ShadowPublication{true, true, cascade, local});
    RENDER_CHECK(!noShadows.Shadows.CascadeGeneration && noShadows.DirectionalCount == 1,
        "disabling shadows does not remove direct-light inputs");
    world.AdditionalDirectionalLights.push_back(RenderDirectionalLight{});
    world.AdditionalDirectionalLights.back().Intensity = 2.0f;
    const auto additional = lighting.BuildFrame(world, settings, ShadowPublication{true, true, cascade, local});
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
    capped.AdditionalDirectionalLights.clear();
    capped.PointLights.resize(MaxLightingPointLights+2);
    capped.SpotLights.resize(MaxLightingSpotLights+2);
    capped.RectLights.resize(MaxLightingRectLights+2);
    capped.PointLights[0].Intensity=25.0f;
    capped.SpotLights[0].Intensity=25.0f;
    capped.RectLights[1].TwoSided=true;
    const auto packedCaps=lighting.BuildFrame(capped, settings);
    RENDER_CHECK(packedCaps.PointCount==MaxLightingPointLights && packedCaps.SpotCount==MaxLightingSpotLights &&
                 packedCaps.RectCount==MaxLightingRectLights && packedCaps.PointLights[0].Intensity==25.0f &&
                 packedCaps.SpotLights[0].Intensity==25.0f && packedCaps.RectLights[1].SignedIntensity==-10.0f,
        "service caps all arrays and preserves physical intensities and two-sided sign");
    settings.EnableShadows=true;
    const auto unavailableShadows=lighting.BuildFrame(world, settings, {true, true, {}, {}});
    RENDER_CHECK(unavailableShadows.Status==LightingFrameStatus::Fallback &&
                 !unavailableShadows.Shadows.CascadedAvailable && !unavailableShadows.Shadows.LocalAtlasAvailable,
        "availability without a published shadow generation cannot claim a ready atlas");

    ReflectionService reflection;
    auto authored = reflection.Prepare(world, {3,0,0}, true, 7);
    RENDER_CHECK(authored.State == ReflectionState::AuthoredPending && authored.SelectedProbeEntityID == 1 &&
                 authored.CubemapPath == "near_probe.axleasset" &&
                 !authored.PrefilterReady && !authored.ProbeGeneration && authored.ParallaxCorrected,
        "authored selection remains pending until a native generation is published");
    const RHI::RHIResourceHandle prefilter{2, 33, 4};
    const auto published=reflection.ResolvePublication(authored, {prefilter, {}}, true, true);
    RENDER_CHECK(published.State==ReflectionState::AuthoredReady && published.PrefilterReady && published.ProbeGeneration==prefilter,
        "native publication supplies exact prefilter identity and readiness");
    const auto failedProbe=reflection.ResolvePublication(authored, {{}, "missing source"}, true, true);
    RENDER_CHECK(failedProbe.State==ReflectionState::Fallback && !failedProbe.PrefilterReady &&
                 !failedProbe.ProbeGeneration && failedProbe.Fallback==ReflectionFallback::SSR,
        "failed publication preserves SSR fallback without claiming ready feedback");
    const auto msaaFallback=reflection.ResolvePublication(authored, {}, false, true);
    const auto neutralFallback=reflection.ResolvePublication(authored, {}, false, false);
    RENDER_CHECK(msaaFallback.Fallback==ReflectionFallback::SkyIBL && neutralFallback.Fallback==ReflectionFallback::NeutralAmbient,
        "publication fallback uses executable SSR and available sky, rather than requested settings");
    RenderWorld noProbe;
    auto fallback = reflection.Prepare(noProbe, {}, true, 8);
    RENDER_CHECK(fallback.State == ReflectionState::Fallback && fallback.Fallback == ReflectionFallback::SSR,
        "reflection service exposes SSR fallback when no local probe exists");
    RenderWorld emptyNearest;
    emptyNearest.ReflectionProbes.push_back(RenderReflectionProbe{3, {1,0,0}, {1,1,1}, {}, 5.0f, 1.0f, 0, true, ""});
    emptyNearest.ReflectionProbes.push_back(RenderReflectionProbe{4, {8,0,0}, {20,20,20}, {}, 5.0f, 1.0f, 0, true, "far_authored.axleasset"});
    const auto authoredPastEmpty = reflection.Prepare(emptyNearest, {0,0,0}, true, 8);
    RENDER_CHECK(authoredPastEmpty.State == ReflectionState::AuthoredPending && authoredPastEmpty.SelectedProbeEntityID == 4,
        "reflection service ignores unauthored probes when selecting an authored generation");
    emptyNearest.ReflectionProbes[0].CubemapPath="near_outside.axleasset";
    emptyNearest.ReflectionProbes[0].Position={2,0,0};
    emptyNearest.ReflectionProbes[0].EntityID=emptyNearest.ReflectionProbes[1].EntityID=0;
    const auto influenced=reflection.Prepare(emptyNearest, {}, false, 8);
    RENDER_CHECK(influenced.SelectedProbe && influenced.SelectedProbe->CubemapPath=="far_authored.axleasset",
        "influence selects the exact authored snapshot even with duplicate/default entity IDs");
    emptyNearest.ReflectionProbes.pop_back();
    const auto outside=reflection.Prepare(emptyNearest, {}, false, 8);
    RENDER_CHECK(!outside.SelectedProbe && !outside.PrefilterReady && outside.Fallback==ReflectionFallback::NeutralAmbient,
        "out-of-influence probes preserve the neutral fallback without authored sky");
    auto invalidation = reflection.EvaluateInvalidation({false, true, false, false, true, false});
    RENDER_CHECK(invalidation.Any() && invalidation.SceneRevisionChanged && invalidation.LightingChanged,
        "reflection invalidation tracks scene and lighting changes independently");

    GIService gi;
    auto disabled = gi.Prepare(world, settings, true, 9);
    RENDER_CHECK(disabled.Status == GIFrameStatus::Disabled && disabled.Fallback == GIFallback::IBL,
        "GI service is inert and falls back to IBL without a grid");
    world.DDGIProbeGrid = RenderDDGIProbeGrid{};
    settings.EnableDDGI = true;
    RENDER_CHECK(!gi.Prepare(world, settings, true, 10).UpdateScheduled,
        "a disabled authored GI grid cannot schedule an update");
    world.DDGIProbeGrid->Enabled=true;
    auto experimental = gi.Prepare(world, settings, true, 10);
    RENDER_CHECK(experimental.Status == GIFrameStatus::Experimental && experimental.UpdateScheduled &&
                 !experimental.IrradianceAvailable && !experimental.VisibilityAvailable &&
                 !experimental.SamplingEnabled && experimental.Fallback==GIFallback::IBL,
        "GI service exposes experimental update boundary without pretending feedback is ready");
    auto noCompute = gi.Prepare(world, settings, false, 10);
    RENDER_CHECK(noCompute.Status == GIFrameStatus::Fallback && noCompute.Fallback == GIFallback::IBL,
        "GI service falls back to IBL when compute capability is unavailable");
    world.DDGIProbeGrid->GridSpacing.x=0.0f;
    const auto invalidGI=gi.Prepare(world, settings, true, 10);
    RENDER_CHECK(invalidGI.Status==GIFrameStatus::Fallback && !invalidGI.UpdateScheduled && invalidGI.Fallback==GIFallback::IBL,
        "invalid GI grid spacing cannot schedule native work");

    std::cout << "  [LightingServices] Lighting/reflection/GI boundary tests passed.\n";
    return true;
}
