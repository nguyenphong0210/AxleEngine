#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <NRI.h>

#include <memory>
#include <string>

namespace Axle::RHI {

    enum class RHIAddressMode : uint8_t {
        Repeat = 0,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder
    };

    inline nri::AddressMode ToNRIAddressMode(RHIAddressMode mode) {
        switch (mode) {
            case RHIAddressMode::Repeat:         return nri::AddressMode::REPEAT;
            case RHIAddressMode::MirroredRepeat: return nri::AddressMode::MIRRORED_REPEAT;
            case RHIAddressMode::ClampToEdge:    return nri::AddressMode::CLAMP_TO_EDGE;
            case RHIAddressMode::ClampToBorder:  return nri::AddressMode::CLAMP_TO_BORDER;
            default:                             return nri::AddressMode::REPEAT;
        }
    }

    enum class RHIFilter : uint8_t {
        Nearest = 0,
        Linear
    };

    inline nri::Filter ToNRIFilter(RHIFilter filter) {
        return (filter == RHIFilter::Linear) ? nri::Filter::LINEAR : nri::Filter::NEAREST;
    }

    enum class RHIFilterOp : uint8_t {
        Average = 0,
        Minimum,
        Maximum
    };

    inline nri::FilterOp ToNRIFilterOp(RHIFilterOp op) {
        switch (op) {
            case RHIFilterOp::Average: return nri::FilterOp::AVERAGE;
            case RHIFilterOp::Minimum: return nri::FilterOp::MIN;
            case RHIFilterOp::Maximum: return nri::FilterOp::MAX;
            default:                   return nri::FilterOp::AVERAGE;
        }
    }

    enum class RHICompareOp : uint8_t {
        None = 0,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always
    };

    inline nri::CompareOp ToNRICompareOp(RHICompareOp op) {
        switch (op) {
            case RHICompareOp::None:           return nri::CompareOp::NONE;
            case RHICompareOp::Less:           return nri::CompareOp::LESS;
            case RHICompareOp::Equal:          return nri::CompareOp::EQUAL;
            case RHICompareOp::LessOrEqual:    return nri::CompareOp::LESS_EQUAL;
            case RHICompareOp::Greater:        return nri::CompareOp::GREATER;
            case RHICompareOp::NotEqual:       return nri::CompareOp::NOT_EQUAL;
            case RHICompareOp::GreaterOrEqual: return nri::CompareOp::GREATER_EQUAL;
            case RHICompareOp::Always:         return nri::CompareOp::ALWAYS;
            default:                           return nri::CompareOp::NONE;
        }
    }

    struct RHISamplerDesc {
        RHIFilter MinFilter = RHIFilter::Linear;
        RHIFilter MagFilter = RHIFilter::Linear;
        RHIFilter MipFilter = RHIFilter::Linear; // Trilinear mip filtering
        RHIFilterOp FilterOp = RHIFilterOp::Average;
        uint8_t Anisotropy = 16;                 // 16x Anisotropic Filtering

        RHIAddressMode AddressU = RHIAddressMode::Repeat;
        RHIAddressMode AddressV = RHIAddressMode::Repeat;
        RHIAddressMode AddressW = RHIAddressMode::Repeat;

        float MipBias = 0.0f;
        float MipMin = 0.0f;
        float MipMax = 1000.0f; // All mip levels

        RHICompareOp CompareOp = RHICompareOp::None;
        bool IsInteger = false;
        bool UnnormalizedCoordinates = false;
        std::string DebugName = "RHISampler";

        // Industry-Standard Presets
        static RHISamplerDesc Anisotropic16x(RHIAddressMode addressMode = RHIAddressMode::Repeat, float mipBias = 0.0f) {
            RHISamplerDesc desc;
            desc.MinFilter = RHIFilter::Linear;
            desc.MagFilter = RHIFilter::Linear;
            desc.MipFilter = RHIFilter::Linear; // Trilinear
            desc.Anisotropy = 16;               // 16x Anisotropy
            desc.AddressU = addressMode;
            desc.AddressV = addressMode;
            desc.AddressW = addressMode;
            desc.MipBias = mipBias;
            desc.DebugName = "Sampler_Anisotropic16x";
            return desc;
        }

        static RHISamplerDesc Trilinear(RHIAddressMode addressMode = RHIAddressMode::Repeat) {
            RHISamplerDesc desc;
            desc.MinFilter = RHIFilter::Linear;
            desc.MagFilter = RHIFilter::Linear;
            desc.MipFilter = RHIFilter::Linear;
            desc.Anisotropy = 1;
            desc.AddressU = addressMode;
            desc.AddressV = addressMode;
            desc.AddressW = addressMode;
            desc.DebugName = "Sampler_Trilinear";
            return desc;
        }

        static RHISamplerDesc Bilinear(RHIAddressMode addressMode = RHIAddressMode::Repeat) {
            RHISamplerDesc desc;
            desc.MinFilter = RHIFilter::Linear;
            desc.MagFilter = RHIFilter::Linear;
            desc.MipFilter = RHIFilter::Nearest;
            desc.Anisotropy = 1;
            desc.AddressU = addressMode;
            desc.AddressV = addressMode;
            desc.AddressW = addressMode;
            desc.DebugName = "Sampler_Bilinear";
            return desc;
        }

        static RHISamplerDesc Point(RHIAddressMode addressMode = RHIAddressMode::ClampToEdge) {
            RHISamplerDesc desc;
            desc.MinFilter = RHIFilter::Nearest;
            desc.MagFilter = RHIFilter::Nearest;
            desc.MipFilter = RHIFilter::Nearest;
            desc.Anisotropy = 1;
            desc.AddressU = addressMode;
            desc.AddressV = addressMode;
            desc.AddressW = addressMode;
            desc.DebugName = "Sampler_Point";
            return desc;
        }

        static RHISamplerDesc ShadowPCF() {
            RHISamplerDesc desc;
            desc.MinFilter = RHIFilter::Linear;
            desc.MagFilter = RHIFilter::Linear;
            desc.MipFilter = RHIFilter::Nearest;
            desc.Anisotropy = 1;
            desc.AddressU = RHIAddressMode::ClampToEdge;
            desc.AddressV = RHIAddressMode::ClampToEdge;
            desc.AddressW = RHIAddressMode::ClampToEdge;
            desc.CompareOp = RHICompareOp::LessOrEqual;
            desc.DebugName = "Sampler_ShadowPCF";
            return desc;
        }

        nri::SamplerDesc ToNRIDesc() const;
    };

    class RHISampler {
    public:
        RHISampler() = default;
        ~RHISampler();

        RHISampler(const RHISampler&) = delete;
        RHISampler& operator=(const RHISampler&) = delete;

        RHISampler(RHISampler&& other) noexcept;
        RHISampler& operator=(RHISampler&& other) noexcept;

        bool Init(const RHISamplerDesc& desc = RHISamplerDesc::Anisotropic16x());
        void Shutdown();

        bool IsValid() const { return m_Sampler != nullptr; }
        nri::Descriptor* GetNative() const { return m_Sampler; }
        const RHISamplerDesc& GetDesc() const { return m_Desc; }

        // Returns static immutable RootSamplerDesc for Pipeline Layout
        nri::RootSamplerDesc ToRootSamplerDesc(uint32_t registerIndex = 0, nri::StageBits stages = nri::StageBits::FRAGMENT_SHADER) const;

    private:
        RHISamplerDesc m_Desc;
        nri::Descriptor* m_Sampler = nullptr;
    };

}
