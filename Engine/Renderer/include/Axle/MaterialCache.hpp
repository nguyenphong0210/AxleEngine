#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/Material.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <functional>

namespace Axle {

    class MaterialCache {
    public:
        using UUIDPathResolver = std::function<std::filesystem::path(UUID)>;
        static void SetUUIDPathResolver(UUIDPathResolver resolver);

        using PathUUIDResolver = std::function<UUID(const std::filesystem::path&)>;
        static void SetPathUUIDResolver(PathUUIDResolver resolver);
        // Texture Cache
        static std::shared_ptr<Texture2D> GetTexture(const std::filesystem::path& path);
        static std::shared_ptr<Texture2D> GetTexture(UUID uuid);
        static void ClearTextureCache();

        // Material Cache
        static std::shared_ptr<Material> GetMaterial(const std::filesystem::path& path);
        static std::shared_ptr<Material> GetMaterial(UUID uuid);
        // Read already loaded authoring values without loading, texture creation or cache mutation.
        static std::shared_ptr<const Material> FindLoadedMaterial(const std::filesystem::path& path);
        static void ReloadMaterial(UUID uuid);
        static void ReloadMaterial(const std::filesystem::path& path);
        static void ClearMaterialCache();

        // Multi-Backend RHI Material Cache
        static std::shared_ptr<RHI::RHIMaterial> GetRHIMaterial(const std::filesystem::path& path);
        static std::shared_ptr<RHI::RHIMaterial> GetRHIMaterial(UUID uuid);
        static void ClearRHIMaterialCache();

        // Master Flush
        static void ClearAll();
    };

}

