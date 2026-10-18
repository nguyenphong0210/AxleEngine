#include <AxleTerrain/TerrainData.hpp>
#include <Axle/Log.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace Axle::Terrain {

    TerrainData::TerrainData(u32 resX, u32 resZ, f32 spacingX, f32 spacingZ) {
        Resize(resX, resZ, spacingX, spacingZ);
    }

    void TerrainData::Resize(u32 resX, u32 resZ, f32 spacingX, f32 spacingZ) {
        m_ResolutionX = std::max(2u, resX);
        m_ResolutionZ = std::max(2u, resZ);
        m_SpacingX = std::max(0.01f, spacingX);
        m_SpacingZ = std::max(0.01f, spacingZ);

        size_t totalSamples = static_cast<size_t>(m_ResolutionX) * m_ResolutionZ;
        m_Heights.assign(totalSamples, 0.0f);
        m_Splatmap.assign(totalSamples, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)); // Default 100% grass

        // Center origin around (0, 0)
        m_Origin = glm::vec3(
            -((m_ResolutionX - 1) * m_SpacingX) * 0.5f,
            0.0f,
            -((m_ResolutionZ - 1) * m_SpacingZ) * 0.5f
        );
    }

    f32 TerrainData::GetHeight(u32 x, u32 z) const {
        if (x >= m_ResolutionX || z >= m_ResolutionZ) {
            return 0.0f;
        }
        return m_Heights[Index(x, z)];
    }

    void TerrainData::SetHeight(u32 x, u32 z, f32 height) {
        if (x < m_ResolutionX && z < m_ResolutionZ) {
            m_Heights[Index(x, z)] = height;
        }
    }

    const glm::vec4& TerrainData::GetSplatWeight(u32 x, u32 z) const {
        static const glm::vec4 s_Default(1.0f, 0.0f, 0.0f, 0.0f);
        if (x >= m_ResolutionX || z >= m_ResolutionZ) {
            return s_Default;
        }
        return m_Splatmap[Index(x, z)];
    }

    void TerrainData::SetSplatWeight(u32 x, u32 z, const glm::vec4& weights) {
        if (x < m_ResolutionX && z < m_ResolutionZ) {
            // Normalize weights
            f32 sum = weights.r + weights.g + weights.b + weights.a;
            if (sum > 0.0001f) {
                m_Splatmap[Index(x, z)] = weights / sum;
            } else {
                m_Splatmap[Index(x, z)] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            }
        }
    }

    bool TerrainData::WorldToGrid(f32 worldX, f32 worldZ, f32& outGridX, f32& outGridZ) const {
        f32 localX = worldX - m_Origin.x;
        f32 localZ = worldZ - m_Origin.z;

        outGridX = localX / m_SpacingX;
        outGridZ = localZ / m_SpacingZ;

        return (outGridX >= 0.0f && outGridX <= static_cast<f32>(m_ResolutionX - 1) &&
                outGridZ >= 0.0f && outGridZ <= static_cast<f32>(m_ResolutionZ - 1));
    }

    glm::vec3 TerrainData::GridToWorld(u32 x, u32 z) const {
        f32 wx = m_Origin.x + static_cast<f32>(x) * m_SpacingX;
        f32 wz = m_Origin.z + static_cast<f32>(z) * m_SpacingZ;
        f32 wy = GetHeight(x, z);
        return glm::vec3(wx, wy, wz);
    }

    glm::vec3 TerrainData::GridFloatToWorld(f32 gx, f32 gz, f32 height) const {
        f32 wx = m_Origin.x + gx * m_SpacingX;
        f32 wz = m_Origin.z + gz * m_SpacingZ;
        return glm::vec3(wx, height, wz);
    }

    f32 TerrainData::SampleHeight(f32 worldX, f32 worldZ) const {
        f32 gx = 0.0f, gz = 0.0f;
        WorldToGrid(worldX, worldZ, gx, gz);

        // Clamp to grid bounds
        gx = std::clamp(gx, 0.0f, static_cast<f32>(m_ResolutionX - 1));
        gz = std::clamp(gz, 0.0f, static_cast<f32>(m_ResolutionZ - 1));

        u32 x0 = static_cast<u32>(std::floor(gx));
        u32 z0 = static_cast<u32>(std::floor(gz));
        u32 x1 = std::min(x0 + 1, m_ResolutionX - 1);
        u32 z1 = std::min(z0 + 1, m_ResolutionZ - 1);

        f32 tx = gx - static_cast<f32>(x0);
        f32 tz = gz - static_cast<f32>(z0);

        f32 h00 = GetHeight(x0, z0);
        f32 h10 = GetHeight(x1, z0);
        f32 h01 = GetHeight(x0, z1);
        f32 h11 = GetHeight(x1, z1);

        f32 h0 = h00 * (1.0f - tx) + h10 * tx;
        f32 h1 = h01 * (1.0f - tx) + h11 * tx;

        return h0 * (1.0f - tz) + h1 * tz;
    }

    glm::vec3 TerrainData::CalculateGridNormal(u32 x, u32 z) const {
        u32 xLeft  = (x > 0) ? x - 1 : x;
        u32 xRight = (x + 1 < m_ResolutionX) ? x + 1 : x;
        u32 zDown  = (z > 0) ? z - 1 : z;
        u32 zUp    = (z + 1 < m_ResolutionZ) ? z + 1 : z;

        f32 dx = (xRight == xLeft) ? m_SpacingX : static_cast<f32>(xRight - xLeft) * m_SpacingX;
        f32 dz = (zUp == zDown) ? m_SpacingZ : static_cast<f32>(zUp - zDown) * m_SpacingZ;

        f32 dhdx = (GetHeight(xRight, z) - GetHeight(xLeft, z)) / dx;
        f32 dhdz = (GetHeight(x, zUp) - GetHeight(x, zDown)) / dz;

        glm::vec3 normal(-dhdx, 1.0f, -dhdz);
        return glm::normalize(normal);
    }

    glm::vec3 TerrainData::SampleNormal(f32 worldX, f32 worldZ) const {
        f32 gx = 0.0f, gz = 0.0f;
        WorldToGrid(worldX, worldZ, gx, gz);

        gx = std::clamp(gx, 0.0f, static_cast<f32>(m_ResolutionX - 1));
        gz = std::clamp(gz, 0.0f, static_cast<f32>(m_ResolutionZ - 1));

        u32 x0 = static_cast<u32>(std::floor(gx));
        u32 z0 = static_cast<u32>(std::floor(gz));
        u32 x1 = std::min(x0 + 1, m_ResolutionX - 1);
        u32 z1 = std::min(z0 + 1, m_ResolutionZ - 1);

        f32 tx = gx - static_cast<f32>(x0);
        f32 tz = gz - static_cast<f32>(z0);

        glm::vec3 n00 = CalculateGridNormal(x0, z0);
        glm::vec3 n10 = CalculateGridNormal(x1, z0);
        glm::vec3 n01 = CalculateGridNormal(x0, z1);
        glm::vec3 n11 = CalculateGridNormal(x1, z1);

        glm::vec3 n0 = glm::mix(n00, n10, tx);
        glm::vec3 n1 = glm::mix(n01, n11, tx);
        glm::vec3 n = glm::mix(n0, n1, tz);

        return glm::normalize(n);
    }

    f32 TerrainData::SampleSlopeAngle(f32 worldX, f32 worldZ) const {
        glm::vec3 n = SampleNormal(worldX, worldZ);
        f32 cosTheta = std::clamp(n.y, 0.0f, 1.0f);
        return glm::degrees(std::acos(cosTheta));
    }

    void TerrainData::GenerateFlat(f32 height) {
        std::fill(m_Heights.begin(), m_Heights.end(), height);
    }

    void TerrainData::GenerateHills(f32 frequency, f32 amplitude, u32 octaves) {
        for (u32 z = 0; z < m_ResolutionZ; ++z) {
            for (u32 x = 0; x < m_ResolutionX; ++x) {
                f32 fx = static_cast<f32>(x) * m_SpacingX;
                f32 fz = static_cast<f32>(z) * m_SpacingZ;

                f32 totalHeight = 0.0f;
                f32 currentAmp = amplitude;
                f32 currentFreq = frequency;

                for (u32 o = 0; o < octaves; ++o) {
                    // Analytical harmonic sine/cosine wave landscape
                    f32 wave1 = std::sin(fx * currentFreq * 1.0f + fz * currentFreq * 0.5f);
                    f32 wave2 = std::cos(fx * currentFreq * 0.7f - fz * currentFreq * 1.2f);
                    f32 wave3 = std::sin((fx + fz) * currentFreq * 0.8f);

                    totalHeight += (wave1 * 0.5f + wave2 * 0.3f + wave3 * 0.2f) * currentAmp;

                    currentFreq *= 2.0f;
                    currentAmp *= 0.5f;
                }

                SetHeight(x, z, totalHeight);
            }
        }
    }

    void TerrainData::GenerateRamp(f32 startHeight, f32 endHeight, bool alongX) {
        for (u32 z = 0; z < m_ResolutionZ; ++z) {
            for (u32 x = 0; x < m_ResolutionX; ++x) {
                f32 t = alongX
                    ? static_cast<f32>(x) / static_cast<f32>(m_ResolutionX - 1)
                    : static_cast<f32>(z) / static_cast<f32>(m_ResolutionZ - 1);

                f32 h = startHeight + t * (endHeight - startHeight);
                SetHeight(x, z, h);
            }
        }
    }

    void TerrainData::AutoGenerateSplatmap(f32 cliffSlopeThreshold, f32 grassMaxSlope) {
        for (u32 z = 0; z < m_ResolutionZ; ++z) {
            for (u32 x = 0; x < m_ResolutionX; ++x) {
                glm::vec3 n = CalculateGridNormal(x, z);
                f32 slopeDeg = glm::degrees(std::acos(std::clamp(n.y, 0.0f, 1.0f)));

                glm::vec4 weights(0.0f);
                if (slopeDeg > cliffSlopeThreshold) {
                    // Cliff / Rock
                    f32 rockWeight = std::clamp((slopeDeg - cliffSlopeThreshold) / 15.0f, 0.5f, 1.0f);
                    weights = glm::vec4(0.0f, 0.0f, 1.0f - rockWeight, rockWeight); // Gravel & Rock
                } else if (slopeDeg > grassMaxSlope) {
                    // Transition to Dirt & Gravel
                    f32 t = (slopeDeg - grassMaxSlope) / (cliffSlopeThreshold - grassMaxSlope);
                    weights = glm::vec4(1.0f - t, t * 0.7f, t * 0.3f, 0.0f);
                } else {
                    // Flat / Rolling Green Grass
                    weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                }

                SetSplatWeight(x, z, weights);
            }
        }
    }

    bool TerrainData::SaveToFile(const std::string& filepath) const {
        std::ofstream out(filepath, std::ios::binary);
        if (!out.is_open()) {
            AXLE_ERROR("TerrainData: Failed to open '{0}' for writing.", filepath);
            return false;
        }

        const char magic[12] = "AXLETERRAIN";
        u32 version = 1;
        out.write(magic, 12);
        out.write(reinterpret_cast<const char*>(&version), sizeof(u32));

        out.write(reinterpret_cast<const char*>(&m_ResolutionX), sizeof(u32));
        out.write(reinterpret_cast<const char*>(&m_ResolutionZ), sizeof(u32));
        out.write(reinterpret_cast<const char*>(&m_SpacingX), sizeof(f32));
        out.write(reinterpret_cast<const char*>(&m_SpacingZ), sizeof(f32));
        out.write(reinterpret_cast<const char*>(&m_Origin), sizeof(glm::vec3));

        size_t sampleCount = static_cast<size_t>(m_ResolutionX) * m_ResolutionZ;
        out.write(reinterpret_cast<const char*>(m_Heights.data()), sampleCount * sizeof(f32));
        out.write(reinterpret_cast<const char*>(m_Splatmap.data()), sampleCount * sizeof(glm::vec4));

        return true;
    }

    bool TerrainData::LoadFromFile(const std::string& filepath) {
        std::ifstream in(filepath, std::ios::binary);
        if (!in.is_open()) {
            AXLE_ERROR("TerrainData: Failed to open '{0}' for reading.", filepath);
            return false;
        }

        char magic[12] = {};
        in.read(magic, 12);
        if (std::string(magic) != "AXLETERRAIN") {
            AXLE_ERROR("TerrainData: Invalid magic header in '{0}'.", filepath);
            return false;
        }

        u32 version = 0;
        in.read(reinterpret_cast<char*>(&version), sizeof(u32));
        if (version != 1) {
            AXLE_ERROR("TerrainData: Unsupported version {0} in '{1}'.", version, filepath);
            return false;
        }

        u32 resX = 0, resZ = 0;
        f32 spX = 1.0f, spZ = 1.0f;
        glm::vec3 origin(0.0f);

        in.read(reinterpret_cast<char*>(&resX), sizeof(u32));
        in.read(reinterpret_cast<char*>(&resZ), sizeof(u32));
        in.read(reinterpret_cast<char*>(&spX), sizeof(f32));
        in.read(reinterpret_cast<char*>(&spZ), sizeof(f32));
        in.read(reinterpret_cast<char*>(&origin), sizeof(glm::vec3));

        Resize(resX, resZ, spX, spZ);
        m_Origin = origin;

        size_t sampleCount = static_cast<size_t>(m_ResolutionX) * m_ResolutionZ;
        in.read(reinterpret_cast<char*>(m_Heights.data()), sampleCount * sizeof(f32));
        in.read(reinterpret_cast<char*>(m_Splatmap.data()), sampleCount * sizeof(glm::vec4));

        return true;
    }

}
