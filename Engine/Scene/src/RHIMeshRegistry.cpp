#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/BuiltinMesh.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>

#include <filesystem>

namespace Axle {

    using namespace Axle::RHI;

    RHIMeshRegistry& RHIMeshRegistry::Get() {
        static RHIMeshRegistry s_Instance;
        return s_Instance;
    }

    RHIMeshRegistry::~RHIMeshRegistry() {
        Shutdown();
    }

    bool RHIMeshRegistry::Init() {
        if (m_Initialized && m_Cube && m_Cube->IsValid() && m_Sphere && m_Sphere->IsValid() &&
            m_Cylinder && m_Cylinder->IsValid() && m_Cone && m_Cone->IsValid() && m_Plane && m_Plane->IsValid()) return true;
        auto& device = RHIDevice::Get();
        if (!device.IsNativeReady()) return false;
        Shutdown();

        // Generate built-in primitive meshes with high-fidelity curvature (Godot & Unity parity)
        m_Cube = RHIMesh::CreateCube(1.0f);
        m_Sphere = RHIMesh::CreateSphere(BuiltinMeshes[1].Radius, BuiltinMeshes[1].Stacks, BuiltinMeshes[1].Segments);
        m_Cylinder = RHIMesh::CreateCylinder(BuiltinMeshes[2].Radius, BuiltinMeshes[2].Height, BuiltinMeshes[2].Segments);
        m_Cone = RHIMesh::CreateCone(BuiltinMeshes[3].Radius, BuiltinMeshes[3].Height, BuiltinMeshes[3].Segments);
        m_Plane = RHIMesh::CreatePlane(BuiltinMeshes[4].Size, BuiltinMeshes[4].Size, 1, 1);

        if (m_Cube) m_MeshCache["builtin://cube"] = m_Cube;
        if (m_Sphere) m_MeshCache["builtin://sphere"] = m_Sphere;
        if (m_Cylinder) m_MeshCache["builtin://cylinder"] = m_Cylinder;
        if (m_Cone) m_MeshCache["builtin://cone"] = m_Cone;
        if (m_Plane) m_MeshCache["builtin://plane"] = m_Plane;

        if (!m_Cube || !m_Sphere || !m_Cylinder || !m_Cone || !m_Plane) {
            Shutdown();
            return false;
        }
        m_Initialized = true;
        return true;
    }

