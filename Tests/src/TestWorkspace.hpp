#pragma once
#include <Axle/FileSystem.hpp>
#include <filesystem>

namespace Axle::Tests {
    // Fixtures are staged by the explicit test build into a writable build-tree root.
    // Tests never discover Editor/user projects or create source-tree asset directories.
    inline void InitializeWorkspace() {
        const std::filesystem::path workspace = AXLE_TEST_WORKSPACE_DIR;
        std::filesystem::create_directories(workspace);
        std::filesystem::current_path(workspace);
        FileSystem::SetAssetDirectory(workspace / "assets");
    }
}
