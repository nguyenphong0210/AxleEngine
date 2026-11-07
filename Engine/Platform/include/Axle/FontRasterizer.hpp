#pragma once
#include <Axle/DecodedImage.hpp>
#include <array>
namespace Axle {
    struct RasterizedFontAtlas {
        ImageSourceData Source;
        std::array<std::array<float,4>,96> GlyphUV{};
    };
    std::shared_ptr<const RasterizedFontAtlas> GetDefaultFontAtlas();
}
