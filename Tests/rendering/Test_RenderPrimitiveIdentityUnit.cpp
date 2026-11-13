#include "RenderTestCommon.hpp"
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/BuiltinMesh.hpp>
#include <glm/gtc/epsilon.hpp>

namespace Axle::RenderTests {

    static bool Test_BuiltinPrimitiveURIsAndUUIDs() {
        // Builtin asset UUID constants
        RENDER_CHECK(BuiltinAssets::CubeUUID == 0xBA5E0001ULL, "Cube UUID is 0xBA5E0001");
        RENDER_CHECK(BuiltinAssets::SphereUUID == 0xBA5E0002ULL, "Sphere UUID is 0xBA5E0002");
        RENDER_CHECK(BuiltinAssets::CylinderUUID == 0xBA5E0003ULL, "Cylinder UUID is 0xBA5E0003");
        RENDER_CHECK(BuiltinAssets::ConeUUID == 0xBA5E0004ULL, "Cone UUID is 0xBA5E0004");
        RENDER_CHECK(BuiltinAssets::PlaneUUID == 0xBA5E0005ULL, "Plane UUID is 0xBA5E0005");

        // AssetManager builtin detection
        RENDER_CHECK(AssetManager::IsBuiltin(BuiltinAssets::CubeUUID), "IsBuiltin accepts Cube UUID");
        RENDER_CHECK(AssetManager::IsBuiltin(BuiltinAssets::SphereUUID), "IsBuiltin accepts Sphere UUID");
        RENDER_CHECK(AssetManager::IsBuiltin(BuiltinAssets::CylinderUUID), "IsBuiltin accepts Cylinder UUID");
        RENDER_CHECK(AssetManager::IsBuiltin(BuiltinAssets::ConeUUID), "IsBuiltin accepts Cone UUID");
        RENDER_CHECK(AssetManager::IsBuiltin(BuiltinAssets::PlaneUUID), "IsBuiltin accepts Plane UUID");

        RENDER_CHECK(!AssetManager::IsBuiltin(UUID(0x12345678ULL)), "Arbitrary UUID is not builtin");
        RENDER_CHECK(!AssetManager::IsBuiltin(UUID(0)), "Zero UUID is not builtin");

        RENDER_CHECK(AssetManager::IsBuiltin("builtin://cube"), "IsBuiltin accepts builtin://cube");
        RENDER_CHECK(AssetManager::IsBuiltin("builtin://sphere"), "IsBuiltin accepts builtin://sphere");
        RENDER_CHECK(AssetManager::IsBuiltin("builtin://cylinder"), "IsBuiltin accepts builtin://cylinder");
        RENDER_CHECK(AssetManager::IsBuiltin("builtin://cone"), "IsBuiltin accepts builtin://cone");
        RENDER_CHECK(AssetManager::IsBuiltin("builtin://plane"), "IsBuiltin accepts builtin://plane");

        RENDER_CHECK(!AssetManager::IsBuiltin("assets/textures/test.png"), "Regular asset path is not builtin");
        RENDER_CHECK(!AssetManager::IsBuiltin(""), "Empty path is not builtin");

        const auto* sphereDefinition = GetBuiltinMeshDefinition(BuiltinMeshId::Sphere);
        RENDER_CHECK(sphereDefinition != nullptr, "Builtin sphere definition exists");
        RENDER_CHECK(sphereDefinition->Stacks >= 64 && sphereDefinition->Segments >= 128,
                     "Builtin sphere uses the M14 high-fidelity tessellation baseline");

        return true;
    }

    static bool Test_ProceduralCubeGeometryContracts() {
        std::vector<RHI::RHIVertex3D> vertices;
        std::vector<uint32_t> indices;

        const float size = 2.0f;
        const float h = size * 0.5f;
        RHI::RHIMesh::GenerateCube(size, vertices, indices);

        RENDER_CHECK(vertices.size() == 24, "Cube generates 24 vertices (6 faces x 4 verts)");
        RENDER_CHECK(indices.size() == 36, "Cube generates 36 indices (12 triangles x 3 indices)");

        for (uint32_t idx : indices) {
            RENDER_CHECK(idx < vertices.size(), "Index references valid vertex");
        }

        // Validate geometry bounds, normals and UV coordinates
        for (const auto& v : vertices) {
            RENDER_CHECK(std::abs(v.Position.x) <= h + 1e-4f, "Position.x within cube bounds");
            RENDER_CHECK(std::abs(v.Position.y) <= h + 1e-4f, "Position.y within cube bounds");
            RENDER_CHECK(std::abs(v.Position.z) <= h + 1e-4f, "Position.z within cube bounds");

            float normLen = glm::length(v.Normal);
            RENDER_CHECK(Near(normLen, 1.0f, 1e-3f), "Cube vertex normal has unit length");

            float tanLen = glm::length(glm::vec3(v.Tangent));
            RENDER_CHECK(Near(tanLen, 1.0f, 1e-3f), "Cube vertex tangent has unit length");

            float dotNT = glm::dot(v.Normal, glm::vec3(v.Tangent));
            RENDER_CHECK(Near(dotNT, 0.0f, 1e-3f), "Cube vertex normal and tangent are orthogonal");

            RENDER_CHECK(v.TexCoord.x >= 0.0f && v.TexCoord.x <= 1.0f, "TexCoord.x in [0, 1]");
            RENDER_CHECK(v.TexCoord.y >= 0.0f && v.TexCoord.y <= 1.0f, "TexCoord.y in [0, 1]");
        }

        return true;
    }

