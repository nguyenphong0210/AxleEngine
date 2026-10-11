#include <Axle/AssetSerializer.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace Axle {

    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    std::filesystem::path AssetSerializer::GetCachePathFor(const std::filesystem::path& axleTextPath) {
        auto parentDir = axleTextPath.parent_path();
        auto cacheDir = parentDir / ".cache";
        std::error_code ec;
        if (!std::filesystem::exists(cacheDir, ec)) {
            std::filesystem::create_directories(cacheDir, ec);
        }
        return cacheDir / (axleTextPath.stem().string() + ".axlebin");
    }

    bool AssetSerializer::SaveToAxleText(const std::filesystem::path& path, const AxleMeshSource& mesh) {
        std::ofstream file(path);
        if (!file.is_open()) {
            AXLE_ERROR("AssetSerializer: Failed to open file for writing: '{0}'", path.string());
            return false;
        }

        file << "# Axle Engine 3D Mesh Asset\n";
        file << "# AI-Friendly & Human-Readable Structured Text Format\n";
        file << "AssetType: Mesh\n";
        file << "Version: 1\n";
        file << "Name: " << mesh.Name << "\n\n";

        file << "Material:\n";
        file << "  PatternMode: " << mesh.PatternMode << "\n";
        file << "  TileScale: " << std::fixed << std::setprecision(2) << mesh.TileScale << "\n";
        file << "  ColorPrimary: [" 
             << mesh.ColorPrimary.r << ", " << mesh.ColorPrimary.g << ", " 
             << mesh.ColorPrimary.b << ", " << mesh.ColorPrimary.a << "]\n";
        file << "  ColorSecondary: [" 
             << mesh.ColorSecondary.r << ", " << mesh.ColorSecondary.g << ", " 
             << mesh.ColorSecondary.b << ", " << mesh.ColorSecondary.a << "]\n\n";

        file << "Bounds:\n";
        file << "  Size: [" 
             << std::setprecision(3) << mesh.BoundingSize.x << ", " 
             << mesh.BoundingSize.y << ", " << mesh.BoundingSize.z << "]\n";
        file << "  HalfExtent: [" 
             << mesh.HalfExtent.x << ", " 
             << mesh.HalfExtent.y << ", " << mesh.HalfExtent.z << "]\n\n";

        file << "# Vertex positions centered at origin [X, Y, Z]\n";
        file << "Vertices:\n";
        for (const auto& v : mesh.Vertices) {
            file << "  - [" 
                 << std::setprecision(4) << v.x << ", " 
                 << v.y << ", " << v.z << "]\n";
        }
        file << "\n";

        file << "# Triangle indices (0-based) [v0, v1, v2]\n";
        file << "Triangles:\n";
        for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3) {
            file << "  - [" 
                 << mesh.Indices[i] << ", " 
                 << mesh.Indices[i + 1] << ", " 
                 << mesh.Indices[i + 2] << "]\n";
        }

        AXLE_INFO("AssetSerializer: Saved .axle text asset '{0}' ({1} vertices, {2} triangles)", 
            path.filename().string(), mesh.Vertices.size(), mesh.Indices.size() / 3);

        return true;
    }

    bool AssetSerializer::LoadFromAxleText(const std::filesystem::path& path, AxleMeshSource& outMesh) {
        std::ifstream file(path);
        if (!file.is_open()) {
            AXLE_ERROR("AssetSerializer: Failed to open .axle text file: '{0}'", path.string());
            return false;
        }

        outMesh = AxleMeshSource{};
        outMesh.Name = path.stem().string();

        enum class Section { None, Material, Bounds, Vertices, Triangles };
        Section currentSection = Section::None;

        std::string line;
        while (std::getline(file, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.rfind("Name:", 0) == 0) {
                outMesh.Name = Trim(trimmed.substr(5));
            } else if (trimmed.rfind("Material:", 0) == 0) {
                currentSection = Section::Material;
            } else if (trimmed.rfind("Bounds:", 0) == 0) {
                currentSection = Section::Bounds;
            } else if (trimmed.rfind("Vertices:", 0) == 0) {
                currentSection = Section::Vertices;
            } else if (trimmed.rfind("Triangles:", 0) == 0) {
                currentSection = Section::Triangles;
            } else {
                switch (currentSection) {
                    case Section::Material: {
                        if (trimmed.rfind("PatternMode:", 0) == 0) {
                            outMesh.PatternMode = std::stoi(Trim(trimmed.substr(12)));
                        } else if (trimmed.rfind("TileScale:", 0) == 0) {
                            outMesh.TileScale = std::stof(Trim(trimmed.substr(10)));
                        } else if (trimmed.rfind("ColorPrimary:", 0) == 0) {
                            float r = 1, g = 1, b = 1, a = 1;
                            sscanf(trimmed.c_str(), "ColorPrimary: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                            outMesh.ColorPrimary = glm::vec4(r, g, b, a);
                        } else if (trimmed.rfind("ColorSecondary:", 0) == 0) {
                            float r = 0, g = 0, b = 0, a = 1;
                            sscanf(trimmed.c_str(), "ColorSecondary: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                            outMesh.ColorSecondary = glm::vec4(r, g, b, a);
                        }
                        break;
                    }
                    case Section::Bounds: {
                        if (trimmed.rfind("Size:", 0) == 0) {
                            float x = 1, y = 1, z = 1;
                            sscanf(trimmed.c_str(), "Size: [ %f , %f , %f ]", &x, &y, &z);
                            outMesh.BoundingSize = glm::vec3(x, y, z);
                        } else if (trimmed.rfind("HalfExtent:", 0) == 0) {
                            float x = 0.5f, y = 0.5f, z = 0.5f;
                            sscanf(trimmed.c_str(), "HalfExtent: [ %f , %f , %f ]", &x, &y, &z);
                            outMesh.HalfExtent = glm::vec3(x, y, z);
                        }
                        break;
                    }
                    case Section::Vertices: {
                        if (trimmed[0] == '-') {
                            float x = 0, y = 0, z = 0;
                            if (sscanf(trimmed.c_str(), "- [ %f , %f , %f ]", &x, &y, &z) == 3) {
                                outMesh.Vertices.push_back(glm::vec3(x, y, z));
                            }
                        }
                        break;
                    }
                    case Section::Triangles: {
                        if (trimmed[0] == '-') {
                            u32 i0 = 0, i1 = 0, i2 = 0;
                            if (sscanf(trimmed.c_str(), "- [ %u , %u , %u ]", &i0, &i1, &i2) == 3) {
                                outMesh.Indices.push_back(i0);
                                outMesh.Indices.push_back(i1);
                                outMesh.Indices.push_back(i2);
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        if (outMesh.Vertices.empty() || outMesh.Indices.empty()) {
            AXLE_WARN("AssetSerializer: .axle file '{0}' contains no vertices/indices", path.string());
            return false;
        }

        // Compute bounds if missing
        if (outMesh.BoundingSize.x <= 0.0f || outMesh.HalfExtent.x <= 0.0f) {
            glm::vec3 minB( 1e9f);
            glm::vec3 maxB(-1e9f);
            for (const auto& v : outMesh.Vertices) {
                minB = glm::min(minB, v);
                maxB = glm::max(maxB, v);
            }
            outMesh.BoundingSize = maxB - minB;
            outMesh.HalfExtent = glm::max(outMesh.BoundingSize * 0.5f, glm::vec3(0.05f));
        }

        AXLE_INFO("AssetSerializer: Parsed .axle text '{0}' ({1} vertices, {2} triangles)", 
            path.filename().string(), outMesh.Vertices.size(), outMesh.Indices.size() / 3);

        return true;
    }

    bool AssetSerializer::SaveToAxleBinary(const std::filesystem::path& binaryPath, const AxleMeshSource& mesh) {
        std::ofstream file(binaryPath, std::ios::binary);
        if (!file.is_open()) {
            AXLE_ERROR("AssetSerializer: Failed to create binary cache '{0}'", binaryPath.string());
            return false;
        }

        const char magic[8] = "AXLEBIN";
        const u32 version = 1;

        file.write(magic, 8);
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));

        // Name
        u32 nameLen = static_cast<u32>(mesh.Name.size());
        file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        if (nameLen > 0) {
            file.write(mesh.Name.data(), nameLen);
        }

        // Material & Bounds
        file.write(reinterpret_cast<const char*>(&mesh.PatternMode), sizeof(mesh.PatternMode));
        file.write(reinterpret_cast<const char*>(&mesh.TileScale), sizeof(mesh.TileScale));
        file.write(reinterpret_cast<const char*>(&mesh.ColorPrimary), sizeof(mesh.ColorPrimary));
        file.write(reinterpret_cast<const char*>(&mesh.ColorSecondary), sizeof(mesh.ColorSecondary));
        file.write(reinterpret_cast<const char*>(&mesh.BoundingSize), sizeof(mesh.BoundingSize));
        file.write(reinterpret_cast<const char*>(&mesh.HalfExtent), sizeof(mesh.HalfExtent));

        // Geometry
        u32 vertexCount = static_cast<u32>(mesh.Vertices.size());
        u32 indexCount = static_cast<u32>(mesh.Indices.size());
        file.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));
        file.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));

        if (vertexCount > 0) {
            file.write(reinterpret_cast<const char*>(mesh.Vertices.data()), vertexCount * sizeof(glm::vec3));
        }
        if (indexCount > 0) {
            file.write(reinterpret_cast<const char*>(mesh.Indices.data()), indexCount * sizeof(u32));
        }

        AXLE_INFO("AssetSerializer: Baked binary cache '{0}' ({1} bytes)", 
            binaryPath.filename().string(), static_cast<uint64_t>(file.tellp()));

        return true;
    }

    bool AssetSerializer::LoadFromAxleBinary(const std::filesystem::path& binaryPath, AxleMeshSource& outMesh) {
        std::ifstream file(binaryPath, std::ios::binary);
        if (!file.is_open()) return false;

        char magic[8];
        u32 version = 0;
        file.read(magic, 8);
        if (std::string(magic, 7) != "AXLEBIN") return false;

        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != 1) return false;

        u32 nameLen = 0;
        file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        if (nameLen > 0) {
            outMesh.Name.resize(nameLen);
            file.read(&outMesh.Name[0], nameLen);
        }

        file.read(reinterpret_cast<char*>(&outMesh.PatternMode), sizeof(outMesh.PatternMode));
        file.read(reinterpret_cast<char*>(&outMesh.TileScale), sizeof(outMesh.TileScale));
        file.read(reinterpret_cast<char*>(&outMesh.ColorPrimary), sizeof(outMesh.ColorPrimary));
        file.read(reinterpret_cast<char*>(&outMesh.ColorSecondary), sizeof(outMesh.ColorSecondary));
        file.read(reinterpret_cast<char*>(&outMesh.BoundingSize), sizeof(outMesh.BoundingSize));
        file.read(reinterpret_cast<char*>(&outMesh.HalfExtent), sizeof(outMesh.HalfExtent));

        u32 vertexCount = 0;
        u32 indexCount = 0;
        file.read(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));
        file.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));

        if (vertexCount == 0 || indexCount == 0) return false;

        outMesh.Vertices.resize(vertexCount);
        file.read(reinterpret_cast<char*>(outMesh.Vertices.data()), vertexCount * sizeof(glm::vec3));

        outMesh.Indices.resize(indexCount);
        file.read(reinterpret_cast<char*>(outMesh.Indices.data()), indexCount * sizeof(u32));

        return true;
    }

    AxleLoadedMesh AssetSerializer::LoadMesh(const std::filesystem::path& axleTextPath) {
        AxleLoadedMesh result;

        if (!std::filesystem::exists(axleTextPath)) {
            AXLE_ERROR("AssetSerializer: Asset path does not exist: '{0}'", axleTextPath.string());
            return result;
        }

        std::filesystem::path cachePath = GetCachePathFor(axleTextPath);
        bool needRebake = true;

        std::error_code ec;
        if (std::filesystem::exists(cachePath, ec)) {
            auto textWriteTime = std::filesystem::last_write_time(axleTextPath, ec);
            auto cacheWriteTime = std::filesystem::last_write_time(cachePath, ec);
            if (!ec && cacheWriteTime >= textWriteTime) {
                needRebake = false;
            }
        }

        AxleMeshSource source;
        if (!needRebake) {
            if (LoadFromAxleBinary(cachePath, source)) {
                result.LoadedFromCache = true;
            } else {
                needRebake = true;
            }
        }

        if (needRebake) {
            if (!LoadFromAxleText(axleTextPath, source)) {
                return result;
            }
            SaveToAxleBinary(cachePath, source);
            result.LoadedFromCache = false;
        }

        // Upload to GPU VertexArray
        std::vector<f32> vertexBufferData;
        vertexBufferData.reserve(source.Vertices.size() * 3);
        for (const auto& v : source.Vertices) {
            vertexBufferData.push_back(v.x);
            vertexBufferData.push_back(v.y);
            vertexBufferData.push_back(v.z);
        }

        auto va = std::make_shared<VertexArray>();
        auto vb = std::make_shared<VertexBuffer>(vertexBufferData.data(), static_cast<u32>(vertexBufferData.size() * sizeof(f32)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" }
        });
        va->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(source.Indices.data(), static_cast<u32>(source.Indices.size()));
        va->SetIndexBuffer(ib);

        result.VAO = va;
        result.SourceData = source;
        result.Success = true;

        AXLE_INFO("AssetSerializer: Ready '{0}' via {1} ({2} vertices, {3} indices)", 
            source.Name, result.LoadedFromCache ? "BINARY CACHE (.axlebin)" : "TEXT RE-BAKE (.axle)", 
            source.Vertices.size(), source.Indices.size());

        return result;
    }

    bool AssetSerializer::ExportMeshToAxle(const std::filesystem::path& axleTextPath,
                                           const std::string& name,
                                           const std::vector<glm::vec3>& vertices,
                                           const std::vector<u32>& indices,
                                           int patternMode,
                                           float tileScale,
                                           const glm::vec4& color1,
                                           const glm::vec4& color2) {
        if (vertices.empty() || indices.empty()) return false;

        // Auto-center geometry
        glm::vec3 minB( 1e9f);
        glm::vec3 maxB(-1e9f);
        for (const auto& v : vertices) {
            minB = glm::min(minB, v);
            maxB = glm::max(maxB, v);
        }

        glm::vec3 center = (minB + maxB) * 0.5f;
        AxleMeshSource source;
        source.Name = name;
        source.PatternMode = patternMode;
        source.TileScale = tileScale;
        source.ColorPrimary = color1;
        source.ColorSecondary = color2;
        source.BoundingSize = maxB - minB;
        source.HalfExtent = glm::max(source.BoundingSize * 0.5f, glm::vec3(0.05f));

        source.Vertices.reserve(vertices.size());
        for (const auto& v : vertices) {
            source.Vertices.push_back(v - center);
        }
        source.Indices = indices;

        // Save text .axle
        if (!SaveToAxleText(axleTextPath, source)) {
            return false;
        }

        // Bake binary cache .axlebin
        auto cachePath = GetCachePathFor(axleTextPath);
        SaveToAxleBinary(cachePath, source);

        return true;
    }

    bool AssetSerializer::ConvertOBJToAxle(const std::filesystem::path& objPath, const std::filesystem::path& outAxlePath) {
        std::ifstream file(objPath);
        if (!file.is_open()) {
            AXLE_ERROR("AssetSerializer: Failed to open OBJ file '{0}'", objPath.string());
            return false;
        }

        std::vector<glm::vec3> temp_positions;
        std::vector<u32> indices;

        std::string line;
        while (std::getline(file, line)) {
            size_t firstNonSpace = line.find_first_not_of(" \t\r\n");
            if (firstNonSpace == std::string::npos) continue;
            if (line[firstNonSpace] == '#') continue;

            std::string sub = line.substr(firstNonSpace);
            if (sub.rfind("v ", 0) == 0) {
                std::istringstream s(sub.substr(2));
                glm::vec3 v(0.0f);
                s >> v.x >> v.y >> v.z;
                temp_positions.push_back(v);
            } else if (sub.rfind("f ", 0) == 0) {
                std::istringstream s(sub.substr(2));
                std::string token;
                std::vector<u32> faceIdx;
                while (s >> token) {
                    size_t slash = token.find('/');
                    std::string idxStr = (slash == std::string::npos) ? token : token.substr(0, slash);
                    if (!idxStr.empty()) {
                        try {
                            int idx = std::stoi(idxStr);
                            if (idx < 0) {
                                idx = static_cast<int>(temp_positions.size()) + idx + 1;
                            }
                            if (idx > 0 && static_cast<size_t>(idx) <= temp_positions.size()) {
                                faceIdx.push_back(static_cast<u32>(idx - 1));
                            }
                        } catch (...) {
                        }
                    }
                }
                for (size_t i = 1; i + 1 < faceIdx.size(); ++i) {
                    indices.push_back(faceIdx[0]);
                    indices.push_back(faceIdx[i]);
                    indices.push_back(faceIdx[i + 1]);
                }
            }
        }

        if (temp_positions.empty() || indices.empty()) {
            AXLE_WARN("AssetSerializer: OBJ file '{0}' contains no valid geometry", objPath.string());
            return false;
        }

        std::string modelName = objPath.stem().string();
        return ExportMeshToAxle(outAxlePath, modelName, temp_positions, indices);
    }

}

