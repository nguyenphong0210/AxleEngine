#include <Axle/GIService.hpp>
#include <cmath>
namespace Axle {
    GIFrame GIService::Prepare(const RenderWorld& world, const SceneRenderSettings& settings, bool computeSupported, uint64_t sceneRevision) const {
        GIFrame frame;
        frame.SceneRevision=sceneRevision;
        if (!settings.EnableDDGI || !world.DDGIProbeGrid || !world.DDGIProbeGrid->Enabled) {
            frame.Diagnostic="ddgi_disabled_or_no_grid: IBL diffuse fallback";
            return frame;
        }
        const auto& grid=*world.DDGIProbeGrid;
        const float energy=grid.EnergyMultiplier*settings.DDGIMultiplier;
        bool valid=std::isfinite(grid.EnergyMultiplier) && grid.EnergyMultiplier>=0.0f && std::isfinite(energy) && energy>=0.0f;
        for (int i=0; i<3; ++i)
            valid=valid && grid.GridDimensions[i]>0 && std::isfinite(grid.GridSpacing[i]) && grid.GridSpacing[i]>0.0f && std::isfinite(grid.GridOrigin[i]);
        if (!valid || !computeSupported) {
            frame.Status=GIFrameStatus::Fallback;
            frame.Diagnostic=valid ? "ddgi_compute_unavailable: IBL diffuse fallback" : "ddgi_invalid_grid: IBL diffuse fallback";
            return frame;
        }
        frame.GridDimensions=grid.GridDimensions;
        frame.GridSpacing=grid.GridSpacing;
        frame.GridOrigin=grid.GridOrigin;
        frame.EnergyMultiplier=energy;
        frame.Status=GIFrameStatus::Experimental;
        frame.UpdateScheduled=true;
        // Scheduling the existing experimental update does not publish usable
        // irradiance/visibility feedback. Diffuse lighting keeps its IBL fallback.
        frame.Diagnostic="ddgi_experimental_update: irradiance/visibility unavailable; IBL diffuse fallback";
        return frame;
    }
}
