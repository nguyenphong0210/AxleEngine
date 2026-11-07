#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <filesystem>

namespace Axle {
    struct ImageDecodeOptions {
        bool FlipVertically = false, SRGB = false, GenerateMips = false, AlphaWeighted = false;
        bool ForceFloat = false;
        uint32_t MaxResolution = 0;
        bool operator==(const ImageDecodeOptions&) const = default;
    };
    struct ImageMipData {
        uint32_t Width = 0, Height = 0;
        std::vector<uint8_t> Pixels; // RGBA8 or four native floats; no GPU/context state.
    };
    struct DecodedImage {
        bool HDR = false;
        uint32_t SourceChannels = 0;
        ImageDecodeOptions Options;
        std::vector<ImageMipData> Mips;
        uint32_t BytesPerPixel() const { return HDR ? 16 : 4; }
    };
    uint64_t ImageContentRevision(std::span<const uint8_t> bytes);
    std::string ImageInterpretationKey(const ImageDecodeOptions& options);
    struct ImageSourceData {
        std::string SourceKey, ImportKey, Error;
        uint64_t Revision = 0;
        std::shared_ptr<const DecodedImage> Image;
        bool UsedLastGood = false;
        std::string RequestKey;
        std::filesystem::path SourcePath;
    };
    ImageSourceData LoadRawImageSource(const std::filesystem::path& path, const ImageDecodeOptions& options = {});
    std::shared_ptr<const DecodedImage> DecodeImage(std::span<const uint8_t> bytes,
        const ImageDecodeOptions& options = {}, std::string* error = nullptr);
}
