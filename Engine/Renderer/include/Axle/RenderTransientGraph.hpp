#pragma once

#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Axle {

    // CPU-only companion to the existing imported-texture RenderGraph. This
    // describes whole resources on one graphics queue; it allocates nothing.
    class RenderTransientGraph {
    public:
        using Resource = uint32_t;
        using PassId = uint32_t;
        enum class Kind { Imported, TransientTexture, TransientBuffer };
        enum class Queue { Graphics, Compute, Copy };
        enum class Access { Read, Write, ReadWrite };
        enum class Load { None, Load, Clear, Discard };
        enum class Store { Preserve, Discard };
        enum class AllocationKind { UnaliasedTransient, IntervalPacked };
        struct Options { bool AllowAliasing = false; };
        enum class Usage {
            ShaderResource, Storage, ColorAttachment, DepthAttachment,
            CopySource, CopyDest, ResolveSource, ResolveDest,
            VertexBuffer, IndexBuffer, ConstantBuffer, IndirectBuffer
        };

        // Supplied value metadata, not a hardware support claim. Later RHI
        // realization must query and revalidate native requirements on its owner.
        struct MemoryRequirements {
            uint64_t SizeBytes = 0;
            uint64_t Alignment = 1;
            uint64_t Granularity = 1;
            uint32_t MemoryType = 0;
            bool MustBeDedicated = false;
        };
        struct ResourceDesc {
            std::string Name;
            Kind Type = Kind::Imported;
            std::variant<RHI::RHITextureDesc, RHI::RHIBufferDesc> Description;
            MemoryRequirements Memory;
            bool ContentsDefined = false; // only valid for imports
            bool AllowAlias = false; // recorded, never enabled by this planner
        };
        struct Use {
            Resource Id = UINT32_MAX;
            Usage State = Usage::ShaderResource;
            // Write/Preserve promises whole-resource initialization. Partial
            // attachment writes preserving old contents must declare LOAD.
            Access Mode = Access::Read;
            Load LoadOp = Load::None;
            Store StoreOp = Store::Preserve;
        };
        struct Pass {
            std::string Name;
            std::vector<Use> Uses;
            std::vector<PassId> Dependencies; // forward references allowed
            Queue ExecutionQueue = Queue::Graphics;
        };
        struct CompatibilityKey {
            Kind Type = Kind::Imported;
            Queue ExecutionQueue = Queue::Graphics;
            RHI::RHIMemoryLocation MemoryLocation = RHI::RHIMemoryLocation::Device;
            RHI::RHITextureFormat Format = RHI::RHITextureFormat::RGBA8_UNORM;
            uint32_t Width = 0, Height = 0, Depth = 0;
            uint32_t MipLevels = 0, ArrayLayers = 0, SampleCount = 0;
            uint32_t UsageBits = 0, StructureStride = 0, MemoryType = 0;
            uint64_t SizeBytes = 0, Alignment = 1, Granularity = 1;
            bool IsCubeMap = false, ByteAddress = false, MustBeDedicated = false;
            bool operator==(const CompatibilityKey&) const = default;
        };
        struct Lifetime {
            Resource Id = UINT32_MAX;
            uint32_t FirstPass = UINT32_MAX; // index in PassOrder, closed interval
            uint32_t LastPass = 0;
            CompatibilityKey Key;
            bool operator==(const Lifetime&) const = default;
        };
        struct Allocation {
            Resource Id = UINT32_MAX;
            uint32_t Block = UINT32_MAX;
            uint64_t SizeBytes = 0;
            bool operator==(const Allocation&) const = default;
        };
        struct AliasEvent {
            uint32_t Block = UINT32_MAX;
            Resource Previous = UINT32_MAX, Next = UINT32_MAX;
            uint32_t BeforePass = 0, AfterPass = 0;
            bool operator==(const AliasEvent&) const = default;
        };
        struct Plan {
            // Owning snapshots prevent edits to the graph from changing a plan.
            std::vector<ResourceDesc> Resources;
            std::vector<Pass> Passes;
            std::vector<PassId> PassOrder;
            std::vector<std::vector<PassId>> Dependencies; // original pass IDs
            std::vector<Lifetime> Lifetimes; // transient resources in ID order
            std::vector<Allocation> Allocations; // resource ID order, abstract block IDs
            std::vector<AliasEvent> AliasEvents;
            AllocationKind Mode = AllocationKind::UnaliasedTransient;
            uint32_t BlockCount = 0;
            uint64_t RequestedBytes = 0;
            uint64_t UnaliasedAllocationBytes = 0;
            uint64_t PlannedAllocationBytes = 0;
            uint64_t PlannedPeakBytes = 0; // all unaliased candidates coexist
            uint32_t IgnoredAliasRequests = 0;
            std::string Diagnostic;
        };

        Resource AddResource(ResourceDesc resource);
        PassId AddPass(Pass pass);
        // No native calls or publication. Failure preserves the destination plan.
        bool Compile(Plan& destination, std::string* error = nullptr) const;
        // CPU planning only; does not enable a native capability. The realizer
        // must re-query requirements and prove barriers before sharing memory.
        bool Compile(Plan& destination, Options options, std::string* error = nullptr) const;

    private:
        std::vector<ResourceDesc> m_Resources;
        std::vector<Pass> m_Passes;
    };
}
