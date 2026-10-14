#pragma once
#include <Axle/Types.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/PhysicsScene.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace Axle {

    class VertexArray;
    class AssetPreviewPanel;

    class SceneHierarchyPanel {
    public:
        SceneHierarchyPanel() = default;
        SceneHierarchyPanel(Scene* scene, PhysicsScene* physicsScene = nullptr);
        ~SceneHierarchyPanel() = default;

        void SetContext(Scene* scene, PhysicsScene* physicsScene = nullptr);
        void SetMeshes(
            std::shared_ptr<VertexArray> cube,
            std::shared_ptr<VertexArray> sphere,
            std::shared_ptr<VertexArray> cylinder,
            std::shared_ptr<VertexArray> cone,
            std::shared_ptr<VertexArray> plane
        );

        void SetAssetPreviewPanel(AssetPreviewPanel* panel) { m_AssetPreviewPanel = panel; }

        void OnImGuiRender(bool* p_hierarchyOpen = nullptr, bool* p_propertiesOpen = nullptr);

        Entity GetSelectedEntity() const { return m_SelectedEntity; }
        void SetSelectedEntity(Entity entity);

        void SetSelectedAsset(const std::filesystem::path& assetPath);
        const std::filesystem::path& GetSelectedAsset() const { return m_SelectedAsset; }
        void ClearSelectedAsset();

        void SetSpawnAssetCallback(std::function<void(const std::filesystem::path&)> callback) { m_OnSpawnAssetCallback = callback; }
        void SetOpenAssemblyEditorCallback(std::function<void(const std::filesystem::path&)> callback) { m_OnOpenAssemblyEditorCallback = callback; }

        Entity CreatePrimitive(int primitiveType, const glm::vec3& position = glm::vec3(0.0f, 2.0f, 0.0f));

    private:
        void DrawEntityNode(Entity entity);
        void DrawComponents(Entity entity);
        void DrawAssetProperties(const std::filesystem::path& path);
        void DrawCreateMenu();

    private:
        Scene* m_Scene = nullptr;
        PhysicsScene* m_PhysicsScene = nullptr;
        Entity m_SelectedEntity{};
        std::filesystem::path m_SelectedAsset;
        AssetPreviewPanel* m_AssetPreviewPanel = nullptr;
        std::vector<Entity> m_EntitiesToDestroy;

        char m_SearchFilter[64] = "";

        std::function<void(const std::filesystem::path&)> m_OnSpawnAssetCallback;
        std::function<void(const std::filesystem::path&)> m_OnOpenAssemblyEditorCallback;

        std::shared_ptr<VertexArray> m_CubeMesh;
        std::shared_ptr<VertexArray> m_SphereMesh;
        std::shared_ptr<VertexArray> m_CylinderMesh;
        std::shared_ptr<VertexArray> m_ConeMesh;
        std::shared_ptr<VertexArray> m_PlaneMesh;
    };

}
