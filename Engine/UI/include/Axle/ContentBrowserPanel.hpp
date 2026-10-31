#pragma once
#include <Axle/Types.hpp>
#include <filesystem>
#include <string>
#include <functional>

namespace Axle {

    class ImGuiLayer;
    class AssetThumbnailManager;
    class AssetPreviewPanel;

    class ContentBrowserPanel {
    public:
        ContentBrowserPanel();
        ContentBrowserPanel(const std::filesystem::path& rootPath);
        ~ContentBrowserPanel();

        void SetRootPath(const std::filesystem::path& rootPath);
        const std::filesystem::path& GetCurrentPath() const { return m_CurrentDirectory; }
        bool IsWindowHovered() const { return m_IsHovered; }

        void SetAssetPreviewPanel(AssetPreviewPanel* previewPanel) { m_AssetPreviewPanel = previewPanel; }
        void SetCompileAssetsCallback(std::function<void()> callback) { m_OnCompileAssetsCallback = callback; }
        void SetOpenSceneCallback(std::function<void(const std::filesystem::path&)> callback) { m_OnOpenSceneCallback = callback; }
        void SetOpenProjectCallback(std::function<void(const std::filesystem::path&)> callback) { m_OnOpenProjectCallback = callback; }
        void SetOpenAssemblyCallback(std::function<void(const std::filesystem::path&)> callback) { m_OnOpenAssemblyCallback = callback; }
        void SetInstantiateAssemblyCallback(std::function<void(const std::filesystem::path&)> callback) { m_OnInstantiateAssemblyCallback = callback; }
        void SetSelectAssetCallback(std::function<void(const std::filesystem::path&)> callback) { m_OnSelectAssetCallback = callback; }
        void SetPackageEntityCallback(std::function<void(uint32_t, const std::filesystem::path&)> callback) { m_OnPackageEntityCallback = callback; }
        void RefreshThumbnails();
        void SetStatusNotification(const std::string& message, float duration = 4.0f);

        // Asset Creation API (Right-Click Menu & Programmatic Creation)
        static std::filesystem::path CreateNewFolder(const std::filesystem::path& parentDir, const std::string& baseName = "New_Folder");
        static std::filesystem::path CreateNewScene(const std::filesystem::path& parentDir, const std::string& baseName = "New_Scene");
        static std::filesystem::path CreateNewMaterial(const std::filesystem::path& parentDir, const std::string& baseName = "New_Material");
        static std::filesystem::path CreateNewTexture(const std::filesystem::path& parentDir, const std::string& baseName = "New_Texture");
        static std::filesystem::path CreateNewVFXAsset(const std::filesystem::path& parentDir, const std::string& baseName = "New_ParticleEmitter");
        static std::filesystem::path CreateNewCanvas(const std::filesystem::path& parentDir, const std::string& baseName = "New_Canvas");
        static std::filesystem::path CreateNewMesh(const std::filesystem::path& parentDir, const std::string& baseName = "New_Mesh");
        static std::filesystem::path CreateNewScript(const std::filesystem::path& parentDir, const std::string& baseName = "NewScript");
        static std::pair<std::filesystem::path, std::filesystem::path> CreateNewClass(const std::filesystem::path& parentDir, const std::string& baseName = "NewClass");
        static std::filesystem::path CreateNewPhysicsMaterial(const std::filesystem::path& parentDir, const std::string& baseName = "New_PhysicsMaterial");
        static std::filesystem::path CreateNewTerrain(const std::filesystem::path& parentDir, const std::string& baseName = "New_Terrain");

        void OnImGuiRender(bool* p_open = nullptr);
        void SetImGuiLayer(ImGuiLayer* layer);

    private:
        void RenderTopBar();
        void RenderBreadcrumb();
        void RenderDirectoryContents();
        void RenderStatusBar(size_t folderCount, size_t fileCount);

    private:
        std::filesystem::path m_RootDirectory;
        std::filesystem::path m_CurrentDirectory;
        std::filesystem::path m_SelectedEntry;

        char m_SearchFilter[64] = "";
        float m_Padding = 14.0f;
        float m_ThumbnailSize = 84.0f;
        bool m_ShowHiddenFiles = false;
        bool m_IsHovered = false;

        std::string m_StatusNotificationMessage;
        float m_StatusNotificationTimer = 0.0f;

        ImGuiLayer* m_ImGuiLayer = nullptr;
        std::unique_ptr<AssetThumbnailManager> m_ThumbnailManager;
        AssetPreviewPanel* m_AssetPreviewPanel = nullptr;
        std::function<void()> m_OnCompileAssetsCallback;
        std::function<void(const std::filesystem::path&)> m_OnOpenSceneCallback;
        std::function<void(const std::filesystem::path&)> m_OnOpenProjectCallback;
        std::function<void(const std::filesystem::path&)> m_OnOpenAssemblyCallback;
        std::function<void(const std::filesystem::path&)> m_OnInstantiateAssemblyCallback;
        std::function<void(const std::filesystem::path&)> m_OnSelectAssetCallback;
        std::function<void(uint32_t, const std::filesystem::path&)> m_OnPackageEntityCallback;
        std::function<void(const std::filesystem::path&)> m_OnOpenUICanvasCallback;
    public:
        void SetOpenUICanvasCallback(std::function<void(const std::filesystem::path&)> callback) { m_OnOpenUICanvasCallback = callback; }
    };

}
