#pragma once
#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/Shader.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/PhysicsScene.hpp>
#include <entt/entt.hpp>

#include <memory>
#include <string>

namespace Axle {

    class Entity;

    class Scene {
    public:
        Scene();
        ~Scene() = default;

        Entity CreateEntity(const std::string& name = "Entity");
        void DestroyEntity(Entity entity);
        Entity FindEntityByBodyID(BodyID bodyID);

        // Update physics synchronization and render all mesh components
        void OnUpdate(Timestep ts, PhysicsScene* physicsScene, const PerspectiveCamera& camera, Shader& shader);

        // Scripting runtime lifecycle
        void OnRuntimeStart();
        void OnUpdateRuntime(Timestep ts);
        void OnRuntimeStop();

        entt::registry& GetRegistry() { return m_Registry; }
        const entt::registry& GetRegistry() const { return m_Registry; }

    private:
        entt::registry m_Registry;

        friend class Entity;
    };

}
