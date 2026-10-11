#pragma once
#include <Axle/Types.hpp>
#include <Axle/Scene.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/VertexArray.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <functional>

namespace Axle {

    struct SceneSerializerContext {
        std::shared_ptr<VertexArray> CubeMesh;
        std::shared_ptr<VertexArray> SphereMesh;
        std::shared_ptr<VertexArray> CylinderMesh;
        std::shared_ptr<VertexArray> ConeMesh;
        std::shared_ptr<VertexArray> PlaneMesh;
        std::function<void(Entity, const std::string&)> ScriptBinder;
    };

    class SceneSerializer {
    public:
        SceneSerializer(Scene* scene, PhysicsScene* physicsScene = nullptr);
        ~SceneSerializer() = default;

        void SetContext(const SceneSerializerContext& context) { m_Context = context; }

        bool Serialize(const std::filesystem::path& filepath);
        bool Deserialize(const std::filesystem::path& filepath);

        std::string SerializeToString();
        bool DeserializeFromString(const std::string& data);

    private:
        Scene* m_Scene = nullptr;
        PhysicsScene* m_PhysicsScene = nullptr;
        SceneSerializerContext m_Context;
    };

}
