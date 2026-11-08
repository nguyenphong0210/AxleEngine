#pragma once

#include <Axle/Types.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace Axle::Terrain {

    struct TerrainSplatLayers {
        static constexpr u32 GRASS  = 0;
        static constexpr u32 DIRT   = 1;
        static constexpr u32 GRAVEL = 2;
        static constexpr u32 ROCK   = 3;
    };

    class TerrainData {
    public:
        TerrainData(u32 resX = 128, u32 resZ = 128, f32 spacingX = 1.0f, f32 spacingZ = 1.0f);
        ~TerrainData() = default;

        void Resize(u32 resX, u32 resZ, f32 spacingX = 1.0f, f32 spacingZ = 1.0f);

        // Grid parameters
        u32 GetResolutionX() const { return m_ResolutionX; }
        u32 GetResolutionZ() const { return m_ResolutionZ; }
        f32 GetSpacingX() const { return m_SpacingX; }
        f32 GetSpacingZ() const { return m_SpacingZ; }
        f32 GetWidth() const { return static_cast<f32>(m_ResolutionX - 1) * m_SpacingX; }
        f32 GetLength() const { return static_cast<f32>(m_ResolutionZ - 1) * m_SpacingZ; }

        const glm::vec3& GetOrigin() const { return m_Origin; }
        void SetOrigin(const glm::vec3& origin) { m_Origin = origin; }

        // Grid-based discrete queries
        f32 GetHeight(u32 x, u32 z) const;
        void SetHeight(u32 x, u32 z, f32 height);

        const glm::vec4& GetSplatWeight(u32 x, u32 z) const;
        void SetSplatWeight(u32 x, u32 z, const glm::vec4& weights);

        std::vector<f32>& GetHeights() { return m_Heights; }
        const std::vector<f32>& GetHeights() const { return m_Heights; }

        std::vector<glm::vec4>& GetSplatmap() { return m_Splatmap; }
        const std::vector<glm::vec4>& GetSplatmap() const { return m_Splatmap; }

        // Coordinate transforms
        bool WorldToGrid(f32 worldX, f32 worldZ, f32& outGridX, f32& outGridZ) const;
        glm::vec3 GridToWorld(u32 x, u32 z) const;
        glm::vec3 GridFloatToWorld(f32 gx, f32 gz, f32 height) const;

        // Continuous bilinear queries
        f32 SampleHeight(f32 worldX, f32 worldZ) const;
        glm::vec3 SampleNormal(f32 worldX, f32 worldZ) const;
        f32 SampleSlopeAngle(f32 worldX, f32 worldZ) const; // In degrees [0, 90]

        // Discrete normal computation for a grid point
        glm::vec3 CalculateGridNormal(u32 x, u32 z) const;

        // Procedural generators
        void GenerateFlat(f32 height = 0.0f);
        void GenerateHills(f32 frequency = 0.03f, f32 amplitude = 8.0f, u32 octaves = 3);
        void GenerateRamp(f32 startHeight, f32 endHeight, bool alongX = false);
        void AutoGenerateSplatmap(f32 cliffSlopeThreshold = 35.0f, f32 grassMaxSlope = 20.0f);

        // Serialization
        bool SaveToFile(const std::string& filepath) const;
        bool LoadFromFile(const std::string& filepath);

    private:
        u32 Index(u32 x, u32 z) const { return z * m_ResolutionX + x; }

    private:
        u32 m_ResolutionX = 128;
        u32 m_ResolutionZ = 128;
        f32 m_SpacingX = 1.0f;
        f32 m_SpacingZ = 1.0f;
        glm::vec3 m_Origin{ 0.0f };

        std::vector<f32> m_Heights;
        std::vector<glm::vec4> m_Splatmap; // (R=Grass, G=Dirt, B=Gravel, A=Rock)
    };

}
