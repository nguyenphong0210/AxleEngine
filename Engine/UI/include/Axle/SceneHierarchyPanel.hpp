#pragma once
#include <Axle/Types.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/PhysicsScene.hpp>

namespace Axle {

    class SceneHierarchyPanel {
    public:
        SceneHierarchyPanel() = default;
        SceneHierarchyPanel(Scene* scene, PhysicsScene* physicsScene = nullptr);
        ~SceneHierarchyPanel() = default;

        void SetContext(Scene* scene, PhysicsScene* physicsScene = nullptr);

        void OnImGuiRender(bool* p_hierarchyOpen = nullptr, bool* p_propertiesOpen = nullptr);

        Entity GetSelectedEntity() const { return m_SelectedEntity; }
        void SetSelectedEntity(Entity entity) { m_SelectedEntity = entity; }

    private:
        void DrawEntityNode(Entity entity);
        void DrawComponents(Entity entity);

    private:
        Scene* m_Scene = nullptr;
        PhysicsScene* m_PhysicsScene = nullptr;
        Entity m_SelectedEntity{};
    };

}
