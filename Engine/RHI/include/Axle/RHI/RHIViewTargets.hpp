#pragma once
#include <Axle/RHI/RHITexture.hpp>
#include <array>
#include <memory>
#include <cstdint>

namespace Axle::RHI {
    enum class RHIViewTarget : uint8_t {
        Color, Depth, MSAAColor, MSAADepth, HDR, FXAA,
        OpaqueColor, OpaqueDepth, ParticleDepth, ScreenNormal, ScreenStorage, History0, History1,
        GTAO, GTAOResolved, Bloom0, Bloom1, Bloom2, Bloom3, Bloom4, Bloom5, Exposure, DDGI, DDGIDistance,
        HiZ0, HiZ1, HiZ2, HiZ3, HiZ4, HiZ5, HiZ6, HiZ7, ReflectionEnvironment, ReflectionResponse,
        SSRColor0, SSRColor1, SSRColor2, SSRColor3, PlanarColor, PlanarDepth,
        FroxelScattering, FroxelIntegrated, FroxelHistory0, FroxelHistory1, Count
    };
    struct RHIDDGIAtlasExtent {
        uint32_t ProbeCount = 0;
        uint32_t AtlasCols = 0;
        uint32_t AtlasRows = 0;
        uint32_t IrradianceWidth = 0;
        uint32_t IrradianceHeight = 0;
        uint32_t DistanceWidth = 0;
        uint32_t DistanceHeight = 0;
        bool Valid = false;
        bool operator==(const RHIDDGIAtlasExtent&) const = default;
    };
    struct RHIFroxelGridExtent {
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t Depth = 0;
        bool operator==(const RHIFroxelGridExtent&) const = default;
    };
    struct RHIFroxelTierSpec {
        RHIFroxelGridExtent Limit;
        uint32_t MinTilePixels;
        const char* Name;
    };
    enum class FroxelHistoryPolicy { Required, AllowStaticFog };

    struct RHIViewTargetDesc {
        uint32_t Width = 1, Height = 1;
        RHITextureFormat ColorFormat = RHITextureFormat::RGBA8_UNORM;
        RHITextureFormat DepthFormat = RHITextureFormat::D32_FLOAT;
        RHITextureFormat NormalFormat = RHITextureFormat::RGBA16_FLOAT;
        uint8_t Samples = 1, HistoryCount = 0;
        bool EnableDepth = true, HDR = false, FXAA = false, OwnPrimary = true, RenderAuxiliary = true;
        bool EnableNormal = false, EnableScreenStorage = false, EnableHiZ = false, EnableBloom = false, EnableGTAO = false, EnableAutoExposure = false, EnableDDGI = false;
        bool EnableReflectionTerms = false;
        bool EnablePlanarReflection = false;
        float PlanarResolutionScale = 0.5f;
        bool EnableVolumetricFog = false;
        uint8_t FroxelQuality = 2; // 0: Low, 1: Medium, 2: High (160x90x64 reference), 3: Ultra
        uint32_t DDGIGridX = 0, DDGIGridY = 0, DDGIGridZ = 0;
        bool operator==(const RHIViewTargetDesc&) const = default;
    };
    // One view owns its complete target generation. Reconfiguration constructs
    // every required native texture/view before publishing any replacement.
    class RHIViewTargets {
    public:
        static constexpr uint32_t MaxHiZLevels = 16;
        static constexpr uint32_t MinSSRHiZLevels = 8;
        static constexpr float MinPlanarResolutionScale = 0.25f;
        static constexpr float MaxPlanarResolutionScale = 1.0f;
        static constexpr uint8_t MaxFroxelQualityTier = 3;
        static constexpr uint8_t DefaultFroxelQualityTier = 2;
        static constexpr uint32_t DDGIIrradianceInterior = 6;
        static constexpr uint32_t DDGIIrradianceBorder = 1;
        static constexpr uint32_t DDGIIrradianceTileSize = DDGIIrradianceInterior + 2 * DDGIIrradianceBorder; // 8x8
        static constexpr uint32_t DDGIDistanceInterior = 14;
        static constexpr uint32_t DDGIDistanceBorder = 1;
        static constexpr uint32_t DDGIDistanceTileSize = DDGIDistanceInterior + 2 * DDGIDistanceBorder; // 16x16
        static constexpr uint32_t MaxDDGIProbes = 16384; // Matches 32x16x32 reference grid
        static constexpr uint32_t MaxDDGIAtlasDimension = 8192;

