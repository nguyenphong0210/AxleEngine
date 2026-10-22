#pragma once
#include <Axle/Types.hpp>
#include <Axle/RHI/RHIMesh.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Axle {

    class RHIMeshRegistry {
    public:
        static RHIMeshRegistry& Get();

        RHIMeshRegistry() = default;
        ~RHIMeshRegistry();

        RHIMeshRegistry(const RHIMeshRegistry&) = delete;
        RHIMeshRegistry& operator=(const RHIMeshRegistry&) = delete;

        // Initialize built-in primitive shapes
        bool Init();

        // Release all registered meshes and free GPU VRAM
        void Shutdown();

        // Built-in Primitive Geometry
        std::shared_ptr<RHI::RHIMesh> GetCube();
        std::shared_ptr<RHI::RHIMesh> GetSphere();
        std::shared_ptr<RHI::RHIMesh> GetCylinder();
        std::shared_ptr<RHI::RHIMesh> GetCone();
        std::shared_ptr<RHI::RHIMesh> GetPlane();

        // Retrieve cached mesh by key
        std::shared_ptr<RHI::RHIMesh> GetMesh(const std::string& key) const;

        // Retrieve or generate procedural terrain
        std::shared_ptr<RHI::RHIMesh> GetProceduralTerrain();

        // Retrieve or load mesh from file path (.axlemesh)
        std::shared_ptr<RHI::RHIMesh> GetOrCreateMesh(const std::string& assetPath);

        // Retrieve or generate procedural terrain heightfield mesh
        std::shared_ptr<RHI::RHIMesh> GetOrCreateTerrainMesh(const std::string& terrainKey,
                                                             const std::vector<float>& heightmap,
                                                             uint32_t width, uint32_t height,
                                                             float gridSpacing = 1.0f,
                                                             float heightScale = 1.0f);

        // Manually register a custom mesh
        void RegisterMesh(const std::string& key, const std::shared_ptr<RHI::RHIMesh>& mesh);

        // Clear all cached meshes
        void Clear();

        size_t GetMeshCount() const { return m_MeshCache.size(); }

    private:
        std::shared_ptr<RHI::RHIMesh> m_Cube;
        std::shared_ptr<RHI::RHIMesh> m_Sphere;
        std::shared_ptr<RHI::RHIMesh> m_Cylinder;
        std::shared_ptr<RHI::RHIMesh> m_Cone;
        std::shared_ptr<RHI::RHIMesh> m_Plane;

        std::unordered_map<std::string, std::shared_ptr<RHI::RHIMesh>> m_MeshCache;
        bool m_Initialized = false;
    };

}
