#pragma once
#include <Axle/Types.hpp>
#include <string>
#include <filesystem>
#include <fstream>

namespace Axle {

    struct PhysicsMaterial {
        float Friction = 0.5f;
        float Restitution = 0.0f; // Bounciness
        float Density = 1000.0f;

        static bool Save(const std::filesystem::path& path, const PhysicsMaterial& mat) {
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
            std::ifstream fin(path);
            if (!fin.is_open()) return false;
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
    };

}
