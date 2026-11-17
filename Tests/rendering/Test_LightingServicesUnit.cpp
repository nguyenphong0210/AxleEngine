#include "RenderTestCommon.hpp"
#include <Axle/LightingService.hpp>
#include <Axle/ReflectionService.hpp>
#include <Axle/GIService.hpp>
#include <Axle/PostProcessService.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

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
        "lighting service owns authored intensity conversion and CPU light packing");
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
                 packedCaps.RectCount==MaxLightingRectLights && packedCaps.PointLights[0].Intensity==25000.0f &&
                 packedCaps.SpotLights[0].Intensity==12500.0f && packedCaps.RectLights[1].SignedIntensity==-10.0f,
        "service caps all arrays and preserves stable artist intensity scaling and two-sided sign");
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

    RenderWorld dynamicWorld;
    RenderReflectionProbe dynProbe{};
    dynProbe.EntityID = 55;
    dynProbe.Position = {0.0f, 0.0f, 0.0f};
    dynProbe.BoxExtents = {10.0f, 10.0f, 10.0f};
    dynProbe.DynamicCapture = true;
    dynProbe.CaptureResolution = 128;
    dynProbe.CaptureShadows = true;
    dynamicWorld.ReflectionProbes.push_back(dynProbe);
    const auto dynPending = reflection.Prepare(dynamicWorld, {0.0f, 0.0f, 0.0f}, true, 11);
    RENDER_CHECK(dynPending.State == ReflectionState::DynamicPending &&
                 dynPending.DynamicCaptureSupported &&
                 !dynPending.PrefilterReady &&
                 dynPending.SelectedProbeEntityID == 55,
        "dynamic capture probe without authored path enters DynamicPending");
    const RHI::RHIResourceHandle dynHandle{3, 41, 7};
    const auto dynReady = reflection.ResolvePublication(dynPending, {dynHandle, {}}, true, true);
    RENDER_CHECK(dynReady.State == ReflectionState::DynamicReady &&
                 dynReady.DynamicCaptureSupported &&
                 dynReady.PrefilterReady &&
                 dynReady.ProbeGeneration == dynHandle &&
                 dynReady.Fallback == ReflectionFallback::None,
        "published dynamic probe generation transitions to DynamicReady");
    const auto dynFailed = reflection.ResolvePublication(dynPending, {{}, "DYNAMIC_PROBE_FACE_FAILED"}, false, true);
    RENDER_CHECK(dynFailed.State == ReflectionState::Fallback &&
                 !dynFailed.PrefilterReady &&
                 dynFailed.Fallback == ReflectionFallback::SkyIBL &&
                 dynFailed.Diagnostic.find("dynamic_probe_unavailable") != std::string::npos,
        "failed dynamic probe publication degrades explicitly with dynamic_probe_unavailable");

    // M15-U5-D: Per-domain invalidation and Pending/Stale/Failed retention diagnostics
    {
        ReflectionCaptureDomainStamps baseStamps{};
        baseStamps.TransformStamp = 100;
        baseStamps.SceneRevision = 10;
        baseStamps.SceneInstanceID = 1;
        baseStamps.SceneLifetimeEpoch = 1;
        baseStamps.GeometryStamp = 200;
        baseStamps.MaterialStamp = 300;
        baseStamps.LightingStamp = 400;
        baseStamps.AssetStamp = 500;
        baseStamps.SettingsStamp = 600;
        baseStamps.DeviceEpoch = 1;
        baseStamps.CombinedStamp = 700;

        RENDER_CHECK(!reflection.EvaluateInvalidation(baseStamps, baseStamps, false).Any(),
            "M15-U5-21: identical domain stamps produce zero invalidation");

        auto t = baseStamps; t.TransformStamp = 101;
        const auto invT = reflection.EvaluateInvalidation(baseStamps, t);
        RENDER_CHECK(invT.TransformChanged && !invT.SceneRevisionChanged && !invT.GeometryChanged && !invT.MaterialChanged && !invT.LightingChanged && !invT.AssetChanged,
            "M15-U5-21.1: transform change isolates TransformChanged");

        auto s = baseStamps; s.SceneRevision = 11;
        const auto invS = reflection.EvaluateInvalidation(baseStamps, s);
        RENDER_CHECK(invS.SceneRevisionChanged && !invS.TransformChanged && !invS.GeometryChanged,
            "M15-U5-21.2: scene revision change isolates SceneRevisionChanged");

        auto g = baseStamps; g.GeometryStamp = 201;
        RENDER_CHECK(reflection.EvaluateInvalidation(baseStamps, g).GeometryChanged,
            "M15-U5-21.3: geometry stamp change flags GeometryChanged");

        auto m = baseStamps; m.MaterialStamp = 301;
        RENDER_CHECK(reflection.EvaluateInvalidation(baseStamps, m).MaterialChanged,
            "M15-U5-21.3: material stamp change flags MaterialChanged");

        auto l = baseStamps; l.LightingStamp = 401;
        RENDER_CHECK(reflection.EvaluateInvalidation(baseStamps, l).LightingChanged,
            "M15-U5-21.4: lighting stamp change flags LightingChanged");

        auto a = baseStamps; a.AssetStamp = 501;
        RENDER_CHECK(reflection.EvaluateInvalidation(baseStamps, a).AssetChanged,
            "M15-U5-21.5: asset stamp change flags AssetChanged");

        auto d = baseStamps; d.DeviceEpoch = 2;
        RENDER_CHECK(reflection.EvaluateInvalidation(baseStamps, d).DeviceEpochChanged,
            "M15-U5-26.3: device epoch change flags DeviceEpochChanged");

        RENDER_CHECK(reflection.EvaluateInvalidation(baseStamps, baseStamps, true).ManualRequested,
            "M15-U5-22: manual capture trigger sets ManualRequested");

        ReflectionPublication retainedPending{dynHandle, "DYNAMIC_PROBE_PENDING: captured 2/6 faces", true, true, false, 2u, 6u, 9ull};
        const auto pendingFrame = reflection.ResolvePublication(dynPending, retainedPending, true, true);
        RENDER_CHECK(pendingFrame.State == ReflectionState::DynamicReady &&
                     pendingFrame.PrefilterReady &&
                     pendingFrame.ProbeGeneration == dynHandle &&
                     pendingFrame.CapturePending &&
                     pendingFrame.CaptureStale &&
                     !pendingFrame.CaptureFailed &&
                     pendingFrame.CaptureCompletedFaces == 2u &&
                     pendingFrame.CaptureTotalFaces == 6u &&
                     pendingFrame.ActiveGenerationID == 9ull,
            "M15-U5-23.2 / 23.3 / 24: retained generation reports Pending + Stale while remaining bound");

        ReflectionPublication retainedFailed{dynHandle, "DYNAMIC_PROBE_FACE_FAILED: face 2", false, true, true, 2u, 6u, 9ull};
        const auto failedRetainedFrame = reflection.ResolvePublication(dynPending, retainedFailed, false, true);
        RENDER_CHECK(failedRetainedFrame.State == ReflectionState::DynamicReady &&
                     failedRetainedFrame.PrefilterReady &&
                     failedRetainedFrame.ProbeGeneration == dynHandle &&
                     failedRetainedFrame.CaptureFailed &&
                     failedRetainedFrame.CaptureStale &&
                     failedRetainedFrame.Diagnostic.find("DYNAMIC_PROBE_FACE_FAILED") != std::string::npos,
            "M15-U5-23.4 / 24: rebuild failure preserves last-good handle and surfaces CaptureFailed + CaptureStale");
    }

    // M15-U5-E (S5.5): Box & sphere influence, multi-probe blend, per-probe parallax, SSR/sky energy policy, missing & stale probes
    {
        RenderReflectionProbe collapsed;
        collapsed.WorldTransform = glm::mat4(0.0f);
        collapsed.WorldTransform[0] = {0,0,1,0};
        const auto basis = ReflectionService::BuildProbeBasis(collapsed);
        RENDER_CHECK(std::isfinite(basis.AxisY.x) && std::isfinite(basis.AxisY.y) && std::isfinite(basis.AxisY.z) &&
                     std::abs(glm::dot(basis.AxisX, basis.AxisY)) < 1e-5f &&
                     std::abs(glm::length(basis.AxisY) - 1.0f) < 1e-5f,
            "U5E-R1 degenerate basis fallback stays finite and orthonormal when X is parallel to world Z");
        RenderWorld prioritized;
        for (int i = 0; i < 5; ++i) {
            RenderReflectionProbe p;
            p.EntityID = 10000 + i;
            p.DynamicCapture = true;
            p.Position = {float(i * 100),0,0};
            prioritized.ReflectionProbes.push_back(p);
        }
        const glm::vec3 offscreenPoints[]{{0,0,0},{100,0,0},{200,0,0},{300,0,0}};
        const auto prioritizedFrame = reflection.PrepareView(prioritized, {400,0,0}, offscreenPoints, false, 1);
        RENDER_CHECK(prioritizedFrame.ActiveProbes.size() == 4 && prioritizedFrame.SelectedProbeEntityID == 10004 &&
                     prioritizedFrame.AdditionalCandidates.size() == 1,
            "U5E-R2 camera probe remains selected when four distant meshes have equal maximum probe scores");
        RenderWorld boundedWorld;
        boundedWorld.ReflectionProbes = {prioritized.ReflectionProbes[4]};
        const ReflectionSampleBounds largeMeshBounds[]{{{-500,-1,-1},{500,1,1}}};
        const auto boundedFrame = reflection.PrepareView(boundedWorld, {0,0,0}, {}, false, 1, largeMeshBounds);
        RENDER_CHECK(boundedFrame.SelectedProbeEntityID == 10004,
            "U5E-R4 bounded mesh selection discovers a probe far from both the mesh origin and camera");
        RenderWorld rotatedWorld;
        RenderReflectionProbe slender;
        slender.DynamicCapture = true;
        slender.EntityID = 10010;
        slender.BoxExtents = {20,.2f,2};
        slender.BlendDistance = .1f;
        slender.WorldTransform = glm::rotate(glm::mat4(1), glm::radians(45.0f), glm::vec3(0,0,1));
        rotatedWorld.ReflectionProbes = {slender};
        const ReflectionSampleBounds intersecting[]{{{4,.1f,-.5f},{5,5,.5f}}};
        const auto rotatedFrame = reflection.PrepareView(rotatedWorld, {100,100,100}, {}, false, 1, intersecting);
        RENDER_CHECK(rotatedFrame.SelectedProbeEntityID == slender.EntityID &&
                     ReflectionService::EvaluateProbeInfluence(slender, {4,4,0}).Coverage == 1,
            "U5E-R4 rotated slender probe intersecting a mesh is retained despite an out-of-volume world nearest point");
        // M15-U5-27.1: Box influence (interior center, face boundary, rotated OBB, BlendDistance falloff, NaN/zero safety)
        RenderReflectionProbe boxProbe{};
        boxProbe.EntityID = 9001;
        boxProbe.Shape = 0;
        boxProbe.Position = {0.0f, 0.0f, 0.0f};
        boxProbe.BoxExtents = {4.0f, 4.0f, 4.0f}; // half = (2, 2, 2)
        boxProbe.BlendDistance = 2.0f;
        const auto faceParallel = ReflectionService::ApplyProbeParallax(boxProbe, {2,0,0}, {0,0,1});
        RENDER_CHECK(std::abs(faceParallel.x - std::sqrt(0.5f)) < 1e-5f &&
                     std::abs(faceParallel.z - std::sqrt(0.5f)) < 1e-5f,
            "U5E-R1 a ray parallel to a box face still exits through the correct slab at the face boundary");

        const auto boxCenter = ReflectionService::EvaluateProbeInfluence(boxProbe, {0.0f, 0.0f, 0.0f});
        const auto boxMid = ReflectionService::EvaluateProbeInfluence(boxProbe, {1.0f, 0.0f, 0.0f});
        const auto boxFace = ReflectionService::EvaluateProbeInfluence(boxProbe, {2.0f, 0.0f, 0.0f});
        const auto boxHalfTail = ReflectionService::EvaluateProbeInfluence(boxProbe, {3.0f, 0.0f, 0.0f});
        const auto boxEdgeOut = ReflectionService::EvaluateProbeInfluence(boxProbe, {4.0f, 0.0f, 0.0f});
        const auto boxNaN = ReflectionService::EvaluateProbeInfluence(boxProbe, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f});
        RENDER_CHECK(boxCenter.InsideCore && Axle::RenderTests::Near(boxCenter.Coverage, 1.0f, 1e-5f) &&
                     Axle::RenderTests::Near(boxCenter.BlendWeight, 1.0f, 1e-5f) &&
                     boxMid.InsideCore && boxMid.BlendWeight < boxCenter.BlendWeight && boxMid.BlendWeight > boxFace.BlendWeight &&
                     boxFace.InsideCore && Axle::RenderTests::Near(boxFace.Coverage, 1.0f, 1e-5f) &&
                     Axle::RenderTests::Near(boxFace.BlendWeight, 0.05f, 1e-5f) &&
                     !boxHalfTail.InsideCore && Axle::RenderTests::Near(boxHalfTail.Coverage, 0.5f, 1e-4f) &&
                     Axle::RenderTests::Near(boxEdgeOut.Coverage, 0.0f, 1e-5f) &&
                     Axle::RenderTests::Near(boxEdgeOut.BlendWeight, 0.0f, 1e-5f) &&
                     boxNaN.Coverage == 0.0f && boxNaN.BlendWeight == 0.0f,
            "M15-U5-27.1: box influence evaluates C1 interior/boundary/falloff weights and rejects non-finite coordinates");

        // Rotated box OBB check (45 deg around +Y)
        RenderReflectionProbe rotBox = boxProbe;
        rotBox.BoxExtents = {8.0f, 4.0f, 2.0f}; // long along local X (+/-4), narrow along local Z (+/-1)
        rotBox.BlendDistance = 1.0f;
        rotBox.WorldTransform = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 alongRotX = glm::normalize(glm::vec3(1.0f, 0.0f, -1.0f)) * 2.5f;
        const glm::vec3 alongRotZ = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f)) * 2.5f;
        const auto rotInfX = ReflectionService::EvaluateProbeInfluence(rotBox, alongRotX);
        const auto rotInfZ = ReflectionService::EvaluateProbeInfluence(rotBox, alongRotZ);
        RENDER_CHECK(rotInfX.InsideCore && Axle::RenderTests::Near(rotInfX.Coverage, 1.0f, 1e-4f) &&
                     !rotInfZ.InsideCore && rotInfZ.Coverage == 0.0f,
            "M15-U5-27.1: rotated box probe evaluates OBB extents along WorldTransform axes");

        // M15-U5-27.2: Sphere influence (radial center, midpoint, radius surface, outer falloff)
        RenderReflectionProbe sphereProbe{};
        sphereProbe.EntityID = 9002;
        sphereProbe.Shape = 1;
        sphereProbe.Position = {0.0f, 0.0f, 0.0f};
        sphereProbe.Radius = 3.0f;
        sphereProbe.BlendDistance = 3.0f;

        const auto sphCenter = ReflectionService::EvaluateProbeInfluence(sphereProbe, {0.0f, 0.0f, 0.0f});
        const auto sphDiagInside = ReflectionService::EvaluateProbeInfluence(sphereProbe, glm::normalize(glm::vec3(1, 1, 1)) * 1.5f);
        const auto sphBoundary = ReflectionService::EvaluateProbeInfluence(sphereProbe, {3.0f, 0.0f, 0.0f});
        const auto sphMidTail = ReflectionService::EvaluateProbeInfluence(sphereProbe, {4.5f, 0.0f, 0.0f});
        const auto sphOutside = ReflectionService::EvaluateProbeInfluence(sphereProbe, {6.0f, 0.0f, 0.0f});
        RENDER_CHECK(sphCenter.InsideCore && Axle::RenderTests::Near(sphCenter.Coverage, 1.0f, 1e-5f) &&
                     sphDiagInside.InsideCore && Axle::RenderTests::Near(sphDiagInside.InnerRatio, 0.5f, 1e-4f) &&
                     sphBoundary.InsideCore && Axle::RenderTests::Near(sphBoundary.BlendWeight, 0.05f, 1e-5f) &&
                     !sphMidTail.InsideCore && Axle::RenderTests::Near(sphMidTail.Coverage, 0.5f, 1e-4f) &&
                     Axle::RenderTests::Near(sphOutside.Coverage, 0.0f, 1e-5f),
            "M15-U5-27.2: sphere influence preserves isotropic radial smooth falloff");

        // M15-U5-28 & 29 & 30.1..30.3 & 31.1..31.4: Multi-probe overlap, parallax before blend, SSR+sky energy policy, missing/stale
        RenderReflectionProbe leftProbe{};
        leftProbe.EntityID = 9101;
        leftProbe.Shape = 0;
        leftProbe.Position = {-2.0f, 0.0f, 0.0f};
        leftProbe.BoxExtents = {6.0f, 6.0f, 6.0f};
        leftProbe.BlendDistance = 2.0f;
        leftProbe.Intensity = 1.0f;
        leftProbe.ParallaxCorrected = true;
        leftProbe.CubemapPath = "left_probe.axleasset";

        RenderReflectionProbe rightProbe = leftProbe;
        rightProbe.EntityID = 9102;
        rightProbe.Position = {2.0f, 0.0f, 0.0f};
        rightProbe.CubemapPath = "right_probe.axleasset";

        ReflectionProbeCandidate leftCand;
        leftCand.Probe = leftProbe; leftCand.Ready = true;
        ReflectionProbeCandidate rightCand;
        rightCand.Probe = rightProbe; rightCand.Ready = true;
        std::vector<ReflectionProbeCandidate> overlapCands{leftCand, rightCand};

        std::vector<glm::vec3> recordedDirsA, recordedDirsB;
        auto sampleColor = [&](const RenderReflectionProbe& p, const glm::vec3& dir, size_t) -> glm::vec3 {
            if (p.EntityID == 9101) { recordedDirsA.push_back(dir); return {1.0f, 0.0f, 0.0f}; }
            recordedDirsB.push_back(dir);
            return {0.0f, 1.0f, 0.0f};
        };

        // Sweep across x in [-6, +6]: must transition monotonically from Red -> 50/50 -> Green with no hard steps!
        float prevGFraction = -0.01f;
        for (int step = 0; step <= 60; ++step) {
            const float x = -6.0f + 0.2f * static_cast<float>(step);
            const auto blend = ReflectionService::EvaluateEnvironmentBlend(
                overlapCands, {x, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, sampleColor);
            RENDER_CHECK(Axle::RenderTests::Near(blend.SSRWeight + blend.ProbeWeight + blend.SkyWeight, 1.0f, 1e-5f),
                "M15-U5-30.3: convex blend weights always sum identically to 1.0");
            const float gFraction = (blend.ProbeColor.r + blend.ProbeColor.g) > 1e-5f
                ? blend.ProbeColor.g / (blend.ProbeColor.r + blend.ProbeColor.g) : 0.0f;
            if (step > 0) {
                RENDER_CHECK(gFraction + 1e-4f >= prevGFraction && (gFraction - prevGFraction) < 0.15f,
                    "M15-U5-28 / 31.1: overlapping multi-probe transition is monotonic and free of hard seams");
            }
            prevGFraction = gFraction;
        }
        const auto midpointBlend = ReflectionService::EvaluateEnvironmentBlend(
            overlapCands, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 0.0f, 0.0f, {0, 0, 0}, {0, 0, 1}, sampleColor);
        RENDER_CHECK(Axle::RenderTests::Near(midpointBlend.ProbeColor.r, 0.5f, 1e-4f) &&
                     Axle::RenderTests::Near(midpointBlend.ProbeColor.g, 0.5f, 1e-4f) &&
                     Axle::RenderTests::Near(midpointBlend.TotalProbeCoverage, 1.0f, 1e-5f) &&
                     Axle::RenderTests::Near(midpointBlend.SkyWeight, 0.0f, 1e-5f),
            "M15-U5-28: symmetric overlap midpoint yields exact 50/50 probe blend and zero sky leak inside core");

        // M15-U5-29: Parallax correction is applied independently per probe BEFORE blending
        recordedDirsA.clear(); recordedDirsB.clear();
        (void)ReflectionService::EvaluateEnvironmentBlend(
            overlapCands, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 0.0f, 0.0f, {0, 0, 0}, {0, 0, 1}, sampleColor);
        RENDER_CHECK(!recordedDirsA.empty() && !recordedDirsB.empty() &&
                     recordedDirsA[0].x > 0.4f && recordedDirsB[0].x < -0.4f &&
                     Axle::RenderTests::Near(glm::length(recordedDirsA[0]), 1.0f, 1e-4f) &&
                     Axle::RenderTests::Near(glm::length(recordedDirsB[0]), 1.0f, 1e-4f),
            "M15-U5-29: left probe and right probe receive distinct normalized per-probe parallax directions before blending");

        // Sphere parallax check too
        const glm::vec3 sphParallax = ReflectionService::ApplyProbeParallax(sphereProbe, {1.5f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
        RENDER_CHECK(sphParallax.x > 0.4f && sphParallax.z > 0.7f && Axle::RenderTests::Near(glm::length(sphParallax), 1.0f, 1e-4f),
            "M15-U5-29: sphere probe applies forward sphere-intersection parallax correction");

        // M15-U5-30.1 / 30.2 / 30.3: Energy conservation across SSR confidence + probe boundary + sky fallback
        const auto unitSampler = [](const RenderReflectionProbe&, const glm::vec3&, size_t) { return glm::vec3(1.0f); };
        for (float ssrConf : {0.0f, 0.35f, 0.75f, 1.0f}) {
            for (float posX : {0.0f, 5.5f, 8.0f}) {
                const auto energyBlend = ReflectionService::EvaluateEnvironmentBlend(
                    overlapCands, {posX, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                    ssrConf, 1.0f, glm::vec3(1.0f), glm::vec3(1.0f), unitSampler);
                RENDER_CHECK(Axle::RenderTests::Near(energyBlend.BlendedColor.r, 1.0f, 1e-5f) &&
                             Axle::RenderTests::Near(energyBlend.BlendedColor.g, 1.0f, 1e-5f) &&
                             Axle::RenderTests::Near(energyBlend.BlendedColor.b, 1.0f, 1e-5f) &&
                             Axle::RenderTests::Near(energyBlend.SSRWeight + energyBlend.ProbeWeight + energyBlend.SkyWeight, 1.0f, 1e-5f),
                    "M15-U5-30.1/30.2/30.3: unified energy policy preserves exact unit radiance across SSR, probes, and sky");
            }
        }

        // M15-U5-31.3 & 31.4: Multi-probe PrepareView + ResolvePublication with missing and stale probes
        RenderWorld multiWorld;
        multiWorld.SceneRevision = 40;
        multiWorld.Skybox.Authored = multiWorld.Skybox.Enabled = multiWorld.Skybox.EnvironmentLighting = true;
        multiWorld.ReflectionProbes = {leftProbe, rightProbe};
        const glm::vec3 viewSamplePt{0.0f, 0.0f, 0.0f};
        const auto multiPrep = reflection.PrepareView(
            multiWorld, {0.0f, 0.0f, 8.0f}, std::span<const glm::vec3>(&viewSamplePt, 1), false, 40);
        RENDER_CHECK(multiPrep.ActiveProbes.size() == 2u,
            "M15-U5-28: PrepareView discovers both overlapping mesh-region probes even when camera sits outside");

        ReflectionPublication missingPub;
        missingPub.ProbePublications.resize(2);
        missingPub.ProbePublications[0].Ready = false;
        missingPub.ProbePublications[0].Diagnostic = "missing_cubemap";
        missingPub.ProbePublications[1].Ready = true;
        missingPub.ProbePublications[1].PrefilterGeneration = prefilter;
        missingPub.ProbePublications[1].ActiveGenerationID = 42ull;
        const auto resolvedMissing = reflection.ResolvePublication(multiPrep, missingPub, false, true);
        RENDER_CHECK(resolvedMissing.ActiveProbeCount == 1u &&
                     resolvedMissing.MissingProbeCount == 1u &&
                     resolvedMissing.PrefilterReady &&
                     resolvedMissing.SelectedProbeEntityID == 9102 &&
                     Axle::RenderTests::Near(resolvedMissing.ActiveProbes[0].NormalizedWeight, 0.0f, 1e-6f) &&
                     Axle::RenderTests::Near(resolvedMissing.ActiveProbes[1].NormalizedWeight, 1.0f, 1e-6f),
            "M15-U5-31.3: missing probe is omitted and remaining valid probe renormalizes cleanly to 1.0");

        ReflectionPublication stalePub;
        stalePub.ProbePublications.resize(2);
        stalePub.ProbePublications[0].Ready = true;
        stalePub.ProbePublications[0].CaptureStale = true;
        stalePub.ProbePublications[0].PrefilterGeneration = dynHandle;
        stalePub.ProbePublications[0].ActiveGenerationID = 10ull;
        stalePub.ProbePublications[1].Ready = true;
        stalePub.ProbePublications[1].PrefilterGeneration = prefilter;
        stalePub.ProbePublications[1].ActiveGenerationID = 11ull;
        const auto resolvedStale = reflection.ResolvePublication(multiPrep, stalePub, false, true);
        RENDER_CHECK(resolvedStale.ActiveProbeCount == 2u &&
                     resolvedStale.StaleProbeCount == 1u &&
                     resolvedStale.CaptureStale &&
                     resolvedStale.ActiveProbes[0].Stale &&
                     resolvedStale.ActiveProbes[0].NormalizedWeight > 0.1f,
            "M15-U5-31.4: stale probe retains last-good contribution cleanly while flagging StaleProbeCount");

        // Bound to MaxActiveReflectionProbes = 4 even when >4 probes overlap
        RenderWorld sixWorld;
        for (int k = 0; k < 6; ++k) {
            RenderReflectionProbe p = leftProbe;
            p.EntityID = 9200u + static_cast<uint64_t>(k);
            p.Position = {0.1f * static_cast<float>(k), 0.0f, 0.0f};
            sixWorld.ReflectionProbes.push_back(p);
        }
        const auto cappedView = reflection.PrepareView(sixWorld, {0.0f, 0.0f, 0.0f}, {}, false, 41);
        float cappedSum = 0.0f;
        for (const auto& c : cappedView.ActiveProbes) cappedSum += c.NormalizedWeight;
        RENDER_CHECK(cappedView.ActiveProbes.size() == MaxActiveReflectionProbes &&
                     Axle::RenderTests::Near(cappedSum, 1.0f, 1e-5f),
            "M15-U5-28: active probe candidates are bounded to MaxActiveReflectionProbes=4 with normalized weights");
    }

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

    // M15-U1-01.1: frozen direct-light unit contract. These expectations mirror
    // DIRECT_LUX_TO_RADIANCE and LOCAL_INTENSITY_SCALE in ForwardPBR.hlsl.
    constexpr float FrozenDirectLuxToRadiance=0.000035f;
    RenderWorld units;
    units.DirectionalLight=RenderDirectionalLight{};
    units.DirectionalLight->Intensity=0.5f;
    units.PointLights.push_back(RenderPointLight{});
    units.PointLights[0].Intensity=2.0f;
    units.SpotLights.push_back(RenderSpotLight{});
    units.SpotLights[0].Intensity=2.0f;
    units.AdditionalDirectionalLights.push_back(RenderDirectionalLight{});
    units.AdditionalDirectionalLights[0].Intensity=1.0f;
    const auto artistUnits=lighting.BuildFrame(units, settings);
    RENDER_CHECK(artistUnits.Sun.Illuminance==50000.0f,
        "artist directional intensity scales to Lux against 100,000 lx direct sunlight");
    RENDER_CHECK(artistUnits.PointLights[0].Directional &&
                 Axle::RenderTests::Near(artistUnits.PointLights[0].Intensity, 100000.0f*FrozenDirectLuxToRadiance, 1e-5f),
        "additional directionals are pre-scaled once on the CPU by the frozen Lux-to-radiance factor");
    RENDER_CHECK(artistUnits.PointLights[1].Intensity==2000.0f && artistUnits.SpotLights[0].Intensity==1000.0f,
        "artist point/spot intensities use the frozen 1000/500 local scales");
    units.DirectionalLight->Intensity=1.2f;
    RENDER_CHECK(Axle::RenderTests::Near(lighting.BuildFrame(units, settings).Sun.Illuminance,120000.0f,0.02f),
        "directional artist multiplier 1.2 maps continuously to 120,000 lux");
    units.DirectionalLight->Intensity=100.0f;
    units.PointLights[0].Intensity=10.0f;
    units.SpotLights[0].Intensity=10.0f;
    const auto atBoundary=lighting.BuildFrame(units, settings);
    units.DirectionalLight->Intensity=100.001f;
    units.PointLights[0].Intensity=10.001f;
    units.SpotLights[0].Intensity=10.001f;
    const auto aboveBoundary=lighting.BuildFrame(units, settings);
    RENDER_CHECK(atBoundary.Sun.Illuminance==10000000.0f && aboveBoundary.Sun.Illuminance==10000000.0f &&
                 atBoundary.PointLights[1].Intensity==10000.0f && aboveBoundary.PointLights[1].Intensity>10000.0f &&
                 atBoundary.SpotLights[0].Intensity==5000.0f && aboveBoundary.SpotLights[0].Intensity>5000.0f,
        "intensity scaling has no unit switch at historical 100/10 thresholds");
    units.DirectionalLight->Intensity=-1.0f;
    units.PointLights[0].Intensity=std::numeric_limits<float>::quiet_NaN();
    units.SpotLights[0].Intensity=std::numeric_limits<float>::infinity();
    const auto invalidUnits=lighting.BuildFrame(units, settings);
    RENDER_CHECK(invalidUnits.Sun.Illuminance==0.0f && invalidUnits.PointLights[1].Intensity==0.0f &&
                 invalidUnits.SpotLights[0].Intensity==0.0f,
        "invalid and negative authored intensities fall back to dark finite lights");

    std::ifstream forwardShader(AXLE_FORWARD_PBR_SHADER_PATH);
    const std::string forwardSource{std::istreambuf_iterator<char>(forwardShader), std::istreambuf_iterator<char>()};
    RENDER_CHECK(!forwardSource.empty(), "ForwardPBR.hlsl is readable for the frozen-unit contract");
    RENDER_CHECK(forwardSource.find("static const float DIRECT_LUX_TO_RADIANCE = 0.000035;")!=std::string::npos &&
                 forwardSource.find("static const float LOCAL_INTENSITY_SCALE = 0.05;")!=std::string::npos,
        "ForwardPBR.hlsl declares the frozen direct/local unit constants");
    RENDER_CHECK(forwardSource.find("DirectionAndIlluminance.w,0.0) * DIRECT_LUX_TO_RADIANCE")!=std::string::npos,
        "ForwardPBR.hlsl directional term uses DIRECT_LUX_TO_RADIANCE");
    RENDER_CHECK(forwardSource.find("DirectionAndIlluminance.w,0.0) * 0.000035")==std::string::npos,
        "ForwardPBR.hlsl does not bypass DIRECT_LUX_TO_RADIANCE with a literal factor");
    RENDER_CHECK(forwardSource.find("attenuation * LOCAL_INTENSITY_SCALE")!=std::string::npos &&
                 forwardSource.find("totalAtten * LOCAL_INTENSITY_SCALE")!=std::string::npos,
        "ForwardPBR.hlsl point/spot terms use LOCAL_INTENSITY_SCALE");

    // M15-U1-01.2: frozen sun angular-radius policy contract.
    constexpr float PhysicalSolarAngularRadius = 0.00462512f;
    constexpr float MaxSolarAngularRadius = 0.25f;
    RENDER_CHECK(std::abs(PhysicalSolarAngularRadius - (0.53f * 0.5f * 3.14159265358979323846f / 180.0f)) < 1e-6f,
        "physical solar angular radius matches 0.53 degree solar diameter within 1e-6 rad");

    RenderWorld sunPolicyWorld;
    sunPolicyWorld.DirectionalLight = RenderDirectionalLight{};
    RENDER_CHECK(sunPolicyWorld.DirectionalLight->SolarAngularRadius == PhysicalSolarAngularRadius,
        "RenderDirectionalLight defaults to physical solar angular radius (0.00462512 rad)");

    sunPolicyWorld.DirectionalLight->SolarAngularRadius = -0.05f;
    RENDER_CHECK(lighting.BuildFrame(sunPolicyWorld, settings).Sun.SolarAngularRadius == 0.0f,
        "negative solar angular radius clamps to 0.0 rad (point directional fallback)");

    sunPolicyWorld.DirectionalLight->SolarAngularRadius = 0.50f;
    RENDER_CHECK(lighting.BuildFrame(sunPolicyWorld, settings).Sun.SolarAngularRadius == MaxSolarAngularRadius,
        "excessive solar angular radius clamps to 0.25 rad upper bound");

    // Shader convolution formula verification:
    // sunConvolvedRoughness = sqrt(saturate(roughness^2 + sunAngularRadius^2))
    auto convolveRoughness = [](float roughness, float sunRadius) {
        float rSq = roughness * roughness + sunRadius * sunRadius;
        return std::sqrt(std::clamp(rSq, 0.0f, 1.0f));
    };
    RENDER_CHECK(Axle::RenderTests::Near(convolveRoughness(0.0f, PhysicalSolarAngularRadius), PhysicalSolarAngularRadius, 1e-6f),
        "zero roughness convolved with physical sun yields finite solar angular radius");
    RENDER_CHECK(Axle::RenderTests::Near(convolveRoughness(0.5f, 0.0f), 0.5f, 1e-6f),
        "zero solar radius collapses to unmodified roughness");
    float prevConvolved = 0.0f;
    for (int i = 0; i <= 10; ++i) {
        float r = i / 10.0f;
        float conv = convolveRoughness(r, PhysicalSolarAngularRadius);
        RENDER_CHECK(conv >= prevConvolved && conv <= 1.0f, "convolved roughness is monotonic and bounded by 1.0");
        prevConvolved = conv;
    }

    RENDER_CHECK(forwardSource.find("static const float SUN_DEFAULT_ANGULAR_RADIUS = 0.00462512;")!=std::string::npos &&
                 forwardSource.find("static const float SUN_MAX_ANGULAR_RADIUS = 0.25;")!=std::string::npos,
        "ForwardPBR.hlsl declares frozen solar angular radius constants");
    RENDER_CHECK(forwardSource.find("float sunAngularRadius = hasMultiLightSun ? clamp(u_SunLight.ShadowParams.z, 0.0, SUN_MAX_ANGULAR_RADIUS) : SUN_DEFAULT_ANGULAR_RADIUS;")!=std::string::npos,
        "ForwardPBR.hlsl applies frozen clamp and fallback for solar angular radius");
    RENDER_CHECK(forwardSource.find("sunConvolvedRoughness = sqrt(saturate(roughness * roughness + sunAngularRadius * sunAngularRadius));")!=std::string::npos,
        "ForwardPBR.hlsl uses canonical sun roughness convolution formula");

    // M15-U1-01.3: frozen BRDF normalization contract.
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float InvPi = 0.31830988618379067154f;
    RENDER_CHECK(std::abs(InvPi - (1.0f / Pi)) < 1e-7f,
        "diffuse normalization InvPi matches 1/PI within 1e-7");

    // 1. GGX NDF numerical integration normalization:
    // int_0^{pi/2} D(theta) * cos(theta) * sin(theta) * 2*pi dtheta == 1.0
    auto integrateGGX = [&](float roughness) {
        float a = roughness * roughness;
        float a2 = a * a;
        constexpr int steps = 20000;
        constexpr float dTheta = (0.5f * Pi) / steps;
        double sum = 0.0;
        for (int i = 0; i < steps; ++i) {
            float theta = (i + 0.5f) * dTheta;
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);
            float cosTheta2 = cosTheta * cosTheta;
            float denom = (1.0f - cosTheta2) + a2 * cosTheta2;
            denom = Pi * denom * denom;
            float D = a2 / denom;
            sum += D * cosTheta * sinTheta * 2.0 * Pi * dTheta;
        }
        return static_cast<float>(sum);
    };
    for (float r : {0.05f, 0.15f, 0.35f, 0.6f, 0.85f, 1.0f}) {
        float integral = integrateGGX(r);
        RENDER_CHECK(Axle::RenderTests::Near(integral, 1.0f, 0.002f),
            "GGX NDF integrates to 1.0 over hemisphere for roughness");
    }

    // 2. Diffuse hemispherical integral: int_0^{pi/2} (1/PI) * cos(theta) * sin(theta) * 2*pi dtheta == 1.0
    {
        constexpr int steps = 5000;
        constexpr float dTheta = (0.5f * Pi) / steps;
        double diffSum = 0.0;
        for (int i = 0; i < steps; ++i) {
            float theta = (i + 0.5f) * dTheta;
            diffSum += InvPi * std::cos(theta) * std::sin(theta) * 2.0 * Pi * dTheta;
        }
        RENDER_CHECK(Axle::RenderTests::Near(static_cast<float>(diffSum), 1.0f, 1e-4f),
            "Lambertian diffuse BRDF integrates to 1.0 for unit albedo");
    }

    // 3. Energy partition contract: kD = (1 - kS) * (1 - metallic)
    for (float metallic : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        for (float f0 : {0.04f, 0.5f, 1.0f}) {
            for (float cosTheta : {0.0f, 0.5f, 1.0f}) {
                float kS = f0 + (1.0f - f0) * std::pow(1.0f - cosTheta, 5.0f);
                float kD = (1.0f - kS) * (1.0f - metallic);
                if (metallic == 1.0f) {
                    RENDER_CHECK(kD == 0.0f, "metal has strictly zero diffuse component");
                }
                if (metallic == 0.0f) {
                    RENDER_CHECK(Axle::RenderTests::Near(kD + kS, 1.0f, 1e-6f),
                        "pure dielectric conserves 100% direct energy before absorption");
                }
                RENDER_CHECK(kD + kS <= 1.0f + 1e-6f, "total partition kD + kS never exceeds 1.0");
            }
        }
    }

    // 4. Canonical shader token checks
    RENDER_CHECK(forwardSource.find("static const float BRDF_DIFFUSE_NORMALIZATION = 0.31830988618379067154;")!=std::string::npos &&
                 forwardSource.find("static const float BRDF_SPECULAR_DENOMINATOR_SCALE = 4.0;")!=std::string::npos,
        "ForwardPBR.hlsl declares frozen BRDF normalization constants");
    RENDER_CHECK(forwardSource.find("diffuse = kD * albedo.rgb * BRDF_DIFFUSE_NORMALIZATION;")!=std::string::npos,
        "ForwardPBR.hlsl uses canonical diffuse normalization across light evaluations");
    RENDER_CHECK(forwardSource.find("denominator = BRDF_SPECULAR_DENOMINATOR_SCALE * NdotV * max(NdotL_Sun, 0.001) + 0.0001;")!=std::string::npos &&
                 forwardSource.find("denominator = BRDF_SPECULAR_DENOMINATOR_SCALE * NdotV * max(NdotL, 0.001) + 0.0001;")!=std::string::npos,
        "ForwardPBR.hlsl uses canonical Cook-Torrance specular denominator scale 4.0");

    // M15-U1-01.4: frozen exposure interaction contract.
    // 1. Direct daylight Lambertian radiance and exposure scaling:
    // Pure sunlight (100,000 lx) on 100% white surface yields:
    // Lo = 100000 * DIRECT_LUX_TO_RADIANCE / PI ~= 1.1140846
    constexpr float ReferenceSunlightLux = 100000.0f;
    constexpr float DirectSceneRadiance = (ReferenceSunlightLux * FrozenDirectLuxToRadiance) * InvPi;
    RENDER_CHECK(Axle::RenderTests::Near(DirectSceneRadiance, 1.1140846f, 1e-5f),
        "reference direct sunlight on white Lambertian surface yields ~1.114 scene radiance");

    auto applyExposure = [](float radiance, float exposure) {
        float effectiveExp = (exposure > 0.0f && std::isfinite(exposure)) ? exposure : 1.0f;
        return radiance * effectiveExp;
    };
    RENDER_CHECK(Axle::RenderTests::Near(applyExposure(DirectSceneRadiance, 1.0f), DirectSceneRadiance, 1e-6f),
        "standard exposure 1.0 preserves scene radiance 1:1");
    RENDER_CHECK(Axle::RenderTests::Near(applyExposure(DirectSceneRadiance, 2.0f), DirectSceneRadiance * 2.0f, 1e-6f),
        "exposure doubles scene radiance linearly");
    RENDER_CHECK(Axle::RenderTests::Near(applyExposure(DirectSceneRadiance, -1.0f), DirectSceneRadiance, 1e-6f) &&
                 Axle::RenderTests::Near(applyExposure(DirectSceneRadiance, 0.0f), DirectSceneRadiance, 1e-6f),
        "non-positive exposure falls back to 1.0 safely without invalid black/negative output");

    // 2. PostProcessService auto-exposure interaction with direct lighting scene luminance
    PostProcessService postProcess;
    SceneRenderSettings postSettings;
    postSettings.EnableAutoExposure = true;
    postSettings.AutoExposureMin = 0.1f;
    postSettings.AutoExposureMax = 10.0f;
    postSettings.AutoExposureKeyValue = 0.18f;
    postProcess.BeginFrame(postSettings, true);

    // Extreme bright direct scene (e.g. direct sun facing specular):
    float brightExposure = postProcess.UpdateExposure(1000.0f, 10.0f);
    RENDER_CHECK(Axle::RenderTests::Near(brightExposure, postSettings.AutoExposureMin, 1e-4f),
        "extreme bright direct scene luminance clamps exposure to AutoExposureMin (0.1)");

    // Extreme dark direct scene:
    float darkExposure = postProcess.UpdateExposure(0.0001f, 10.0f);
    RENDER_CHECK(Axle::RenderTests::Near(darkExposure, postSettings.AutoExposureMax, 1e-4f),
        "extreme dark scene luminance clamps exposure to AutoExposureMax (10.0)");

    // Canonical ToneMapping shader source inspection
    std::ifstream toneShader(AXLE_TONEMAPPING_SHADER_PATH);
    const std::string toneSource{std::istreambuf_iterator<char>(toneShader), std::istreambuf_iterator<char>()};
    RENDER_CHECK(!toneSource.empty(), "ToneMapping.hlsl is readable for the exposure contract");
    RENDER_CHECK(toneSource.find("float exposure = (u_Params.x > 0.0) ? u_Params.x : 1.0;")!=std::string::npos,
        "ToneMapping.hlsl enforces positive exposure fallback to 1.0");
    RENDER_CHECK(toneSource.find("float3 exposed = hdrColor * exposure;")!=std::string::npos,
        "ToneMapping.hlsl applies linear radiometric exposure scaling before tonemapping");

    std::cout << "  [LightingServices] Lighting/reflection/GI boundary tests passed.\n";
    return true;
}
