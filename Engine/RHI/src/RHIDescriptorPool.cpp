#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/Log.hpp>

namespace Axle::RHI {

    RHIDescriptorPool::~RHIDescriptorPool() {
        Shutdown();
    }

    RHIDescriptorPool::RHIDescriptorPool(RHIDescriptorPool&& other) noexcept
        : m_Desc(other.m_Desc)
        , m_Pool(other.m_Pool)
        , m_AllocatedSets(other.m_AllocatedSets)
    {
        other.m_Pool = nullptr;
        other.m_AllocatedSets = 0;
    }

    RHIDescriptorPool& RHIDescriptorPool::operator=(RHIDescriptorPool&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_Desc = other.m_Desc;
            m_Pool = other.m_Pool;
            m_AllocatedSets = other.m_AllocatedSets;
            other.m_Pool = nullptr;
            other.m_AllocatedSets = 0;
        }
        return *this;
    }

    bool RHIDescriptorPool::Init(const RHIDescriptorPoolDesc& desc) {
        Shutdown();

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) {
            AXLE_CORE_ERROR("RHIDescriptorPool::Init failed: RHIDevice is not initialized!");
            return false;
        }

        auto* nativeDevice = rhiDevice.GetNRIDevice();
        const auto& nriCore = rhiDevice.GetNRICore();
        if (!nativeDevice) return false;

        m_Desc = desc;

        nri::DescriptorPoolDesc poolDesc = {};
        poolDesc.descriptorSetMaxNum = m_Desc.MaxDescriptorSets;
        poolDesc.textureMaxNum = m_Desc.MaxTextures;
        poolDesc.samplerMaxNum = m_Desc.MaxSamplers;
        poolDesc.constantBufferMaxNum = m_Desc.MaxConstantBuffers;
        poolDesc.storageTextureMaxNum = m_Desc.MaxStorageTextures;
        poolDesc.bufferMaxNum = m_Desc.MaxBuffers;
        poolDesc.storageBufferMaxNum = m_Desc.MaxStorageBuffers;
        poolDesc.structuredBufferMaxNum = m_Desc.MaxStructuredBuffers;
        poolDesc.flags = m_Desc.Flags;

        nri::Result res = nriCore.CreateDescriptorPool(*nativeDevice, poolDesc, m_Pool);
        if (res != nri::Result::SUCCESS || !m_Pool) {
            AXLE_CORE_ERROR("RHIDescriptorPool::Init: CreateDescriptorPool failed for '{0}' with code {1}!",
                m_Desc.DebugName, static_cast<uint32_t>(res));
            return false;
        }

        m_AllocatedSets = 0;
        AXLE_CORE_INFO("RHIDescriptorPool: Initialized '{0}' (MaxSets: {1}, MaxTextures: {2}, MaxSamplers: {3})",
            m_Desc.DebugName, m_Desc.MaxDescriptorSets, m_Desc.MaxTextures, m_Desc.MaxSamplers);
        return true;
    }

    void RHIDescriptorPool::Shutdown() {
        if (m_Pool) {
            auto& rhiDevice = RHIDevice::Get();
            if (rhiDevice.IsInitialized()) {
                rhiDevice.GetNRICore().DestroyDescriptorPool(m_Pool);
            }
            m_Pool = nullptr;
        }
        m_AllocatedSets = 0;
    }

    nri::DescriptorSet* RHIDescriptorPool::AllocateDescriptorSet(const RHIPipelineLayout& layout, uint32_t setIndex, uint32_t variableNum) {
        if (!m_Pool || !layout.IsValid()) return nullptr;

        auto& rhiDevice = RHIDevice::Get();
        const auto& nriCore = rhiDevice.GetNRICore();

        nri::DescriptorSet* set = nullptr;
        nri::Result res = nriCore.AllocateDescriptorSets(*m_Pool, *layout.GetNative(), setIndex, &set, 1, variableNum);
        if (res != nri::Result::SUCCESS || !set) {
            AXLE_CORE_ERROR("RHIDescriptorPool: AllocateDescriptorSets failed for setIndex {0} with code {1}!",
                setIndex, static_cast<uint32_t>(res));
            return nullptr;
        }

        m_AllocatedSets++;
        return set;
    }

    std::vector<nri::DescriptorSet*> RHIDescriptorPool::AllocateDescriptorSets(const RHIPipelineLayout& layout, uint32_t setIndex, uint32_t count, uint32_t variableNum) {
        std::vector<nri::DescriptorSet*> sets;
        if (!m_Pool || !layout.IsValid() || count == 0) return sets;

        sets.resize(count, nullptr);
        auto& rhiDevice = RHIDevice::Get();
        const auto& nriCore = rhiDevice.GetNRICore();

        nri::Result res = nriCore.AllocateDescriptorSets(*m_Pool, *layout.GetNative(), setIndex, sets.data(), count, variableNum);
        if (res != nri::Result::SUCCESS) {
            AXLE_CORE_ERROR("RHIDescriptorPool: AllocateDescriptorSets batch failed with code {0}!", static_cast<uint32_t>(res));
            sets.clear();
            return sets;
        }

        m_AllocatedSets += count;
        return sets;
    }

    void RHIDescriptorPool::UpdateTextureRange(nri::DescriptorSet* set, uint32_t rangeIndex, uint32_t baseSlot, const std::vector<nri::Descriptor*>& descriptors) {
        if (!set || descriptors.empty()) return;

        auto& rhiDevice = RHIDevice::Get();
        const auto& nriCore = rhiDevice.GetNRICore();

        nri::UpdateDescriptorRangeDesc updateDesc = {};
        updateDesc.descriptorSet = set;
        updateDesc.rangeIndex = rangeIndex;
        updateDesc.baseDescriptor = baseSlot;
        updateDesc.descriptors = descriptors.data();
        updateDesc.descriptorNum = static_cast<uint32_t>(descriptors.size());

        nriCore.UpdateDescriptorRanges(&updateDesc, 1);
    }

    void RHIDescriptorPool::UpdateTexture(nri::DescriptorSet* set, uint32_t rangeIndex, uint32_t slot, nri::Descriptor* srv) {
        if (!set || !srv) return;
        std::vector<nri::Descriptor*> descs = { srv };
        UpdateTextureRange(set, rangeIndex, slot, descs);
    }

    void RHIDescriptorPool::UpdateSamplerRange(nri::DescriptorSet* set, uint32_t rangeIndex, uint32_t baseSlot, const std::vector<nri::Descriptor*>& samplers) {
        if (!set || samplers.empty()) return;

        auto& rhiDevice = RHIDevice::Get();
        const auto& nriCore = rhiDevice.GetNRICore();

        nri::UpdateDescriptorRangeDesc updateDesc = {};
        updateDesc.descriptorSet = set;
        updateDesc.rangeIndex = rangeIndex;
        updateDesc.baseDescriptor = baseSlot;
        updateDesc.descriptors = samplers.data();
        updateDesc.descriptorNum = static_cast<uint32_t>(samplers.size());

        nriCore.UpdateDescriptorRanges(&updateDesc, 1);
    }

    void RHIDescriptorPool::UpdateSampler(nri::DescriptorSet* set, uint32_t rangeIndex, uint32_t slot, nri::Descriptor* sampler) {
        if (!set || !sampler) return;
        std::vector<nri::Descriptor*> descs = { sampler };
        UpdateSamplerRange(set, rangeIndex, slot, descs);
    }

    void RHIDescriptorPool::Reset() {
        if (m_Pool) {
            auto& rhiDevice = RHIDevice::Get();
            if (rhiDevice.IsInitialized()) {
                rhiDevice.GetNRICore().ResetDescriptorPool(*m_Pool);
            }
            m_AllocatedSets = 0;
        }
    }

    void RHIDescriptorPool::Bind(nri::CommandBuffer* cmdBuffer) {
        if (m_Pool && cmdBuffer) {
            auto& rhiDevice = RHIDevice::Get();
            if (rhiDevice.IsInitialized()) {
                rhiDevice.GetNRICore().CmdSetDescriptorPool(*cmdBuffer, *m_Pool);
            }
        }
    }

    void RHIDescriptorPool::BindDescriptorSet(nri::CommandBuffer* cmdBuffer, uint32_t setIndex, nri::DescriptorSet* set, nri::BindPoint bindPoint) {
        if (cmdBuffer && set) {
            auto& rhiDevice = RHIDevice::Get();
            if (rhiDevice.IsInitialized()) {
                nri::SetDescriptorSetDesc setDesc = {};
                setDesc.setIndex = setIndex;
                setDesc.descriptorSet = set;
                setDesc.bindPoint = bindPoint;
                rhiDevice.GetNRICore().CmdSetDescriptorSet(*cmdBuffer, setDesc);
            }
        }
    }

}
