#pragma once
#include <Axle/Types.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Scene.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/Components.hpp>

#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace Axle {

    struct AssemblyNode {
        std::string Tag = "Entity";
        TransformComponent Transform;
        bool HasMesh = false;
        MeshInstance3D Mesh;
        bool HasCollisionShape = false;
        CollisionShape3D CollisionShape;
        bool HasRigidBody = false;
        RigidBody3D RigidBody;
        bool HasMaterial = false;
        MaterialComponent Material;
        std::vector<AssemblyNode> Children;
    };

    struct AssemblyData {
        std::string Name = "New_Assembly";
        std::filesystem::path SourcePath;
        AssemblyNode RootNode;
        bool IsValid = false;
    };

    class AssemblySerializer {
    public:
        // Check if an .axle file contains AssetType: Assembly
        static bool IsAssemblyFile(const std::filesystem::path& filepath);

        // Serialize an Entity hierarchy from a Scene to a unified .axle file (AssetType: Assembly)
        static bool Serialize(const std::filesystem::path& filepath,
                              Entity rootEntity,
                              PhysicsScene* physicsScene = nullptr,
                              const SceneSerializerContext* context = nullptr);

        // Deserialize an .axle Assembly file and spawn it as an entity hierarchy in targetScene with UUID remapping
        static Entity Deserialize(const std::filesystem::path& filepath,
                                  Scene* targetScene,
                                  PhysicsScene* physicsScene = nullptr,
                                  const glm::vec3& spawnPosition = glm::vec3(0.0f),
                                  const SceneSerializerContext* context = nullptr);

        // Load Assembly data structure in memory without spawning into active Scene (for Assembly Editor)
        static AssemblyData LoadAssemblyData(const std::filesystem::path& filepath,
                                             const SceneSerializerContext* context = nullptr);

        // Save Assembly data structure from memory back to .axle file (from Assembly Editor)
        static bool SaveAssemblyData(const std::filesystem::path& filepath,
                                     const AssemblyData& data);

        // Create an empty starter assembly template (.axle)
        static bool CreateEmptyAssembly(const std::filesystem::path& filepath,
                                        const std::string& name = "New_Assembly");
    };

}
