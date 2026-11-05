#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/AssetTypes.hpp>
#include <filesystem>
#include <cstdint>
#include <string>

namespace Axle {

    struct AssetDiagnostic {
        enum class Level {
            Info,
            Warning,
            Error
        };

        Level Severity = Level::Error;
        std::string Message;
        std::filesystem::path AssetPath;
        UUID AssetUUID{ 0 };
    };

    struct AssetMetadata {
        UUID Handle{ 0 };
        AssetType Type = AssetType::None;
        std::filesystem::path FilePath;
        uint32_t Version = 1;

        bool IsValid() const {
            return Type != AssetType::None && !FilePath.empty(); // Raw resources are identified by path, not UUID.
        }

        operator bool() const {
            return IsValid();
        }
    };

}
