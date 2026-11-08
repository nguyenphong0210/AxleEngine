#include <AxleTerrain/TerrainSplineCarver.hpp>
#include <AxleSpline/TrackGenerator.hpp>
#include <algorithm>
#include <cmath>

namespace Axle::Terrain {

    bool TerrainSplineCarver::CarveSplineRoad(
        TerrainData& terrain,
        const Axle::Spline::Curve3D& curve,
        const CarveSettings& settings
    ) {
        f32 totalLength = curve.GetTotalLength();
        if (totalLength <= 0.001f || curve.GetNodeCount() < 2) {
            return false;
        }

        f32 step = std::max(0.2f, settings.SampleStep);
        u32 sampleCount = static_cast<u32>(std::ceil(totalLength / step)) + 1;

        f32 halfRoad = settings.RoadWidth * 0.5f;
        f32 shoulder = settings.ShoulderWidth;
        f32 totalRadius = halfRoad + shoulder;

        u32 resX = terrain.GetResolutionX();
        u32 resZ = terrain.GetResolutionZ();

        for (u32 i = 0; i < sampleCount; ++i) {
            f32 dist = std::min(static_cast<f32>(i) * step, totalLength);
            auto frame = curve.SampleFrame(dist);

            glm::vec3 center = frame.Position;
            f32 rad = totalRadius * frame.Scale;

            f32 gxMin = 0.0f, gzMin = 0.0f;
            f32 gxMax = 0.0f, gzMax = 0.0f;

            terrain.WorldToGrid(center.x - rad, center.z - rad, gxMin, gzMin);
            terrain.WorldToGrid(center.x + rad, center.z + rad, gxMax, gzMax);

            i32 startX = std::clamp(static_cast<i32>(std::floor(gxMin)), 0, static_cast<i32>(resX - 1));
            i32 endX   = std::clamp(static_cast<i32>(std::ceil(gxMax)),  0, static_cast<i32>(resX - 1));
            i32 startZ = std::clamp(static_cast<i32>(std::floor(gzMin)), 0, static_cast<i32>(resZ - 1));
            i32 endZ   = std::clamp(static_cast<i32>(std::ceil(gzMax)),  0, static_cast<i32>(resZ - 1));

            for (i32 gz = startZ; gz <= endZ; ++gz) {
                for (i32 gx = startX; gx <= endX; ++gx) {
                    glm::vec3 wp = terrain.GridToWorld(gx, gz);
                    glm::vec3 delta = wp - center;

                    // Projection along tangent
                    f32 dt = glm::dot(delta, frame.Tangent);
                    if (std::abs(dt) > step * 0.8f) {
                        continue;
                    }

                    // Lateral offset along binormal
                    f32 lat = std::abs(glm::dot(delta, frame.Binormal));
                    f32 hr = halfRoad * frame.Scale;
                    f32 sh = shoulder * frame.Scale;

                    if (lat <= hr) {
                        // Roadbed: match spline road surface
                        terrain.SetHeight(gx, gz, center.y);
                        if (settings.PaintRoadbed) {
                            // Paint dirt & gravel under track
                            terrain.SetSplatWeight(gx, gz, glm::vec4(0.0f, 0.5f, 0.5f, 0.0f));
                        }
                    } else if (lat <= hr + sh && sh > 0.001f) {
                        // Shoulder: smooth transition
                        f32 t = (lat - hr) / sh;
                        f32 w = t * t * (3.0f - 2.0f * t); // smoothstep
                        f32 curH = terrain.GetHeight(gx, gz);
                        terrain.SetHeight(gx, gz, glm::mix(center.y, curH, w));
                        if (settings.PaintRoadbed) {
                            glm::vec4 curSplat = terrain.GetSplatWeight(gx, gz);
                            glm::vec4 roadSplat(0.0f, 0.4f, 0.4f, 0.2f);
                            terrain.SetSplatWeight(gx, gz, glm::mix(roadSplat, curSplat, w));
                        }
                    }
                }
            }
        }

        return true;
    }

    bool TerrainSplineCarver::SnapSplineToTerrain(
        Axle::Spline::SplineComponent& splineComp,
        const TerrainData& terrain,
        f32 heightOffset
    ) {
        if (splineComp.Curve.GetNodeCount() == 0) {
            return false;
        }

        auto& nodes = splineComp.Curve.GetNodes();
        for (auto& node : nodes) {
            f32 h = terrain.SampleHeight(node.Position.x, node.Position.z);
            node.Position.y = h + heightOffset;
        }

        splineComp.Curve.RebuildCache();
        return true;
    }

}
