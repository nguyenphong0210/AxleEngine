#include <Axle/RHI/RHISampler.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/Log.hpp>

namespace Axle::RHI {

    nri::SamplerDesc RHISamplerDesc::ToNRIDesc() const {
        nri::SamplerDesc desc = {};
        desc.filters.min = ToNRIFilter(MinFilter);
        desc.filters.mag = ToNRIFilter(MagFilter);
        desc.filters.mip = ToNRIFilter(MipFilter);
        desc.filters.op  = ToNRIFilterOp(FilterOp);

        desc.anisotropy = Anisotropy;
        desc.mipBias    = MipBias;
        desc.mipMin     = MipMin;
        desc.mipMax     = MipMax;

        desc.addressModes.u = ToNRIAddressMode(AddressU);
        desc.addressModes.v = ToNRIAddressMode(AddressV);
        desc.addressModes.w = ToNRIAddressMode(AddressW);

        desc.compareOp = ToNRICompareOp(CompareOp);
        desc.borderColor.f = { 0.0f, 0.0f, 0.0f, 0.0f };
        desc.isInteger = IsInteger;
        desc.unnormalizedCoordinates = UnnormalizedCoordinates;

        return desc;
    }

    RHISampler::~RHISampler() {
        Shutdown();
    }

    RHISampler::RHISampler(RHISampler&& other) noexcept
        : m_Desc(other.m_Desc)
        , m_Sampler(other.m_Sampler)
    {
        other.m_Sampler = nullptr;
    }

    RHISampler& RHISampler::operator=(RHISampler&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_Desc = other.m_Desc;
            m_Sampler = other.m_Sampler;
            other.m_Sampler = nullptr;
        }
        return *this;
    }

    bool RHISampler::Init(const RHISamplerDesc& desc) {
        Shutdown();

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) {
            AXLE_CORE_ERROR("RHISampler::Init failed: RHIDevice is not initialized!");
            return false;
        }

        auto* nativeDevice = rhiDevice.GetNRIDevice();
        const auto& nriCore = rhiDevice.GetNRICore();
        if (!nativeDevice) return false;

        m_Desc = desc;
        nri::SamplerDesc nriDesc = m_Desc.ToNRIDesc();

        nri::Result res = nriCore.CreateSampler(*nativeDevice, nriDesc, m_Sampler);
        if (res != nri::Result::SUCCESS || !m_Sampler) {
            AXLE_CORE_ERROR("RHISampler::Init: CreateSampler failed for '{0}' with code {1}!",
                m_Desc.DebugName, static_cast<uint32_t>(res));
            return false;
        }

        AXLE_CORE_INFO("RHISampler: Initialized '{0}' (Aniso: {1}x, Trilinear: {2})",
            m_Desc.DebugName, m_Desc.Anisotropy, (m_Desc.MipFilter == RHIFilter::Linear ? "YES" : "NO"));
        return true;
    }

    void RHISampler::Shutdown() {
        if (m_Sampler) {
            auto& rhiDevice = RHIDevice::Get();
            if (rhiDevice.IsInitialized()) {
                rhiDevice.GetNRICore().DestroyDescriptor(m_Sampler);
            }
            m_Sampler = nullptr;
        }
    }

    nri::RootSamplerDesc RHISampler::ToRootSamplerDesc(uint32_t registerIndex, nri::StageBits stages) const {
        nri::RootSamplerDesc rootSampler = {};
        rootSampler.registerIndex = registerIndex;
        rootSampler.desc = m_Desc.ToNRIDesc();
        rootSampler.shaderStages = stages;
        return rootSampler;
    }

}
