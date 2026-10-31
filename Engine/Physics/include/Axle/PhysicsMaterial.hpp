#pragma once
#include <Axle/Types.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/FileSystem.hpp>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace Axle {

    struct PhysicsMaterial {
        std::string Name = "PhysicsMaterial";
        float Friction = 0.5f;
        float Restitution = 0.0f; // Bounciness
        float Density = 1000.0f;

        static std::string Serialize(const PhysicsMaterial& mat) {
            std::ostringstream ss;
            ss << "PhysicsMaterial {\n";
            ss << "    name: \"" << (mat.Name.empty() ? "PhysicsMaterial" : mat.Name) << "\"\n";
            ss << "    friction: " << std::fixed << std::setprecision(2) << mat.Friction << "\n";
            ss << "    restitution: " << std::fixed << std::setprecision(2) << mat.Restitution << "\n";
            ss << "    density: " << std::fixed << std::setprecision(1) << mat.Density << "\n";
            ss << "}\n";
            return ss.str();
        }

        static bool Deserialize(std::string_view source, PhysicsMaterial& outMat) {
            if (source.find("PhysicsMaterial {") != std::string_view::npos || AssetDSL::PeekAssetKind(source) == AssetKind::PhysicsMaterial) {
                AssetDocument doc;
                std::string err;
                if (AssetDSL::ParseSource(source, doc, &err) && doc.IsValid() && doc.Root) {
                    outMat.Name = doc.Root->GetString("name", "PhysicsMaterial");
                    outMat.Friction = doc.Root->GetFloat("friction", 0.5f);
                    outMat.Restitution = doc.Root->GetFloat("restitution", 0.0f);
                    outMat.Density = doc.Root->GetFloat("density", 1000.0f);
                    return true;
                }
            }

            // Fallback for legacy format
            std::istringstream fin{ std::string(source) };
            std::string line;
            while (std::getline(fin, line)) {
                size_t start = line.find_first_not_of(" \t\r\n");
                if (start == std::string::npos) continue;
                std::string trimmed = line.substr(start);
                if (trimmed.rfind("Friction:", 0) == 0) {
                    try { outMat.Friction = std::stof(trimmed.substr(9)); } catch (...) {}
                } else if (trimmed.rfind("Restitution:", 0) == 0) {
                    try { outMat.Restitution = std::stof(trimmed.substr(12)); } catch (...) {}
                } else if (trimmed.rfind("Density:", 0) == 0) {
                    try { outMat.Density = std::stof(trimmed.substr(8)); } catch (...) {}
                }
            }
            return true;
        }

        static bool Save(const std::filesystem::path& path, const PhysicsMaterial& mat) {
            if (path.extension() == ".axleasset") {
                return FileSystem::WriteTextFile(path, Serialize(mat));
            }
            std::ofstream fout(path);
            if (!fout.is_open()) return false;
            fout << "# Axle Physics Material Asset\n";
            fout << "PhysicsMaterial:\n";
            fout << "  Friction: " << mat.Friction << "\n";
            fout << "  Restitution: " << mat.Restitution << "\n";
            fout << "  Density: " << mat.Density << "\n";
            fout.close();
            return true;
        }

        static bool Load(const std::filesystem::path& path, PhysicsMaterial& outMat) {
            std::filesystem::path actualPath = path;
            if (!FileSystem::Exists(actualPath)) {
                if (actualPath.extension() == ".physmat") {
                    actualPath.replace_extension(".axleasset");
                }
            }
            std::string content = FileSystem::ReadTextFile(actualPath);
            if (content.empty()) return false;
            outMat.Name = actualPath.stem().string();
            return Deserialize(content, outMat);
        }
    };

}
