#pragma once
#include <Axle/RHI/RHITexture.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace Axle {
    // Owned by the renderer, so textures are released before the RHI device.
    // Color maps use sRGB views; normal/ORM/roughness/metallic/AO maps stay linear.
    class MaterialTextureCache {
    public:
        std::shared_ptr<RHI::RHITexture> Get(const std::string& path, bool color);
        void Clear() { m_Entries.clear(); }
    private:
        struct Entry {
            std::filesystem::file_time_type Modified{};
            std::shared_ptr<RHI::RHITexture> Texture;
        };
        std::unordered_map<std::string, Entry> m_Entries;
    };
}
