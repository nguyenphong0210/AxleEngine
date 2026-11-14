#include <Axle/ReflectionService.hpp>
#include <algorithm>
#include <cmath>
namespace Axle {
    ReflectionFrame ReflectionService::Prepare(const RenderWorld& world, const glm::vec3& cameraPosition, bool enableSSR, uint64_t sceneRevision) const {
        ReflectionFrame frame;
        frame.SceneRevision=sceneRevision;
        frame.Fallback=enableSSR ? ReflectionFallback::SSR :
            (world.Skybox.Authored && world.Skybox.Enabled && world.Skybox.EnvironmentLighting
                ? ReflectionFallback::SkyIBL : ReflectionFallback::NeutralAmbient);
        frame.State=ReflectionState::Fallback;
        frame.Diagnostic="no_authored_probe_influence: " + std::string(FallbackName(frame.Fallback)) + " fallback";
        // Preserve the live renderer's single-probe influence policy. Ties keep
        // source order, and out-of-influence probes cannot replace the sky.
        const RenderReflectionProbe* selected=nullptr;
        float selectedWeight=0.0f;
        for (const auto& probe : world.ReflectionProbes) {
            if (probe.CubemapPath.empty()) continue;
            const glm::vec3 local=cameraPosition-probe.Position;
            float distanceOutside=0.0f;
            float influence=1.0f;
            if (probe.Shape==1) {
                distanceOutside=std::max(glm::length(local)-probe.Radius, 0.0f);
                influence=std::max(probe.Radius, 0.001f);
            } else {
                const glm::vec3 extents=glm::max(probe.BoxExtents*0.5f, glm::vec3(0.001f));
                distanceOutside=glm::length(glm::max(glm::abs(local)-extents, glm::vec3(0.0f)));
                influence=glm::length(extents);
            }
            if (!std::isfinite(distanceOutside) || !std::isfinite(influence)) continue;
            const float weight=1.0f-std::clamp(distanceOutside/influence, 0.0f, 1.0f);
            if (weight>selectedWeight) { selectedWeight=weight; selected=&probe; }
        }
        if (!selected) return frame;
        frame.SelectedProbe=*selected;
        frame.SelectedProbeCount=1;
        frame.SelectedProbeEntityID=selected->EntityID;
        frame.CubemapPath=selected->CubemapPath;
        frame.ParallaxCorrected=selected->ParallaxCorrected;
        frame.State=ReflectionState::AuthoredPending;
        frame.Diagnostic="authored_probe_pending: native prefilter publication required";
        return frame;
    }
    ReflectionFrame ReflectionService::ResolvePublication(ReflectionFrame frame, const ReflectionPublication& publication,
                                                          bool ssrAvailable, bool skyIBLAvailable) const {
        frame.ProbeGeneration={};
        frame.PrefilterReady=false;
        frame.Fallback=ssrAvailable ? ReflectionFallback::SSR :
            (skyIBLAvailable ? ReflectionFallback::SkyIBL : ReflectionFallback::NeutralAmbient);
        if (frame.SelectedProbe && publication.PrefilterGeneration) {
            frame.ProbeGeneration=publication.PrefilterGeneration;
            frame.PrefilterReady=true;
            frame.State=ReflectionState::AuthoredReady;
            frame.Fallback=ReflectionFallback::None;
            frame.Diagnostic=publication.Diagnostic;
        } else {
            frame.State=ReflectionState::Fallback;
            frame.Diagnostic=std::string(frame.SelectedProbe ? "authored_probe_unavailable: " : "no_authored_probe_influence: ") +
                FallbackName(frame.Fallback) + " fallback";
            if (!publication.Diagnostic.empty()) frame.Diagnostic+="; "+publication.Diagnostic;
        }
        return frame;
    }
    const char* ReflectionService::FallbackName(ReflectionFallback fallback){switch(fallback){case ReflectionFallback::SSR:return "SSR";case ReflectionFallback::LocalProbe:return "LocalProbe";case ReflectionFallback::SkyIBL:return "SkyIBL";case ReflectionFallback::NeutralAmbient:return "NeutralAmbient";default:return "None";}}
}
