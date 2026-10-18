#include <AxleTerrain/TerrainSculptor.hpp>
#include <algorithm>
#include <cmath>

namespace Axle::Terrain {

    f32 TerrainSculptor::CalculateFalloff(f32 dist, f32 radius, BrushFalloff falloff) {
        if (radius <= 0.0001f || dist >= radius) {
            return 0.0f;
        }

        f32 u = dist / radius; // [0, 1)

        switch (falloff) {
            case BrushFalloff::Linear:
                return 1.0f - u;

            case BrushFalloff::Smoothstep: {
                f32 t = 1.0f - u;
                return t * t * (3.0f - 2.0f * t);
            }

            case BrushFalloff::Spherical: {
                f32 diff = 1.0f - u * u;
                return (diff > 0.0f) ? std::sqrt(diff) : 0.0f;
            }

            default:
                return 1.0f - u;
        }
    }

    SculptResult TerrainSculptor::ApplyBrush(
        TerrainData& terrain,
        f32 centerX,
        f32 centerZ,
        const SculptSettings& settings,
        f32 dt
    ) {
        SculptResult result;
        result.Modified = false;

        f32 rad = std::max(0.1f, settings.Radius);
        f32 minX = centerX - rad;
        f32 maxX = centerX + rad;
        f32 minZ = centerZ - rad;
        f32 maxZ = centerZ + rad;

        f32 gxMin = 0.0f, gzMin = 0.0f;
        f32 gxMax = 0.0f, gzMax = 0.0f;

        terrain.WorldToGrid(minX, minZ, gxMin, gzMin);
        terrain.WorldToGrid(maxX, maxZ, gxMax, gzMax);

        u32 resX = terrain.GetResolutionX();
        u32 resZ = terrain.GetResolutionZ();

        i32 startX = std::clamp(static_cast<i32>(std::floor(gxMin)), 0, static_cast<i32>(resX - 1));
        i32 endX   = std::clamp(static_cast<i32>(std::ceil(gxMax)),  0, static_cast<i32>(resX - 1));
        i32 startZ = std::clamp(static_cast<i32>(std::floor(gzMin)), 0, static_cast<i32>(resZ - 1));
        i32 endZ   = std::clamp(static_cast<i32>(std::ceil(gzMax)),  0, static_cast<i32>(resZ - 1));

        if (startX > endX || startZ > endZ) {
            return result;
        }

        f32 rate = settings.Strength * dt;
        f32 dirtyMinY = 1e9f;
        f32 dirtyMaxY = -1e9f;

        for (i32 gz = startZ; gz <= endZ; ++gz) {
            for (i32 gx = startX; gx <= endX; ++gx) {
                glm::vec3 wp = terrain.GridToWorld(gx, gz);
                f32 dist = glm::distance(glm::vec2(wp.x, wp.z), glm::vec2(centerX, centerZ));

                if (dist > rad) {
                    continue;
                }

                f32 weight = CalculateFalloff(dist, rad, settings.Falloff);
                if (weight <= 0.0001f) {
                    continue;
                }

                result.Modified = true;
                f32 currentH = terrain.GetHeight(gx, gz);

                switch (settings.Mode) {
                    case BrushMode::Raise: {
                        f32 newH = currentH + rate * weight;
                        terrain.SetHeight(gx, gz, newH);
                        dirtyMinY = std::min(dirtyMinY, newH);
                        dirtyMaxY = std::max(dirtyMaxY, newH);
                        break;
                    }

                    case BrushMode::Lower: {
                        f32 newH = currentH - rate * weight;
                        terrain.SetHeight(gx, gz, newH);
                        dirtyMinY = std::min(dirtyMinY, newH);
                        dirtyMaxY = std::max(dirtyMaxY, newH);
                        break;
                    }

                    case BrushMode::Smooth: {
                        // Average 8-connected neighbors
                        f32 sumH = 0.0f;
                        u32 count = 0;
                        for (i32 dz = -1; dz <= 1; ++dz) {
                            for (i32 dx = -1; dx <= 1; ++dx) {
                                i32 nx = gx + dx;
                                i32 nz = gz + dz;
                                if (nx >= 0 && nx < static_cast<i32>(resX) &&
                                    nz >= 0 && nz < static_cast<i32>(resZ)) {
                                    sumH += terrain.GetHeight(nx, nz);
                                    count++;
                                }
                            }
                        }
                        f32 avgH = (count > 0) ? (sumH / count) : currentH;
                        f32 factor = std::clamp(rate * weight * 2.0f, 0.0f, 1.0f);
                        f32 newH = currentH + (avgH - currentH) * factor;
                        terrain.SetHeight(gx, gz, newH);
                        dirtyMinY = std::min(dirtyMinY, newH);
                        dirtyMaxY = std::max(dirtyMaxY, newH);
                        break;
                    }

                    case BrushMode::Flatten: {
                        f32 factor = std::clamp(rate * weight * 2.0f, 0.0f, 1.0f);
                        f32 newH = currentH + (settings.TargetHeight - currentH) * factor;
                        terrain.SetHeight(gx, gz, newH);
                        dirtyMinY = std::min(dirtyMinY, newH);
                        dirtyMaxY = std::max(dirtyMaxY, newH);
                        break;
                    }

                    case BrushMode::PaintTexture: {
                        glm::vec4 curSplat = terrain.GetSplatWeight(gx, gz);
                        glm::vec4 targetSplat(0.0f);
                        u32 layer = std::min(settings.PaintLayer, 3u);
                        targetSplat[layer] = 1.0f;

                        f32 factor = std::clamp(rate * weight * 3.0f, 0.0f, 1.0f);
                        glm::vec4 newSplat = glm::mix(curSplat, targetSplat, factor);
                        terrain.SetSplatWeight(gx, gz, newSplat);
                        dirtyMinY = std::min(dirtyMinY, currentH);
                        dirtyMaxY = std::max(dirtyMaxY, currentH);
                        break;
                    }
                }
            }
        }

        if (result.Modified) {
            result.DirtyMin = glm::vec3(minX, dirtyMinY - 1.0f, minZ);
            result.DirtyMax = glm::vec3(maxX, dirtyMaxY + 1.0f, maxZ);
        }

        return result;
    }

    void TerrainSculptor::MarkDirtyChunks(
        std::vector<TerrainChunk>& chunks,
        const glm::vec3& dirtyMin,
        const glm::vec3& dirtyMax
    ) {
        for (auto& chunk : chunks) {
            // AABB overlap test in XZ
            bool overlapX = (chunk.BoundsMin.x <= dirtyMax.x) && (chunk.BoundsMax.x >= dirtyMin.x);
            bool overlapZ = (chunk.BoundsMin.z <= dirtyMax.z) && (chunk.BoundsMax.z >= dirtyMin.z);

            if (overlapX && overlapZ) {
                chunk.NeedsRebuild = true;
            }
        }
    }

}
