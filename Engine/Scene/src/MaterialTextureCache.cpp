#include <Axle/MaterialTextureCache.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <Axle/Log.hpp>
#include <stb_image.h>
#include <limits>
#include <algorithm>
#include <cctype>

namespace Axle {
    std::shared_ptr<RHI::RHITexture> MaterialTextureCache::Get(const std::string& path, bool color) {
        if (path.empty() || !RHI::RHIDevice::Get().IsNativeReady()) return nullptr;
        auto source = FileSystem::ResolveAssetPath(path);
        auto extension = source.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (extension == ".axleasset") {
            TextureDescriptor descriptor;
            if (!TextureDescriptor::Load(source.string(), descriptor)) return nullptr;
            source = FileSystem::ResolveAssetPath(descriptor.Source);
            color = color && descriptor.GetColorSpace() == TextureColorSpace::sRGB;
        }
        std::error_code ec;
        auto modified = std::filesystem::last_write_time(source, ec);
        std::string key = source.lexically_normal().generic_string() + (color ? "|sRGB" : "|linear");
        auto cached = m_Entries.find(key);
        if (cached != m_Entries.end() && cached->second.Modified == modified) return cached->second.Texture;
        auto& entry = m_Entries[key];
        entry.Modified = modified;
        entry.Texture.reset();
        auto bytes = FileSystem::ReadBinaryFile(source);
        if (bytes.empty() || bytes.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return nullptr;

        int width = 0, height = 0, components = 0;
        const int size = static_cast<int>(bytes.size());
        bool hdr = stbi_is_hdr_from_memory(bytes.data(), size) != 0;
        stbi_set_flip_vertically_on_load_thread(1);
        void* pixels = hdr
            ? static_cast<void*>(stbi_loadf_from_memory(bytes.data(), size, &width, &height, &components, 4))
            : static_cast<void*>(stbi_load_from_memory(bytes.data(), size, &width, &height, &components, 4));
        if (!pixels) {
            AXLE_CORE_WARN("Material texture could not be decoded: {0}", source.string());
            return nullptr;
        }
        auto texture = std::make_shared<RHI::RHITexture>();
        RHI::RHITextureDesc desc;
        desc.Width = static_cast<uint32_t>(width);
        desc.Height = static_cast<uint32_t>(height);
        desc.Usage = RHI::RHITextureUsage::ShaderResource;
        desc.Format = hdr ? RHI::RHITextureFormat::RGBA32_FLOAT
            : (color ? RHI::RHITextureFormat::RGBA8_SRGB : RHI::RHITextureFormat::RGBA8_UNORM);
        desc.DebugName = "Material_" + source.filename().string();
        bool success = texture->Init(desc);
        if (success) {
            auto& device = RHI::RHIDevice::Get();
            auto* queue = device.GetGraphicsQueue();
            const auto& helper = device.GetNRIHelper();
            nri::TextureSubresourceUploadDesc subresource{};
            subresource.slices = pixels;
            subresource.sliceNum = 1;
            subresource.rowPitch = desc.Width * (hdr ? 16u : 4u);
            subresource.slicePitch = subresource.rowPitch * desc.Height;
            nri::TextureUploadDesc upload{};
            upload.subresources = &subresource;
            upload.texture = texture->GetNative();
            upload.planes = nri::PlaneBits::COLOR;
            upload.after.access = nri::AccessBits::SHADER_RESOURCE;
            upload.after.layout = nri::Layout::SHADER_RESOURCE;
            upload.after.stages = nri::StageBits::ALL;
            success = queue && helper.UploadData && device.UploadData(&upload, 1, nullptr, 0) == nri::Result::SUCCESS;
        }
        stbi_image_free(pixels);
        if (!success) {
            AXLE_CORE_WARN("Material texture GPU upload failed: {0}", source.string());
            return nullptr;
        }
        entry.Texture = texture;
        return texture;
    }
}