    void RHIMeshRegistry::Shutdown() {
        Clear();
        m_Cube.reset();
        m_Sphere.reset();
        m_Cylinder.reset();
        m_Cone.reset();
        m_Plane.reset();
        m_Initialized = false;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetCube() {
        if ((!m_Cube || !m_Cube->IsValid()) && !Init()) return nullptr;
        return m_Cube;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetSphere() {
        if ((!m_Sphere || !m_Sphere->IsValid()) && !Init()) return nullptr;
        return m_Sphere;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetCylinder() {
        if ((!m_Cylinder || !m_Cylinder->IsValid()) && !Init()) return nullptr;
        return m_Cylinder;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetCone() {
        if ((!m_Cone || !m_Cone->IsValid()) && !Init()) return nullptr;
        return m_Cone;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetPlane() {
        if ((!m_Plane || !m_Plane->IsValid()) && !Init()) return nullptr;
        return m_Plane;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetMesh(const std::string& key) const {
        auto it = m_MeshCache.find(key);
        if (it != m_MeshCache.end()) return it->second;
        return nullptr;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetProceduralTerrain() {
        auto it = m_MeshCache.find("ProceduralTerrain");
        if (it != m_MeshCache.end() && it->second && it->second->IsValid()) {
            return it->second;
        }

        const uint32_t resX = 16;
        const uint32_t resZ = 16;
        std::vector<float> heights(resX * resZ);
        for (uint32_t z = 0; z < resZ; ++z) {
            for (uint32_t x = 0; x < resX; ++x) {
                heights[z * resX + x] = std::sin(static_cast<float>(x) * 0.4f) * std::cos(static_cast<float>(z) * 0.4f) * 2.0f;
            }
        }
        return GetOrCreateTerrainMesh("ProceduralTerrain", heights, resX, resZ, 2.0f, 1.0f);
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetOrCreateMesh(const std::string& assetPath) {
        if (assetPath.empty()) return nullptr;
        if (!RHIDevice::Get().IsNativeReady()) return nullptr;

        auto it = m_MeshCache.find(assetPath);
        if (it != m_MeshCache.end() && it->second && it->second->IsValid()) {
            return it->second;
        }

        // Built-in standard primitive aliases
        if (ParseBuiltinMesh(assetPath) == BuiltinMeshId::Plane) {
            auto plane = GetPlane();
            if (plane) m_MeshCache[assetPath] = plane;
            return plane;
        }
        if (ParseBuiltinMesh(assetPath) == BuiltinMeshId::Cube) {
            auto cube = GetCube();
            if (cube) m_MeshCache[assetPath] = cube;
            return cube;
        }
        if (ParseBuiltinMesh(assetPath) == BuiltinMeshId::Sphere) {
            auto sphere = GetSphere();
            if (sphere) m_MeshCache[assetPath] = sphere;
            return sphere;
        }
        if (ParseBuiltinMesh(assetPath) == BuiltinMeshId::Cylinder) {
            auto cyl = GetCylinder();
            if (cyl) m_MeshCache[assetPath] = cyl;
            return cyl;
        }
        if (ParseBuiltinMesh(assetPath) == BuiltinMeshId::Cone) {
            auto cone = GetCone();
            if (cone) m_MeshCache[assetPath] = cone;
            return cone;
        }

        // Try direct file path
        std::filesystem::path targetPath(assetPath);
        if (!std::filesystem::exists(targetPath)) {
            // Try resolving via FileSystem
            targetPath = Axle::FileSystem::ResolveAssetPath(assetPath);
        }
        if (!std::filesystem::exists(targetPath)) {
            // Try prefixing models/
            targetPath = Axle::FileSystem::ResolveAssetPath("models/" + assetPath);
        }

        if (std::filesystem::exists(targetPath)) {
            auto loaded = Axle::AssetSerializer::LoadMesh(targetPath);
            if (loaded.Success && !loaded.SourceData.Vertices.empty()) {
                const auto& source=loaded.SourceData;
                auto mesh=source.UVs.size()==source.Vertices.size()
                    ? RHIMesh::CreateFromRaw(source.Vertices,source.Indices,source.Normals,source.UVs,source.ColorPrimary,targetPath.filename().string())
                    : RHIMesh::CreateFromAxleMesh(source.Vertices,source.Indices,source.ColorPrimary,targetPath.filename().string());
                if (mesh && mesh->IsValid()) {
                    m_MeshCache[assetPath] = mesh;
                    AXLE_CORE_INFO("RHIMeshRegistry: Cached GPU mesh '{0}' ({1} verts, {2} idx)",
                        assetPath, mesh->GetVertexCount(), mesh->GetIndexCount());
                    return mesh;
                }
            }
        }

        return nullptr;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetOrCreateTerrainMesh(const std::string& terrainKey,
                                                                     const std::vector<float>& heightmap,
                                                                     uint32_t width, uint32_t height,
                                                                     float gridSpacing, float heightScale)
    {
        std::string key = terrainKey.empty() ? "Terrain_Default" : terrainKey;
        auto it = m_MeshCache.find(key);
        if (it != m_MeshCache.end() && it->second && it->second->IsValid()) {
            return it->second;
        }

        auto mesh = RHIMesh::CreateTerrainHeightfield(width, height, heightmap, glm::vec3(gridSpacing, heightScale, gridSpacing));
        if (mesh && mesh->IsValid()) {
            m_MeshCache[key] = mesh;
            AXLE_CORE_INFO("RHIMeshRegistry: Generated terrain heightfield '{0}' ({1} verts, {2} idx)",
                key, mesh->GetVertexCount(), mesh->GetIndexCount());
            return mesh;
        }

        return nullptr;
    }

    void RHIMeshRegistry::RegisterMesh(const std::string& key, const std::shared_ptr<RHIMesh>& mesh) {
        if (!key.empty() && mesh) {
            m_MeshCache[key] = mesh;
        }
    }

    void RHIMeshRegistry::Clear() {
        m_MeshCache.clear();
    }

}
