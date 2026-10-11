#pragma once
#include <Axle/Types.hpp>
#include <filesystem>
#include <string>
#include <functional>

namespace Axle {

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
        void RefreshThumbnails();
        void SetStatusNotification(const std::string& message, float duration = 4.0f);

        void OnImGuiRender(bool* p_open = nullptr);

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

        std::unique_ptr<AssetThumbnailManager> m_ThumbnailManager;
        AssetPreviewPanel* m_AssetPreviewPanel = nullptr;
        std::function<void()> m_OnCompileAssetsCallback;
        std::function<void(const std::filesystem::path&)> m_OnOpenSceneCallback;
        std::function<void(const std::filesystem::path&)> m_OnOpenProjectCallback;
    };

}
