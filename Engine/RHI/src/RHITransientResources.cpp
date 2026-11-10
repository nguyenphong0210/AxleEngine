#include <Axle/RHI/RHITransientResources.hpp>

namespace Axle::RHI {
    RHITransientView MakeTransientView(const RHITexture& texture) {
        return {texture.GetDesc(), texture.AcquireLease(), texture.GetNative(), nullptr,
            texture.GetSRV(), texture.GetRTV(), texture.GetDSV(), nullptr};
    }
    RHITransientView MakeTransientView(const RHIBuffer& buffer) {
        return {buffer.GetDesc(), buffer.AcquireLease(), nullptr, buffer.GetNative(),
            nullptr, nullptr, nullptr, buffer.GetCBV()};
    }
    bool SameTransientDescription(const RHITransientDesc& a, const RHITransientDesc& b) {
        if (a.index() != b.index()) return false;
        if (const auto* x = std::get_if<RHITextureDesc>(&a)) {
            const auto& y = std::get<RHITextureDesc>(b);
            return x->Width == y.Width && x->Height == y.Height && x->Depth == y.Depth &&
                x->MipLevels == y.MipLevels && x->ArrayLayers == y.ArrayLayers && x->SampleCount == y.SampleCount &&
                x->IsCubeMap == y.IsCubeMap && x->Format == y.Format && x->Usage == y.Usage;
        }
        const auto& x = std::get<RHIBufferDesc>(a); const auto& y = std::get<RHIBufferDesc>(b);
        return x.Size == y.Size && x.StructureStride == y.StructureStride && x.Usage == y.Usage &&
            x.MemoryLocation == y.MemoryLocation && x.ByteAddress == y.ByteAddress;
    }
    bool ValidateTransientView(const RHITransientView& view) {
        auto& device = RHIDevice::Get();
        if (!device.IsOwnerThread() || !view.IsLive() || view.Lease.GetHandle().Epoch != device.GetEpoch() ||
            bool(view.Texture) == bool(view.Buffer) || std::holds_alternative<RHITextureDesc>(view.Description) != bool(view.Texture)) return false;
        const auto handle = view.Lease.GetHandle();
        const void* native = view.Texture ? static_cast<const void*>(view.Texture) : view.Buffer;
        if (device.GetResources().AcquireNative(native).GetHandle() != handle) return false;
        for (auto* descriptor : {view.SRV, view.RTV, view.DSV, view.CBV})
            if (descriptor && device.GetResources().AcquireNative(descriptor).GetHandle() != handle) return false;
        if (view.Texture) {
            const auto& requested = std::get<RHITextureDesc>(view.Description);
            const auto& actual = device.GetNRICore().GetTextureDesc(*view.Texture);
            return actual.width == requested.Width && actual.height == requested.Height && actual.depth == requested.Depth &&
                actual.mipNum == requested.MipLevels && actual.layerNum == requested.ArrayLayers && actual.sampleNum == requested.SampleCount &&
                actual.format == ToNRIFormat(requested.Format) && actual.usage == ToNRITextureUsage(requested.Usage);
        }
        const auto& requested = std::get<RHIBufferDesc>(view.Description);
        const auto& actual = device.GetNRICore().GetBufferDesc(*view.Buffer);
        return actual.size == requested.Size && actual.structureStride == requested.StructureStride &&
            actual.usage == ToNRIBufferUsage(requested.Usage) && actual.byteAddress == requested.ByteAddress;
    }
    struct RHITransientResources::Storage {
        struct Entry {
            RHITexture Texture;
            RHIBuffer Buffer;
            RHITransientView View;
            RHITransientRequirements Requirements;
        };
        uint64_t Epoch = 0, AllocatedBytes = 0;
        std::vector<Entry> Entries;
    };
    RHITransientResources::RHITransientResources() = default;
    RHITransientResources::~RHITransientResources() = default;
    RHITransientResources::RHITransientResources(RHITransientResources&&) noexcept = default;
    RHITransientResources& RHITransientResources::operator=(RHITransientResources&&) noexcept = default;

    bool RHITransientResources::InitCommitted(const std::vector<RHITransientDesc>& descriptions) {
        auto& device = RHIDevice::Get();
        if (!device.CanCreateResources() || descriptions.size() >= UINT32_MAX) return false;
        auto candidate = std::make_unique<Storage>(); candidate->Epoch = device.GetEpoch();
        candidate->Entries.resize(descriptions.size());
        for (size_t i = 0; i < descriptions.size(); ++i) {
            auto& entry = candidate->Entries[i];
            nri::MemoryDesc requirements{};
            if (const auto* texture = std::get_if<RHITextureDesc>(&descriptions[i])) {
                if (!entry.Texture.Init(*texture)) return false;
                entry.View = MakeTransientView(entry.Texture);
                device.GetNRICore().GetTextureMemoryDesc(*entry.View.Texture, nri::MemoryLocation::DEVICE, requirements);
            } else {
                const auto& buffer = std::get<RHIBufferDesc>(descriptions[i]);
                if (buffer.MemoryLocation != RHIMemoryLocation::Device || !entry.Buffer.Init(buffer)) return false;
                entry.View = MakeTransientView(entry.Buffer);
                device.GetNRICore().GetBufferMemoryDesc(*entry.View.Buffer, nri::MemoryLocation::DEVICE, requirements);
            }
            if (!requirements.size || !requirements.alignment ||
                candidate->AllocatedBytes > UINT64_MAX - requirements.size) return false;
            entry.Requirements = {requirements.size, requirements.alignment, requirements.type, requirements.mustBeDedicated};
            candidate->AllocatedBytes += requirements.size;
        }
        m_Storage = std::move(candidate);
        return true;
    }
    const RHITransientView* RHITransientResources::Get(uint32_t index) const {
        auto& device = RHIDevice::Get();
        if (!device.IsOwnerThread() || !m_Storage || m_Storage->Epoch != device.GetEpoch() || index >= m_Storage->Entries.size()) return nullptr;
        const auto& view = m_Storage->Entries[index].View;
        return view.IsLive() ? &view : nullptr;
    }
    const RHITransientRequirements* RHITransientResources::GetRequirements(uint32_t index) const {
        return Get(index) ? &m_Storage->Entries[index].Requirements : nullptr;
    }
    uint64_t RHITransientResources::GetAllocatedBytes() const { return m_Storage ? m_Storage->AllocatedBytes : 0; }
}
