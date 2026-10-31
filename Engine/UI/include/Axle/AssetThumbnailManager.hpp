#pragma once
#include <Axle/Types.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/VertexArray.hpp>
#include <glm/glm.hpp>
#include <filesystem>
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>

namespace Axle {

    class ImGuiLayer;
    namespace RHI { class RHITexture; }

    struct AssetCachedMeta {
        std::string Name;
        size_t VertexCount = 0;
        size_t TriangleCount = 0;
        glm::vec3 BoundingSize{ 1.0f };
        glm::vec3 Center{ 0.0f };
        float MaxDimension = 1.0f;
        glm::vec4 ColorPrimary{ 0.85f, 0.88f, 0.92f, 1.0f };
        glm::vec4 ColorSecondary{ 0.65f, 0.70f, 0.76f, 1.0f };
        int PatternMode = 1;
        float TileScale = 8.0f;
        bool IsPackage = false;
        size_t SubPartCount = 1;
    };

    class AssetThumbnailManager {
    public:
        AssetThumbnailManager();
        ~AssetThumbnailManager();

        // Non-copyable
        AssetThumbnailManager(const AssetThumbnailManager&) = delete;
        AssetThumbnailManager& operator=(const AssetThumbnailManager&) = delete;

        void SetImGuiLayer(ImGuiLayer* layer) { m_ImGuiLayer = layer; }

        // Returns GPU Texture Descriptor / GL Texture ID (or 0 if pending/not available)
        uint64_t GetOrCreateThumbnail(const std::filesystem::path& path);

        // Retrieve native aspect ratio/dimensions of the loaded texture thumbnail
        glm::vec2 GetThumbnailDimensions(const std::filesystem::path& path) const;

        // Retrieve cached metadata (if already loaded/generated)
        const AssetCachedMeta* GetCachedMeta(const std::filesystem::path& path) const;

        // Clear all thumbnail textures and caches
        void Clear();

        // Update tick for queued thumbnail rendering (called each frame)
        void OnUpdate();

    private:
        void InitRenderer();
        bool RenderThumbnail(const std::filesystem::path& path, u32& outTexID, AssetCachedMeta& outMeta);

    private:
        ImGuiLayer* m_ImGuiLayer = nullptr;
        u32 m_FBO = 0;
        u32 m_RBO = 0;
        std::unique_ptr<Shader> m_Shader;
        std::shared_ptr<VertexArray> m_PreviewSphereMesh;

        std::unordered_map<std::string, uint64_t> m_ThumbnailCache;
        std::unordered_map<std::string, glm::vec2> m_ThumbnailDimensions;
        std::unordered_map<std::string, std::shared_ptr<RHI::RHITexture>> m_RHIThumbnails;
        std::unordered_map<std::string, AssetCachedMeta> m_MetaCache;
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_LoadedTextures;
        std::vector<u32> m_GeneratedTextures;
        std::vector<std::filesystem::path> m_PendingQueue;

        const u32 m_ThumbnailResolution = 128;
        const size_t m_MaxGenerationsPerFrame = 6;
    };

}
