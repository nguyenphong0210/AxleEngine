#include <Axle/ImageSource.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <Axle/FileSystem.hpp>
#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace Axle {
    ImageSourceData LoadImageSource(const std::filesystem::path& path, const ImageDecodeOptions& options) {
        auto source = FileSystem::ResolveAssetPath(path);
        auto extension = source.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".axleasset") return LoadRawImageSource(source, options);
        static std::mutex mutex;
        static std::unordered_map<std::string, ImageSourceData> lastGood;
        const auto authoredKey = std::to_string(FileSystem::GetSourceGeneration()) + ":" + source.lexically_normal().generic_string() + "|" + ImageInterpretationKey(options);
        TextureDescriptor descriptor;
        auto effective = options;
        ImageSourceData result;
        if (!TextureDescriptor::Load(source.string(), descriptor) || descriptor.Source.empty()) result.Error = "Invalid texture descriptor";
        else {
            effective.SRGB = options.SRGB && descriptor.GetColorSpace() == TextureColorSpace::sRGB;
            effective.GenerateMips = options.GenerateMips && descriptor.GenerateMips;
            effective.MaxResolution = options.MaxResolution ? (std::min)(options.MaxResolution, descriptor.MaxResolution ? descriptor.MaxResolution : options.MaxResolution) : descriptor.MaxResolution;
            result = LoadRawImageSource(FileSystem::ResolveAssetPath(descriptor.Source), effective);
            result.ImportKey = descriptor.Type + "|" + descriptor.Filter + "|" + descriptor.WrapU + "|" + descriptor.WrapV +
                "|" + descriptor.Compression + "|" + std::to_string(descriptor.Exposure) + "|" + std::to_string(descriptor.GeneratePrefilter);
        }
        std::lock_guard lock(mutex);
        result.RequestKey = authoredKey;
        if (result.Image) {
            if (!result.UsedLastGood) lastGood[authoredKey] = result;
        } else if (auto found = lastGood.find(authoredKey); found != lastGood.end()) {
            auto error = result.Error; result = found->second; result.UsedLastGood = true; result.Error = std::move(error);
        }
        if (lastGood.size() > 256) for (auto it = lastGood.begin(); it != lastGood.end(); ++it) if (it->first != authoredKey) { lastGood.erase(it); break; }
        return result;
    }
}
