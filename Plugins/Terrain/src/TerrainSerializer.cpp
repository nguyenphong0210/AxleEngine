#include <AxleTerrain/TerrainSerializer.hpp>
#include <AxleTerrain/TerrainComponent.hpp>
#include <AxleTerrain/TerrainMesh.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <algorithm>
#include <filesystem>
#include <string>

namespace Axle::Terrain {

    namespace {
        static std::string Trim(const std::string& str) {
            size_t first = str.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return "";
            size_t last = str.find_last_not_of(" \t\r\n");
            return str.substr(first, (last - first + 1));
        }
    }

    void TerrainSerializer::Serialize(Entity entity, std::ostream& out) {
        if (!entity.HasComponent<TerrainComponent>()) return;

        auto& tc = entity.GetComponent<TerrainComponent>();
        out << "    TerrainComponent:\n";
        out << "      ResolutionX: " << tc.Data.GetResolutionX() << "\n";
        out << "      ResolutionZ: " << tc.Data.GetResolutionZ() << "\n";
        out << "      SpacingX: " << tc.Data.GetSpacingX() << "\n";
        out << "      SpacingZ: " << tc.Data.GetSpacingZ() << "\n";
        if (!tc.DataFilePath.empty()) {
            out << "      DataFilePath: " << tc.DataFilePath << "\n";
        }
        out << "      UseChunkedLOD: " << (tc.UseChunkedLOD ? "true" : "false") << "\n";
        out << "      ChunkSize: " << tc.ChunkSize << "\n";
        out << "      GeneratePhysics: " << (tc.GeneratePhysics ? "true" : "false") << "\n";
        out << "      Friction: " << tc.Friction << "\n";
        out << "      Restitution: " << tc.Restitution << "\n";
    }

    bool TerrainSerializer::Deserialize(Entity entity, const std::string& sectionName, const std::vector<std::string>& lines) {
        if (sectionName != "TerrainComponent") return false;

        u32 resX = 128;
        u32 resZ = 128;
        f32 spacingX = 1.0f;
        f32 spacingZ = 1.0f;
        std::string dataFilePath;
        bool useChunkedLOD = false;
        u32 chunkSize = 32;
        bool generatePhysics = true;
        f32 friction = 0.85f;
        f32 restitution = 0.05f;

        for (const auto& rawLine : lines) {
            std::string trimmed = Trim(rawLine);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.rfind("ResolutionX:", 0) == 0) {
                try { resX = std::stoul(Trim(trimmed.substr(12))); } catch (...) {}
            } else if (trimmed.rfind("ResolutionZ:", 0) == 0) {
                try { resZ = std::stoul(Trim(trimmed.substr(12))); } catch (...) {}
            } else if (trimmed.rfind("SpacingX:", 0) == 0) {
                try { spacingX = std::stof(Trim(trimmed.substr(9))); } catch (...) {}
            } else if (trimmed.rfind("SpacingZ:", 0) == 0) {
                try { spacingZ = std::stof(Trim(trimmed.substr(9))); } catch (...) {}
            } else if (trimmed.rfind("DataFilePath:", 0) == 0) {
                dataFilePath = Trim(trimmed.substr(13));
                if (!dataFilePath.empty() && (dataFilePath.front() == '"' || dataFilePath.front() == '\'')) {
                    dataFilePath = dataFilePath.substr(1, dataFilePath.length() - 2);
                }
            } else if (trimmed.rfind("UseChunkedLOD:", 0) == 0) {
                useChunkedLOD = (Trim(trimmed.substr(14)) == "true");
            } else if (trimmed.rfind("ChunkSize:", 0) == 0) {
                try { chunkSize = std::stoul(Trim(trimmed.substr(10))); } catch (...) {}
            } else if (trimmed.rfind("GeneratePhysics:", 0) == 0) {
                generatePhysics = (Trim(trimmed.substr(16)) == "true");
            } else if (trimmed.rfind("Friction:", 0) == 0) {
                try { friction = std::stof(Trim(trimmed.substr(9))); } catch (...) {}
            } else if (trimmed.rfind("Restitution:", 0) == 0) {
                try { restitution = std::stof(Trim(trimmed.substr(12))); } catch (...) {}
            }
        }

        if (!entity.HasComponent<TerrainComponent>()) {
            entity.AddComponent<TerrainComponent>(resX, resZ, spacingX, spacingZ);
        }
        auto& tc = entity.GetComponent<TerrainComponent>();
        tc.DataFilePath = dataFilePath;
        tc.UseChunkedLOD = useChunkedLOD;
        tc.ChunkSize = chunkSize;
        tc.GeneratePhysics = generatePhysics;
        tc.Friction = friction;
        tc.Restitution = restitution;

        bool loadedFromFile = false;
        if (!dataFilePath.empty()) {
            std::filesystem::path resolved = dataFilePath;
            if (!FileSystem::Exists(resolved)) {
                resolved = FileSystem::ResolveAssetPath(dataFilePath);
            }
            if (FileSystem::Exists(resolved)) {
                if (tc.Data.LoadFromFile(resolved.string())) {
                    loadedFromFile = true;
                    AXLE_INFO("TerrainSerializer: Loaded terrain data from '{0}' ({1}x{2})", resolved.string(), tc.Data.GetResolutionX(), tc.Data.GetResolutionZ());
                }
            }
        }

        if (!loadedFromFile) {
            tc.Data.Resize(resX, resZ, spacingX, spacingZ);
        }

        return true;
    }

}
