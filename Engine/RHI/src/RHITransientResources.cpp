#include <Axle/RHI/RHITransientResources.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>

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
        std::vector<RHIResourceLease> MemoryBlocks;
        std::vector<uint32_t> BlockIds;
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
    bool RHITransientResources::QueryRequirements(const RHITransientDesc& description, RHITransientRequirements& requirements) {
        nri::MemoryDesc queried{};
        const auto* texture = std::get_if<RHITextureDesc>(&description);
        const bool ok = texture ? RHITexture::QueryMemoryRequirements(*texture, queried) :
            RHIBuffer::QueryMemoryRequirements(std::get<RHIBufferDesc>(description), queried);
        if (!ok || !queried.size || !queried.alignment) return false;
        requirements = {queried.size, queried.alignment, queried.type, queried.mustBeDedicated};
        return true;
    }
    bool RHITransientResources::InitPlaced(const std::vector<RHITransientDesc>& descriptions, const std::vector<uint32_t>& blocks) {
        auto& device = RHIDevice::Get();
        if (!device.CanCreateResources() || !device.GetCapabilities().HasPlacedResourcePrimitives ||
            descriptions.size() >= UINT32_MAX || (!blocks.empty() && blocks.size() != descriptions.size())) return false;
        auto candidate = std::make_unique<Storage>(); candidate->Epoch = device.GetEpoch();
        candidate->Entries.resize(descriptions.size());
        struct Block { uint32_t First = UINT32_MAX; nri::Memory* Native = nullptr; };
        std::vector<Block> memory(descriptions.size());
        std::vector<uint32_t> assignments;
        for (uint32_t i = 0; i < descriptions.size(); ++i) {
            const auto block = blocks.empty() ? i : blocks[i];
            if (block >= descriptions.size()) return false;
            assignments.push_back(block);
            const auto* buffer = std::get_if<RHIBufferDesc>(&descriptions[i]);
            if (buffer && buffer->MemoryLocation != RHIMemoryLocation::Device) return false;
            auto& requirements = candidate->Entries[i].Requirements;
            if (!QueryRequirements(descriptions[i], requirements)) return false;
            auto& backing = memory[block];
            if (backing.First == UINT32_MAX) backing.First = i;
            else {
                const auto& previous = candidate->Entries[backing.First].Requirements;
                if (!device.GetCapabilities().HasResourceAliasingFeature || requirements.MustBeDedicated || previous.MustBeDedicated ||
                    !SameTransientDescription(descriptions[i], descriptions[backing.First]) ||
                    requirements.SizeBytes != previous.SizeBytes || requirements.Alignment != previous.Alignment ||
                    requirements.MemoryType != previous.MemoryType) return false;
            }
        }
        const auto core = device.GetNRICore();
        bool gap = false;
        for (auto& block : memory) {
            if (block.First == UINT32_MAX) { gap = true; continue; }
            if (gap) return false;
            const auto& requirements = candidate->Entries[block.First].Requirements;
            nri::AllocateMemoryDesc allocation{};
            allocation.size = requirements.SizeBytes; allocation.type = requirements.MemoryType;
            if (const auto* texture = std::get_if<RHITextureDesc>(&descriptions[block.First]))
                allocation.allowMultisampleTextures = texture->SampleCount > 1;
            if (candidate->AllocatedBytes > UINT64_MAX - allocation.size) return false;
            if (ScopedRHIFault::Invoke("AllocateTransientMemory", [&] {
                return core.AllocateMemory(*device.GetNRIDevice(), allocation, block.Native);
            }) != nri::Result::SUCCESS || !block.Native) return false;
            auto* native = block.Native;
            auto lease = device.GetResources().Publish(RHIResourceKind::Memory, "Transient block", {native},
                [core, native] { core.FreeMemory(native); });
            if (!lease.IsLive()) { core.FreeMemory(block.Native); return false; }
            candidate->MemoryBlocks.push_back(std::move(lease));
            candidate->AllocatedBytes += allocation.size;
        }
        for (uint32_t i = 0; i < descriptions.size(); ++i) {
            auto& entry = candidate->Entries[i];
            auto* backing = memory[assignments[i]].Native;
            if (const auto* texture = std::get_if<RHITextureDesc>(&descriptions[i])) {
                if (!entry.Texture.InitPlaced(*texture, backing)) return false;
                entry.View = MakeTransientView(entry.Texture);
            } else {
                if (!entry.Buffer.InitPlaced(std::get<RHIBufferDesc>(descriptions[i]), backing, 0)) return false;
                entry.View = MakeTransientView(entry.Buffer);
            }
        }
        candidate->BlockIds = std::move(assignments);
        m_Storage = std::move(candidate); return true;
    }
    bool RHITransientResources::RecordAlias(RHICommandBuffer& commands, uint32_t previous, uint32_t next, RHIAccess nextAccess) {
        auto& device = RHIDevice::Get();
        const auto* before = Get(previous); const auto* after = Get(next);
        if (!before || !after || previous == next || !commands.IsRecording() ||
            !device.CanSubmit() || !device.GetCapabilities().HasResourceAliasingFeature ||
            (device.GetBackend() != RHIType::Vulkan &&
                !(device.GetBackend() == RHIType::Direct3D12 && device.GetCapabilities().IsEnhancedBarriersSupported)) ||
            m_Storage->BlockIds.size() != m_Storage->Entries.size() ||
            m_Storage->BlockIds[previous] != m_Storage->BlockIds[next] ||
            !device.GetResources().CanAliasAfterCopySource(commands.GetNative(), before->Lease, after->Lease) ||
            (nextAccess != RHIAccess::CopyDest && !(after->Texture && nextAccess == RHIAccess::ColorAttachment)) ||
            !commands.KeepAlive(before->Lease) || !commands.KeepAlive(after->Lease)) return false;
        RHIBarrierGroup barriers;
        // Previous writes have already transitioned to copy-read. This alias
        // boundary waits for those reads before the next full clear/copy. It
        // deliberately performs no texture layout transition or write flush.
        // D3D12 requires enhanced barriers: legacy NRI global barriers are UAV-only.
        barriers.AddGlobal(RHIAccess::CopySource, nextAccess, RHIPipelineStage::AllCommands, RHIPipelineStage::AllCommands);
        return ScopedRHIFault::Invoke("TransientAliasBarrier", [&] {
            commands.PipelineBarrier(barriers);
            return commands.IsRecording() ? nri::Result::SUCCESS : nri::Result::FAILURE;
        }) == nri::Result::SUCCESS;
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
