#include <Axle/GIService.hpp>
#include <Axle/RenderContracts.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {
    GIFrame GIService::Prepare(const RenderWorld& world, const SceneRenderSettings& settings, bool computeSupported, uint64_t sceneRevision) const {
        GIFrame frame;
        frame.SceneRevision = sceneRevision;
        if (!settings.EnableDDGI || !world.DDGIProbeGrid || !world.DDGIProbeGrid->Enabled) {
            frame.Diagnostic = "ddgi_disabled_or_no_grid: IBL diffuse fallback";
            return frame;
        }
        const auto& grid = *world.DDGIProbeGrid;
        const float energy = grid.EnergyMultiplier * settings.DDGIMultiplier;
        std::string reason;
        const bool gridValid = DDGIContract::ValidateGrid(grid, &reason) && std::isfinite(energy) && energy >= 0.0f;
        if (!gridValid || !computeSupported) {
            frame.Status = GIFrameStatus::Fallback;
            frame.Diagnostic = gridValid ? "ddgi_compute_unavailable: IBL diffuse fallback"
                                         : ("ddgi_invalid_grid: " + reason + "; IBL diffuse fallback");
            return frame;
        }
        const auto extent = DDGIContract::ComputeAtlasExtent(grid.GridDimensions);
        frame.GridDimensions = grid.GridDimensions;
        frame.GridSpacing = grid.GridSpacing;
        frame.GridOrigin = grid.GridOrigin;
        frame.EnergyMultiplier = energy;
        frame.ProbeCount = extent.ProbeCount;
        frame.MaxProbesPerFrame = std::clamp(static_cast<uint32_t>(grid.MaxProbesPerFrame), 1u, DDGIContract::HardMaxProbesPerFrame);
        frame.UpdateFraction = std::clamp(grid.UpdateFraction, 0.01f, 1.0f);
        frame.Hysteresis = std::clamp(grid.Hysteresis, 0.0f, 0.99f);
        frame.ViewBias = std::max(0.0f, grid.ViewBias);
        frame.NormalBias = std::max(0.0f, grid.NormalBias);
        frame.Status = GIFrameStatus::Experimental;
        frame.UpdateScheduled = true;
        // Experimental update prepares bounded atlases; diffuse feedback wiring completes in U7-B/U7-C.
        frame.Diagnostic = "ddgi_experimental_update: irradiance/visibility unavailable; IBL diffuse fallback";
        return frame;
    }
}
