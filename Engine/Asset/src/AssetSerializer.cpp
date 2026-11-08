#include <Axle/AssetSerializer.hpp>
#include <Axle/Path.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include <glm/gtc/type_ptr.hpp>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <ufbx.h>

namespace Axle {

    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    static std::string SanitizeFilename(const std::string& name) {
        std::string result;
        result.reserve(name.size());
        for (char c : name) {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == ' ') {
                result += '_';
            } else {
                result += c;
            }
        }
        std::string trimmed = Trim(result);
        return trimmed.empty() ? "Submesh" : trimmed;
    }

    std::filesystem::path AssetSerializer::GetCachePathFor(const std::filesystem::path& axleTextPath) {
        auto parentDir = axleTextPath.parent_path();
        auto cacheDir = parentDir / ".cache";
        std::error_code ec;
        if (!FileSystem::HasMountedPackage() && !std::filesystem::exists(cacheDir, ec)) {
            std::filesystem::create_directories(cacheDir, ec);
        }
        return cacheDir / (axleTextPath.stem().string() + ".axlebin");
    }

    bool AssetSerializer::SaveToAxleText(const std::filesystem::path& path, const AxleMeshSource& mesh) {
        if (path.extension() == ".axleasset") {
            std::ostringstream file;
            file << "Mesh {\n";
            file << "    name: \"" << (mesh.Name.empty() ? path.stem().string() : mesh.Name) << "\"\n";
            file << "    bounds: [" << std::fixed << std::setprecision(3) << mesh.BoundingSize.x << ", " << mesh.BoundingSize.y << ", " << mesh.BoundingSize.z << "]\n";
            file << "    patternMode: " << mesh.PatternMode << "\n";
            file << "    tileScale: " << std::fixed << std::setprecision(2) << mesh.TileScale << "\n";
            file << "    colorPrimary: \"" << AssetDSL::FormatHexColor(mesh.ColorPrimary) << "\"\n";
            file << "    colorSecondary: \"" << AssetDSL::FormatHexColor(mesh.ColorSecondary) << "\"\n\n";

            file << "    vertices: [\n";
            for (size_t i = 0; i < mesh.Vertices.size(); ++i) {
                const auto& v = mesh.Vertices[i];
                file << "        [" << std::setprecision(4) << v.x << ", " << v.y << ", " << v.z << "]"
                     << (i + 1 < mesh.Vertices.size() ? "," : "") << "\n";
            }
            file << "    ]\n\n";

            if (mesh.Normals.size()==mesh.Vertices.size()) {
                file << "    normals: [\n";
                for (size_t i=0;i<mesh.Normals.size();++i) {
                    auto n=mesh.Normals[i];
                    file << "        [" << std::setprecision(9) << n.x << ", " << n.y << ", " << n.z << "]" << (i+1<mesh.Normals.size()?",":"") << "\n";
                }
                file << "    ]\n";
            }
            if (mesh.UVs.size()==mesh.Vertices.size()) {
                file << "    texcoords: [\n";
                for (size_t i=0;i<mesh.UVs.size();++i) {
                    auto uv=mesh.UVs[i];
                    file << "        [" << std::setprecision(9) << uv.x << ", " << uv.y << ", 0]" << (i+1<mesh.UVs.size()?",":"") << "\n";
                }
                file << "    ]\n";
            }
            file << "    indices: [\n";
            for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3) {
                file << "        [" << mesh.Indices[i] << ", " << mesh.Indices[i+1] << ", " << mesh.Indices[i+2] << "]"
                     << (i + 3 < mesh.Indices.size() ? "," : "") << "\n";
            }
            file << "    ]\n";
            file << "}\n";

            if (!FileSystem::WriteIdentifiedTextFile(path, file.str())) return false;

            // Automatically bake to binary cache
            std::filesystem::path cachePath = GetCachePathFor(path);
            SaveToAxleBinary(cachePath, mesh);
            return true;
        }

        std::ostringstream file;

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

        if (mesh.Normals.size()==mesh.Vertices.size()) {
            file << "Normals:\n";
            for (auto n:mesh.Normals) file << "  - [" << std::setprecision(9) << n.x << ", " << n.y << ", " << n.z << "]\n";
        }
        if (mesh.UVs.size()==mesh.Vertices.size()) {
            file << "UVs:\n";
            for (auto uv:mesh.UVs) file << "  - [" << std::setprecision(9) << uv.x << ", " << uv.y << "]\n";
        }
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

        return FileSystem::WriteIdentifiedTextFile(path, file.str());
    }

    bool AssetSerializer::LoadFromAxleText(const std::filesystem::path& path, AxleMeshSource& outMesh) {
        std::filesystem::path actualPath = path;
        if (!FileSystem::Exists(actualPath)) {
            if (actualPath.extension() == ".axlemesh" || actualPath.extension() == ".axle") {
                actualPath.replace_extension(".axleasset");
            }
        }

        std::string content = FileSystem::ReadTextFile(actualPath);
        if (content.empty()) {
            AXLE_ERROR("AssetSerializer: Failed to read mesh text file: '{0}'", path.string());
            return false;
        }

        // Check for pure unified .axleasset Mesh block
        if (content.find("Mesh {") != std::string::npos || AssetDSL::PeekAssetKind(content) == AssetKind::Mesh) {
            AssetDocument doc;
            std::string err;
            if (AssetDSL::ParseSource(content, doc, &err) && doc.IsValid() && doc.Root) {
                outMesh = AxleMeshSource{};
                outMesh.Name = doc.Root->GetString("name", actualPath.stem().string());
                if (doc.Root->HasProperty("bounds")) {
                    outMesh.BoundingSize = doc.Root->GetVec3("bounds");
                    outMesh.HalfExtent = outMesh.BoundingSize * 0.5f;
                }
                if (doc.Root->HasProperty("patternMode")) {
                    outMesh.PatternMode = doc.Root->GetInt("patternMode", 1);
                }
                if (doc.Root->HasProperty("tileScale")) {
                    outMesh.TileScale = doc.Root->GetFloat("tileScale", 8.0f);
                }
                if (doc.Root->HasProperty("colorPrimary")) {
                    outMesh.ColorPrimary = doc.Root->GetVec4("colorPrimary", glm::vec4(0.85f, 0.88f, 0.92f, 1.0f));
                }
                if (doc.Root->HasProperty("colorSecondary")) {
                    outMesh.ColorSecondary = doc.Root->GetVec4("colorSecondary", glm::vec4(0.65f, 0.70f, 0.76f, 1.0f));
                }
                if (doc.Root->HasProperty("vertices")) {
                    outMesh.Vertices = doc.Root->GetProperty("vertices")->AsVec3Array();
                }
                if (doc.Root->HasProperty("normals")) outMesh.Normals=doc.Root->GetProperty("normals")->AsVec3Array();
                if (doc.Root->HasProperty("texcoords")) {
                    for (auto uv:doc.Root->GetProperty("texcoords")->AsVec3Array()) outMesh.UVs.emplace_back(uv.x,uv.y);
                }
                if (doc.Root->HasProperty("indices")) {
                    auto tris = doc.Root->GetProperty("indices")->AsIVec3Array();
                    outMesh.Indices.reserve(tris.size() * 3);
                    for (const auto& tri : tris) {
                        outMesh.Indices.push_back(static_cast<u32>(tri.x));
                        outMesh.Indices.push_back(static_cast<u32>(tri.y));
                        outMesh.Indices.push_back(static_cast<u32>(tri.z));
                    }
                }

                if (outMesh.BoundingSize == glm::vec3(1.0f) && !outMesh.Vertices.empty()) {
                    glm::vec3 minV(1e9f), maxV(-1e9f);
                    for (const auto& v : outMesh.Vertices) {
                        minV = glm::min(minV, v);
                        maxV = glm::max(maxV, v);
                    }
                    outMesh.BoundingSize = glm::max(maxV - minV, glm::vec3(0.01f));
                    outMesh.HalfExtent = outMesh.BoundingSize * 0.5f;
                }

                return true;
            }
        }



        std::istringstream file(content);
        outMesh = AxleMeshSource{};
        outMesh.Name = path.stem().string();

        enum class Section { None, Material, Bounds, Vertices, Normals, UVs, Triangles };
        Section currentSection = Section::None;

        std::string line;
        while (std::getline(file, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.rfind("AssetType:", 0) == 0) {
                std::string val = Trim(trimmed.substr(10));
                if (val == "Assembly") {
                    return false;
                }
            } else if (trimmed.rfind("Name:", 0) == 0) {
                outMesh.Name = Trim(trimmed.substr(5));
            } else if (trimmed.rfind("Material:", 0) == 0) {
                currentSection = Section::Material;
            } else if (trimmed.rfind("Bounds:", 0) == 0) {
                currentSection = Section::Bounds;
            } else if (trimmed.rfind("Vertices:", 0) == 0) {
                currentSection = Section::Vertices;
            } else if (trimmed=="Normals:") {
                currentSection=Section::Normals;
            } else if (trimmed=="UVs:") {
                currentSection=Section::UVs;
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
                    case Section::Normals: {
                        glm::vec3 n(0);
                        if (std::sscanf(trimmed.c_str(),"- [ %f , %f , %f ]",&n.x,&n.y,&n.z)==3) outMesh.Normals.push_back(n);
                        break;
                    }
                    case Section::UVs: {
                        glm::vec2 uv(0);
                        if (std::sscanf(trimmed.c_str(),"- [ %f , %f ]",&uv.x,&uv.y)==2) outMesh.UVs.push_back(uv);
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
        if (!binaryPath.parent_path().empty()) {
            std::error_code ec;
            std::filesystem::create_directories(binaryPath.parent_path(), ec);
        }
        std::ofstream file(binaryPath, std::ios::binary);
        if (!file.is_open()) {
            AXLE_ERROR("AssetSerializer: Failed to create binary cache '{0}'", binaryPath.string());
            return false;
        }

        const char magic[8] = "AXLEBIN";
        const u32 version = 2;

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

        u32 normalCount=mesh.Normals.size()==vertexCount?vertexCount:0;
        u32 uvCount=mesh.UVs.size()==vertexCount?vertexCount:0;
        file.write(reinterpret_cast<const char*>(&normalCount),sizeof(normalCount));
        file.write(reinterpret_cast<const char*>(&uvCount),sizeof(uvCount));
        if (normalCount) file.write(reinterpret_cast<const char*>(mesh.Normals.data()),size_t(normalCount)*sizeof(glm::vec3));
        if (uvCount) file.write(reinterpret_cast<const char*>(mesh.UVs.data()),size_t(uvCount)*sizeof(glm::vec2));

        AXLE_INFO("AssetSerializer: Baked binary cache '{0}' ({1} bytes)",
            binaryPath.filename().string(), static_cast<uint64_t>(file.tellp()));

        return true;
    }

    bool AssetSerializer::LoadFromAxleBinary(const std::filesystem::path& binaryPath, AxleMeshSource& outMesh) {
        auto buffer = FileSystem::ReadBinaryFile(binaryPath);
        if (buffer.empty()) return false;

        const uint8_t* ptr = buffer.data();
        size_t remaining = buffer.size();

        if (remaining < 8) return false;
        char magic[8];
        memcpy(magic, ptr, 8);
        ptr += 8; remaining -= 8;
        if (std::string(magic, 7) != "AXLEBIN") return false;

        if (remaining < sizeof(u32)) return false;
        u32 version = *reinterpret_cast<const u32*>(ptr);
        ptr += sizeof(u32); remaining -= sizeof(u32);
        if (version!=1 && version!=2) return false;
        outMesh=AxleMeshSource{};

        if (remaining < sizeof(u32)) return false;
        u32 nameLen = *reinterpret_cast<const u32*>(ptr);
        ptr += sizeof(u32); remaining -= sizeof(u32);
        if (nameLen > 0) {
            if (remaining < nameLen) return false;
            outMesh.Name.assign(reinterpret_cast<const char*>(ptr), nameLen);
            ptr += nameLen; remaining -= nameLen;
        }

        struct HeaderProps {
            i32 PatternMode;
            f32 TileScale;
            glm::vec4 ColorPrimary;
            glm::vec4 ColorSecondary;
            glm::vec3 BoundingSize;
            glm::vec3 HalfExtent;
        };
        if (remaining < sizeof(HeaderProps)) return false;
        const auto* props = reinterpret_cast<const HeaderProps*>(ptr);
        outMesh.PatternMode = props->PatternMode;
        outMesh.TileScale = props->TileScale;
        outMesh.ColorPrimary = props->ColorPrimary;
        outMesh.ColorSecondary = props->ColorSecondary;
        outMesh.BoundingSize = props->BoundingSize;
        outMesh.HalfExtent = props->HalfExtent;
        ptr += sizeof(HeaderProps); remaining -= sizeof(HeaderProps);

        if (remaining < sizeof(u32) * 2) return false;
        u32 vertexCount = *reinterpret_cast<const u32*>(ptr);
        ptr += sizeof(u32); remaining -= sizeof(u32);
        u32 indexCount = *reinterpret_cast<const u32*>(ptr);
        ptr += sizeof(u32); remaining -= sizeof(u32);

        if (vertexCount == 0 || indexCount == 0) return false;
        if (remaining < vertexCount * sizeof(glm::vec3) + indexCount * sizeof(u32)) return false;

        outMesh.Vertices.resize(vertexCount);
        memcpy(outMesh.Vertices.data(), ptr, vertexCount * sizeof(glm::vec3));
        ptr += vertexCount * sizeof(glm::vec3);

        outMesh.Indices.resize(indexCount);
        memcpy(outMesh.Indices.data(), ptr, indexCount * sizeof(u32));
        ptr+=size_t(indexCount)*sizeof(u32);
        remaining-=size_t(vertexCount)*sizeof(glm::vec3)+size_t(indexCount)*sizeof(u32);
        if (version>=2) {
            if (remaining<sizeof(u32)*2) return false;
            u32 normalCount=0,uvCount=0;
            memcpy(&normalCount,ptr,sizeof(u32)); memcpy(&uvCount,ptr+sizeof(u32),sizeof(u32));
            ptr+=sizeof(u32)*2; remaining-=sizeof(u32)*2;
            if ((normalCount && normalCount!=vertexCount)||(uvCount && uvCount!=vertexCount)) return false;
            size_t normalBytes=size_t(normalCount)*sizeof(glm::vec3),uvBytes=size_t(uvCount)*sizeof(glm::vec2);
            if (remaining<normalBytes+uvBytes) return false;
            outMesh.Normals.resize(normalCount); outMesh.UVs.resize(uvCount);
            if (normalBytes) memcpy(outMesh.Normals.data(),ptr,normalBytes);
            if (uvBytes) memcpy(outMesh.UVs.data(),ptr+normalBytes,uvBytes);
        }


        return true;
    }

    AxleLoadedMesh AssetSerializer::LoadMesh(const std::filesystem::path& axleTextPath) {
        AxleLoadedMesh result;

        if (!FileSystem::Exists(axleTextPath)) {
            AXLE_ERROR("AssetSerializer: Asset path does not exist: '{0}'", axleTextPath.string());
            return result;
        }

        std::filesystem::path cachePath = GetCachePathFor(axleTextPath);
        bool needRebake = true;

        if (FileSystem::Exists(cachePath)) {
            std::error_code ec;
            auto textWriteTime = std::filesystem::last_write_time(axleTextPath, ec);
            auto cacheWriteTime = std::filesystem::last_write_time(cachePath, ec);
            if (!ec && cacheWriteTime >= textWriteTime) {
                needRebake = false;
            } else if (FileSystem::HasMountedPackage()) {
                // In packaged VFS mode, cache is authoritative
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

        result.SourceData = std::move(source);
        result.Success = true;

        AXLE_INFO("AssetSerializer: Ready '{0}' via {1} ({2} vertices, {3} indices)",
            result.SourceData.Name, result.LoadedFromCache ? "BINARY CACHE (.axlebin)" : "TEXT RE-BAKE (.axle)",
            result.SourceData.Vertices.size(), result.SourceData.Indices.size());

        return result;
    }

    bool AssetSerializer::SavePackageManifest(const std::filesystem::path& manifestPath,
                                              const std::string& packageName,
                                              const std::vector<AxlePackagePart>& parts,
                                              const glm::vec3& boundingSize,
                                              const glm::vec3& halfExtent) {
        std::ostringstream file;

        file << "# Axle Engine 3D Model Package\n";
        file << "# AI-Friendly & Human-Readable Structured Text Format\n";
        file << "AssetType: ModelPackage\n";
        file << "Version: 1\n";
        file << "Name: " << packageName << "\n\n";

        file << "Bounds:\n";
        file << "  Size: ["
             << std::setprecision(3) << boundingSize.x << ", "
             << boundingSize.y << ", " << boundingSize.z << "]\n";
        file << "  HalfExtent: ["
             << halfExtent.x << ", "
             << halfExtent.y << ", " << halfExtent.z << "]\n\n";

        file << "Parts:\n";
        for (const auto& part : parts) {
            file << "  - Name: " << part.Name << "\n";
            file << "    Path: " << part.RelativePath << "\n";
            file << "    Position: ["
                 << std::setprecision(4) << part.LocalTranslation.x << ", "
                 << part.LocalTranslation.y << ", " << part.LocalTranslation.z << "]\n";
            file << "    Rotation: ["
                 << part.LocalRotation.x << ", " << part.LocalRotation.y << ", "
                 << part.LocalRotation.z << ", " << part.LocalRotation.w << "]\n";
            file << "    Scale: ["
                 << part.LocalScale.x << ", " << part.LocalScale.y << ", " << part.LocalScale.z << "]\n";
        }

        if (!FileSystem::WriteIdentifiedTextFile(manifestPath,file.str())) return false;
        AXLE_INFO("AssetSerializer: Saved multi-part package manifest '{0}' ({1} sub-parts)",
            manifestPath.filename().string(), parts.size());
        return true;
    }

    AxleLoadedPackage AssetSerializer::LoadPackage(const std::filesystem::path& axlePath) {
        AxleLoadedPackage pkg;
        std::ifstream file(axlePath);
        if (!file.is_open()) {
            AXLE_ERROR("AssetSerializer: Failed to open package file '{0}'", axlePath.string());
            return pkg;
        }

        std::string line;
        bool isPackage = false;
        std::string pkgName = axlePath.stem().string();
        glm::vec3 boundingSize{ 1.0f };
        glm::vec3 halfExtent{ 0.5f };

        int headerLines = 0;
        while (std::getline(file, line) && headerLines++ < 20) {
            std::string trimmed = Trim(line);
            if (trimmed.rfind("AssetType:", 0) == 0) {
                std::string type = Trim(trimmed.substr(10));
                if (type == "ModelPackage") {
                    isPackage = true;
                }
                break;
            }
        }

        if (!isPackage) {
            auto singleMesh = LoadMesh(axlePath);
            if (singleMesh.Success) {
                pkg.Name = singleMesh.SourceData.Name;
                pkg.BoundingSize = singleMesh.SourceData.BoundingSize;
                pkg.HalfExtent = singleMesh.SourceData.HalfExtent;
                pkg.IsPackage = false;
                pkg.Success = true;

                AxlePackagePart part;
                part.Name = pkg.Name;
                part.RelativePath = axlePath.filename().string();
                part.LoadedMesh = singleMesh;
                pkg.Parts.push_back(part);
            }
            return pkg;
        }

        file.clear();
        file.seekg(0);

        enum class Section { None, Bounds, Parts };
        Section currentSection = Section::None;

        AxlePackagePart currentPart;
        bool hasCurrentPart = false;

        auto finalizeCurrentPart = [&]() {
            if (hasCurrentPart && !currentPart.RelativePath.empty()) {
                auto partFullPath = axlePath.parent_path() / currentPart.RelativePath;
                currentPart.LoadedMesh = LoadMesh(partFullPath);
                pkg.Parts.push_back(currentPart);
                currentPart = AxlePackagePart{};
                hasCurrentPart = false;
            }
        };

        while (std::getline(file, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.rfind("Name:", 0) == 0 && currentSection != Section::Parts) {
                pkgName = Trim(trimmed.substr(5));
            } else if (trimmed == "Bounds:") {
                currentSection = Section::Bounds;
            } else if (trimmed == "Parts:") {
                currentSection = Section::Parts;
            } else if (currentSection == Section::Bounds) {
                if (trimmed.rfind("Size:", 0) == 0) {
                    auto b1 = trimmed.find('[');
                    auto b2 = trimmed.find(']');
                    if (b1 != std::string::npos && b2 != std::string::npos) {
                        std::string inside = trimmed.substr(b1 + 1, b2 - b1 - 1);
                        std::replace(inside.begin(), inside.end(), ',', ' ');
                        std::istringstream iss(inside);
                        iss >> boundingSize.x >> boundingSize.y >> boundingSize.z;
                    }
                } else if (trimmed.rfind("HalfExtent:", 0) == 0) {
                    auto b1 = trimmed.find('[');
                    auto b2 = trimmed.find(']');
                    if (b1 != std::string::npos && b2 != std::string::npos) {
                        std::string inside = trimmed.substr(b1 + 1, b2 - b1 - 1);
                        std::replace(inside.begin(), inside.end(), ',', ' ');
                        std::istringstream iss(inside);
                        iss >> halfExtent.x >> halfExtent.y >> halfExtent.z;
                    }
                }
            } else if (currentSection == Section::Parts) {
                if (trimmed.rfind("- Name:", 0) == 0) {
                    finalizeCurrentPart();
                    hasCurrentPart = true;
                    currentPart.Name = Trim(trimmed.substr(7));
                } else if (trimmed.rfind("Path:", 0) == 0) {
                    currentPart.RelativePath = Trim(trimmed.substr(5));
                } else if (trimmed.rfind("Position:", 0) == 0) {
                    auto b1 = trimmed.find('[');
                    auto b2 = trimmed.find(']');
                    if (b1 != std::string::npos && b2 != std::string::npos) {
                        std::string inside = trimmed.substr(b1 + 1, b2 - b1 - 1);
                        std::replace(inside.begin(), inside.end(), ',', ' ');
                        std::istringstream iss(inside);
                        iss >> currentPart.LocalTranslation.x >> currentPart.LocalTranslation.y >> currentPart.LocalTranslation.z;
                    }
                } else if (trimmed.rfind("Rotation:", 0) == 0) {
                    auto b1 = trimmed.find('[');
                    auto b2 = trimmed.find(']');
                    if (b1 != std::string::npos && b2 != std::string::npos) {
                        std::string inside = trimmed.substr(b1 + 1, b2 - b1 - 1);
                        std::replace(inside.begin(), inside.end(), ',', ' ');
                        std::istringstream iss(inside);
                        iss >> currentPart.LocalRotation.x >> currentPart.LocalRotation.y >> currentPart.LocalRotation.z >> currentPart.LocalRotation.w;
                    }
                } else if (trimmed.rfind("Scale:", 0) == 0) {
                    auto b1 = trimmed.find('[');
                    auto b2 = trimmed.find(']');
                    if (b1 != std::string::npos && b2 != std::string::npos) {
                        std::string inside = trimmed.substr(b1 + 1, b2 - b1 - 1);
                        std::replace(inside.begin(), inside.end(), ',', ' ');
                        std::istringstream iss(inside);
                        iss >> currentPart.LocalScale.x >> currentPart.LocalScale.y >> currentPart.LocalScale.z;
                    }
                }
            }
        }
        finalizeCurrentPart();

        pkg.Name = pkgName;
        pkg.BoundingSize = boundingSize;
        pkg.HalfExtent = halfExtent;
        pkg.IsPackage = true;
        pkg.Success = !pkg.Parts.empty();

        return pkg;
    }

    bool AssetSerializer::ExportMeshToAxle(const std::filesystem::path& axleTextPath,
                                           const std::string& name,
                                           const std::vector<glm::vec3>& vertices,
                                           const std::vector<u32>& indices,
                                           int patternMode,
                                           float tileScale,
                                           const glm::vec4& color1,
                                           const glm::vec4& color2,
                                           const std::vector<glm::vec3>& normals,
                                           const std::vector<glm::vec2>& uvs) {
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
        if (normals.size()==vertices.size()) source.Normals=normals;
        if (uvs.size()==vertices.size()) source.UVs=uvs;

        // Save text .axle
        if (!SaveToAxleText(axleTextPath, source)) {
            return false;
        }

        // Bake binary cache .axlebin
        auto cachePath = GetCachePathFor(axleTextPath);
        SaveToAxleBinary(cachePath, source);

        return true;
    }

    bool AssetSerializer::ConvertOBJToAxle(const std::filesystem::path& objPath,
                                           const std::filesystem::path& outAxlePath,
                                           bool splitSubmeshes,
                                           int patternMode,
                                           float tileScale,
                                           const glm::vec4& color1,
                                           const glm::vec4& color2) {
        std::ifstream file(objPath);
        if (!file.is_open()) {
            AXLE_ERROR("AssetSerializer: Failed to open OBJ file '{0}'", objPath.string());
            return false;
        }

        struct ObjGroupData {
            std::string Name;
            std::vector<u32> Triangles;
        };

        std::vector<glm::vec3> objPositions,objNormals,temp_positions,normals;
        std::vector<glm::vec2> objUVs,uvs;
        std::unordered_map<std::string,u32> corners;
        bool allNormals=true,allUVs=true;
        auto resolveIndex=[](const std::string& text,size_t count)->int {
            if (text.empty()) return -1;
            int index=std::stoi(text);
            index=index>0?index-1:int(count)+index;
            return index>=0 && size_t(index)<count?index:-1;
        };
        std::vector<ObjGroupData> groups;
        groups.push_back({ objPath.stem().string(), {} });

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
                objPositions.push_back(v);
            } else if (sub.rfind("vt ",0)==0) {
                std::istringstream values(sub.substr(3)); glm::vec2 uv(0); values>>uv.x>>uv.y; objUVs.push_back(uv);
            } else if (sub.rfind("vn ",0)==0) {
                std::istringstream values(sub.substr(3)); glm::vec3 n(0); values>>n.x>>n.y>>n.z; objNormals.push_back(n);
            } else if (sub.rfind("o ", 0) == 0 || sub.rfind("g ", 0) == 0) {
                if (splitSubmeshes) {
                    std::string gName = Trim(sub.substr(2));
                    if (!groups.empty() && groups.back().Triangles.empty()) {
                        groups.back().Name = gName.empty() ? ("Part_" + std::to_string(groups.size())) : gName;
                    } else {
                        groups.push_back({ gName.empty() ? ("Part_" + std::to_string(groups.size() + 1)) : gName, {} });
                    }
                }
            } else if (sub.rfind("f ", 0) == 0) {
                std::istringstream s(sub.substr(2));
                std::string token;
                std::vector<u32> faceIdx;
                while (s >> token) {
                    if (token[0]=='#') break;
                    try {
                        size_t slash=token.find('/'),second=slash==std::string::npos?std::string::npos:token.find('/',slash+1);
                        int p=resolveIndex(token.substr(0,slash),objPositions.size());
                        int uv=slash==std::string::npos?-1:resolveIndex(token.substr(slash+1,second==std::string::npos?std::string::npos:second-slash-1),objUVs.size());
                        int n=second==std::string::npos?-1:resolveIndex(token.substr(second+1),objNormals.size());
                        if (p<0) continue;
                        std::string key=std::to_string(p)+"/"+std::to_string(uv)+"/"+std::to_string(n);
                        auto [it,inserted]=corners.emplace(key,static_cast<u32>(temp_positions.size()));
                        if (inserted) {
                            temp_positions.push_back(objPositions[p]);
                            normals.push_back(n>=0?objNormals[n]:glm::vec3(0));
                            uvs.push_back(uv>=0?objUVs[uv]:glm::vec2(0));
                            allNormals&=n>=0; allUVs&=uv>=0;
                        }
                        faceIdx.push_back(it->second);
                    } catch (...) {}
                }
                for (size_t i = 1; i + 1 < faceIdx.size(); ++i) {
                    groups.back().Triangles.push_back(faceIdx[0]);
                    groups.back().Triangles.push_back(faceIdx[i]);
                    groups.back().Triangles.push_back(faceIdx[i + 1]);
                }
            }
        }

        if (!allNormals) normals.clear();
        if (!allUVs) uvs.clear();

        std::vector<ObjGroupData> validGroups;
        for (auto& g : groups) {
            if (!g.Triangles.empty()) validGroups.push_back(std::move(g));
        }

        if (temp_positions.empty() || validGroups.empty()) {
            AXLE_WARN("AssetSerializer: OBJ file '{0}' contains no valid geometry", objPath.string());
            return false;
        }

        if (splitSubmeshes && validGroups.size() > 1) {
            std::vector<AxleSubmeshSource> submeshes;
            std::unordered_map<std::string, int> nameCounts;
            glm::vec3 overallMin( 1e9f);
            glm::vec3 overallMax(-1e9f);

            for (const auto& g : validGroups) {
                std::string partName = SanitizeFilename(g.Name);
                if (nameCounts[partName]++ > 0) {
                    partName += "_" + std::to_string(nameCounts[partName]);
                }

                std::unordered_map<u32, u32> globalToLocal;
                std::vector<glm::vec3> localVertices;
                std::vector<u32> localIndices;

                glm::vec3 partMin( 1e9f);
                glm::vec3 partMax(-1e9f);

                for (u32 gIdx : g.Triangles) {
                    auto it = globalToLocal.find(gIdx);
                    if (it == globalToLocal.end()) {
                        u32 newIdx = static_cast<u32>(localVertices.size());
                        globalToLocal[gIdx] = newIdx;
                        glm::vec3 pos = temp_positions[gIdx];
                        localVertices.push_back(pos);
                        partMin = glm::min(partMin, pos);
                        partMax = glm::max(partMax, pos);
                        overallMin = glm::min(overallMin, pos);
                        overallMax = glm::max(overallMax, pos);
                        localIndices.push_back(newIdx);
                    } else {
                        localIndices.push_back(it->second);
                    }
                }

                if (localVertices.empty() || localIndices.empty()) continue;

                glm::vec3 partCenter = (partMin + partMax) * 0.5f;

                AxleSubmeshSource sub;
                sub.Name = partName;
                sub.LocalTranslation = partCenter;
                sub.BoundingSize = partMax - partMin;
                sub.HalfExtent = glm::max(sub.BoundingSize * 0.5f, glm::vec3(0.02f));
                sub.PatternMode = patternMode;
                sub.TileScale = tileScale;
                sub.ColorPrimary = color1;
                sub.ColorSecondary = color2;
                sub.Vertices.reserve(localVertices.size());
                for (const auto& v : localVertices) {
                    sub.Vertices.push_back(v - partCenter);
                }
                if (!normals.empty()) {
                    sub.Normals.resize(localVertices.size());
                    for (auto [global,local]:globalToLocal) sub.Normals[local]=normals[global];
                }
                if (!uvs.empty()) {
                    sub.UVs.resize(localVertices.size());
                    for (auto [global,local]:globalToLocal) sub.UVs[local]=uvs[global];
                }
                sub.Indices = std::move(localIndices);
                submeshes.push_back(std::move(sub));
            }

            if (submeshes.size() > 1) {
                auto partsFolder = outAxlePath.parent_path() / outAxlePath.stem();
                std::error_code ec;
                std::filesystem::create_directories(partsFolder, ec);

                std::vector<AxlePackagePart> packageParts;
                packageParts.reserve(submeshes.size());

                for (const auto& sub : submeshes) {
                    auto partAxlePath = partsFolder / (sub.Name + ".axle");
                    ExportMeshToAxle(partAxlePath, sub.Name, sub.Vertices, sub.Indices, sub.PatternMode, sub.TileScale, sub.ColorPrimary, sub.ColorSecondary,sub.Normals,sub.UVs);

                    AxlePackagePart p;
                    p.Name = sub.Name;
                    p.RelativePath = (outAxlePath.stem() / (sub.Name + ".axle")).generic_string();
                    p.LocalTranslation = sub.LocalTranslation;
                    packageParts.push_back(p);
                }

                glm::vec3 totalSize = overallMax - overallMin;
                glm::vec3 totalHalf = glm::max(totalSize * 0.5f, glm::vec3(0.05f));
                return SavePackageManifest(outAxlePath, outAxlePath.stem().string(), packageParts, totalSize, totalHalf);
            }
        }

        std::vector<u32> allIndices;
        for (const auto& g : validGroups) {
            allIndices.insert(allIndices.end(), g.Triangles.begin(), g.Triangles.end());
        }

        std::string modelName = objPath.stem().string();
        return ExportMeshToAxle(outAxlePath, modelName, temp_positions, allIndices, patternMode, tileScale, color1, color2,normals,uvs);
    }

    bool AssetSerializer::ConvertGLTFToAxle(const std::filesystem::path& gltfPath,
                                            const std::filesystem::path& outAxlePath,
                                            bool splitSubmeshes,
                                            int patternMode,
                                            float tileScale,
                                            const glm::vec4& color1,
                                            const glm::vec4& color2) {
        cgltf_options options{};
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, gltfPath.string().c_str(), &data);
        if (result != cgltf_result_success) {
            AXLE_ERROR("AssetSerializer: Failed to parse glTF file '{0}'", gltfPath.string());
            return false;
        }

        result = cgltf_load_buffers(&options, data, gltfPath.string().c_str());
        if (result != cgltf_result_success) {
            AXLE_ERROR("AssetSerializer: Failed to load glTF buffers for '{0}'", gltfPath.string());
            cgltf_free(data);
            return false;
        }

        struct RawPart {
            std::string Name;
            std::vector<glm::vec3> Vertices,Normals;
            std::vector<glm::vec2> UVs;
            bool AllNormals=true,HasUVs=false;
            std::vector<u32> Indices;
        };
        std::vector<RawPart> rawParts;

        if (data->nodes_count > 0) {
            for (size_t n = 0; n < data->nodes_count; ++n) {
                const auto* node = &data->nodes[n];
                if (!node->mesh) continue;

                cgltf_float world_mat[16];
                cgltf_node_transform_world(node, world_mat);
                glm::mat4 transform = glm::make_mat4(world_mat);

                std::string partName = node->name ? node->name : (node->mesh->name ? node->mesh->name : "Part");

                RawPart part;
                part.Name = SanitizeFilename(partName);

                const auto* mesh = node->mesh;
                for (size_t p = 0; p < mesh->primitives_count; ++p) {
                    const auto& prim = mesh->primitives[p];
                    if (prim.type != cgltf_primitive_type_triangles) continue;

                    u32 vertexOffset = static_cast<u32>(part.Vertices.size());

                    for (size_t a = 0; a < prim.attributes_count; ++a) {
                        if (prim.attributes[a].type == cgltf_attribute_type_position) {
                            cgltf_accessor* acc = prim.attributes[a].data;
                            part.Vertices.reserve(part.Vertices.size() + acc->count);
                            for (size_t i = 0; i < acc->count; ++i) {
                                float pos[3] = { 0.0f, 0.0f, 0.0f };
                                cgltf_accessor_read_float(acc, i, pos, 3);
                                glm::vec4 worldPos = transform * glm::vec4(pos[0], pos[1], pos[2], 1.0f);
                                part.Vertices.push_back(glm::vec3(worldPos));
                            }
                            break;
                        }
                    }

                    const cgltf_accessor* normals=nullptr;
                    const cgltf_accessor* uvs=nullptr;
                    for (size_t a=0;a<prim.attributes_count;++a) {
                        if (prim.attributes[a].type==cgltf_attribute_type_normal) normals=prim.attributes[a].data;
                        if (prim.attributes[a].type==cgltf_attribute_type_texcoord && prim.attributes[a].index==0) uvs=prim.attributes[a].data;
                    }
                    part.AllNormals&=normals!=nullptr; part.HasUVs|=uvs!=nullptr;
                    for (size_t i=0;i<part.Vertices.size()-vertexOffset;++i) {
                        float n[3]={0,1,0},uv[2]={0,0};
                        if (normals) cgltf_accessor_read_float(normals,i,n,3);
                        if (uvs) cgltf_accessor_read_float(uvs,i,uv,2);
                        auto transformedNormal=glm::transpose(glm::inverse(glm::mat3(transform)))*glm::vec3(n[0],n[1],n[2]);
                        part.Normals.push_back(glm::length(transformedNormal)>1e-6f?glm::normalize(transformedNormal):glm::vec3(0,1,0));
                        part.UVs.emplace_back(uv[0],uv[1]);
                    }

                    if (prim.indices) {
                        cgltf_accessor* idxAcc = prim.indices;
                        part.Indices.reserve(part.Indices.size() + idxAcc->count);
                        for (size_t i = 0; i < idxAcc->count; ++i) {
                            u32 idx = static_cast<u32>(cgltf_accessor_read_index(idxAcc, i));
                            part.Indices.push_back(vertexOffset + idx);
                        }
                    } else {
                        size_t vertCount = part.Vertices.size() - vertexOffset;
                        part.Indices.reserve(part.Indices.size() + vertCount);
                        for (size_t i = 0; i < vertCount; ++i) {
                            part.Indices.push_back(vertexOffset + static_cast<u32>(i));
                        }
                    }
                }

                if (glm::determinant(glm::mat3(transform))<0) {
                    for (size_t i=0;i+2<part.Indices.size();i+=3) std::swap(part.Indices[i+1],part.Indices[i+2]);
                }
                if (!part.AllNormals) part.Normals.clear();
                if (!part.HasUVs) part.UVs.clear();
                if (!part.Vertices.empty() && !part.Indices.empty()) {
                    rawParts.push_back(std::move(part));
                }
            }
        }

        if (rawParts.empty()) {
            for (size_t m = 0; m < data->meshes_count; ++m) {
                const auto& mesh = data->meshes[m];
                std::string partName = mesh.name ? mesh.name : ("Part_" + std::to_string(m + 1));
                RawPart part;
                part.Name = SanitizeFilename(partName);

                for (size_t p = 0; p < mesh.primitives_count; ++p) {
                    const auto& prim = mesh.primitives[p];
                    if (prim.type != cgltf_primitive_type_triangles) continue;

                    u32 vertexOffset = static_cast<u32>(part.Vertices.size());

                    for (size_t a = 0; a < prim.attributes_count; ++a) {
                        if (prim.attributes[a].type == cgltf_attribute_type_position) {
                            cgltf_accessor* acc = prim.attributes[a].data;
                            part.Vertices.reserve(part.Vertices.size() + acc->count);
                            for (size_t i = 0; i < acc->count; ++i) {
                                float pos[3] = { 0.0f, 0.0f, 0.0f };
                                cgltf_accessor_read_float(acc, i, pos, 3);
                                part.Vertices.push_back(glm::vec3(pos[0], pos[1], pos[2]));
                            }
                            break;
                        }
                    }

                    const cgltf_accessor* normals=nullptr;
                    const cgltf_accessor* uvs=nullptr;
                    for (size_t a=0;a<prim.attributes_count;++a) {
                        if (prim.attributes[a].type==cgltf_attribute_type_normal) normals=prim.attributes[a].data;
                        if (prim.attributes[a].type==cgltf_attribute_type_texcoord && prim.attributes[a].index==0) uvs=prim.attributes[a].data;
                    }
                    part.AllNormals&=normals!=nullptr; part.HasUVs|=uvs!=nullptr;
                    for (size_t i=0;i<part.Vertices.size()-vertexOffset;++i) {
                        float n[3]={0,1,0},uv[2]={0,0};
                        if (normals) cgltf_accessor_read_float(normals,i,n,3);
                        if (uvs) cgltf_accessor_read_float(uvs,i,uv,2);
                        auto transformedNormal=glm::vec3(n[0],n[1],n[2]);
                        part.Normals.push_back(glm::length(transformedNormal)>1e-6f?glm::normalize(transformedNormal):glm::vec3(0,1,0));
                        part.UVs.emplace_back(uv[0],uv[1]);
                    }

                    if (prim.indices) {
                        cgltf_accessor* idxAcc = prim.indices;
                        part.Indices.reserve(part.Indices.size() + idxAcc->count);
                        for (size_t i = 0; i < idxAcc->count; ++i) {
                            u32 idx = static_cast<u32>(cgltf_accessor_read_index(idxAcc, i));
                            part.Indices.push_back(vertexOffset + idx);
                        }
                    } else {
                        size_t vertCount = part.Vertices.size() - vertexOffset;
                        part.Indices.reserve(part.Indices.size() + vertCount);
                        for (size_t i = 0; i < vertCount; ++i) {
                            part.Indices.push_back(vertexOffset + static_cast<u32>(i));
                        }
                    }
                }

                if (!part.AllNormals) part.Normals.clear();
                if (!part.HasUVs) part.UVs.clear();
                if (!part.Vertices.empty() && !part.Indices.empty()) {
                    rawParts.push_back(std::move(part));
                }
            }
        }

        cgltf_free(data);

        if (rawParts.empty()) {
            AXLE_WARN("AssetSerializer: glTF file '{0}' contains no triangle geometry", gltfPath.string());
            return false;
        }

        if (splitSubmeshes && rawParts.size() > 1) {
            std::unordered_map<std::string, int> nameCounts;
            glm::vec3 overallMin( 1e9f);
            glm::vec3 overallMax(-1e9f);
            std::vector<AxleSubmeshSource> submeshes;

            for (auto& raw : rawParts) {
                if (nameCounts[raw.Name]++ > 0) {
                    raw.Name += "_" + std::to_string(nameCounts[raw.Name]);
                }

                glm::vec3 partMin( 1e9f);
                glm::vec3 partMax(-1e9f);
                for (const auto& v : raw.Vertices) {
                    partMin = glm::min(partMin, v);
                    partMax = glm::max(partMax, v);
                    overallMin = glm::min(overallMin, v);
                    overallMax = glm::max(overallMax, v);
                }

                glm::vec3 partCenter = (partMin + partMax) * 0.5f;

                AxleSubmeshSource sub;
                sub.Name = raw.Name;
                sub.LocalTranslation = partCenter;
                sub.BoundingSize = partMax - partMin;
                sub.HalfExtent = glm::max(sub.BoundingSize * 0.5f, glm::vec3(0.02f));
                sub.PatternMode = patternMode;
                sub.TileScale = tileScale;
                sub.ColorPrimary = color1;
                sub.ColorSecondary = color2;
                sub.Vertices.reserve(raw.Vertices.size());
                for (const auto& v : raw.Vertices) {
                    sub.Vertices.push_back(v - partCenter);
                }
                sub.Normals=std::move(raw.Normals); sub.UVs=std::move(raw.UVs);
                sub.Indices = std::move(raw.Indices);
                submeshes.push_back(std::move(sub));
            }

            if (submeshes.size() > 1) {
                auto partsFolder = outAxlePath.parent_path() / outAxlePath.stem();
                std::error_code ec;
                std::filesystem::create_directories(partsFolder, ec);

                std::vector<AxlePackagePart> packageParts;
                packageParts.reserve(submeshes.size());

                for (const auto& sub : submeshes) {
                    auto partAxlePath = partsFolder / (sub.Name + ".axle");
                    ExportMeshToAxle(partAxlePath, sub.Name, sub.Vertices, sub.Indices, sub.PatternMode, sub.TileScale, sub.ColorPrimary, sub.ColorSecondary,sub.Normals,sub.UVs);

                    AxlePackagePart p;
                    p.Name = sub.Name;
                    p.RelativePath = (outAxlePath.stem() / (sub.Name + ".axle")).generic_string();
                    p.LocalTranslation = sub.LocalTranslation;
                    packageParts.push_back(p);
                }

                glm::vec3 totalSize = overallMax - overallMin;
                glm::vec3 totalHalf = glm::max(totalSize * 0.5f, glm::vec3(0.05f));
                return SavePackageManifest(outAxlePath, outAxlePath.stem().string(), packageParts, totalSize, totalHalf);
            }
        }

        std::vector<glm::vec3> allVertices,allNormals;
        std::vector<glm::vec2> allUVs;
        bool completeNormals=true,completeUVs=true;
        std::vector<u32> allIndices;
        for (const auto& part : rawParts) {
            u32 vOffset = static_cast<u32>(allVertices.size());
            allVertices.insert(allVertices.end(), part.Vertices.begin(), part.Vertices.end());
            completeNormals&=part.Normals.size()==part.Vertices.size(); completeUVs&=part.UVs.size()==part.Vertices.size();
            allNormals.insert(allNormals.end(),part.Normals.begin(),part.Normals.end());
            allUVs.insert(allUVs.end(),part.UVs.begin(),part.UVs.end());
            for (u32 idx : part.Indices) {
                allIndices.push_back(vOffset + idx);
            }
        }

        if (!completeNormals) allNormals.clear();
        if (!completeUVs) allUVs.clear();
        std::string modelName = gltfPath.stem().string();
        return ExportMeshToAxle(outAxlePath, modelName, allVertices, allIndices, patternMode, tileScale, color1, color2,allNormals,allUVs);
    }

    bool AssetSerializer::ConvertFBXToAxle(const std::filesystem::path& fbxPath,
                                           const std::filesystem::path& outAxlePath,
                                           bool splitSubmeshes,
                                           int patternMode,
                                           float tileScale,
                                           const glm::vec4& color1,
                                           const glm::vec4& color2) {
        ufbx_load_opts opts{};
        opts.target_axes = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters = 1.0f;
        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(fbxPath.string().c_str(), &opts, &error);
        if (!scene) {
            AXLE_ERROR("AssetSerializer: Failed to load FBX '{0}': {1}", fbxPath.string(), error.description.data);
            return false;
        }

        struct RawPart {
            std::string Name;
            std::vector<glm::vec3> Vertices,Normals;
            std::vector<glm::vec2> UVs;
            std::vector<u32> Indices;
        };
        std::vector<RawPart> rawParts;

        if (scene->nodes.count > 0) {
            for (size_t i = 0; i < scene->nodes.count; ++i) {
                ufbx_node* node = scene->nodes.data[i];
                if (!node || !node->mesh || node->mesh->num_triangles == 0) continue;

                ufbx_mesh* mesh = node->mesh;
                std::string partName = node->name.length > 0 ? SanitizeFilename(node->name.data)
                                     : (mesh->name.length > 0 ? SanitizeFilename(mesh->name.data) : "Part");

                RawPart part;
                part.Name = partName;
                ufbx_matrix normalMatrix{};
                normalMatrix=ufbx_matrix_for_normals(&node->geometry_to_world);
                std::vector<uint32_t> triangleCorners(mesh->max_face_triangles*3);
                for (size_t f=0;f<mesh->faces.count;++f) {
                    size_t triangles=ufbx_triangulate_face(triangleCorners.data(),triangleCorners.size(),mesh,mesh->faces.data[f]);
                    for (size_t i=0;i<triangles*3;++i) {
                        auto corner=triangleCorners[i];
                        auto position=ufbx_get_vertex_vec3(&mesh->vertex_position,corner);
                        position=ufbx_transform_position(&node->geometry_to_world,position);
                        part.Vertices.emplace_back(float(position.x),float(position.y),float(position.z));
                        if (mesh->vertex_normal.exists) {
                            auto normal=ufbx_get_vertex_vec3(&mesh->vertex_normal,corner);
                            normal=ufbx_transform_direction(&normalMatrix,normal);
                            if (ufbx_matrix_determinant(&node->geometry_to_world)<0) { normal.x=-normal.x; normal.y=-normal.y; normal.z=-normal.z; }
                            auto n=glm::vec3(float(normal.x),float(normal.y),float(normal.z));
                            part.Normals.push_back(glm::length(n)>1e-6f?glm::normalize(n):glm::vec3(0,1,0));
                        }
                        if (mesh->vertex_uv.exists) {
                            auto uv=ufbx_get_vertex_vec2(&mesh->vertex_uv,corner);
                            part.UVs.emplace_back(float(uv.x),float(uv.y));
                        }
                        part.Indices.push_back(static_cast<u32>(part.Indices.size()));
                    }
                }

                if (ufbx_matrix_determinant(&node->geometry_to_world)<0) {
                    for (size_t i=0;i+2<part.Indices.size();i+=3) std::swap(part.Indices[i+1],part.Indices[i+2]);
                }
                if (!part.Vertices.empty() && !part.Indices.empty()) {
                    rawParts.push_back(std::move(part));
                }
            }
        }

        if (rawParts.empty()) {
            for (size_t m = 0; m < scene->meshes.count; ++m) {
                ufbx_mesh* mesh = scene->meshes.data[m];
                if (!mesh || mesh->num_triangles == 0) continue;

                std::string partName = mesh->name.length > 0 ? SanitizeFilename(mesh->name.data) : ("Part_" + std::to_string(m + 1));
                RawPart part;
                part.Name = partName;
                ufbx_matrix normalMatrix{};
                std::vector<uint32_t> triangleCorners(mesh->max_face_triangles*3);
                for (size_t f=0;f<mesh->faces.count;++f) {
                    size_t triangles=ufbx_triangulate_face(triangleCorners.data(),triangleCorners.size(),mesh,mesh->faces.data[f]);
                    for (size_t i=0;i<triangles*3;++i) {
                        auto corner=triangleCorners[i];
                        auto position=ufbx_get_vertex_vec3(&mesh->vertex_position,corner);
                        part.Vertices.emplace_back(float(position.x),float(position.y),float(position.z));
                        if (mesh->vertex_normal.exists) {
                            auto normal=ufbx_get_vertex_vec3(&mesh->vertex_normal,corner);
                            auto n=glm::vec3(float(normal.x),float(normal.y),float(normal.z));
                            part.Normals.push_back(glm::length(n)>1e-6f?glm::normalize(n):glm::vec3(0,1,0));
                        }
                        if (mesh->vertex_uv.exists) {
                            auto uv=ufbx_get_vertex_vec2(&mesh->vertex_uv,corner);
                            part.UVs.emplace_back(float(uv.x),float(uv.y));
                        }
                        part.Indices.push_back(static_cast<u32>(part.Indices.size()));
                    }
                }

                if (!part.Vertices.empty() && !part.Indices.empty()) {
                    rawParts.push_back(std::move(part));
                }
            }
        }

        ufbx_free_scene(scene);

        if (rawParts.empty()) {
            AXLE_WARN("AssetSerializer: FBX file '{0}' contains no triangle geometry", fbxPath.string());
            return false;
        }

        if (splitSubmeshes && rawParts.size() > 1) {
            std::unordered_map<std::string, int> nameCounts;
            glm::vec3 overallMin( 1e9f);
            glm::vec3 overallMax(-1e9f);
            std::vector<AxleSubmeshSource> submeshes;

            for (auto& raw : rawParts) {
                if (nameCounts[raw.Name]++ > 0) {
                    raw.Name += "_" + std::to_string(nameCounts[raw.Name]);
                }

                glm::vec3 partMin( 1e9f);
                glm::vec3 partMax(-1e9f);
                for (const auto& v : raw.Vertices) {
                    partMin = glm::min(partMin, v);
                    partMax = glm::max(partMax, v);
                    overallMin = glm::min(overallMin, v);
                    overallMax = glm::max(overallMax, v);
                }

                glm::vec3 partCenter = (partMin + partMax) * 0.5f;

                AxleSubmeshSource sub;
                sub.Name = raw.Name;
                sub.LocalTranslation = partCenter;
                sub.BoundingSize = partMax - partMin;
                sub.HalfExtent = glm::max(sub.BoundingSize * 0.5f, glm::vec3(0.02f));
                sub.PatternMode = patternMode;
                sub.TileScale = tileScale;
                sub.ColorPrimary = color1;
                sub.ColorSecondary = color2;
                sub.Vertices.reserve(raw.Vertices.size());
                for (const auto& v : raw.Vertices) {
                    sub.Vertices.push_back(v - partCenter);
                }
                sub.Normals=std::move(raw.Normals); sub.UVs=std::move(raw.UVs);
                sub.Indices = std::move(raw.Indices);
                submeshes.push_back(std::move(sub));
            }

            if (submeshes.size() > 1) {
                auto partsFolder = outAxlePath.parent_path() / outAxlePath.stem();
                std::error_code ec;
                std::filesystem::create_directories(partsFolder, ec);

                std::vector<AxlePackagePart> packageParts;
                packageParts.reserve(submeshes.size());

                for (const auto& sub : submeshes) {
                    auto partAxlePath = partsFolder / (sub.Name + ".axle");
                    ExportMeshToAxle(partAxlePath, sub.Name, sub.Vertices, sub.Indices, sub.PatternMode, sub.TileScale, sub.ColorPrimary, sub.ColorSecondary,sub.Normals,sub.UVs);

                    AxlePackagePart p;
                    p.Name = sub.Name;
                    p.RelativePath = (outAxlePath.stem() / (sub.Name + ".axle")).generic_string();
                    p.LocalTranslation = sub.LocalTranslation;
                    packageParts.push_back(p);
                }

                glm::vec3 totalSize = overallMax - overallMin;
                glm::vec3 totalHalf = glm::max(totalSize * 0.5f, glm::vec3(0.05f));
                return SavePackageManifest(outAxlePath, outAxlePath.stem().string(), packageParts, totalSize, totalHalf);
            }
        }

        std::vector<glm::vec3> allVertices,allNormals;
        std::vector<glm::vec2> allUVs;
        bool completeNormals=true,completeUVs=true;
        std::vector<u32> allIndices;
        for (const auto& part : rawParts) {
            u32 vOffset = static_cast<u32>(allVertices.size());
            allVertices.insert(allVertices.end(), part.Vertices.begin(), part.Vertices.end());
            completeNormals&=part.Normals.size()==part.Vertices.size(); completeUVs&=part.UVs.size()==part.Vertices.size();
            allNormals.insert(allNormals.end(),part.Normals.begin(),part.Normals.end());
            allUVs.insert(allUVs.end(),part.UVs.begin(),part.UVs.end());
            for (u32 idx : part.Indices) {
                allIndices.push_back(vOffset + idx);
            }
        }

        if (!completeNormals) allNormals.clear();
        if (!completeUVs) allUVs.clear();
        std::string modelName = fbxPath.stem().string();
        return ExportMeshToAxle(outAxlePath, modelName, allVertices, allIndices, patternMode, tileScale, color1, color2,allNormals,allUVs);
    }

    bool AssetSerializer::AutoConvertModel(const std::filesystem::path& modelPath,
                                           const std::filesystem::path& outAxlePath,
                                           bool splitSubmeshes,
                                           int patternMode,
                                           float tileScale,
                                           const glm::vec4& color1,
                                           const glm::vec4& color2) {
        std::string ext = modelPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });

        if (ext == ".obj") {
            return ConvertOBJToAxle(modelPath, outAxlePath, splitSubmeshes, patternMode, tileScale, color1, color2);
        }
        if (ext == ".gltf" || ext == ".glb") {
            return ConvertGLTFToAxle(modelPath, outAxlePath, splitSubmeshes, patternMode, tileScale, color1, color2);
        }
        if (ext == ".fbx") {
            return ConvertFBXToAxle(modelPath, outAxlePath, splitSubmeshes, patternMode, tileScale, color1, color2);
        }

        AXLE_ERROR("AssetSerializer: Unsupported model format '{0}'", ext);
        return false;
    }

    AssetSerializer::AssetCompileResult AssetSerializer::CompileAssetDirectory(const std::filesystem::path& assetDirectory, bool forceRebakeAll) {
        AssetCompileResult result;
        if (!std::filesystem::exists(assetDirectory)) {
            AXLE_WARN("AssetSerializer: Asset directory '{0}' does not exist", PathToUtf8(assetDirectory));
            return result;
        }

        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetDirectory, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;

            auto path = entry.path();
            auto ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });

            if (ext == ".axle") {
                // Check if package manifest
                std::ifstream testFile(path);
                const std::string header((std::istreambuf_iterator<char>(testFile)), {});
                // Assembly is authored scene data; compiling it as mesh data
                // would discard its native component sections.
                if (header.starts_with("AssetType: Assembly") || header.find("\nAssetType: Assembly") != std::string::npos) continue;
                bool isPackage = header.starts_with("Package:") || header.find("\nPackage:") != std::string::npos;

                if (isPackage) {
                    auto pkg = LoadPackage(path);
                    if (pkg.Success) {
                        result.RecompiledCount++;
                    } else {
                        result.FailedCount++;
                    }
                } else {
                    std::filesystem::path cachePath = GetCachePathFor(path);
                    bool needRebake = forceRebakeAll;

                    if (!needRebake) {
                        if (!std::filesystem::exists(cachePath, ec)) {
                            needRebake = true;
                        } else {
                            auto textWriteTime = std::filesystem::last_write_time(path, ec);
                            auto cacheWriteTime = std::filesystem::last_write_time(cachePath, ec);
                            if (ec || textWriteTime > cacheWriteTime) {
                                needRebake = true;
                            }
                        }
                    }

                    if (needRebake) {
                        AxleMeshSource source;
                        if (LoadFromAxleText(path, source)) {
                            if (SaveToAxleBinary(cachePath, source)) {
                                result.RecompiledCount++;
                            } else {
                                result.FailedCount++;
                            }
                        } else {
                            result.FailedCount++;
                        }
                    } else {
                        result.UpToDateCount++;
                    }
                }
            }
        }

        AXLE_INFO("AssetSerializer: Compilation scan completed for '{0}' (Recompiled: {1}, Up-to-date: {2}, Failed: {3})",
            PathToUtf8(assetDirectory), result.RecompiledCount, result.UpToDateCount, result.FailedCount);

        return result;
    }

    bool AssetSerializer::ConvertAxleToOBJ(const std::filesystem::path& axlePath,
                                           const std::filesystem::path& outObjPath) {
        if (!std::filesystem::exists(axlePath)) {
            AXLE_ERROR("AssetSerializer: Cannot export to OBJ, file does not exist: '{0}'", axlePath.string());
            return false;
        }

        std::filesystem::path targetObjPath = outObjPath.empty()
            ? (axlePath.parent_path() / (axlePath.stem().string() + ".obj"))
            : outObjPath;
        std::filesystem::path targetMtlPath = targetObjPath.parent_path() / (targetObjPath.stem().string() + ".mtl");

        std::string ext = axlePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        AxleLoadedPackage pkg;
        if (ext == ".axlebin") {
            AxleMeshSource source;
            if (!LoadFromAxleBinary(axlePath, source)) {
                AXLE_ERROR("AssetSerializer: Failed to read binary cache '{0}'", axlePath.string());
                return false;
            }
            pkg.Name = source.Name;
            pkg.BoundingSize = source.BoundingSize;
            pkg.HalfExtent = source.HalfExtent;
            pkg.IsPackage = false;
            pkg.Success = true;

            AxlePackagePart part;
            part.Name = source.Name;
            part.RelativePath = axlePath.filename().string();
            part.LoadedMesh.SourceData = source;
            part.LoadedMesh.Success = true;
            pkg.Parts.push_back(part);
        } else {
            pkg = LoadPackage(axlePath);
            if (!pkg.Success || pkg.Parts.empty()) {
                AXLE_ERROR("AssetSerializer: Failed to load .axle package or mesh '{0}'", axlePath.string());
                return false;
            }
        }

        // Export Wavefront Material Library (.mtl)
        std::ofstream mtlFile(targetMtlPath);
        if (mtlFile.is_open()) {
            mtlFile << "# Wavefront MTL Material Library generated by AxleEngine\n";
            mtlFile << "# Source: " << axlePath.filename().string() << "\n\n";
            mtlFile << std::fixed << std::setprecision(4);

            for (size_t pIdx = 0; pIdx < pkg.Parts.size(); ++pIdx) {
                const auto& part = pkg.Parts[pIdx];
                const auto& src = part.LoadedMesh.SourceData;
                std::string safePartName = SanitizeFilename(part.Name.empty() ? ("Part_" + std::to_string(pIdx)) : part.Name);
                std::string matName = "Material_" + safePartName;

                mtlFile << "newmtl " << matName << "\n";
                mtlFile << "Ns 32.0000\n";
                mtlFile << "Ka 0.2000 0.2000 0.2000\n";
                mtlFile << "Kd " << src.ColorPrimary.r << " " << src.ColorPrimary.g << " " << src.ColorPrimary.b << "\n";
                mtlFile << "Ks 0.5000 0.5000 0.5000\n";
                mtlFile << "d " << src.ColorPrimary.a << "\n";
                mtlFile << "illum 2\n\n";
            }
            mtlFile.close();
        }

        // Export Wavefront OBJ (.obj)
        std::ofstream objFile(targetObjPath);
        if (!objFile.is_open()) {
            AXLE_ERROR("AssetSerializer: Failed to create OBJ file: '{0}'", targetObjPath.string());
            return false;
        }

        objFile << "# Wavefront OBJ exported by AxleEngine\n";
        objFile << "# Model: " << pkg.Name << "\n";
        objFile << "# Source: " << axlePath.filename().string() << "\n";
        objFile << "mtllib " << targetMtlPath.filename().string() << "\n\n";
        objFile << std::fixed << std::setprecision(6);

        u32 globalVertexOffset = 0;
        size_t totalExportedFaces = 0;
        size_t totalExportedVerts = 0;

        for (size_t pIdx = 0; pIdx < pkg.Parts.size(); ++pIdx) {
            const auto& part = pkg.Parts[pIdx];
            const auto& src = part.LoadedMesh.SourceData;

            if (src.Vertices.empty() || src.Indices.empty()) {
                continue;
            }

            std::string safePartName = SanitizeFilename(part.Name.empty() ? ("Part_" + std::to_string(pIdx)) : part.Name);
            std::string matName = "Material_" + safePartName;

            glm::mat4 partTransform(1.0f);
            if (pkg.IsPackage) {
                partTransform = glm::translate(glm::mat4(1.0f), part.LocalTranslation)
                              * glm::mat4_cast(part.LocalRotation)
                              * glm::scale(glm::mat4(1.0f), part.LocalScale);
            }

            std::vector<glm::vec3> transformedVerts;
            transformedVerts.reserve(src.Vertices.size());
            for (const auto& v : src.Vertices) {
                transformedVerts.push_back(glm::vec3(partTransform * glm::vec4(v, 1.0f)));
            }

            // Compute smooth per-vertex normals
            std::vector<glm::vec3> normals(transformedVerts.size(), glm::vec3(0.0f));
            for (size_t i = 0; i + 2 < src.Indices.size(); i += 3) {
                u32 i0 = src.Indices[i];
                u32 i1 = src.Indices[i + 1];
                u32 i2 = src.Indices[i + 2];
                if (i0 < transformedVerts.size() && i1 < transformedVerts.size() && i2 < transformedVerts.size()) {
                    glm::vec3 e1 = transformedVerts[i1] - transformedVerts[i0];
                    glm::vec3 e2 = transformedVerts[i2] - transformedVerts[i0];
                    glm::vec3 fn = glm::cross(e1, e2);
                    normals[i0] += fn;
                    normals[i1] += fn;
                    normals[i2] += fn;
                }
            }
            for (auto& n : normals) {
                float len = glm::length(n);
                if (len > 1e-6f) n /= len;
                else n = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            objFile << "o " << safePartName << "\n";
            objFile << "g " << safePartName << "\n";
            objFile << "usemtl " << matName << "\n";
            objFile << "s 1\n";

            // Positions
            for (const auto& v : transformedVerts) {
                objFile << "v " << v.x << " " << v.y << " " << v.z << "\n";
            }

            // Normals
            for (const auto& n : normals) {
                objFile << "vn " << n.x << " " << n.y << " " << n.z << "\n";
            }

            // Faces (1-based indices in Wavefront OBJ format)
            for (size_t i = 0; i + 2 < src.Indices.size(); i += 3) {
                u32 i0 = src.Indices[i] + globalVertexOffset + 1;
                u32 i1 = src.Indices[i + 1] + globalVertexOffset + 1;
                u32 i2 = src.Indices[i + 2] + globalVertexOffset + 1;
                objFile << "f " << i0 << "//" << i0 << " " << i1 << "//" << i1 << " " << i2 << "//" << i2 << "\n";
            }
            objFile << "\n";

            globalVertexOffset += static_cast<u32>(transformedVerts.size());
            totalExportedVerts += transformedVerts.size();
            totalExportedFaces += src.Indices.size() / 3;
        }

        objFile.close();

        AXLE_INFO("AssetSerializer: Exported '{0}' to Wavefront OBJ '{1}' ({2} parts, {3} vertices, {4} faces)",
            axlePath.filename().string(), targetObjPath.filename().string(), pkg.Parts.size(), totalExportedVerts, totalExportedFaces);

        return totalExportedVerts > 0;
    }

    bool AssetSerializer::ConvertAxlePackagePartsToOBJ(const std::filesystem::path& manifestPath,
                                                       const std::filesystem::path& outDir) {
        if (!std::filesystem::exists(manifestPath)) return false;
        auto pkg = LoadPackage(manifestPath);
        if (!pkg.Success || pkg.Parts.empty()) return false;

        std::filesystem::path targetDir = outDir.empty()
            ? (manifestPath.parent_path() / (manifestPath.stem().string() + "_obj_parts"))
            : outDir;
        std::error_code ec;
        std::filesystem::create_directories(targetDir, ec);

        size_t exportedCount = 0;
        for (size_t i = 0; i < pkg.Parts.size(); ++i) {
            const auto& part = pkg.Parts[i];
            const auto& src = part.LoadedMesh.SourceData;
            if (src.Vertices.empty() || src.Indices.empty()) continue;

            std::string safeName = SanitizeFilename(part.Name.empty() ? ("Part_" + std::to_string(i)) : part.Name);
            auto outPartObj = targetDir / (safeName + ".obj");
            auto outPartMtl = targetDir / (safeName + ".mtl");

            std::ofstream mtl(outPartMtl);
            if (mtl.is_open()) {
                mtl << "# Wavefront MTL Material Library generated by AxleEngine\n";
                mtl << std::fixed << std::setprecision(4);
                mtl << "newmtl Material_" << safeName << "\n";
                mtl << "Ns 32.0000\n";
                mtl << "Ka 0.2000 0.2000 0.2000\n";
                mtl << "Kd " << src.ColorPrimary.r << " " << src.ColorPrimary.g << " " << src.ColorPrimary.b << "\n";
                mtl << "Ks 0.5000 0.5000 0.5000\n";
                mtl << "d " << src.ColorPrimary.a << "\n";
                mtl << "illum 2\n";
                mtl.close();
            }

            std::ofstream obj(outPartObj);
            if (obj.is_open()) {
                obj << "# Wavefront OBJ exported by AxleEngine\n";
                obj << "mtllib " << outPartMtl.filename().string() << "\n\n";
                obj << "o " << safeName << "\n";
                obj << "g " << safeName << "\n";
                obj << "usemtl Material_" << safeName << "\n";
                obj << "s 1\n";
                obj << std::fixed << std::setprecision(6);

                // Compute normals
                std::vector<glm::vec3> normals(src.Vertices.size(), glm::vec3(0.0f));
                for (size_t f = 0; f + 2 < src.Indices.size(); f += 3) {
                    u32 i0 = src.Indices[f];
                    u32 i1 = src.Indices[f + 1];
                    u32 i2 = src.Indices[f + 2];
                    if (i0 < src.Vertices.size() && i1 < src.Vertices.size() && i2 < src.Vertices.size()) {
                        glm::vec3 fn = glm::cross(src.Vertices[i1] - src.Vertices[i0],
                                                  src.Vertices[i2] - src.Vertices[i0]);
                        normals[i0] += fn;
                        normals[i1] += fn;
                        normals[i2] += fn;
                    }
                }
                for (auto& n : normals) {
                    float len = glm::length(n);
                    if (len > 1e-6f) n /= len;
                    else n = glm::vec3(0.0f, 1.0f, 0.0f);
                }

                for (const auto& v : src.Vertices) {
                    obj << "v " << v.x << " " << v.y << " " << v.z << "\n";
                }
                for (const auto& n : normals) {
                    obj << "vn " << n.x << " " << n.y << " " << n.z << "\n";
                }
                for (size_t f = 0; f + 2 < src.Indices.size(); f += 3) {
                    u32 i0 = src.Indices[f] + 1;
                    u32 i1 = src.Indices[f + 1] + 1;
                    u32 i2 = src.Indices[f + 2] + 1;
                    obj << "f " << i0 << "//" << i0 << " " << i1 << "//" << i1 << " " << i2 << "//" << i2 << "\n";
                }
                obj.close();
                exportedCount++;
            }
        }

        AXLE_INFO("AssetSerializer: Exported {0} separate subparts to directory '{1}'", exportedCount, targetDir.string());
        return exportedCount > 0;
    }

}
