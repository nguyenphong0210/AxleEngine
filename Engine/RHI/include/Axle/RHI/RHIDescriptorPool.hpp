#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHISampler.hpp>
#include <NRI.h>

#include <vector>
#include <memory>
#include <string>
#include <cstdint>

namespace Axle::RHI {

    struct RHIDescriptorPoolDesc {
        uint32_t MaxDescriptorSets = 256;
        uint32_t MaxTextures = 2048;         // Tier 2 indexing capacity
        uint32_t MaxSamplers = 64;
        uint32_t MaxConstantBuffers = 256;
        uint32_t MaxStorageTextures = 256;
        uint32_t MaxBuffers = 256;
        uint32_t MaxStorageBuffers = 256;
        uint32_t MaxStructuredBuffers = 256;
        nri::DescriptorPoolBits Flags = nri::DescriptorPoolBits::NONE;
        std::string DebugName = "RHIDescriptorPool";

        static RHIDescriptorPoolDesc Tier2Bindless(uint32_t maxTextures = 2048, uint32_t maxSets = 256) {
            RHIDescriptorPoolDesc desc;
            desc.MaxDescriptorSets = maxSets;
            desc.MaxTextures = maxTextures;
            desc.MaxSamplers = 64;
            desc.MaxConstantBuffers = 256;
            desc.DebugName = "DescriptorPool_Tier2Bindless";
            return desc;
        }
    };

    class RHIDescriptorPool {
    public:
        RHIDescriptorPool() = default;
        ~RHIDescriptorPool();

        RHIDescriptorPool(const RHIDescriptorPool&) = delete;
        RHIDescriptorPool& operator=(const RHIDescriptorPool&) = delete;

        RHIDescriptorPool(RHIDescriptorPool&& other) noexcept;
        RHIDescriptorPool& operator=(RHIDescriptorPool&& other) noexcept;

        bool Init(const RHIDescriptorPoolDesc& desc = RHIDescriptorPoolDesc());
        void Shutdown();

        bool IsValid() const { return m_Pool && m_Lease.IsLive(); }
        nri::DescriptorPool* GetNative() const { return IsValid() ? m_Pool : nullptr; }
        RHIResourceLease AcquireLease() const { return m_Lease; }
        const RHIDescriptorPoolDesc& GetDesc() const { return m_Desc; }
        uint32_t GetAllocatedSetCount() const { return m_AllocatedSets; }

        // Allocate a single descriptor set for a given layout & set index
        nri::DescriptorSet* AllocateDescriptorSet(const RHIPipelineLayout& layout, uint32_t setIndex, uint32_t variableNum = 0);

        // Allocate multiple descriptor sets
        std::vector<nri::DescriptorSet*> AllocateDescriptorSets(const RHIPipelineLayout& layout, uint32_t setIndex, uint32_t count, uint32_t variableNum = 0);

        // Update texture range within a descriptor set
        bool UpdateTextureRange(nri::DescriptorSet* set, uint32_t rangeIndex, uint32_t baseSlot, const std::vector<nri::Descriptor*>& descriptors);
        bool UpdateTexture(nri::DescriptorSet* set, uint32_t rangeIndex, uint32_t slot, nri::Descriptor* srv);

        // Update sampler range within a descriptor set
        bool UpdateSamplerRange(nri::DescriptorSet* set, uint32_t rangeIndex, uint32_t baseSlot, const std::vector<nri::Descriptor*>& samplers);
        bool UpdateSampler(nri::DescriptorSet* set, uint32_t rangeIndex, uint32_t slot, nri::Descriptor* sampler);

        // Publish a fresh arena; outstanding leases retain the old descriptor sets.
        void Reset();

        // Command Buffer binding helpers
        void Bind(nri::CommandBuffer* cmdBuffer);
        static void BindDescriptorSet(nri::CommandBuffer* cmdBuffer, uint32_t setIndex, nri::DescriptorSet* set, nri::BindPoint bindPoint = nri::BindPoint::GRAPHICS);

    private:
        RHIDescriptorPoolDesc m_Desc;
        RHIResourceLease m_Lease;
        std::vector<RHIResourceLease> m_Sets;
        nri::DescriptorPool* m_Pool = nullptr;
        uint32_t m_AllocatedSets = 0;
    };

}
