#include <Axle/RHIMeshRegistry.hpp>
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
        if (m_Initialized) return true;
        auto& device = RHIDevice::Get();
        if (!device.IsInitialized()) return false;

        // Generate built-in primitive meshes
        m_Cube = RHIMesh::CreateCube(1.0f);
        m_Sphere = RHIMesh::CreateSphere(0.5f, 24, 24);
        m_Cylinder = RHIMesh::CreateCylinder(0.5f, 1.0f, 24);
        m_Cone = RHIMesh::CreateCone(0.5f, 1.0f, 24);
        m_Plane = RHIMesh::CreatePlane(10.0f, 10.0f, 10, 10);

        if (m_Cube) m_MeshCache["Cube"] = m_Cube;
        if (m_Sphere) m_MeshCache["Sphere"] = m_Sphere;
        if (m_Cylinder) m_MeshCache["Cylinder"] = m_Cylinder;
        if (m_Cone) m_MeshCache["Cone"] = m_Cone;
        if (m_Plane) m_MeshCache["Plane"] = m_Plane;

        // Pre-cache chassis and wheel if assets exist
        auto chassis = GetOrCreateMesh("SportsCar_Chassis.axlemesh");
        if (chassis) m_MeshCache["SportsCar_Chassis"] = chassis;

        auto wheel = GetOrCreateMesh("SportsCar_Wheel.axlemesh");
        if (wheel) m_MeshCache["SportsCar_Wheel"] = wheel;

        GetProceduralTerrain();

        m_Initialized = true;
        return true;
    }

    void RHIMeshRegistry::Shutdown() {
        Clear();
        if (m_Cube) { m_Cube->Shutdown(); m_Cube.reset(); }
        if (m_Sphere) { m_Sphere->Shutdown(); m_Sphere.reset(); }
        if (m_Cylinder) { m_Cylinder->Shutdown(); m_Cylinder.reset(); }
        if (m_Cone) { m_Cone->Shutdown(); m_Cone.reset(); }
        if (m_Plane) { m_Plane->Shutdown(); m_Plane.reset(); }
        m_Initialized = false;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetCube() {
        if (!m_Cube && !m_Initialized) Init();
        return m_Cube;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetSphere() {
        if (!m_Sphere && !m_Initialized) Init();
        return m_Sphere;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetCylinder() {
        if (!m_Cylinder && !m_Initialized) Init();
        return m_Cylinder;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetCone() {
        if (!m_Cone && !m_Initialized) Init();
        return m_Cone;
    }

    std::shared_ptr<RHIMesh> RHIMeshRegistry::GetPlane() {
        if (!m_Plane && !m_Initialized) Init();
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

        auto it = m_MeshCache.find(assetPath);
        if (it != m_MeshCache.end() && it->second && it->second->IsValid()) {
            return it->second;
        }

        // Built-in standard primitive aliases
        if (assetPath == "builtin://plane" || assetPath == "Plane") {
            auto plane = GetPlane();
            if (plane) m_MeshCache[assetPath] = plane;
            return plane;
        }
        if (assetPath == "builtin://cube" || assetPath == "Cube") {
            auto cube = GetCube();
            if (cube) m_MeshCache[assetPath] = cube;
            return cube;
        }
        if (assetPath == "builtin://sphere" || assetPath == "Sphere") {
            auto sphere = GetSphere();
            if (sphere) m_MeshCache[assetPath] = sphere;
            return sphere;
        }
        if (assetPath == "builtin://cylinder" || assetPath == "Cylinder") {
            auto cyl = GetCylinder();
            if (cyl) m_MeshCache[assetPath] = cyl;
            return cyl;
        }
        if (assetPath == "builtin://cone" || assetPath == "Cone") {
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
                auto mesh = RHIMesh::CreateFromAxleMesh(loaded.SourceData.Vertices, loaded.SourceData.Indices,
                                                        loaded.SourceData.ColorPrimary,
                                                        targetPath.filename().string());
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
        for (auto& [key, mesh] : m_MeshCache) {
            if (mesh) {
                mesh->Shutdown();
            }
        }
        m_MeshCache.clear();
    }

}
