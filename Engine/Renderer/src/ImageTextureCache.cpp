#include <Axle/ImageTextureCache.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/Window.hpp>
#include <Axle/Log.hpp>
#include <glad/glad.h>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

namespace Axle {
    namespace {
        struct Entry {
            uint64_t Epoch = 0, Revision = 0, Used = 0;
            std::shared_ptr<RHI::RHITexture> Native;
            std::shared_ptr<Texture2D> GL;
        };
        struct Cache {
            uint64_t Clock = 0;
            std::unordered_map<std::string, Entry> Entries;
            std::unordered_set<uint64_t> NativeOwners, GLOwners;
            std::unordered_map<std::string, std::weak_ptr<RHI::RHITexture>> NativeLastGood;
            std::unordered_map<std::string, std::weak_ptr<Texture2D>> GLLastGood;
            ImageTextureCache::SourceResolver Resolver;
        };
        Cache& State() { static Cache cache; return cache; }
        template<class T> std::shared_ptr<T> Last(const std::unordered_map<std::string, std::weak_ptr<T>>& aliases, const std::string& key) {
            auto found = aliases.find(key); return found == aliases.end() ? nullptr : found->second.lock();
        }
        template<class T> void Remember(std::unordered_map<std::string, std::weak_ptr<T>>& aliases, const std::string& key, const std::shared_ptr<T>& resource) {
            aliases[key] = resource;
            if (aliases.size() > 512) for (auto it = aliases.begin(); it != aliases.end(); ++it) if (it->first != key) { aliases.erase(it); break; }
        }
        std::string Key(const ImageSourceData& source) {
            return source.SourceKey + "|" + source.ImportKey + "|" + ImageInterpretationKey(source.Image->Options) +
                "|" + std::to_string(source.Image->HDR);
        }
        bool Valid(const ImageSourceData& source) {
            if (!source.Image || source.SourceKey.empty() || source.Image->Mips.empty()) return false;
            uint32_t width = source.Image->Mips.front().Width, height = source.Image->Mips.front().Height;
            if (!width || !height) return false;
            for (const auto& mip : source.Image->Mips) {
                if (mip.Width != width || mip.Height != height || static_cast<uint64_t>(width) * height * source.Image->BytesPerPixel() != mip.Pixels.size() ||
                    mip.Pixels.size() > UINT32_MAX) return false;
                width = (std::max)(1u, width / 2); height = (std::max)(1u, height / 2);
            }
            return true;
        }
        void RetireOwner(uint64_t epoch, bool native) {
            auto& cache = State();
            for (auto it = cache.Entries.begin(); it != cache.Entries.end();)
                if (it->second.Epoch == epoch && (native ? bool(it->second.Native) : bool(it->second.GL))) it = cache.Entries.erase(it); else ++it;
            (native ? cache.NativeOwners : cache.GLOwners).erase(epoch);
            const auto prefix = std::to_string(epoch) + ":";
            if (native) {
                for (auto it = cache.NativeLastGood.begin(); it != cache.NativeLastGood.end();)
                    if (it->first.starts_with(prefix)) it = cache.NativeLastGood.erase(it); else ++it;
            } else for (auto it = cache.GLLastGood.begin(); it != cache.GLLastGood.end();)
                if (it->first.starts_with(prefix)) it = cache.GLLastGood.erase(it); else ++it;
        }
        void Bound(const std::string& current) {
            auto& entries = State().Entries;
            if (entries.size() <= 256) return;
            auto oldest = entries.end();
            for (auto it = entries.begin(); it != entries.end(); ++it)
                if (it->first != current && (oldest == entries.end() || it->second.Used < oldest->second.Used)) oldest = it;
            if (oldest != entries.end()) entries.erase(oldest);
        }
    }
    void ImageTextureCache::SetSourceResolver(SourceResolver resolver) { State().Resolver = std::move(resolver); }
    ImageSourceData ImageTextureCache::Resolve(const std::filesystem::path& path, const ImageDecodeOptions& options) {
        return State().Resolver ? State().Resolver(path, options) : LoadRawImageSource(path, options);
    }
    std::shared_ptr<RHI::RHITexture> ImageTextureCache::GetNative(const ImageSourceData& source) {
        auto& device = RHI::RHIDevice::Get();
        if (!device.CanCreateResources()) return {};
        const auto epoch = device.GetEpoch(); auto& cache = State();
        const auto request = std::to_string(epoch) + ":" + (source.RequestKey.empty() ? source.SourceKey : source.RequestKey);
        auto previous = Last(cache.NativeLastGood, request);
        if (!Valid(source)) return previous && previous->IsValid() ? previous : nullptr;
        if (cache.NativeOwners.insert(epoch).second)
            device.GetResources().BeforeDeviceClose([epoch] { RetireOwner(epoch, true); });
        const auto key = "N:" + std::to_string(epoch) + ":" + Key(source);
        auto& entry = cache.Entries[key]; entry.Used = ++cache.Clock;
        if (entry.Native && entry.Native->IsValid() && entry.Revision == source.Revision) {
            Remember(cache.NativeLastGood, request, entry.Native); return entry.Native;
        }
        const auto& image = *source.Image;
        auto texture = std::make_shared<RHI::RHITexture>();
        RHI::RHITextureDesc desc; desc.Width = image.Mips.front().Width; desc.Height = image.Mips.front().Height;
        desc.MipLevels = static_cast<uint32_t>(image.Mips.size()); desc.Usage = RHI::RHITextureUsage::ShaderResource;
        desc.Format = image.HDR ? RHI::RHITextureFormat::RGBA32_FLOAT : image.Options.SRGB ? RHI::RHITextureFormat::RGBA8_SRGB : RHI::RHITextureFormat::RGBA8_UNORM;
        desc.DebugName = source.SourceKey;
        bool success = texture->Init(desc);
        if (success) {
            std::vector<nri::TextureSubresourceUploadDesc> levels;
            for (const auto& mip : image.Mips) levels.push_back({mip.Pixels.data(), 1, mip.Width * image.BytesPerPixel(), static_cast<uint32_t>(mip.Pixels.size())});
            nri::TextureUploadDesc upload{}; upload.texture = texture->GetNative(); upload.subresources = levels.data(); upload.planes = nri::PlaneBits::COLOR;
            upload.after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::ALL};
            success = device.UploadData(&upload, 1, nullptr, 0) == nri::Result::SUCCESS;
        }
        if (success) { entry.Native = std::move(texture); entry.Revision = source.Revision; entry.Epoch = epoch; }
        auto result = entry.Native && entry.Native->IsValid() ? entry.Native : previous && previous->IsValid() ? previous : nullptr;
        if (result) Remember(cache.NativeLastGood, request, result);
        Bound(key); return result;
    }
    std::shared_ptr<Texture2D> ImageTextureCache::GetGL(const ImageSourceData& source, const TextureSpecification& requested) {
        const auto epoch = Window::GetContextGeneration();
        if (!epoch || !glCreateTextures || !glTexImage2D) return {};
        auto& cache = State();
        const auto request = std::to_string(epoch) + ":" + (source.RequestKey.empty() ? source.SourceKey : source.RequestKey);
        auto previousGood = Last(cache.GLLastGood, request);
        if (!Valid(source)) return previousGood && previousGood->IsLoaded() ? previousGood : nullptr;
        if (cache.GLOwners.insert(epoch).second) Window::BeforeContextClose(epoch, [epoch] { RetireOwner(epoch, false); });
        auto spec = requested; const auto& image = *source.Image;
        spec.Width = image.Mips.front().Width; spec.Height = image.Mips.front().Height;
        spec.Format = image.HDR ? ImageFormat::RGBA32F : ImageFormat::RGBA8; spec.sRGB = image.Options.SRGB;
        const auto key = "G:" + std::to_string(epoch) + ":" + Key(source) + ":" + std::to_string(static_cast<int>(spec.MinFilter)) + ":" +
            std::to_string(static_cast<int>(spec.MagFilter)) + ":" + std::to_string(static_cast<int>(spec.WrapS)) + ":" + std::to_string(static_cast<int>(spec.WrapT));
        auto& entry = cache.Entries[key]; entry.Used = ++cache.Clock;
        if (entry.GL && entry.GL->IsLoaded() && entry.Revision == source.Revision) {
            Remember(cache.GLLastGood, request, entry.GL); return entry.GL;
        }
        auto fallback = [&] { auto result = entry.GL && entry.GL->IsLoaded() ? entry.GL : previousGood && previousGood->IsLoaded() ? previousGood : nullptr; Bound(key); return result; };
        GLint maximum = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
        if (spec.Width > static_cast<uint32_t>(maximum) || spec.Height > static_cast<uint32_t>(maximum)) return fallback();
        auto texture = std::make_shared<Texture2D>(spec, image.Mips.front().Pixels.data());
        if (!texture->IsLoaded()) return fallback();
        texture->m_FilePath = source.SourcePath.generic_string();
        const auto internal = image.HDR ? GL_RGBA32F : image.Options.SRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        GLint previous = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous); glBindTexture(GL_TEXTURE_2D, texture->GetRendererID());
        for (size_t i = 1; i < image.Mips.size(); ++i) {
            const auto& mip = image.Mips[i];
            glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), internal, mip.Width, mip.Height, 0, GL_RGBA, image.HDR ? GL_FLOAT : GL_UNSIGNED_BYTE, mip.Pixels.data());
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(image.Mips.size() - 1));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, image.Mips.size() > 1 ? (spec.MinFilter == TextureFilter::Linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : (spec.MinFilter == TextureFilter::Linear ? GL_LINEAR : GL_NEAREST));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
        const auto error = glGetError();
        if (error != GL_NO_ERROR) { AXLE_CORE_ERROR("Image GPU upload encountered GL error {0}", error); return fallback(); }
        entry.GL = std::move(texture); entry.Revision = source.Revision; entry.Epoch = epoch;
        auto result = entry.GL; Remember(cache.GLLastGood, request, result);
        Bound(key); return result;
    }
    void ImageTextureCache::Clear() { State().Entries.clear(); State().NativeLastGood.clear(); State().GLLastGood.clear(); }
}
