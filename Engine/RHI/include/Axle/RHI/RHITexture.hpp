#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <NRI.h>
#include <string>

namespace Axle::RHI {

    enum class RHITextureFormat {
        RGBA8_UNORM,
        RGBA8_SRGB,
        BGRA8_UNORM,
        RGBA16_FLOAT,
        RGBA32_FLOAT,
        R32_FLOAT,
        D32_FLOAT,
        D24_UNORM_S8_UINT
    };

    inline nri::Format ToNRIFormat(RHITextureFormat format) {
        switch (format) {
            case RHITextureFormat::RGBA8_UNORM:       return nri::Format::RGBA8_UNORM;
            case RHITextureFormat::RGBA8_SRGB:        return nri::Format::RGBA8_SRGB;
            case RHITextureFormat::BGRA8_UNORM:       return nri::Format::BGRA8_UNORM;
            case RHITextureFormat::RGBA16_FLOAT:      return nri::Format::RGBA16_SFLOAT;
            case RHITextureFormat::RGBA32_FLOAT:      return nri::Format::RGBA32_SFLOAT;
            case RHITextureFormat::R32_FLOAT:         return nri::Format::R32_SFLOAT;
            case RHITextureFormat::D32_FLOAT:         return nri::Format::D32_SFLOAT;
            case RHITextureFormat::D24_UNORM_S8_UINT: return nri::Format::D24_UNORM_S8_UINT;
            default:                                  return nri::Format::RGBA8_UNORM;
        }
    }

    enum class RHITextureUsage : uint32_t {
        None            = 0,
        ShaderResource  = 1 << 0,
        ColorAttachment = 1 << 1,
        DepthStencil    = 1 << 2,
        Storage         = 1 << 3,
        CopySource      = 1 << 4,
        CopyDest        = 1 << 5
    };

    inline RHITextureUsage operator|(RHITextureUsage a, RHITextureUsage b) {
        return static_cast<RHITextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline bool operator&(RHITextureUsage a, RHITextureUsage b) {
        return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
    }

    inline nri::TextureUsageBits ToNRITextureUsage(RHITextureUsage usage) {
        nri::TextureUsageBits bits = nri::TextureUsageBits::NONE;
        if (usage & RHITextureUsage::ShaderResource)  bits |= nri::TextureUsageBits::SHADER_RESOURCE;
        if (usage & RHITextureUsage::ColorAttachment) bits |= nri::TextureUsageBits::COLOR_ATTACHMENT;
        if (usage & RHITextureUsage::DepthStencil)    bits |= nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT;
        if (usage & RHITextureUsage::Storage)         bits |= nri::TextureUsageBits::SHADER_RESOURCE_STORAGE;
        return bits;
    }

    struct RHITextureDesc {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t Depth = 1;
        uint32_t MipLevels = 1;
        uint32_t ArrayLayers = 1;
        uint32_t SampleCount = 1;
        bool IsCubeMap = false;
        RHITextureFormat Format = RHITextureFormat::RGBA8_UNORM;
        RHITextureUsage Usage = RHITextureUsage::ShaderResource | RHITextureUsage::ColorAttachment;
        std::string DebugName = "RHITexture";

        static RHITextureDesc RenderTarget2D(uint32_t width, uint32_t height, RHITextureFormat format = RHITextureFormat::RGBA8_UNORM, const std::string& name = "RenderTarget2D") {
            RHITextureDesc desc;
            desc.Width = width;
            desc.Height = height;
            desc.Format = format;
            desc.Usage = RHITextureUsage::ShaderResource | RHITextureUsage::ColorAttachment;
            desc.DebugName = name;
            return desc;
        }

        static RHITextureDesc DepthStencil2D(uint32_t width, uint32_t height, RHITextureFormat format = RHITextureFormat::D32_FLOAT, const std::string& name = "DepthStencil2D") {
            RHITextureDesc desc;
            desc.Width = width;
            desc.Height = height;
            desc.Format = format;
            desc.Usage = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
            desc.DebugName = name;
            return desc;
        }

        static RHITextureDesc Cubemap(uint32_t size, uint32_t mipLevels = 1, RHITextureFormat format = RHITextureFormat::RGBA8_UNORM, const std::string& name = "RHICubemap") {
            RHITextureDesc desc;
            desc.Width = size;
            desc.Height = size;
            desc.ArrayLayers = 6;
            desc.MipLevels = mipLevels;
            desc.IsCubeMap = true;
            desc.Format = format;
            desc.Usage = RHITextureUsage::ShaderResource;
            desc.DebugName = name;
            return desc;
        }
    };

    class RHITexture {
    public:
        RHITexture() = default;
        ~RHITexture();

        RHITexture(const RHITexture&) = delete;
        RHITexture& operator=(const RHITexture&) = delete;

        RHITexture(RHITexture&& other) noexcept;
        RHITexture& operator=(RHITexture&& other) noexcept;

        bool Init(const RHITextureDesc& desc);
        void Shutdown();

        bool IsValid() const { return m_Texture != nullptr; }
        const RHITextureDesc& GetDesc() const { return m_Desc; }
        uint32_t GetWidth() const { return m_Desc.Width; }
        uint32_t GetHeight() const { return m_Desc.Height; }
        RHITextureFormat GetFormat() const { return m_Desc.Format; }

        nri::Texture* GetNative() const { return m_Texture; }
        nri::Descriptor* GetSRV() const { return m_SRV; }
        nri::Descriptor* GetRTV() const { return m_RTV; }
        nri::Descriptor* GetDSV() const { return m_DSV; }

        // Returns native platform descriptor handle (e.g. D3D12_CPU_DESCRIPTOR_HANDLE ptr for SRV)
        uint64_t GetNativeSRVHandle() const;
        uint64_t GetNativeRTVHandle() const;
        uint64_t GetNativeDSVHandle() const;

    private:
        RHITextureDesc m_Desc;
        nri::Texture* m_Texture = nullptr;
        nri::Descriptor* m_SRV = nullptr;
        nri::Descriptor* m_RTV = nullptr;
        nri::Descriptor* m_DSV = nullptr;
    };

}
