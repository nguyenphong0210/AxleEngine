#pragma once
#include <Axle/Types.hpp>
#include <Axle/GuiTextureLease.hpp>
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
    class Framebuffer;
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

        void SetImGuiLayer(ImGuiLayer* layer);

        // Returns GPU Texture Descriptor / GL Texture ID (or 0 if pending/not available)
        uint64_t GetOrCreateThumbnail(const std::filesystem::path& path);

        // Retrieve native aspect ratio/dimensions of the loaded texture thumbnail
        glm::vec2 GetThumbnailDimensions(const std::filesystem::path& path) const;

        // Retrieve cached metadata (if already loaded/generated)
        const AssetCachedMeta* GetCachedMeta(const std::filesystem::path& path) const;

        // Clear caches now; retire GPU textures after the current UI submission.
        void Clear();

        // Invalidate and remove a specific thumbnail from cache so it re-renders
        void InvalidateThumbnail(const std::filesystem::path& path);
        static void Invalidate(const std::filesystem::path& path);
        static uint64_t GetPreview(const std::filesystem::path& path);

        // Update tick for queued thumbnail rendering (called each frame)
        void OnUpdate();
        size_t GetCachedCount() const { return m_ThumbnailCache.size(); }
        size_t GetPendingCount() const { return m_PendingQueue.size(); }

    private:
        void RetireTextures(std::vector<uint64_t> handles,
                            std::vector<std::shared_ptr<RHI::RHITexture>> textures,
                            std::vector<std::shared_ptr<Texture2D>> loaded,
                            std::vector<u32> generated);
        bool RenderThumbnail(const std::filesystem::path& path, uint64_t& outTexHandle, AssetCachedMeta& outMeta);
        uint64_t GetRevision(const std::filesystem::path& path) const;

    private:
        static AssetThumbnailManager* s_Instance;
        ImGuiLayer* m_ImGuiLayer = nullptr;
        GuiTextureRegistrar m_TextureRegistrar;
        GuiDeferredReleaser m_DeferredRelease;
        std::unordered_map<std::string, GuiTextureLease> m_GuiThumbnails;
        std::shared_ptr<VertexArray> m_PreviewSphereMesh;

        std::unordered_map<std::string, uint64_t> m_ThumbnailCache;
        std::unordered_map<std::string, glm::vec2> m_ThumbnailDimensions;
        std::unordered_map<std::string, std::shared_ptr<RHI::RHITexture>> m_RHIThumbnails;
        std::unordered_map<std::string, AssetCachedMeta> m_MetaCache;
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_LoadedTextures;
        std::unordered_map<std::string, uint64_t> m_AssetRevisions;
        std::unordered_map<std::string, std::vector<std::filesystem::path>> m_SourceDependencies;
        std::vector<u32> m_GeneratedTextures;
        std::unordered_map<std::string, std::shared_ptr<Framebuffer>> m_GLThumbnails;
        static constexpr size_t MaxCacheEntries = 256;
        std::vector<std::filesystem::path> m_PendingQueue;

        const u32 m_ThumbnailResolution = 128;
        const size_t m_MaxGenerationsPerFrame = 6;
    };

}
