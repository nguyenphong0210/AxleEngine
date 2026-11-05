#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/AssetMetadata.hpp>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <string>

namespace Axle {

    class AssetRegistrySerializer {
    public:
        static inline constexpr uint32_t CurrentVersion = 1;
        static inline constexpr const char* RegistryFileName = "AssetRegistry.axleregistry";

        // Deterministically serializes entries (sorted by generic relative path) atomically via FileSystem::WriteTextFileAtomically
        static bool Serialize(const std::filesystem::path& registryFilePath,
                              const std::unordered_map<UUID, AssetMetadata>& entries,
                              std::string* outError = nullptr);

        // Deserializes and validates registry; reports duplicate/corrupt/unsupported records
        static bool Deserialize(const std::filesystem::path& registryFilePath,
                                std::unordered_map<UUID, AssetMetadata>& outEntries,
                                std::unordered_map<std::string, UUID>& outPathToUUID,
                                std::vector<AssetDiagnostic>* outDiagnostics = nullptr,
                                std::string* outError = nullptr);
    };

}
