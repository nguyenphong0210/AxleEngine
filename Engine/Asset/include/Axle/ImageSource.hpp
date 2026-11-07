#pragma once
#include <Axle/DecodedImage.hpp>
namespace Axle {
    // CPU-only authored-reference resolution. The Renderer consumes ImageSourceData
    // without acquiring a dependency on Asset, a window or a GPU device.
    ImageSourceData LoadImageSource(const std::filesystem::path& path, const ImageDecodeOptions& options = {});
}
