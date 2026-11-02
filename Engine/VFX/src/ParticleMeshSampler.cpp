#include <Axle/ParticleMeshSampler.hpp>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace Axle {

    void ParticleMeshSampler::Reset() {
        m_Triangles.clear();
        m_Cdf.clear();
        m_TotalArea = 0.0f;
        m_LastError.clear();
    }

    bool ParticleMeshSampler::Build(const std::vector<glm::vec3>& positions,
                                   const std::vector<uint32_t>& indices,
                                   const std::vector<glm::vec3>& normals) {
        Reset();
        if (positions.empty() || indices.size() < 3) {
            m_LastError = "Mesh positions or indices are empty";
            return false;
        }

        bool hasNormals = (normals.size() == positions.size());
        std::vector<ParticleMeshTriangle> candidateTriangles;
        candidateTriangles.reserve(indices.size() / 3);

        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            uint32_t idx0 = indices[i];
            uint32_t idx1 = indices[i + 1];
            uint32_t idx2 = indices[i + 2];
            if (idx0 >= positions.size() || idx1 >= positions.size() || idx2 >= positions.size()) {
                continue;
            }

            glm::vec3 v0 = positions[idx0];
            glm::vec3 v1 = positions[idx1];
            glm::vec3 v2 = positions[idx2];

            glm::vec3 crossP = glm::cross(v1 - v0, v2 - v0);
            float area = 0.5f * glm::length(crossP);
            if (area <= 1e-7f || !std::isfinite(area)) {
                continue; // Exclude degenerate triangle
            }

            ParticleMeshTriangle tri;
            tri.V0 = v0;
            tri.V1 = v1;
            tri.V2 = v2;
            tri.Area = area;
            tri.HasNormals = hasNormals;
            if (hasNormals) {
                tri.N0 = normals[idx0];
                tri.N1 = normals[idx1];
                tri.N2 = normals[idx2];
            }
            candidateTriangles.push_back(tri);
        }

        return BuildFromTriangles(candidateTriangles);
    }

    bool ParticleMeshSampler::BuildFromTriangles(const std::vector<ParticleMeshTriangle>& triangles) {
        Reset();
        m_Triangles.reserve(triangles.size());
        float accumArea = 0.0f;

        for (const auto& tri : triangles) {
            if (tri.Area > 1e-7f && std::isfinite(tri.Area)) {
                m_Triangles.push_back(tri);
                accumArea += tri.Area;
            }
        }

        if (m_Triangles.empty() || accumArea <= 1e-7f) {
            m_LastError = "No non-degenerate triangles found in mesh";
            return false;
        }

        m_TotalArea = accumArea;
        m_Cdf.resize(m_Triangles.size());
        float runningSum = 0.0f;
        for (size_t i = 0; i < m_Triangles.size(); ++i) {
            runningSum += m_Triangles[i].Area;
            m_Cdf[i] = runningSum / m_TotalArea;
        }
        m_Cdf.back() = 1.0f;
        return true;
    }

    bool ParticleMeshSampler::LoadFromOBJ(const std::string& filepath) {
        Reset();
        std::ifstream file(filepath);
        if (!file.is_open()) {
            m_LastError = "Failed to open OBJ file: " + filepath;
            return false;
        }

        std::vector<glm::vec3> temp_positions;
        std::vector<glm::vec3> temp_normals;
        std::vector<uint32_t> indices;

        std::string line;
        while (std::getline(file, line)) {
            size_t firstNonSpace = line.find_first_not_of(" \t\r\n");
            if (firstNonSpace == std::string::npos) continue;
            if (line[firstNonSpace] == '#') continue;

            std::string sub = line.substr(firstNonSpace);
            if (sub.rfind("v ", 0) == 0) {
                std::istringstream s(sub.substr(2));
                glm::vec3 v(0.0f);
                s >> v.x >> v.y >> v.z;
                temp_positions.push_back(v);
            } else if (sub.rfind("vn ", 0) == 0) {
                std::istringstream s(sub.substr(3));
                glm::vec3 n(0.0f);
                s >> n.x >> n.y >> n.z;
                temp_normals.push_back(n);
            } else if (sub.rfind("f ", 0) == 0) {
                std::istringstream s(sub.substr(2));
                std::string token;
                std::vector<uint32_t> faceIdx;
                while (s >> token) {
                    size_t slash = token.find('/');
                    std::string idxStr = (slash == std::string::npos) ? token : token.substr(0, slash);
                    if (!idxStr.empty()) {
                        try {
                            int idx = std::stoi(idxStr);
                            if (idx < 0) {
                                idx = static_cast<int>(temp_positions.size()) + idx + 1;
                            }
                            if (idx > 0 && static_cast<size_t>(idx) <= temp_positions.size()) {
                                faceIdx.push_back(static_cast<uint32_t>(idx - 1));
                            }
                        } catch (...) {}
                    }
                }
                for (size_t i = 1; i + 1 < faceIdx.size(); ++i) {
                    indices.push_back(faceIdx[0]);
                    indices.push_back(faceIdx[i]);
                    indices.push_back(faceIdx[i + 1]);
                }
            }
        }

        return Build(temp_positions, indices);
    }

    bool ParticleMeshSampler::Sample(glm::vec3& outPos, glm::vec3& outNormal, std::mt19937& rng) const {
        if (!IsValid()) {
            m_LastError = "MeshSurface sampler has no valid geometry (empty or degenerate); falling back to origin point.";
            outPos = glm::vec3(0.0f);
            outNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            return false;
        }

        std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
        float r = dist01(rng);

        auto it = std::lower_bound(m_Cdf.begin(), m_Cdf.end(), r);
        size_t triIdx = 0;
        if (it != m_Cdf.end()) {
            triIdx = std::min(static_cast<size_t>(std::distance(m_Cdf.begin(), it)), m_Triangles.size() - 1);
        } else {
            triIdx = m_Triangles.size() - 1;
        }

        const auto& tri = m_Triangles[triIdx];

        // Square-root barycentric coordinates (Turk 1990)
        float u1 = dist01(rng);
        float u2 = dist01(rng);
        float sqrtU1 = std::sqrt(u1);
        float u = 1.0f - sqrtU1;
        float v = u2 * sqrtU1;
        float w = 1.0f - u - v;

        outPos = u * tri.V0 + v * tri.V1 + w * tri.V2;

        if (tri.HasNormals) {
            glm::vec3 interpNormal = u * tri.N0 + v * tri.N1 + w * tri.N2;
            float lenSq = glm::dot(interpNormal, interpNormal);
            if (lenSq > 1e-8f) {
                outNormal = interpNormal * (1.0f / std::sqrt(lenSq));
                return true;
            }
        }

        glm::vec3 e1 = tri.V1 - tri.V0;
        glm::vec3 e2 = tri.V2 - tri.V0;
        glm::vec3 faceNormal = glm::cross(e1, e2);
        float fnLenSq = glm::dot(faceNormal, faceNormal);
        if (fnLenSq > 1e-8f) {
            outNormal = faceNormal * (1.0f / std::sqrt(fnLenSq));
        } else {
            outNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        return true;
    }

} // namespace Axle

