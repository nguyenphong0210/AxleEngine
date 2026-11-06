#pragma once
#include <Axle/Types.hpp>
#include <Axle/FileSystem.hpp>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cctype>

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
            std::istringstream fin{ std::string(source) };
            std::string line;
            bool foundKey = false;
            while (std::getline(fin, line)) {
                size_t start = line.find_first_not_of(" \t\r\n");
                if (start == std::string::npos) continue;
                std::string trimmed = line.substr(start);
                if (trimmed.empty() || trimmed[0] == '#' || trimmed.rfind("//", 0) == 0) continue;

                auto parseVal = [](const std::string& str) -> std::string {
                    size_t s = str.find_first_not_of(" \t\r\n\":");
                    if (s == std::string::npos) return "";
                    size_t e = str.find_last_not_of(" \t\r\n\",");
                    if (e == std::string::npos || e < s) return "";
                    return str.substr(s, e - s + 1);
                };

                std::string lower = trimmed;
                for (char& c : lower) c = (char)std::tolower((unsigned char)c);

                if (lower.rfind("name:", 0) == 0) {
                    outMat.Name = parseVal(trimmed.substr(5));
                    foundKey = true;
                } else if (lower.rfind("friction:", 0) == 0) {
                    try { outMat.Friction = std::stof(parseVal(trimmed.substr(9))); foundKey = true; } catch (...) {}
                } else if (lower.rfind("restitution:", 0) == 0) {
                    try { outMat.Restitution = std::stof(parseVal(trimmed.substr(12))); foundKey = true; } catch (...) {}
                } else if (lower.rfind("density:", 0) == 0) {
                    try { outMat.Density = std::stof(parseVal(trimmed.substr(8))); foundKey = true; } catch (...) {}
                }
            }
            return foundKey;
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
