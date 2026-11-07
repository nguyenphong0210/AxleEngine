#include <Axle/DecodedImage.hpp>
#include <Axle/FileSystem.hpp>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace Axle {
    uint64_t ImageContentRevision(std::span<const uint8_t> bytes) {
        uint64_t value = 14695981039346656037ull;
        for (auto byte : bytes) { value ^= byte; value *= 1099511628211ull; }
        return value;
    }
    std::shared_ptr<const DecodedImage> DecodeImage(std::span<const uint8_t> bytes,
        const ImageDecodeOptions& options, std::string* error) {
        auto fail = [&](const char* reason) -> std::shared_ptr<const DecodedImage> { if (error) *error = reason; return {}; };
        if (error) error->clear();
        if (bytes.empty() || bytes.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return fail("Empty or oversized image source");
        stbi_set_flip_vertically_on_load_thread(options.FlipVertically);
        int width = 0, height = 0, channels = 0;
        const auto size = static_cast<int>(bytes.size());
        const bool hdr = options.ForceFloat || stbi_is_hdr_from_memory(bytes.data(), size) != 0;
        void* pixels = hdr ? static_cast<void*>(stbi_loadf_from_memory(bytes.data(), size, &width, &height, &channels, 4)) :
            static_cast<void*>(stbi_load_from_memory(bytes.data(), size, &width, &height, &channels, 4));
        if (!pixels) return fail(stbi_failure_reason() ? stbi_failure_reason() : "Image decoding failed");
        const auto release = std::unique_ptr<void, decltype(&stbi_image_free)>(pixels, stbi_image_free);
        if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) * height > SIZE_MAX / (hdr ? 16u : 4u)) return fail("Invalid image dimensions");
        auto image = std::make_shared<DecodedImage>(); image->HDR = hdr; image->SourceChannels = channels; image->Options = options;
        ImageMipData base{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        base.Pixels.resize(static_cast<size_t>(width) * height * image->BytesPerPixel());
        std::memcpy(base.Pixels.data(), pixels, base.Pixels.size());
        image->Mips.push_back(std::move(base));
        const bool resize = options.MaxResolution && (static_cast<uint32_t>(width) > options.MaxResolution || static_cast<uint32_t>(height) > options.MaxResolution);
        if (!options.GenerateMips && !options.AlphaWeighted && !resize) return image;
        const auto& source = image->Mips.front().Pixels;
        auto sample = [&](size_t index, unsigned channel) {
            if (hdr) { float value; std::memcpy(&value, source.data() + (index * 4 + channel) * sizeof(float), sizeof(value)); return static_cast<double>(value); }
            double value = source[index * 4 + channel] / 255.0;
            if (options.SRGB && channel < 3) value = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
            return value;
        };
        double transparent[3]{1, 1, 1}, totalAlpha = 0, sums[3]{};
        if (options.AlphaWeighted) {
            for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                const auto alpha = sample(i, 3); totalAlpha += alpha;
                for (unsigned c = 0; c < 3; ++c) sums[c] += sample(i, c) * alpha;
            }
            if (totalAlpha > 0) for (unsigned c = 0; c < 3; ++c) transparent[c] = sums[c] / totalAlpha;
        }
        // Build from the original image to keep odd dimensions and transparent
        // glyph edges consistent across GL and native uploads.
        std::vector<ImageMipData> levels;
        for (uint32_t w = width, h = height;; w = (std::max)(1u, w / 2), h = (std::max)(1u, h / 2)) {
            if (options.MaxResolution && (w > options.MaxResolution || h > options.MaxResolution)) continue;
            ImageMipData mip{w, h}; mip.Pixels.resize(static_cast<size_t>(w) * h * image->BytesPerPixel());
            for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
                const uint32_t x0 = static_cast<uint64_t>(x) * width / w, x1 = static_cast<uint64_t>(x + 1) * width / w;
                const uint32_t y0 = static_cast<uint64_t>(y) * height / h, y1 = static_cast<uint64_t>(y + 1) * height / h;
                double rgba[4]{};
                for (uint32_t sy = y0; sy < y1; ++sy) for (uint32_t sx = x0; sx < x1; ++sx) {
                    const auto index = static_cast<size_t>(sy) * width + sx;
                    const auto alpha = sample(index, 3); rgba[3] += alpha;
                    for (unsigned c = 0; c < 3; ++c) rgba[c] += sample(index, c) * (options.AlphaWeighted ? alpha : 1.0);
                }
                const auto count = static_cast<double>(x1 - x0) * (y1 - y0), alpha = rgba[3];
                rgba[3] /= count;
                for (unsigned c = 0; c < 3; ++c) rgba[c] = options.AlphaWeighted ? (alpha > 0 ? rgba[c] / alpha : transparent[c]) : rgba[c] / count;
                for (unsigned c = 0; c < 4; ++c) {
                    const auto index = (static_cast<size_t>(y) * w + x) * 4 + c;
                    if (hdr) { const auto value = static_cast<float>(rgba[c]); std::memcpy(mip.Pixels.data() + index * sizeof(value), &value, sizeof(value)); }
                    else {
                        auto value = rgba[c];
                        if (options.SRGB && c < 3) value = value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - 0.055;
                        mip.Pixels[index] = static_cast<uint8_t>((std::clamp)(std::lround(value * 255), 0l, 255l));
                    }
                }
            }
            levels.push_back(std::move(mip));
            if (!options.GenerateMips || (w == 1 && h == 1)) break;
        }
        image->Mips = std::move(levels); return image;
    }
    std::string ImageInterpretationKey(const ImageDecodeOptions& options) {
        return std::to_string(options.FlipVertically) + ":" + std::to_string(options.SRGB) + ":" +
            std::to_string(options.GenerateMips) + ":" + std::to_string(options.AlphaWeighted) + ":" + std::to_string(options.ForceFloat) + ":" + std::to_string(options.MaxResolution);
    }
    ImageSourceData LoadRawImageSource(const std::filesystem::path& path, const ImageDecodeOptions& options) {
        static std::mutex mutex;
        struct Entry { ImageSourceData Data; uint64_t Used = 0; };
        static std::unordered_map<std::string, Entry> entries;
        static uint64_t clock = 0;
        const auto source = FileSystem::ResolveAssetPath(path);
        std::error_code error;
        auto canonical = std::filesystem::absolute(source, error).lexically_normal();
        if (error) canonical = source.lexically_normal();
        const auto sourceKey = std::to_string(FileSystem::GetSourceGeneration()) + ":" + canonical.generic_string();
        const auto key = sourceKey + "|" + ImageInterpretationKey(options);
        auto bytes = FileSystem::ReadBinaryFile(source);
        std::lock_guard lock(mutex);
        auto& entry = entries[key]; entry.Used = ++clock;
        const auto revision = ImageContentRevision(bytes);
        if (!bytes.empty() && entry.Data.Image && entry.Data.Revision == revision) return entry.Data;
        std::string failure;
        auto image = DecodeImage(bytes, options, &failure);
        if (image) {
            entry.Data = {sourceKey, {}, {}, revision, std::move(image), false};
            entry.Data.RequestKey = key;
            entry.Data.SourcePath = canonical;
        }
        auto result = entry.Data;
        if (result.SourceKey.empty()) result.SourceKey = sourceKey;
        result.RequestKey = key;
        result.SourcePath = canonical;
        if (!image && !failure.empty()) { result.Error = std::move(failure); result.UsedLastGood = result.Image != nullptr; }
        if (entries.size() > 256) {
            auto oldest = entries.end();
            for (auto it = entries.begin(); it != entries.end(); ++it) if (it->first != key && (oldest == entries.end() || it->second.Used < oldest->second.Used)) oldest = it;
            if (oldest != entries.end()) entries.erase(oldest);
        }
        return result;
    }
}
