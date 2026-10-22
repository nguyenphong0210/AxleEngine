#include <Axle/RHI/RHIBindless.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/Log.hpp>

namespace Axle::RHI {

    RHIBindlessRegistry& RHIBindlessRegistry::Get() {
        static RHIBindlessRegistry s_Instance;
        return s_Instance;
    }

    RHIBindlessRegistry::~RHIBindlessRegistry() {
        Shutdown();
    }

    bool RHIBindlessRegistry::Init(uint32_t maxTextures) {
        if (m_Initialized) return true;

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) {
            AXLE_CORE_ERROR("RHIBindlessRegistry::Init failed: RHIDevice is not initialized!");
            return false;
        }

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Capacity = std::max(256u, maxTextures);

        // 1. Create 16x Anisotropic Trilinear Sampler
        if (!m_AnisoSampler.Init(RHISamplerDesc::Anisotropic16x())) {
            AXLE_CORE_ERROR("RHIBindlessRegistry: Failed to create 16x Anisotropic Sampler!");
            return false;
        }

        // 2. Build Pipeline Layout for Tier 2 Bindless Indexing
        nri::RootConstantDesc rootConst = {};
        rootConst.registerIndex = 0;
        rootConst.size = sizeof(RHIObjectPushConstants); // 128 bytes
        rootConst.shaderStages = nri::StageBits::ALL;

        nri::DescriptorRangeDesc ranges[2] = {};
        // Unbounded / large texture array: Texture2D g_Textures[m_Capacity] : register(t0, space1);
        ranges[0].baseRegisterIndex = 0;
        ranges[0].descriptorNum = m_Capacity;
        ranges[0].descriptorType = nri::DescriptorType::TEXTURE;
        ranges[0].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        // Sampler: SamplerState g_Sampler : register(s0, space1);
        ranges[1].baseRegisterIndex = 0;
        ranges[1].descriptorNum = 1;
        ranges[1].descriptorType = nri::DescriptorType::SAMPLER;
        ranges[1].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc setDesc = {};
        setDesc.registerSpace = 1;
        setDesc.ranges = ranges;
        setDesc.rangeNum = 2;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootConstants = &rootConst;
        layoutDesc.rootConstantNum = 1;
        layoutDesc.descriptorSets = &setDesc;
        layoutDesc.descriptorSetNum = 1;
        layoutDesc.shaderStages = nri::StageBits::ALL;

        if (!m_Layout.Init(layoutDesc)) {
            AXLE_CORE_ERROR("RHIBindlessRegistry: Failed to initialize bindless pipeline layout!");
            return false;
        }

        // 3. Initialize Descriptor Pool with Tier 2 capacity
        RHIDescriptorPoolDesc poolDesc = RHIDescriptorPoolDesc::Tier2Bindless(m_Capacity, 16);
        if (!m_Pool.Init(poolDesc)) {
            AXLE_CORE_ERROR("RHIBindlessRegistry: Failed to initialize descriptor pool!");
            return false;
        }

        // 4. Allocate Master Bindless Descriptor Set
        m_DescriptorSet = m_Pool.AllocateDescriptorSet(m_Layout, 0);
        if (!m_DescriptorSet) {
            AXLE_CORE_ERROR("RHIBindlessRegistry: Failed to allocate master bindless descriptor set!");
            return false;
        }

        // 5. Update Sampler in Set (Range index 1, slot 0)
        m_Pool.UpdateSampler(m_DescriptorSet, 1, 0, m_AnisoSampler.GetNative());

        // 6. Pre-populate Slots 0..3 with Default Fallback Textures
        m_Slots.resize(m_Capacity, nullptr);
        m_FreeList.clear();
        for (uint32_t i = m_Capacity - 1; i >= 4; --i) {
            m_FreeList.push_back(i);
        }

        auto whiteTex = RHIMaterial::GetDefaultWhiteTexture();
        auto normalTex = RHIMaterial::GetDefaultFlatNormalTexture();
        auto roughTex = RHIMaterial::GetDefaultRoughnessTexture();
        auto blackTex = RHIMaterial::GetDefaultBlackTexture();

