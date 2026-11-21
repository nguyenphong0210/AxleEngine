#pragma once
#include <filesystem>
#include <memory>

class EditorLayer;

namespace Axle {
    class Layer;
    std::shared_ptr<Layer> CreateEditorVisualCaptureLayer(
        std::shared_ptr<EditorLayer> editor,
        const std::filesystem::path& planPath);
}
