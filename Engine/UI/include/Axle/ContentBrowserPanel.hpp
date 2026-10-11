#pragma once
#include <Axle/Types.hpp>
#include <filesystem>
#include <string>

namespace Axle {

    class ContentBrowserPanel {
    public:
        ContentBrowserPanel();
        ContentBrowserPanel(const std::filesystem::path& rootPath);
        ~ContentBrowserPanel() = default;

        void SetRootPath(const std::filesystem::path& rootPath);
        const std::filesystem::path& GetCurrentPath() const { return m_CurrentDirectory; }
        bool IsWindowHovered() const { return m_IsHovered; }

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
    };

}
