#pragma once
#include <Axle/Types.hpp>
#include <Axle/VertexArray.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>

namespace Axle {

    struct AxleMeshSource {
        std::string Name = "UnnamedMesh";
        int PatternMode = 1;
        float TileScale = 2.0f;
        glm::vec4 ColorPrimary{ 0.95f, 0.45f, 0.15f, 1.0f };
        glm::vec4 ColorSecondary{ 0.16f, 0.20f, 0.26f, 1.0f };

        std::vector<glm::vec3> Vertices;
        std::vector<u32> Indices;

        glm::vec3 BoundingSize{ 1.0f };
        glm::vec3 HalfExtent{ 0.5f };
    };

    struct AxleLoadedMesh {
        std::shared_ptr<VertexArray> VAO;
        AxleMeshSource SourceData;
        bool LoadedFromCache = false;
        bool Success = false;
    };

    class AssetSerializer {
    public:
        // Structured text format (.axle) - AI & Human Editable
        static bool SaveToAxleText(const std::filesystem::path& path, const AxleMeshSource& mesh);
        static bool LoadFromAxleText(const std::filesystem::path& path, AxleMeshSource& outMesh);

        // Binary cache format (.axlebin) - Ultra-fast 1ms reading
        static bool SaveToAxleBinary(const std::filesystem::path& binaryPath, const AxleMeshSource& mesh);
        static bool LoadFromAxleBinary(const std::filesystem::path& binaryPath, AxleMeshSource& outMesh);

        // High-level smart loader with automatic cache validation & re-baking
        static AxleLoadedMesh LoadMesh(const std::filesystem::path& axleTextPath);

        // Export mesh data to .axle text file & binary cache
        static bool ExportMeshToAxle(const std::filesystem::path& axleTextPath,
                                     const std::string& name,
                                     const std::vector<glm::vec3>& vertices,
                                     const std::vector<u32>& indices,
                                     int patternMode = 1,
                                     float tileScale = 2.0f,
                                     const glm::vec4& color1 = glm::vec4(0.95f, 0.45f, 0.15f, 1.0f),
                                     const glm::vec4& color2 = glm::vec4(0.16f, 0.20f, 0.26f, 1.0f));

        // Helper to locate or create cache path in .cache/ subfolder
        static std::filesystem::path GetCachePathFor(const std::filesystem::path& axleTextPath);

        // Convert an OBJ model into .axle structured text + auto-baked .axlebin cache
        static bool ConvertOBJToAxle(const std::filesystem::path& objPath,
                                     const std::filesystem::path& outAxlePath,
                                     int patternMode = 1,
                                     float tileScale = 2.0f,
                                     const glm::vec4& color1 = glm::vec4(0.95f, 0.45f, 0.15f, 1.0f),
                                     const glm::vec4& color2 = glm::vec4(0.16f, 0.20f, 0.26f, 1.0f));

        // Convert glTF 2.0 (.gltf / .glb) into .axle structured text + auto-baked .axlebin cache
        static bool ConvertGLTFToAxle(const std::filesystem::path& gltfPath,
                                      const std::filesystem::path& outAxlePath,
                                      int patternMode = 1,
                                      float tileScale = 2.0f,
                                      const glm::vec4& color1 = glm::vec4(0.95f, 0.45f, 0.15f, 1.0f),
                                      const glm::vec4& color2 = glm::vec4(0.16f, 0.20f, 0.26f, 1.0f));

        // Convert FBX model (.fbx) into .axle structured text + auto-baked .axlebin cache
        static bool ConvertFBXToAxle(const std::filesystem::path& fbxPath,
                                     const std::filesystem::path& outAxlePath,
                                     int patternMode = 1,
                                     float tileScale = 2.0f,
                                     const glm::vec4& color1 = glm::vec4(0.95f, 0.45f, 0.15f, 1.0f),
                                     const glm::vec4& color2 = glm::vec4(0.16f, 0.20f, 0.26f, 1.0f));

        // Unified 3D auto-converter supporting .obj, .gltf, .glb, and .fbx
        static bool AutoConvertModel(const std::filesystem::path& modelPath,
                                     const std::filesystem::path& outAxlePath,
                                     int patternMode = 1,
                                     float tileScale = 2.0f,
                                     const glm::vec4& color1 = glm::vec4(0.95f, 0.45f, 0.15f, 1.0f),
                                     const glm::vec4& color2 = glm::vec4(0.16f, 0.20f, 0.26f, 1.0f));
    };

}
