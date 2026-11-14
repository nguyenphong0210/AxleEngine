#include <Axle/ReflectionService.hpp>
#include <limits>
namespace Axle {
    ReflectionFrame ReflectionService::Prepare(const RenderWorld& world, const glm::vec3& cameraPosition, bool enableSSR, uint64_t sceneRevision) const {
        ReflectionFrame frame; frame.SceneRevision=sceneRevision; if (world.ReflectionProbes.empty()) { frame.State=enableSSR?ReflectionState::Fallback:ReflectionState::Disabled; frame.Fallback=enableSSR?ReflectionFallback::SSR:ReflectionFallback::SkyIBL; frame.Diagnostic=enableSSR?"no_local_probe: SSR/sky fallback":"no_local_probe: sky IBL fallback"; return frame; }
        const RenderReflectionProbe* selected=nullptr; float best=std::numeric_limits<float>::max(); for (const auto& probe:world.ReflectionProbes) { if (probe.CubemapPath.empty()) continue; float d=glm::length(probe.Position-cameraPosition); if(d<best){best=d;selected=&probe;} }
        if (!selected) { frame.State=enableSSR?ReflectionState::Fallback:ReflectionState::Disabled; frame.Fallback=enableSSR?ReflectionFallback::SSR:ReflectionFallback::SkyIBL; frame.Diagnostic=enableSSR?"no_authored_probe: SSR/sky fallback":"no_authored_probe: sky IBL fallback"; return frame; }
        frame.SelectedProbeCount=1; frame.SelectedProbeEntityID=selected->EntityID; frame.CubemapPath=selected->CubemapPath; frame.ParallaxCorrected=selected->ParallaxCorrected; frame.ProbeGeneration=selected->EntityID^(sceneRevision+0x9e3779b97f4a7c15ull);
        if(!selected->CubemapPath.empty()){frame.State=ReflectionState::AuthoredReady;frame.Fallback=ReflectionFallback::None;frame.PrefilterReady=true;frame.Diagnostic="authored_probe_boundary: dynamic capture and multi-probe blend deferred to M15";} else {frame.State=ReflectionState::Fallback;frame.Fallback=enableSSR?ReflectionFallback::SSR:ReflectionFallback::SkyIBL;frame.Diagnostic="probe_without_cubemap: fallback selected";} return frame;
    }
    const char* ReflectionService::FallbackName(ReflectionFallback fallback){switch(fallback){case ReflectionFallback::SSR:return "SSR";case ReflectionFallback::LocalProbe:return "LocalProbe";case ReflectionFallback::SkyIBL:return "SkyIBL";case ReflectionFallback::NeutralAmbient:return "NeutralAmbient";default:return "None";}}
}
