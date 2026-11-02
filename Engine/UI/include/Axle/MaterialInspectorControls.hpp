#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Axle {
    struct MaterialComponent;
    class MaterialInspectorControls {
    public:
        bool Draw(MaterialComponent& material);
        static std::vector<std::filesystem::path> FindTextures(const std::filesystem::path& root);
    private:
        bool TextureSlot(const char* id, std::string& path, bool& enabled,
                         const std::function<void(const std::string&)>& assign, bool showEnabled = true);
        char m_Search[128]{};
        std::vector<std::filesystem::path> m_Textures;
        std::string m_Error;
    };
}
