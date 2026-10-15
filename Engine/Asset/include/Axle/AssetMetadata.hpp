#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/AssetTypes.hpp>
#include <filesystem>

namespace Axle {

    struct AssetMetadata {
        UUID Handle{ 0 };
        AssetType Type = AssetType::None;
        std::filesystem::path FilePath;

        bool IsValid() const {
            return Handle != 0 && Type != AssetType::None;
        }

        operator bool() const {
            return IsValid();
        }
    };

}
