#pragma once

#include <Axle/Types.hpp>
#include <string>
#include <string_view>
#include <filesystem>
#include <Axle/AssetDSL.hpp>

namespace Axle {

    enum class TextureColorSpace : uint8_t {
        sRGB,
        Linear
    };

    struct TextureDescriptor {
        std::string Name;
        std::string Source; // e.g. "textures/car_body_diffuse.png"
        std::string Type = "Color"; // "Color" (sRGB), "NormalMap" (Linear), "Data" (ORM / Roughness), "CubemapHDR"
        std::string Filter = "Anisotropic16x"; // "Point", "Linear", "Anisotropic16x"
        std::string WrapU = "Repeat"; // "Repeat", "Clamp", "Mirror"
        std::string WrapV = "Repeat";
        bool GenerateMips = true;
        uint32_t MaxResolution = 2048;
        std::string Compression = "BC7";
        float Exposure = 1.0f;
        bool GeneratePrefilter = false;

        TextureColorSpace GetColorSpace() const {
            if (Type == "NormalMap" || Type == "Data") return TextureColorSpace::Linear;
            return TextureColorSpace::sRGB;
        }

        static bool Load(const std::string& filepath, TextureDescriptor& outDesc);
        static bool Save(const std::string& filepath, const TextureDescriptor& desc);
        static std::string Serialize(const TextureDescriptor& desc);
        static bool Deserialize(std::string_view source, TextureDescriptor& outDesc);
    };

} // namespace Axle