        if (whiteTex && whiteTex->GetSRV()) {
            m_Slots[FallbackWhiteIndex] = whiteTex->GetSRV();
            m_Pool.UpdateTexture(m_DescriptorSet, 0, FallbackWhiteIndex, whiteTex->GetSRV());
        }
        if (normalTex && normalTex->GetSRV()) {
            m_Slots[FallbackFlatNormalIndex] = normalTex->GetSRV();
            m_Pool.UpdateTexture(m_DescriptorSet, 0, FallbackFlatNormalIndex, normalTex->GetSRV());
        }
        if (roughTex && roughTex->GetSRV()) {
            m_Slots[FallbackHalfRoughnessIndex] = roughTex->GetSRV();
            m_Pool.UpdateTexture(m_DescriptorSet, 0, FallbackHalfRoughnessIndex, roughTex->GetSRV());
        }
        if (blackTex && blackTex->GetSRV()) {
            m_Slots[FallbackBlackIndex] = blackTex->GetSRV();
            m_Pool.UpdateTexture(m_DescriptorSet, 0, FallbackBlackIndex, blackTex->GetSRV());
        }

        m_Initialized = true;
        AXLE_CORE_INFO("RHIBindlessRegistry: Initialized with Tier 2 Bindless Indexing (Capacity: {0} textures, 16x Anisotropic)",
            m_Capacity);
        return true;
    }

    void RHIBindlessRegistry::Shutdown() {
        if (!m_Initialized) return;

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pool.Shutdown();
        m_Layout.Shutdown();
        m_AnisoSampler.Shutdown();
        m_DescriptorSet = nullptr;
        m_Slots.clear();
        m_FreeList.clear();
        m_Initialized = false;
        AXLE_CORE_INFO("RHIBindlessRegistry: Shutdown completed.");
    }

    uint32_t RHIBindlessRegistry::GetRegisteredCount() const {
        uint32_t count = 4; // fallbacks
        for (size_t i = 4; i < m_Slots.size(); ++i) {
            if (m_Slots[i] != nullptr) count++;
        }
        return count;
    }

    uint32_t RHIBindlessRegistry::RegisterTexture(nri::Descriptor* srv) {
        if (!m_Initialized || !srv) return FallbackWhiteIndex;

        std::lock_guard<std::mutex> lock(m_Mutex);

        // Check if already registered
        for (uint32_t i = 0; i < m_Slots.size(); ++i) {
            if (m_Slots[i] == srv) return i;
        }

        if (m_FreeList.empty()) {
            AXLE_CORE_WARN("RHIBindlessRegistry: Capacity full ({0}), falling back to slot 0", m_Capacity);
            return FallbackWhiteIndex;
        }

        uint32_t index = m_FreeList.back();
        m_FreeList.pop_back();

        m_Slots[index] = srv;
        m_Pool.UpdateTexture(m_DescriptorSet, 0, index, srv);
        return index;
    }

    uint32_t RHIBindlessRegistry::RegisterTexture(const RHITexture& texture) {
        if (!texture.IsValid()) return FallbackWhiteIndex;
        return RegisterTexture(texture.GetSRV());
    }

    void RHIBindlessRegistry::UnregisterTexture(uint32_t index) {
        if (!m_Initialized || index < 4 || index >= m_Capacity) return;

        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Slots[index] != nullptr) {
            m_Slots[index] = nullptr;
            // Clear slot with fallback black
            auto blackTex = RHIMaterial::GetDefaultBlackTexture();
            if (blackTex && blackTex->GetSRV()) {
                m_Pool.UpdateTexture(m_DescriptorSet, 0, index, blackTex->GetSRV());
            }
            m_FreeList.push_back(index);
        }
    }

    void RHIBindlessRegistry::Bind(nri::CommandBuffer* cmdBuffer, uint32_t setIndex) {
        if (!m_Initialized || !cmdBuffer || !m_DescriptorSet) return;
        m_Pool.Bind(cmdBuffer);
        RHIDescriptorPool::BindDescriptorSet(cmdBuffer, setIndex, m_DescriptorSet, nri::BindPoint::GRAPHICS);
    }

}
