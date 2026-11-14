#include <Axle/GIService.hpp>
namespace Axle {
    GIFrame GIService::Prepare(const RenderWorld& world, const SceneRenderSettings& settings, bool computeSupported, uint64_t sceneRevision) const {
        GIFrame frame; frame.SceneRevision=sceneRevision; if(!settings.EnableDDGI||!world.DDGIProbeGrid){frame.Diagnostic="ddgi_disabled_or_no_grid: IBL diffuse fallback";return frame;} const auto& grid=*world.DDGIProbeGrid; frame.GridDimensions=grid.GridDimensions;frame.GridSpacing=grid.GridSpacing;
        if(!computeSupported){frame.Status=GIFrameStatus::Fallback;frame.Diagnostic="ddgi_compute_unavailable: IBL diffuse fallback";return frame;} frame.Status=GIFrameStatus::Experimental;frame.Fallback=GIFallback::None;frame.UpdateScheduled=true;frame.Diagnostic="ddgi_boundary_ready: irradiance/visibility feedback deferred to M15";return frame;
    }
}
