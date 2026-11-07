#include <Axle/RHI/RHISampler.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <cmath>

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
        auto& device = RHIDevice::Get();
        if (!device.CanCreateResources() || !std::isfinite(desc.MipMin) || !std::isfinite(desc.MipMax) ||
            !std::isfinite(desc.MipBias) || desc.MipMin < 0 || desc.MipMax < desc.MipMin ||
            desc.Anisotropy < 1 || desc.Anisotropy > 16 ||
            desc.MinFilter > RHIFilter::Linear || desc.MagFilter > RHIFilter::Linear || desc.MipFilter > RHIFilter::Linear ||
            desc.FilterOp > RHIFilterOp::Maximum || desc.CompareOp > RHICompareOp::Always ||
            desc.AddressU > RHIAddressMode::ClampToBorder || desc.AddressV > RHIAddressMode::ClampToBorder || desc.AddressW > RHIAddressMode::ClampToBorder)
            return false;
        const auto& limits = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice());
        if (desc.Anisotropy > limits.other.samplerAnisotropyMax || std::abs(desc.MipBias) > limits.other.samplerLodBiasMax) return false;
        if (desc.UnnormalizedCoordinates && (!limits.shaderFeatures.unnormalizedCoordinates ||
            desc.MinFilter != desc.MagFilter || desc.MipFilter != RHIFilter::Nearest || desc.MipMin != 0 || desc.MipMax != 0 ||
            desc.Anisotropy != 1 || desc.CompareOp != RHICompareOp::None || desc.AddressU < RHIAddressMode::ClampToEdge || desc.AddressV < RHIAddressMode::ClampToEdge)) return false;
        RHISampler candidate;
        candidate.m_Desc = desc;
        const auto native = desc.ToNRIDesc();
        const auto result = ScopedRHIFault::Invoke("CreateSampler", [&] { return device.GetNRICore().CreateSampler(*device.GetNRIDevice(), native, candidate.m_Sampler); });
        if (result != nri::Result::SUCCESS || !candidate.m_Sampler) return false;
        *this = std::move(candidate);
        return true;
    }

    void RHISampler::Shutdown() {
        if (m_Sampler) {
            auto& rhiDevice = RHIDevice::Get();
            if (rhiDevice.IsNativeReady()) {
                auto core = rhiDevice.GetNRICore(); auto sampler = m_Sampler;
                rhiDevice.ReleaseNative([core, sampler] { core.DestroyDescriptor(sampler); });
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
