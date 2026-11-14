#include <Axle/MaterialTextureCache.hpp>
#include <Axle/ImageSource.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/FileSystem.hpp>

namespace Axle {
    std::shared_ptr<RHI::RHITexture> MaterialTextureCache::Get(const std::string& path, bool color, std::string* diagnostic) {
        if (diagnostic) diagnostic->clear();
        if (path.empty()) return {};
        ImageDecodeOptions options; options.FlipVertically = true; options.SRGB = color; options.GenerateMips = true;
        const auto source = LoadImageSource(path, options);
        auto texture = ImageTextureCache::GetNative(source);
        if (diagnostic && (source.UsedLastGood || !source.Error.empty() || !texture))
            *diagnostic = "SURFACE_TEXTURE_FALLBACK: " + path + ": " +
                (source.Error.empty() ? "native image publication unavailable; retaining last good or neutral texture" : source.Error);
        const auto key = std::to_string(FileSystem::GetSourceGeneration()) + ":" + path + (color ? "|sRGB" : "|linear");
        if (texture) m_Entries[key].Texture = texture;
        auto found = m_Entries.find(key);
        return texture ? texture : found != m_Entries.end() && found->second.Texture && found->second.Texture->IsValid() ? found->second.Texture : nullptr;
    }
}
