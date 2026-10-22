#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHISampler.hpp>
#include <Axle/RHI/RHIPipeline.hpp>

#include <vector>
#include <memory>
#include <mutex>
#include <cstdint>

namespace Axle::RHI {

    // =========================================================================
    // Tier 2 Bindless Texture Registry & Descriptor Indexing
    // =========================================================================
    class RHIBindlessRegistry {
    public:
        static RHIBindlessRegistry& Get();

        RHIBindlessRegistry() = default;
        ~RHIBindlessRegistry();

        RHIBindlessRegistry(const RHIBindlessRegistry&) = delete;
        RHIBindlessRegistry& operator=(const RHIBindlessRegistry&) = delete;

        // Initialize bindless table with maximum texture capacity
        bool Init(uint32_t maxTextures = 2048);
        void Shutdown();

        bool IsInitialized() const { return m_Initialized; }
        uint32_t GetCapacity() const { return m_Capacity; }
        uint32_t GetRegisteredCount() const;

        // Register texture SRV and return bindless index (0..Capacity-1)
        uint32_t RegisterTexture(nri::Descriptor* srv);
        uint32_t RegisterTexture(const RHITexture& texture);

        // Unregister texture slot
        void UnregisterTexture(uint32_t index);

        // Access underlying descriptor resources
        nri::DescriptorSet* GetDescriptorSet() const { return m_DescriptorSet; }
        RHIDescriptorPool& GetDescriptorPool() { return m_Pool; }
        const RHISampler& GetDefaultSampler() const { return m_AnisoSampler; }
        const RHIPipelineLayout& GetPipelineLayout() const { return m_Layout; }

        // Pre-allocated fallback slots
        static constexpr uint32_t FallbackWhiteIndex = 0;
        static constexpr uint32_t FallbackFlatNormalIndex = 1;
        static constexpr uint32_t FallbackHalfRoughnessIndex = 2;
        static constexpr uint32_t FallbackBlackIndex = 3;

        uint32_t GetFallbackWhiteIndex() const { return FallbackWhiteIndex; }
        uint32_t GetFallbackFlatNormalIndex() const { return FallbackFlatNormalIndex; }
        uint32_t GetFallbackHalfRoughnessIndex() const { return FallbackHalfRoughnessIndex; }
        uint32_t GetFallbackBlackIndex() const { return FallbackBlackIndex; }

        // Command Buffer binding helpers
        void Bind(nri::CommandBuffer* cmdBuffer, uint32_t setIndex = 0);

    private:
        bool m_Initialized = false;
        uint32_t m_Capacity = 2048;

        RHIDescriptorPool m_Pool;
        RHIPipelineLayout m_Layout;
        nri::DescriptorSet* m_DescriptorSet = nullptr;
        RHISampler m_AnisoSampler;

        std::vector<nri::Descriptor*> m_Slots;
        std::vector<uint32_t> m_FreeList;
        std::mutex m_Mutex;
    };

}