    static bool Test_ProceduralSphereGeometryContracts() {
        std::vector<RHI::RHIVertex3D> vertices;
        std::vector<uint32_t> indices;

        const float radius = 1.5f;
        const uint32_t stacks = 16;
        const uint32_t sectors = 16;
        RHI::RHIMesh::GenerateSphere(radius, stacks, sectors, vertices, indices);

        RENDER_CHECK(!vertices.empty(), "Sphere vertices not empty");
        RENDER_CHECK(!indices.empty(), "Sphere indices not empty");
        RENDER_CHECK((indices.size() % 3) == 0, "Sphere index count is a multiple of 3 (triangles)");

        for (uint32_t idx : indices) {
            RENDER_CHECK(idx < vertices.size(), "Sphere index references valid vertex");
        }

        for (const auto& v : vertices) {
            float dist = glm::length(v.Position);
            RENDER_CHECK(Near(dist, radius, 1e-3f), "Sphere vertex sits on radius surface");

            float normLen = glm::length(v.Normal);
            RENDER_CHECK(Near(normLen, 1.0f, 1e-3f), "Sphere vertex normal has unit length");

            // For a sphere centered at origin, normal must equal normalized position
            glm::vec3 expectedNormal = glm::normalize(v.Position);
            RENDER_CHECK(Near(v.Normal, expectedNormal, 1e-2f), "Sphere normal matches normalized position");
        }

        return true;
    }

    static bool Test_ProceduralPlaneGeometryContracts() {
        std::vector<RHI::RHIVertex3D> vertices;
        std::vector<uint32_t> indices;

        const float sizeX = 4.0f;
        const float sizeZ = 4.0f;
        const uint32_t resX = 2;
        const uint32_t resZ = 2;
        RHI::RHIMesh::GeneratePlane(sizeX, sizeZ, resX, resZ, vertices, indices);

        // Plane resolution grid: (resX + 1) * (resZ + 1)
        uint32_t expectedVerts = (resX + 1) * (resZ + 1);
        uint32_t expectedIndices = resX * resZ * 6;

        RENDER_CHECK(vertices.size() == expectedVerts, "Plane vertex count matches grid formula");
        RENDER_CHECK(indices.size() == expectedIndices, "Plane index count matches quad count x 6");

        for (const auto& v : vertices) {
            RENDER_CHECK(Near(v.Position.y, 0.0f, 1e-4f), "Plane Y is coplanar at 0");
            RENDER_CHECK(Near(v.Normal, glm::vec3(0.0f, 1.0f, 0.0f), 1e-4f), "Plane normal points up (+Y)");
        }

        return true;
    }

    static bool Test_ProceduralCylinderAndConeContracts() {
        std::vector<RHI::RHIVertex3D> cylVerts, coneVerts;
        std::vector<uint32_t> cylIndices, coneIndices;

        RHI::RHIMesh::GenerateCylinder(1.0f, 2.0f, 16, cylVerts, cylIndices);
        RENDER_CHECK(!cylVerts.empty(), "Cylinder vertices non-empty");
        RENDER_CHECK(!cylIndices.empty(), "Cylinder indices non-empty");
        RENDER_CHECK((cylIndices.size() % 3) == 0, "Cylinder triangles well-formed");

        RHI::RHIMesh::GenerateCone(1.0f, 2.0f, 16, coneVerts, coneIndices);
        RENDER_CHECK(!coneVerts.empty(), "Cone vertices non-empty");
        RENDER_CHECK(!coneIndices.empty(), "Cone indices non-empty");
        RENDER_CHECK((coneIndices.size() % 3) == 0, "Cone triangles well-formed");

        return true;
    }

} // namespace Axle::RenderTests

bool RunRenderPrimitiveIdentityUnitTests() {
    std::cout << "  [RenderPrimitives] Running primitive identity & geometry contracts...\n";
    if (!Axle::RenderTests::Test_BuiltinPrimitiveURIsAndUUIDs()) return false;
    if (!Axle::RenderTests::Test_ProceduralCubeGeometryContracts()) return false;
    if (!Axle::RenderTests::Test_ProceduralSphereGeometryContracts()) return false;
    if (!Axle::RenderTests::Test_ProceduralPlaneGeometryContracts()) return false;
    if (!Axle::RenderTests::Test_ProceduralCylinderAndConeContracts()) return false;
    std::cout << "  [RenderPrimitives] All primitive identity tests passed.\n";
    return true;
}

