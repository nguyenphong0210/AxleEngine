#pragma once
#include <Axle/DecodedImage.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <functional>
namespace Axle {
    // Render-owner publication only. Asset decoding supplies CPU data; every GPU
    // replacement is a complete candidate and recoverable failure keeps last good.
    class ImageTextureCache {
    public:
        using SourceResolver = std::function<ImageSourceData(const std::filesystem::path&, const ImageDecodeOptions&)>;
        static void SetSourceResolver(SourceResolver resolver);
        static ImageSourceData Resolve(const std::filesystem::path& path, const ImageDecodeOptions& options);
        static std::shared_ptr<RHI::RHITexture> GetNative(const ImageSourceData& source);
        static std::shared_ptr<Texture2D> GetGL(const ImageSourceData& source, const TextureSpecification& specification = {});
        static void Clear();
    };
}
