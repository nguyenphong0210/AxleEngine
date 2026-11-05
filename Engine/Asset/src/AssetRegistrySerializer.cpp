#include <Axle/AssetRegistrySerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace Axle {

    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    bool AssetRegistrySerializer::Serialize(const std::filesystem::path& registryFilePath,
                                            const std::unordered_map<UUID, AssetMetadata>& entries,
                                            std::string* outError) {
        std::stringstream out;
        out << "AssetRegistry:\n";
        out << "  Version: " << CurrentVersion << "\n";
        out << "  Entries:\n";

        // Collect and sort entries deterministically by generic relative path
        std::vector<const AssetMetadata*> sortedEntries;
        sortedEntries.reserve(entries.size());
        for (const auto& [uuid, meta] : entries) {
            if (meta.IsValid()) {
                sortedEntries.push_back(&meta);
            }
        }

        std::sort(sortedEntries.begin(), sortedEntries.end(), [](const AssetMetadata* a, const AssetMetadata* b) {
            return a->FilePath.generic_string() < b->FilePath.generic_string();
        });

        for (const auto* meta : sortedEntries) {
            out << "    - UUID: " << static_cast<uint64_t>(meta->Handle) << "\n";
            out << "      Path: " << meta->FilePath.generic_string() << "\n";
            out << "      Type: " << AssetTypeToString(meta->Type) << "\n";
        }

        std::string content = out.str();
        if (!FileSystem::WriteTextFileAtomically(registryFilePath, content, outError)) {
            AXLE_ERROR("AssetRegistrySerializer: Failed to atomically write registry file '{0}': {1}",
                       registryFilePath.generic_string(), outError ? *outError : "unknown error");
            return false;
        }

        return true;
    }

    bool AssetRegistrySerializer::Deserialize(const std::filesystem::path& registryFilePath,
                                              std::unordered_map<UUID, AssetMetadata>& outEntries,
                                              std::unordered_map<std::string, UUID>& outPathToUUID,
                                              std::vector<AssetDiagnostic>* outDiagnostics,
                                              std::string* outError) {
        if (!std::filesystem::exists(registryFilePath)) {
            std::string err = "Asset registry file does not exist: " + registryFilePath.generic_string();
            if (outError) *outError = err;
            return false;
        }

        std::string content = FileSystem::ReadTextFile(registryFilePath);
        if (content.empty()) {
            std::string err = "Asset registry file is empty or unreadable: " + registryFilePath.generic_string();
            if (outError) *outError = err;
            if (outDiagnostics) {
                outDiagnostics->push_back({ AssetDiagnostic::Level::Error, err, registryFilePath, 0 });
            }
            return false;
        }

        std::istringstream stream(content);
        std::string line;

        bool foundRegistryHeader = false;
        uint32_t version = 0;
        bool hasVersion = false;
        bool inEntries = false;

        struct ParsedEntry {
            uint64_t RawUUID = 0;
            bool HasUUID = false;
            bool UUIDCorrupt = false;
            std::string PathStr;
            std::string TypeStr;
        };

        std::vector<ParsedEntry> parsedEntries;
        ParsedEntry currentEntry;
        bool buildingEntry = false;

        auto finalizeEntry = [&]() {
            if (buildingEntry) {
                parsedEntries.push_back(currentEntry);
                currentEntry = ParsedEntry{};
                buildingEntry = false;
            }
        };

        while (std::getline(stream, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.rfind("AssetRegistry:", 0) == 0) {
                foundRegistryHeader = true;
                continue;
            }

            if (!foundRegistryHeader) {
                continue;
            }

            if (trimmed.rfind("Version:", 0) == 0) {
                std::string val = Trim(trimmed.substr(8));
                try {
                    version = static_cast<uint32_t>(std::stoul(val));
                    hasVersion = true;
                } catch (...) {
                    std::string err = "Corrupt Asset registry Version field: " + val;
                    if (outError) *outError = err;
                    if (outDiagnostics) outDiagnostics->push_back({ AssetDiagnostic::Level::Error, err, registryFilePath, 0 });
                    return false;
                }

                if (version > CurrentVersion) {
                    std::string err = "Unsupported asset registry version: " + std::to_string(version) +
                                      " (highest supported: " + std::to_string(CurrentVersion) + ")";
                    if (outError) *outError = err;
                    if (outDiagnostics) outDiagnostics->push_back({ AssetDiagnostic::Level::Error, err, registryFilePath, 0 });
                    return false;
                }
                continue;
            }

            if (trimmed.rfind("Entries:", 0) == 0) {
                inEntries = true;
                continue;
            }

            if (inEntries) {
                if (trimmed.rfind("- UUID:", 0) == 0) {
                    finalizeEntry();
                    buildingEntry = true;
                    std::string val = Trim(trimmed.substr(7));
                    try {
                        currentEntry.RawUUID = std::stoull(val);
                        currentEntry.HasUUID = (currentEntry.RawUUID != 0);
                    } catch (...) {
                        currentEntry.UUIDCorrupt = true;
                    }
                } else if (trimmed.rfind("-", 0) == 0) {
                    finalizeEntry();
                    buildingEntry = true;
                    std::string rest = Trim(trimmed.substr(1));
                    if (rest.rfind("UUID:", 0) == 0) {
                        std::string val = Trim(rest.substr(5));
                        try {
                            currentEntry.RawUUID = std::stoull(val);
                            currentEntry.HasUUID = (currentEntry.RawUUID != 0);
                        } catch (...) {
                            currentEntry.UUIDCorrupt = true;
                        }
                    }
                } else if (trimmed.rfind("UUID:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(5));
                    try {
                        currentEntry.RawUUID = std::stoull(val);
                        currentEntry.HasUUID = (currentEntry.RawUUID != 0);
                    } catch (...) {
                        currentEntry.UUIDCorrupt = true;
                    }
                } else if (trimmed.rfind("Path:", 0) == 0) {
                    currentEntry.PathStr = Trim(trimmed.substr(5));
                } else if (trimmed.rfind("Type:", 0) == 0) {
                    currentEntry.TypeStr = Trim(trimmed.substr(5));
                }
            }
        }
        finalizeEntry();

        if (!foundRegistryHeader || !hasVersion) {
            std::string err = "Asset registry file missing 'AssetRegistry' header or 'Version' field";
            if (outError) *outError = err;
            if (outDiagnostics) outDiagnostics->push_back({ AssetDiagnostic::Level::Error, err, registryFilePath, 0 });
            return false;
        }

        bool hasErrors = false;
        for (const auto& pe : parsedEntries) {
            if (pe.UUIDCorrupt || !pe.HasUUID) {
                std::string err = "Corrupt or invalid zero UUID in registry entry for path: '" + pe.PathStr + "'";
                if (outDiagnostics) outDiagnostics->push_back({ AssetDiagnostic::Level::Error, err, pe.PathStr, 0 });
                hasErrors = true;
                continue;
            }

            if (pe.PathStr.empty()) {
                std::string err = "Registry entry missing Path field for UUID: " + std::to_string(pe.RawUUID);
                if (outDiagnostics) outDiagnostics->push_back({ AssetDiagnostic::Level::Error, err, registryFilePath, UUID{ pe.RawUUID } });
                hasErrors = true;
                continue;
            }

            std::filesystem::path relPath(pe.PathStr);
            std::string normPathKey = relPath.lexically_normal().generic_string();
            std::transform(normPathKey.begin(), normPathKey.end(), normPathKey.begin(), [](unsigned char c) { return std::tolower(c); });

            AssetType type = AssetTypeFromString(pe.TypeStr);
            if (type == AssetType::None) {
                std::string err = "Invalid or unknown AssetType '" + pe.TypeStr + "' in registry entry: " + pe.PathStr;
                if (outDiagnostics) outDiagnostics->push_back({ AssetDiagnostic::Level::Error, err, relPath, UUID{ pe.RawUUID } });
                hasErrors = true;
                continue;
            }

            UUID uuid{ pe.RawUUID };

            // Check for duplicate UUID
            if (outEntries.find(uuid) != outEntries.end()) {
                std::string err = "Duplicate UUID detected in registry: " + std::to_string(pe.RawUUID) +
                                  " for path '" + pe.PathStr + "' (already assigned to '" +
                                  outEntries[uuid].FilePath.generic_string() + "')";
                if (outDiagnostics) outDiagnostics->push_back({ AssetDiagnostic::Level::Error, err, relPath, uuid });
                hasErrors = true;
                continue;
            }

            // Check for duplicate Path
            if (outPathToUUID.find(normPathKey) != outPathToUUID.end()) {
                std::string err = "Duplicate path detected in registry: '" + normPathKey + "'";
                if (outDiagnostics) outDiagnostics->push_back({ AssetDiagnostic::Level::Error, err, relPath, uuid });
                hasErrors = true;
                continue;
            }

            AssetMetadata meta;
            meta.Handle = uuid;
            meta.FilePath = relPath;
            meta.Type = type;
            meta.Version = version;

            outEntries[uuid] = meta;
            outPathToUUID[normPathKey] = uuid;
        }

        if (hasErrors) {
            if (outError) *outError = "Asset registry contained corrupt or duplicate records";
            return false;
        }

        return true;
    }

}
