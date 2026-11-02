#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <random>
#include <memory>

namespace Axle {

    struct ParticleMeshTriangle {
        glm::vec3 V0{ 0.0f };
        glm::vec3 V1{ 0.0f };
        glm::vec3 V2{ 0.0f };
        glm::vec3 N0{ 0.0f, 1.0f, 0.0f };
        glm::vec3 N1{ 0.0f, 1.0f, 0.0f };
        glm::vec3 N2{ 0.0f, 1.0f, 0.0f };
        float Area = 0.0f;
        bool HasNormals = false;
    };

    class ParticleMeshSampler {
    public:
        ParticleMeshSampler() = default;
        ~ParticleMeshSampler() = default;

        // Build sampler from vertices, triangle indices, and optional per-vertex normals
        bool Build(const std::vector<glm::vec3>& positions,
                   const std::vector<uint32_t>& indices,
                   const std::vector<glm::vec3>& normals = {});

        // Build directly from a list of triangles
        bool BuildFromTriangles(const std::vector<ParticleMeshTriangle>& triangles);

        // Load Wavefront OBJ file directly on CPU (headless / device-free)
        bool LoadFromOBJ(const std::string& filepath);

        void Reset();

        bool IsValid() const { return !m_Triangles.empty() && m_TotalArea > 1e-7f; }
        float GetTotalArea() const { return m_TotalArea; }
        size_t GetTriangleCount() const { return m_Triangles.size(); }
        const std::string& GetLastError() const { return m_LastError; }

        // Area-weighted barycentric square-root sampling
        bool Sample(glm::vec3& outPos, glm::vec3& outNormal, std::mt19937& rng) const;

    private:
        std::vector<ParticleMeshTriangle> m_Triangles;
        std::vector<float> m_Cdf; // Cumulative distribution function normalized [0, 1]
        float m_TotalArea = 0.0f;
        mutable std::string m_LastError;
    };

} // namespace Axle

