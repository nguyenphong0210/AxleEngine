#pragma once
#include <Axle/AssetManager.hpp>
#include <Axle/FileSystem.hpp>

namespace Axle::Tests {
    // GPU fixtures own their authored/capture root; they must not use Editor assets.
    class ScopedAssetTestRoot {
    public:
        explicit ScopedAssetTestRoot(const std::filesystem::path& root)
            : m_PreviousAssets(AssetManager::GetAssetDirectory()), m_PreviousFiles(FileSystem::GetAssetDirectory()) {
            AssetManager::Init(root);
        }
        ~ScopedAssetTestRoot() {
            AssetManager::Shutdown();
            if (!m_PreviousAssets.empty()) AssetManager::Init(m_PreviousAssets);
            FileSystem::SetAssetDirectory(m_PreviousFiles);
        }
        ScopedAssetTestRoot(const ScopedAssetTestRoot&) = delete;
        ScopedAssetTestRoot& operator=(const ScopedAssetTestRoot&) = delete;
    private:
        std::filesystem::path m_PreviousAssets, m_PreviousFiles;
    };
}
