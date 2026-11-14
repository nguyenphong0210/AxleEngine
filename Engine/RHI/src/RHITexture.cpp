#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <algorithm>
#include <limits>

namespace Axle::RHI {

    RHITexture::~RHITexture() {
        Shutdown();
    }

    RHITexture::RHITexture(RHITexture&& other) noexcept
        : m_Desc(std::move(other.m_Desc))
        , m_Lease(std::move(other.m_Lease))
        , m_Texture(other.m_Texture)
        , m_Memory(other.m_Memory)
        , m_OwnsMemory(other.m_OwnsMemory)
        , m_SRV(other.m_SRV)
        , m_RTV(other.m_RTV)
        , m_DSV(other.m_DSV)
        , m_UAV(other.m_UAV) {
        other.m_Texture = nullptr;
        other.m_Memory = nullptr;
        other.m_OwnsMemory = false;
        other.m_SRV = nullptr;
        other.m_RTV = nullptr;
        other.m_DSV = nullptr;
        other.m_UAV = nullptr;
    }

    RHITexture& RHITexture::operator=(RHITexture&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_Desc = std::move(other.m_Desc);
            m_Lease = std::move(other.m_Lease);
            m_Texture = other.m_Texture;
            m_Memory = other.m_Memory;
            m_OwnsMemory = other.m_OwnsMemory;
            m_SRV = other.m_SRV;
            m_RTV = other.m_RTV;
            m_DSV = other.m_DSV;
            m_UAV = other.m_UAV;
            other.m_Texture = nullptr;
            other.m_Memory = nullptr;
            other.m_OwnsMemory = false;
            other.m_SRV = nullptr;
            other.m_RTV = nullptr;
            other.m_DSV = nullptr;
            other.m_UAV = nullptr;
        }
        return *this;
    }

    bool RHITexture::Init(const RHITextureDesc& desc) {
        RHITexture candidate;
        if (!candidate.Build(desc)) return false;
        *this = std::move(candidate);
        return true;
    }

    bool RHITexture::InitPlaced(const RHITextureDesc& desc, nri::Memory* memory, uint64_t offset) {
        if (!memory) return false;
        RHITexture candidate;
        if (!candidate.Build(desc, memory, offset)) return false;
        *this = std::move(candidate); return true;
    }

    bool RHITexture::QueryMemoryRequirements(const RHITextureDesc& desc, nri::MemoryDesc& requirements) {
        RHITexture probe; nri::MemoryDesc queried{};
        if (!probe.Build(desc, nullptr, 0, &queried)) return false;
        // The unbound probe was never published or recorded and owns no memory.
        RHIDevice::Get().GetNRICore().DestroyTexture(probe.m_Texture); probe.m_Texture = nullptr;
        requirements = queried; return true;
    }

    bool RHITexture::Build(const RHITextureDesc& desc, nri::Memory* borrowedMemory, uint64_t offset, nri::MemoryDesc* queryOnly) {
        ScopedRHIOperation operation(desc.DebugName);
        m_Desc = desc;

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.CanCreateResources()) {
            AXLE_CORE_ERROR("RHITexture::Init failed: RHIDevice is not initialized!");
            return false;
        }

        const auto& core = rhiDevice.GetNRICore();
        auto* dev = rhiDevice.GetNRIDevice();

        const auto& limits = core.GetDeviceDesc(*dev);
        const auto format = ToNRIFormat(desc.Format);
        const bool depthFormat = desc.Format == RHITextureFormat::D32_FLOAT || desc.Format == RHITextureFormat::D24_UNORM_S8_UINT;
        const uint32_t dimLimit = desc.Depth > 1 ? limits.dimensions.texture3DMaxDim : limits.dimensions.texture2DMaxDim;
        const uint32_t nativeLimit = std::numeric_limits<nri::Dim_t>::max();
        uint32_t maxMips = 0;
        for (auto dim = std::max({desc.Width, desc.Height, desc.Depth}); dim; dim >>= 1) ++maxMips;
        if (!desc.Width || !desc.Height || !desc.Depth || !desc.ArrayLayers || !desc.MipLevels ||
            desc.Width > std::min(dimLimit, nativeLimit) || desc.Height > std::min(dimLimit, nativeLimit) ||
            desc.Depth > std::min(dimLimit, nativeLimit) || desc.ArrayLayers > limits.dimensions.textureLayerMaxNum ||
            desc.MipLevels > maxMips || format == nri::Format::UNKNOWN ||
            (static_cast<uint32_t>(desc.Usage) & ~127u) || desc.Usage == RHITextureUsage::None ||
            (desc.Depth > 1 && (desc.ArrayLayers != 1 || desc.IsCubeMap || (desc.Usage & RHITextureUsage::DepthStencil))) ||
            (desc.IsCubeMap && (desc.Width != desc.Height || desc.ArrayLayers != 6 || desc.SampleCount != 1)) ||
            ((desc.Usage & RHITextureUsage::DepthStencil) && !depthFormat) ||
            (depthFormat && ((desc.Usage & RHITextureUsage::ColorAttachment) || (desc.Usage & RHITextureUsage::Storage))) ||
            (desc.SampleCount != 1 && desc.SampleCount != 2 && desc.SampleCount != 4 && desc.SampleCount != 8) ||
            (desc.SampleCount > 1 && (desc.Depth > 1 || desc.MipLevels != 1 || (desc.Usage & RHITextureUsage::HostTransfer))) ||
            (desc.SampleCount > 1 && (desc.Usage & RHITextureUsage::Storage)) ||
            (((desc.Usage & RHITextureUsage::ColorAttachment) || (desc.Usage & RHITextureUsage::DepthStencil)) &&
                (desc.Width > limits.dimensions.attachmentMaxDim || desc.Height > limits.dimensions.attachmentMaxDim ||
                 desc.ArrayLayers > limits.dimensions.attachmentLayerMaxNum))) {
            AXLE_CORE_ERROR("RHITexture '{}': invalid dimensions, format, samples or usage", desc.DebugName);
            return rhiDevice.ObserveResult(nri::Result::INVALID_ARGUMENT, "ValidateTextureDesc", desc.DebugName);
        }
        // Pinned NRI attaches sliced-3D metadata to these Vulkan views, which
        // cannot legally span mip levels. Reject rather than silently narrow the SRV.
        if (desc.Depth > 1 && desc.MipLevels > 1 && (desc.Usage & RHITextureUsage::ShaderResource) &&
            rhiDevice.GetBackend() == RHIType::Vulkan)
            return rhiDevice.ObserveResult(nri::Result::UNSUPPORTED, "ValidateTextureDesc", "Vulkan multi-mip 3D shader views are unsupported by pinned NRI");
        const auto support = rhiDevice.GetTextureFormatSupport(format);
        auto required = nri::FormatSupportBits::UNSUPPORTED;
        if (desc.Usage & RHITextureUsage::ShaderResource) required |= nri::FormatSupportBits::TEXTURE;
        if (desc.Usage & RHITextureUsage::ColorAttachment) required |= nri::FormatSupportBits::COLOR_ATTACHMENT;
        if (desc.Usage & RHITextureUsage::DepthStencil) required |= nri::FormatSupportBits::DEPTH_STENCIL_ATTACHMENT;
        if (desc.Usage & RHITextureUsage::Storage) required |= nri::FormatSupportBits::STORAGE_TEXTURE;
        if (desc.Usage & RHITextureUsage::HostTransfer) required |= nri::FormatSupportBits::HOST_COPY;
        if (desc.SampleCount == 2) required |= nri::FormatSupportBits::MULTISAMPLE_2X;
        if (desc.SampleCount == 4) required |= nri::FormatSupportBits::MULTISAMPLE_4X;
        if (desc.SampleCount == 8) required |= nri::FormatSupportBits::MULTISAMPLE_8X;
        if (support == nri::FormatSupportBits::UNSUPPORTED ||
            (static_cast<uint32_t>(support) & static_cast<uint32_t>(required)) != static_cast<uint32_t>(required)) {
            AXLE_CORE_ERROR("RHITexture '{}': requested format usage is unsupported", desc.DebugName);
            return rhiDevice.ObserveResult(nri::Result::UNSUPPORTED, "ValidateTextureFormat", desc.DebugName);
        }

        nri::TextureDesc texDesc = {};
        texDesc.type = (desc.Depth > 1) ? nri::TextureType::TEXTURE_3D : nri::TextureType::TEXTURE_2D;
        texDesc.usage = ToNRITextureUsage(desc.Usage);
        texDesc.format = ToNRIFormat(desc.Format);
        texDesc.width = static_cast<nri::Dim_t>(desc.Width);
        texDesc.height = static_cast<nri::Dim_t>(desc.Height);
        texDesc.depth = static_cast<nri::Dim_t>(desc.Depth);
        texDesc.mipNum = static_cast<nri::Dim_t>(desc.MipLevels);
        texDesc.layerNum = static_cast<nri::Dim_t>(desc.ArrayLayers);
        texDesc.sampleNum = static_cast<nri::Sample_t>(desc.SampleCount);

        // Separate wrapper/allocation/binding stages expose ownership even if a
        // later native stage fails. The candidate destructor rolls each stage back.
        auto res = ScopedRHIFault::Invoke("CreateTexture", [&] { return core.CreateTexture(*dev, texDesc, m_Texture); });
        if (res != nri::Result::SUCCESS || !m_Texture) return false;
        nri::MemoryDesc memory{};
        core.GetTextureMemoryDesc(*m_Texture, nri::MemoryLocation::DEVICE, memory);
        if (!memory.size || !memory.alignment || memory.size > limits.memory.allocationMaxSize ||
            offset % memory.alignment || offset > UINT64_MAX - memory.size || (memory.mustBeDedicated && offset)) return false;
        if (queryOnly) { *queryOnly = memory; return true; }
        m_Memory = borrowedMemory; m_OwnsMemory = borrowedMemory == nullptr;
        if (m_OwnsMemory) {
            nri::AllocateMemoryDesc allocation{};
            allocation.size = memory.size; allocation.type = memory.type;
            allocation.allowMultisampleTextures = desc.SampleCount > 1;
            res = ScopedRHIFault::Invoke("AllocateTextureMemory", [&] { return core.AllocateMemory(*dev, allocation, m_Memory); });
            if (res != nri::Result::SUCCESS || !m_Memory) return false;
        }
        nri::BindTextureMemoryDesc binding{m_Texture, m_Memory, offset};
        res = ScopedRHIFault::Invoke("BindTextureMemory", [&] { return core.BindTextureMemory(&binding, 1); });
        if (res != nri::Result::SUCCESS) return false;

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
            srvView.mipNum = static_cast<nri::Dim_t>(desc.MipLevels);
            srvView.layerOffset = 0;
            srvView.layerNum = static_cast<nri::Dim_t>(desc.ArrayLayers);

            res = ScopedRHIFault::Invoke("CreateTextureSRV", [&] { return core.CreateTextureView(srvView, m_SRV); });
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

            res = ScopedRHIFault::Invoke("CreateTextureRTV", [&] { return core.CreateTextureView(rtvView, m_RTV); });
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

            res = ScopedRHIFault::Invoke("CreateTextureDSV", [&] { return core.CreateTextureView(dsvView, m_DSV); });
            if (res != nri::Result::SUCCESS || !m_DSV) {
                AXLE_CORE_ERROR("RHITexture::Init failed to create DSV! Result: {0}", static_cast<int>(res));
                Shutdown();
                return false;
            }
        }

        // 4. Create Storage View (UAV) if requested
        if (desc.Usage & RHITextureUsage::Storage) {
            nri::TextureViewDesc uavView = {};
            uavView.texture = m_Texture;
            uavView.type = (desc.ArrayLayers > 1) ? nri::TextureView::STORAGE_TEXTURE_ARRAY : nri::TextureView::STORAGE_TEXTURE;
            uavView.format = ToNRIFormat(desc.Format);
            uavView.mipOffset = 0;
            uavView.mipNum = 1;
            uavView.layerOffset = 0;
            uavView.layerNum = static_cast<nri::Dim_t>(desc.ArrayLayers);

            res = ScopedRHIFault::Invoke("CreateTextureUAV", [&] { return core.CreateTextureView(uavView, m_UAV); });
            if (res != nri::Result::SUCCESS || !m_UAV) {
                AXLE_CORE_ERROR("RHITexture::Init failed to create UAV! Result: {0}", static_cast<int>(res));
                Shutdown();
                return false;
            }
        }

        const auto functions = core;
        auto srv = m_SRV, rtv = m_RTV, dsv = m_DSV, uav = m_UAV;
        auto texture = m_Texture; auto ownedMemory = m_OwnsMemory ? m_Memory : nullptr;
        std::vector<RHIResourceLease> dependencies;
        if (borrowedMemory) {
            auto lease = rhiDevice.GetResources().AcquireNative(borrowedMemory);
            if (lease.IsLive()) dependencies.push_back(std::move(lease));
        }
        m_Lease = rhiDevice.GetResources().Publish(RHIResourceKind::Texture, desc.DebugName, {texture, srv, rtv, dsv, uav},
            [functions, srv, rtv, dsv, uav, texture, ownedMemory] {
                if (srv) functions.DestroyDescriptor(srv);
                if (rtv) functions.DestroyDescriptor(rtv);
                if (dsv) functions.DestroyDescriptor(dsv);
                if (uav) functions.DestroyDescriptor(uav);
                functions.DestroyTexture(texture);
                if (ownedMemory) functions.FreeMemory(ownedMemory);
            }, std::move(dependencies));
        auto& resources = rhiDevice.GetResources();
        resources.DefineViewAccess(srv, nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE);
        resources.DefineViewAccess(uav, nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::Layout::SHADER_RESOURCE_STORAGE);
        resources.DefineViewAccess(rtv, nri::AccessBits::COLOR_ATTACHMENT, nri::Layout::COLOR_ATTACHMENT);
        resources.DefineViewAccess(dsv, nri::AccessBits::DEPTH_STENCIL_ATTACHMENT_WRITE, nri::Layout::DEPTH_STENCIL_ATTACHMENT);
        const auto planes = desc.Format == RHITextureFormat::D24_UNORM_S8_UINT ? nri::PlaneBits::DEPTH | nri::PlaneBits::STENCIL :
            desc.Format == RHITextureFormat::D32_FLOAT ? nri::PlaneBits::DEPTH : nri::PlaneBits::COLOR;
        return m_Lease.IsLive() && rhiDevice.GetResources().DefineTextureState(m_Lease,
            static_cast<uint16_t>(desc.MipLevels), static_cast<uint16_t>(desc.ArrayLayers), planes);
    }

    RHITexture RHITexture::RetainGeneration() const {
        RHITexture snapshot;
        if (!IsValid()) return snapshot;
        snapshot.m_Desc = m_Desc; snapshot.m_Lease = m_Lease;
        snapshot.m_Texture = m_Texture; snapshot.m_Memory = m_Memory;
        snapshot.m_OwnsMemory = m_OwnsMemory;
        snapshot.m_SRV = m_SRV; snapshot.m_RTV = m_RTV; snapshot.m_DSV = m_DSV; snapshot.m_UAV = m_UAV;
        return snapshot;
    }

    void RHITexture::Shutdown() {
        auto& device = RHIDevice::Get();
        if (m_Lease) m_Lease.Reset();
        else if (device.IsNativeReady()) {
            const auto core = device.GetNRICore();
            auto srv = m_SRV, rtv = m_RTV, dsv = m_DSV, uav = m_UAV;
            auto texture = m_Texture; auto memory = m_OwnsMemory ? m_Memory : nullptr;
            device.ReleaseNative([core, srv, rtv, dsv, uav, texture, memory] {
                if (srv) core.DestroyDescriptor(srv);
                if (rtv) core.DestroyDescriptor(rtv);
                if (dsv) core.DestroyDescriptor(dsv);
                if (uav) core.DestroyDescriptor(uav);
                if (texture) core.DestroyTexture(texture);
                if (memory) core.FreeMemory(memory);
            });
        }
        m_SRV = m_RTV = m_DSV = m_UAV = nullptr;
        m_Texture = nullptr;
        m_Memory = nullptr;
        m_OwnsMemory = false;
        m_Desc = {};
    }

    uint64_t RHITexture::GetNativeSRVHandle() const {
        if (!IsValid() || !m_SRV) return 0;
        const auto& core = RHIDevice::Get().GetNRICore();
        return core.GetDescriptorNativeObject(m_SRV);
    }

    uint64_t RHITexture::GetNativeRTVHandle() const {
        if (!IsValid() || !m_RTV) return 0;
        const auto& core = RHIDevice::Get().GetNRICore();
        return core.GetDescriptorNativeObject(m_RTV);
    }

    uint64_t RHITexture::GetNativeDSVHandle() const {
        if (!IsValid() || !m_DSV) return 0;
        const auto& core = RHIDevice::Get().GetNRICore();
        return core.GetDescriptorNativeObject(m_DSV);
    }

    uint64_t RHITexture::GetNativeUAVHandle() const {
        if (!IsValid() || !m_UAV) return 0;
        const auto& core = RHIDevice::Get().GetNRICore();
        return core.GetDescriptorNativeObject(m_UAV);
    }

}