        // Number of natural floor-halving reduction levels from full extent down to 1x1.
        // Returns 0 for invalid zero extents.
        static uint32_t ComputeUsefulHiZLevelCount(uint32_t width, uint32_t height);
        // Total Hi-Z levels allocated by a view target: at least MinSSRHiZLevels for SSR
        // static descriptor compatibility, expanding up to UsefulHiZLevelCount for large views.
        static uint32_t ComputeAllocatedHiZLevelCount(uint32_t width, uint32_t height);
        // Scaled single-dimension extent for planar reflection targets.
        static uint32_t ComputePlanarTargetDimension(uint32_t extent, float scale);
        // Bounded 3D froxel grid dimensions for a view extent and quality tier [0..3].
        // Tier 2 (High) resolves to 160x90x64 at 720p/1080p/1440p; invalid inputs return {0, 0, 0}.
        static RHIFroxelGridExtent ComputeFroxelGridExtent(uint32_t width, uint32_t height, uint8_t qualityTier);
        static const RHIFroxelTierSpec* GetFroxelTierSpec(uint8_t qualityTier);
        // Bounded 2D octahedral atlas layout for an active DDGI probe grid.
        static RHIDDGIAtlasExtent ComputeDDGIAtlasExtent(uint32_t gridX, uint32_t gridY, uint32_t gridZ);

        bool Configure(const RHIViewTargetDesc& desc, const RHITexture* externalColor = nullptr,
                       const RHITexture* externalDepth = nullptr,
                       FroxelHistoryPolicy fogHistory = FroxelHistoryPolicy::Required);
        RHITexture* Get(RHIViewTarget target) const {
            const auto slot = static_cast<size_t>(target); return slot < m_Textures.size() ? m_Textures[slot].get() : nullptr;
        }
        RHITexture* GetHiZ(uint32_t level) const {
            if (level < 8) {
                return Get(static_cast<RHIViewTarget>(static_cast<uint8_t>(RHIViewTarget::HiZ0) + level));
            }
            if (level < MaxHiZLevels) {
                return m_ExtraHiZTextures[level - 8].get();
            }
            return nullptr;
        }
        uint32_t GetHiZLevelCount() const { return m_HiZAllocatedLevels; }
        uint32_t GetHiZUsefulLevelCount() const { return m_HiZUsefulLevels; }
        RHITexture* GetGTAO() const { return Get(RHIViewTarget::GTAO); }
        RHITexture* GetGTAOResolved() const { return Get(RHIViewTarget::GTAOResolved); }
        RHITexture* GetExposure() const { return Get(RHIViewTarget::Exposure); }
        RHITexture* GetDDGI() const { return Get(RHIViewTarget::DDGI); }
        RHITexture* GetDDGIDistance() const { return Get(RHIViewTarget::DDGIDistance); }
        RHITexture* GetBloom(uint32_t level) const {
            return level < 6 ? Get(static_cast<RHIViewTarget>(static_cast<uint8_t>(RHIViewTarget::Bloom0) + level)) : nullptr;
        }
        RHITexture* GetSSRColor(uint32_t level) const {
            return level < 4 ? Get(static_cast<RHIViewTarget>(static_cast<uint8_t>(RHIViewTarget::SSRColor0) + level)) : nullptr;
        }
        RHITexture* GetPlanarColor() const { return Get(RHIViewTarget::PlanarColor); }
        RHITexture* GetPlanarDepth() const { return Get(RHIViewTarget::PlanarDepth); }
        RHITexture* GetFroxelScattering() const { return Get(RHIViewTarget::FroxelScattering); }
        RHITexture* GetFroxelIntegrated() const { return Get(RHIViewTarget::FroxelIntegrated); }
        RHITexture* GetFroxelHistory(uint32_t slot) const {
            return slot < 2 ? Get(slot == 0 ? RHIViewTarget::FroxelHistory0 : RHIViewTarget::FroxelHistory1) : nullptr;
        }
        const RHIViewTargetDesc& GetDesc() const { return m_Desc; }
        uint64_t GetGeneration() const { return m_Generation; }
        bool IsValid() const;
        // Validates the published generation against its descriptor contract.
        bool Validate() const;
    private:
        RHIViewTargetDesc m_Desc;
        std::array<std::unique_ptr<RHITexture>, static_cast<size_t>(RHIViewTarget::Count)> m_Textures;
        std::array<std::unique_ptr<RHITexture>, MaxHiZLevels - 8> m_ExtraHiZTextures;
        bool m_Initialized = false;
        uint64_t m_Generation = 0;
        uint32_t m_HiZUsefulLevels = 0;
        uint32_t m_HiZAllocatedLevels = 0;
    };
}
