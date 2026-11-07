#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <algorithm>

namespace Axle::RHI {

    RHIDescriptorPool::~RHIDescriptorPool() {
        Shutdown();
    }

    RHIDescriptorPool::RHIDescriptorPool(RHIDescriptorPool&& other) noexcept
        : m_Desc(other.m_Desc)
        , m_Lease(std::move(other.m_Lease))
        , m_Sets(std::move(other.m_Sets))
        , m_Pool(other.m_Pool)
        , m_AllocatedSets(other.m_AllocatedSets)
    {
        other.m_Pool = nullptr;
        other.m_AllocatedSets = 0;
    }

    RHIDescriptorPool& RHIDescriptorPool::operator=(RHIDescriptorPool&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_Lease = std::move(other.m_Lease);
            m_Sets = std::move(other.m_Sets);
            m_Desc = other.m_Desc;
            m_Pool = other.m_Pool;
            m_AllocatedSets = other.m_AllocatedSets;
            other.m_Pool = nullptr;
            other.m_AllocatedSets = 0;
        }
        return *this;
    }

    bool RHIDescriptorPool::Init(const RHIDescriptorPoolDesc& desc) {
        if (!desc.MaxDescriptorSets) return false;
        RHIDescriptorPool candidate;

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.CanCreateResources()) {
            AXLE_CORE_ERROR("RHIDescriptorPool::Init failed: RHIDevice is not initialized!");
            return false;
        }

        auto* nativeDevice = rhiDevice.GetNRIDevice();
        const auto& nriCore = rhiDevice.GetNRICore();
        if (!nativeDevice) return false;

        candidate.m_Desc = desc;

        nri::DescriptorPoolDesc poolDesc = {};
        poolDesc.descriptorSetMaxNum = candidate.m_Desc.MaxDescriptorSets;
        poolDesc.textureMaxNum = candidate.m_Desc.MaxTextures;
        poolDesc.samplerMaxNum = candidate.m_Desc.MaxSamplers;
        poolDesc.constantBufferMaxNum = candidate.m_Desc.MaxConstantBuffers;
        poolDesc.storageTextureMaxNum = candidate.m_Desc.MaxStorageTextures;
        poolDesc.bufferMaxNum = candidate.m_Desc.MaxBuffers;
        poolDesc.storageBufferMaxNum = candidate.m_Desc.MaxStorageBuffers;
        poolDesc.structuredBufferMaxNum = candidate.m_Desc.MaxStructuredBuffers;
        poolDesc.flags = candidate.m_Desc.Flags;

        nri::Result res = ScopedRHIFault::Invoke("CreateDescriptorPool", [&] { return nriCore.CreateDescriptorPool(*nativeDevice, poolDesc, candidate.m_Pool); });
        if (res != nri::Result::SUCCESS || !candidate.m_Pool) {
            AXLE_CORE_ERROR("RHIDescriptorPool::Init: CreateDescriptorPool failed for '{0}' with code {1}!",
                candidate.m_Desc.DebugName, static_cast<uint32_t>(res));
            return false;
        }

        candidate.m_AllocatedSets = 0;
        const auto functions = nriCore; auto pool = candidate.m_Pool;
        candidate.m_Lease = rhiDevice.GetResources().Publish(RHIResourceKind::Pool, desc.DebugName, {pool},
            [functions, pool] { functions.DestroyDescriptorPool(pool); });
        if (!candidate.m_Lease.IsLive()) return false;
        AXLE_CORE_INFO("RHIDescriptorPool: Initialized '{0}' (MaxSets: {1}, MaxTextures: {2}, MaxSamplers: {3})",
            candidate.m_Desc.DebugName, candidate.m_Desc.MaxDescriptorSets, candidate.m_Desc.MaxTextures, candidate.m_Desc.MaxSamplers);
        *this = std::move(candidate);
        return true;
    }

    void RHIDescriptorPool::Shutdown() {
        m_Sets.clear();
        if (m_Lease) { m_Lease.Reset(); m_Pool = nullptr; }
        else if (m_Pool) {
            auto& rhiDevice = RHIDevice::Get();
            if (rhiDevice.IsNativeReady()) {
                auto core = rhiDevice.GetNRICore(); auto pool = m_Pool;
                rhiDevice.ReleaseNative([core, pool] { core.DestroyDescriptorPool(pool); });
            }
            m_Pool = nullptr;
        }
        m_AllocatedSets = 0;
    }

    nri::DescriptorSet* RHIDescriptorPool::AllocateDescriptorSet(const RHIPipelineLayout& layout, uint32_t setIndex, uint32_t variableNum) {
        if (!IsValid() || !layout.IsValid() || m_AllocatedSets >= m_Desc.MaxDescriptorSets || setIndex >= layout.GetDescriptorSets().size()) return nullptr;

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
        auto lease = rhiDevice.GetResources().Publish(RHIResourceKind::DescriptorSet, "DescriptorSet", {set}, {}, {m_Lease, layout.AcquireLease()});
        if (!lease.IsLive()) return nullptr;
        m_Sets.push_back(std::move(lease));
        return set;
    }

    std::vector<nri::DescriptorSet*> RHIDescriptorPool::AllocateDescriptorSets(const RHIPipelineLayout& layout, uint32_t setIndex, uint32_t count, uint32_t variableNum) {
        std::vector<nri::DescriptorSet*> sets;
        if (!IsValid() || !layout.IsValid() || !count || count > m_Desc.MaxDescriptorSets - m_AllocatedSets || setIndex >= layout.GetDescriptorSets().size()) return sets;

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
        for (auto* set : sets) {
            auto lease = rhiDevice.GetResources().Publish(RHIResourceKind::DescriptorSet, "DescriptorSet", {set}, {}, {m_Lease, layout.AcquireLease()});
            if (!lease.IsLive()) return {};
            m_Sets.push_back(std::move(lease));
        }
        return sets;
    }

    void RHIDescriptorPool::UpdateTextureRange(nri::DescriptorSet* set, uint32_t rangeIndex, uint32_t baseSlot, const std::vector<nri::Descriptor*>& descriptors) {
        if (!IsValid() || !set || descriptors.empty()) return;

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.CanCreateResources()) return;
        const auto& nriCore = rhiDevice.GetNRICore();
        auto setLease = rhiDevice.GetResources().AcquireNative(set);
        if (!setLease.IsLive() || std::none_of(m_Sets.begin(), m_Sets.end(), [&](const auto& owned) {
            return owned.GetHandle() == setLease.GetHandle();
        })) return;
        std::vector<RHIResourceLease> resources;
        for (auto* descriptor : descriptors) {
            auto lease = rhiDevice.GetResources().AcquireNative(descriptor);
            if (!lease.IsLive()) return;
            resources.push_back(std::move(lease));
        }
        if (!rhiDevice.GetResources().ReplaceDescriptorRange(setLease, rangeIndex, baseSlot, std::move(resources))) return;

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
        UpdateTextureRange(set, rangeIndex, baseSlot, samplers);
    }

    void RHIDescriptorPool::UpdateSampler(nri::DescriptorSet* set, uint32_t rangeIndex, uint32_t slot, nri::Descriptor* sampler) {
        if (!set || !sampler) return;
        std::vector<nri::Descriptor*> descs = { sampler };
        UpdateSamplerRange(set, rangeIndex, slot, descs);
    }

    void RHIDescriptorPool::Reset() {
        // Replacing the arena preserves old descriptor-set generations and their
        // layout/dependency ownership for clients retaining leases.
        if (IsValid()) Init(m_Desc);
    }

    void RHIDescriptorPool::Bind(nri::CommandBuffer* cmdBuffer) {
        if (IsValid() && cmdBuffer) {
            auto& rhiDevice = RHIDevice::Get();
            if (rhiDevice.IsNativeReady()) {
                rhiDevice.GetNRICore().CmdSetDescriptorPool(*cmdBuffer, *m_Pool);
            }
        }
    }

    void RHIDescriptorPool::BindDescriptorSet(nri::CommandBuffer* cmdBuffer, uint32_t setIndex, nri::DescriptorSet* set, nri::BindPoint bindPoint) {
        if (cmdBuffer && set) {
            auto& rhiDevice = RHIDevice::Get();
            if (rhiDevice.IsNativeReady()) {
                nri::SetDescriptorSetDesc setDesc = {};
                setDesc.setIndex = setIndex;
                setDesc.descriptorSet = set;
                setDesc.bindPoint = bindPoint;
                rhiDevice.GetNRICore().CmdSetDescriptorSet(*cmdBuffer, setDesc);
            }
        }
    }

}
