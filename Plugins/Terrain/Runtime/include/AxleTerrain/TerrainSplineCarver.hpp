#pragma once

#include <AxleTerrain/TerrainData.hpp>
#include <AxleSpline/SplineComponent.hpp>
#include <AxleSpline/Curve3D.hpp>

namespace Axle::Terrain {

    struct CarveSettings {
        f32 RoadWidth = 10.0f;
        f32 ShoulderWidth = 6.0f;
        f32 SampleStep = 1.0f;
        bool PaintRoadbed = true;
    };

    class TerrainSplineCarver {
    public:
        // Carve and flatten terrain under a spline track curve
        static bool CarveSplineRoad(
            TerrainData& terrain,
            const Axle::Spline::Curve3D& curve,
            const CarveSettings& settings = CarveSettings()
        );

        // Snap all spline curve control points down to terrain surface
        static bool SnapSplineToTerrain(
            Axle::Spline::SplineComponent& splineComp,
            const TerrainData& terrain,
            f32 heightOffset = 0.15f
        );
    };

}
