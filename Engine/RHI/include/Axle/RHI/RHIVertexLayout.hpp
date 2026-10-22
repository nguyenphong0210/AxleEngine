#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <NRI.h>
#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

namespace Axle::RHI {

    // Standard 64-byte unified PBR 3D vertex layout
    // Perfectly cache-line aligned (64 bytes = 16 x 4-byte floats)
    struct RHIVertex3D {
        glm::vec3 Position{ 0.0f };                                 // 12 bytes [offset 0]
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };                       // 12 bytes [offset 12]
        glm::vec4 Tangent{ 1.0f, 0.0f, 0.0f, 1.0f };                // 16 bytes [offset 24] (xyz = tangent, w = handedness / bitangent sign)
        glm::vec2 TexCoord{ 0.0f };                                 //  8 bytes [offset 40]
        glm::vec4 Color{ 1.0f };                                    // 16 bytes [offset 48] (splat weights for terrain or vertex tint)

        bool operator==(const RHIVertex3D& other) const {
            return Position == other.Position &&
                   Normal == other.Normal &&
                   Tangent == other.Tangent &&
                   TexCoord == other.TexCoord &&
                   Color == other.Color;
        }
    };
    static_assert(sizeof(RHIVertex3D) == 64, "RHIVertex3D must be exactly 64 bytes (cache-line aligned)!");

    // Extended 96-byte skinned skeletal mesh vertex layout
    struct RHISkinnedVertex3D {
        glm::vec3 Position{ 0.0f };                                 // 12 bytes [offset 0]
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };                       // 12 bytes [offset 12]
        glm::vec4 Tangent{ 1.0f, 0.0f, 0.0f, 1.0f };                // 16 bytes [offset 24]
        glm::vec2 TexCoord{ 0.0f };                                 //  8 bytes [offset 40]
        glm::vec4 Color{ 1.0f };                                    // 16 bytes [offset 48]
        glm::uvec4 JointIndices{ 0 };                               // 16 bytes [offset 64] (bone IDs up to 4 influences)
        glm::vec4 JointWeights{ 1.0f, 0.0f, 0.0f, 0.0f };          // 16 bytes [offset 80] (normalized bone weights)
    };
    static_assert(sizeof(RHISkinnedVertex3D) == 96, "RHISkinnedVertex3D must be exactly 96 bytes!");

    // Generate NRI VertexAttributeDesc array for RHIVertex3D
    inline std::vector<nri::VertexAttributeDesc> GetRHIVertex3DAttributes(uint16_t streamIndex = 0) {
        std::vector<nri::VertexAttributeDesc> attribs(5);

        // 0: Position (float3)
        attribs[0].d3d = { "POSITION", 0 };
        attribs[0].vk = { 0 };
        attribs[0].offset = offsetof(RHIVertex3D, Position);
        attribs[0].format = nri::Format::RGB32_SFLOAT;
        attribs[0].streamIndex = streamIndex;

        // 1: Normal (float3)
        attribs[1].d3d = { "NORMAL", 0 };
        attribs[1].vk = { 1 };
        attribs[1].offset = offsetof(RHIVertex3D, Normal);
        attribs[1].format = nri::Format::RGB32_SFLOAT;
        attribs[1].streamIndex = streamIndex;

        // 2: Tangent (float4)
        attribs[2].d3d = { "TANGENT", 0 };
        attribs[2].vk = { 2 };
        attribs[2].offset = offsetof(RHIVertex3D, Tangent);
        attribs[2].format = nri::Format::RGBA32_SFLOAT;
        attribs[2].streamIndex = streamIndex;

        // 3: TexCoord (float2)
        attribs[3].d3d = { "TEXCOORD", 0 };
        attribs[3].vk = { 3 };
        attribs[3].offset = offsetof(RHIVertex3D, TexCoord);
        attribs[3].format = nri::Format::RG32_SFLOAT;
        attribs[3].streamIndex = streamIndex;

        // 4: Color (float4)
        attribs[4].d3d = { "COLOR", 0 };
        attribs[4].vk = { 4 };
        attribs[4].offset = offsetof(RHIVertex3D, Color);
        attribs[4].format = nri::Format::RGBA32_SFLOAT;
        attribs[4].streamIndex = streamIndex;

        return attribs;
    }

    // Generate NRI VertexStreamDesc for RHIVertex3D
    inline nri::VertexStreamDesc GetRHIVertex3DStreamDesc(uint16_t bindingSlot = 0) {
        nri::VertexStreamDesc desc{};
        desc.bindingSlot = bindingSlot;
        desc.stepRate = nri::VertexStreamStepRate::PER_VERTEX;
        desc.stride = static_cast<uint16_t>(sizeof(RHIVertex3D));
        return desc;
    }

    // Generate NRI VertexAttributeDesc array for RHISkinnedVertex3D
    inline std::vector<nri::VertexAttributeDesc> GetRHISkinnedVertex3DAttributes(uint16_t streamIndex = 0) {
        std::vector<nri::VertexAttributeDesc> attribs = GetRHIVertex3DAttributes(streamIndex);
        attribs.resize(7);

        // 5: JointIndices (uvec4)
        attribs[5].d3d = { "BLENDINDICES", 0 };
        attribs[5].vk = { 5 };
        attribs[5].offset = offsetof(RHISkinnedVertex3D, JointIndices);
        attribs[5].format = nri::Format::RGBA32_UINT;
        attribs[5].streamIndex = streamIndex;

        // 6: JointWeights (float4)
        attribs[6].d3d = { "BLENDWEIGHT", 0 };
        attribs[6].vk = { 6 };
        attribs[6].offset = offsetof(RHISkinnedVertex3D, JointWeights);
        attribs[6].format = nri::Format::RGBA32_SFLOAT;
        attribs[6].streamIndex = streamIndex;

        return attribs;
    }

    inline nri::VertexStreamDesc GetRHISkinnedVertex3DStreamDesc(uint16_t bindingSlot = 0) {
        nri::VertexStreamDesc desc{};
        desc.bindingSlot = bindingSlot;
        desc.stepRate = nri::VertexStreamStepRate::PER_VERTEX;
        desc.stride = static_cast<uint16_t>(sizeof(RHISkinnedVertex3D));
        return desc;
    }

    // Utility: Calculate MikkTSpace-compatible tangents and handedness for indexed triangle mesh
    inline void ComputeMeshTangents(std::vector<RHIVertex3D>& vertices, const std::vector<uint32_t>& indices) {
        if (vertices.empty() || indices.empty() || (indices.size() % 3) != 0) return;

        std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0f));

        for (size_t i = 0; i < indices.size(); i += 3) {
            uint32_t i1 = indices[i];
            uint32_t i2 = indices[i + 1];
            uint32_t i3 = indices[i + 2];

            if (i1 >= vertices.size() || i2 >= vertices.size() || i3 >= vertices.size()) continue;

            const glm::vec3& v1 = vertices[i1].Position;
            const glm::vec3& v2 = vertices[i2].Position;
            const glm::vec3& v3 = vertices[i3].Position;

            const glm::vec2& w1 = vertices[i1].TexCoord;
            const glm::vec2& w2 = vertices[i2].TexCoord;
            const glm::vec2& w3 = vertices[i3].TexCoord;

            float x1 = v2.x - v1.x;
            float x2 = v3.x - v1.x;
            float y1 = v2.y - v1.y;
            float y2 = v3.y - v1.y;
            float z1 = v2.z - v1.z;
            float z2 = v3.z - v1.z;

            float s1 = w2.x - w1.x;
            float s2 = w3.x - w1.x;
            float t1 = w2.y - w1.y;
            float t2 = w3.y - w1.y;

            float r = (s1 * t2 - s2 * t1);
            if (std::abs(r) > 1e-7f) {
                r = 1.0f / r;
                glm::vec3 sdir((t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r);
                glm::vec3 tdir((s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r, (s1 * z2 - s2 * z1) * r);

                tan1[i1] += sdir;
                tan1[i2] += sdir;
                tan1[i3] += sdir;

                tan2[i1] += tdir;
                tan2[i2] += tdir;
                tan2[i3] += tdir;
            }
        }

        for (size_t a = 0; a < vertices.size(); ++a) {
            const glm::vec3& n = vertices[a].Normal;
            const glm::vec3& t = tan1[a];

            if (glm::dot(t, t) > 1e-7f) {
                // Gram-Schmidt orthogonalize
                glm::vec3 tangentXYZ = glm::normalize(t - n * glm::dot(n, t));

                // Calculate handedness
                float handedness = (glm::dot(glm::cross(n, t), tan2[a]) < 0.0f) ? -1.0f : 1.0f;
                vertices[a].Tangent = glm::vec4(tangentXYZ, handedness);
            } else {
                // Fallback orthogonal tangent
                glm::vec3 up = (std::abs(n.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                glm::vec3 tangentXYZ = glm::normalize(glm::cross(up, n));
                vertices[a].Tangent = glm::vec4(tangentXYZ, 1.0f);
            }
        }
    }

}
