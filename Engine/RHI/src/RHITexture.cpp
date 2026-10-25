#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/Log.hpp>

namespace Axle::RHI {

    RHITexture::~RHITexture() {
        Shutdown();
    }

    RHITexture::RHITexture(RHITexture&& other) noexcept
        : m_Desc(std::move(other.m_Desc))
        , m_Texture(other.m_Texture)
        , m_SRV(other.m_SRV)
        , m_RTV(other.m_RTV)
        , m_DSV(other.m_DSV) {
        other.m_Texture = nullptr;
        other.m_SRV = nullptr;
        other.m_RTV = nullptr;
        other.m_DSV = nullptr;
    }

    RHITexture& RHITexture::operator=(RHITexture&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_Desc = std::move(other.m_Desc);
            m_Texture = other.m_Texture;
            m_SRV = other.m_SRV;
            m_RTV = other.m_RTV;
            m_DSV = other.m_DSV;
            other.m_Texture = nullptr;
            other.m_SRV = nullptr;
            other.m_RTV = nullptr;
            other.m_DSV = nullptr;
        }
        return *this;
    }

    bool RHITexture::Init(const RHITextureDesc& desc) {
        Shutdown();
        m_Desc = desc;

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) {
            AXLE_CORE_ERROR("RHITexture::Init failed: RHIDevice is not initialized!");
            return false;
        }

        const auto& core = rhiDevice.GetNRICore();
        auto* dev = rhiDevice.GetNRIDevice();

        uint32_t maxDim = std::max(desc.Width, desc.Height);
        uint32_t maxMips = (maxDim > 0) ? (1 + static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(maxDim))))) : 1;
        uint32_t actualMips = std::max(1u, std::min(desc.MipLevels, maxMips));

        nri::TextureDesc texDesc = {};
        texDesc.type = (desc.Depth > 1) ? nri::TextureType::TEXTURE_3D : nri::TextureType::TEXTURE_2D;
        texDesc.usage = ToNRITextureUsage(desc.Usage);
        texDesc.format = ToNRIFormat(desc.Format);
        texDesc.width = static_cast<nri::Dim_t>(desc.Width);
        texDesc.height = static_cast<nri::Dim_t>(desc.Height);
        texDesc.depth = static_cast<nri::Dim_t>(desc.Depth);
        texDesc.mipNum = static_cast<nri::Dim_t>(actualMips);
        texDesc.layerNum = static_cast<nri::Dim_t>(desc.ArrayLayers);
        texDesc.sampleNum = static_cast<nri::Sample_t>(desc.SampleCount);

        nri::Result res = core.CreateCommittedTexture(*dev, nri::MemoryLocation::DEVICE, 0.0f, texDesc, m_Texture);
        if (res != nri::Result::SUCCESS || !m_Texture) {
            AXLE_CORE_ERROR("RHITexture::Init failed to create committed texture! Result: {0}", static_cast<int>(res));
            return false;
        }


        if (!desc.DebugName.empty()) {
            core.SetDebugName(m_Texture, desc.DebugName.c_str());
        }

        // 1. Create Shader Resource View (SRV) if requested
        if (desc.Usage & RHITextureUsage::ShaderResource) {
            nri::TextureViewDesc srvView = {};
            srvView.texture = m_Texture;
            srvView.type = desc.IsCubeMap ? nri::TextureView::TEXTURE_CUBE : ((desc.ArrayLayers > 1) ? nri::TextureView::TEXTURE_ARRAY : nri::TextureView::TEXTURE);
            srvView.format = ToNRIFormat(desc.Format);
            srvView.mipOffset = 0;
            srvView.mipNum = static_cast<nri::Dim_t>(actualMips);
            srvView.layerOffset = 0;
            srvView.layerNum = static_cast<nri::Dim_t>(desc.ArrayLayers);

            res = core.CreateTextureView(srvView, m_SRV);
            if (res != nri::Result::SUCCESS || !m_SRV) {
                AXLE_CORE_ERROR("RHITexture::Init failed to create SRV! Result: {0}", static_cast<int>(res));
                Shutdown();
                return false;
            }
        }

        // 2. Create Render Target View (RTV) if requested
        if (desc.Usage & RHITextureUsage::ColorAttachment) {
            nri::TextureViewDesc rtvView = {};
            rtvView.texture = m_Texture;
            rtvView.type = nri::TextureView::COLOR_ATTACHMENT;
            rtvView.format = ToNRIFormat(desc.Format);
            rtvView.mipOffset = 0;
            rtvView.mipNum = 1;
            rtvView.layerOffset = 0;
            rtvView.layerNum = 1;

            res = core.CreateTextureView(rtvView, m_RTV);
            if (res != nri::Result::SUCCESS || !m_RTV) {
                AXLE_CORE_ERROR("RHITexture::Init failed to create RTV! Result: {0}", static_cast<int>(res));
                Shutdown();
                return false;
            }
        }

        // 3. Create Depth Stencil View (DSV) if requested
        if (desc.Usage & RHITextureUsage::DepthStencil) {
            nri::TextureViewDesc dsvView = {};
            dsvView.texture = m_Texture;
            dsvView.type = nri::TextureView::DEPTH_STENCIL_ATTACHMENT;
            dsvView.format = ToNRIFormat(desc.Format);
            dsvView.mipOffset = 0;
            dsvView.mipNum = 1;
            dsvView.layerOffset = 0;
            dsvView.layerNum = 1;

            res = core.CreateTextureView(dsvView, m_DSV);
            if (res != nri::Result::SUCCESS || !m_DSV) {
                AXLE_CORE_ERROR("RHITexture::Init failed to create DSV! Result: {0}", static_cast<int>(res));
                Shutdown();
                return false;
            }
        }

        return true;
    }

    void RHITexture::Shutdown() {
        if (!m_Texture) return;

        auto& rhiDevice = RHIDevice::Get();
        if (rhiDevice.IsInitialized()) {
            const auto& core = rhiDevice.GetNRICore();
            if (m_SRV) {
                core.DestroyDescriptor(m_SRV);
                m_SRV = nullptr;
            }
            if (m_RTV) {
                core.DestroyDescriptor(m_RTV);
                m_RTV = nullptr;
            }
            if (m_DSV) {
                core.DestroyDescriptor(m_DSV);
                m_DSV = nullptr;
            }
            core.DestroyTexture(m_Texture);
            m_Texture = nullptr;
        }
    }

    uint64_t RHITexture::GetNativeSRVHandle() const {
        if (!m_SRV) return 0;
        const auto& core = RHIDevice::Get().GetNRICore();
        return core.GetDescriptorNativeObject(m_SRV);
    }

    uint64_t RHITexture::GetNativeRTVHandle() const {
        if (!m_RTV) return 0;
        const auto& core = RHIDevice::Get().GetNRICore();
        return core.GetDescriptorNativeObject(m_RTV);
    }

    uint64_t RHITexture::GetNativeDSVHandle() const {
        if (!m_DSV) return 0;
        const auto& core = RHIDevice::Get().GetNRICore();
        return core.GetDescriptorNativeObject(m_DSV);
    }

}
