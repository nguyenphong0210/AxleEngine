#include <Axle/RenderTransientGraph.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_set>
#include <utility>

namespace Axle {
    namespace {
        using Graph = RenderTransientGraph;

        bool Add(uint64_t a, uint64_t b, uint64_t& result) {
            if (a > UINT64_MAX - b) return false;
            result = a + b;
            return true;
        }
        bool Multiply(uint64_t a, uint64_t b, uint64_t& result) {
            if (b && a > UINT64_MAX / b) return false;
            result = a * b;
            return true;
        }
        bool PowerOfTwo(uint64_t value) { return value && !(value & (value - 1)); }

        bool TextureDescription(const RHI::RHITextureDesc& desc, uint64_t& minimumBytes) {
            using Format = RHI::RHITextureFormat;
            using Usage = RHI::RHITextureUsage;
            uint32_t bytesPerTexel = 0;
            switch (desc.Format) {
                case Format::RGBA8_UNORM: case Format::RGBA8_SRGB: case Format::BGRA8_UNORM:
                case Format::R32_FLOAT: case Format::R32_UINT: case Format::D32_FLOAT:
                case Format::D24_UNORM_S8_UINT: bytesPerTexel = 4; break;
                case Format::RGBA16_FLOAT: bytesPerTexel = 8; break;
                case Format::RGBA32_FLOAT: bytesPerTexel = 16; break;
                default: return false;
            }
            const bool depthFormat = desc.Format == Format::D32_FLOAT || desc.Format == Format::D24_UNORM_S8_UINT;
            uint32_t maxMips = 0;
            for (auto dim = std::max({desc.Width, desc.Height, desc.Depth}); dim; dim >>= 1) ++maxMips;
            if (!desc.Width || !desc.Height || !desc.Depth || !desc.MipLevels || !desc.ArrayLayers ||
                desc.MipLevels > maxMips || desc.Usage == Usage::None ||
                (static_cast<uint32_t>(desc.Usage) & ~127u) ||
                (desc.SampleCount != 1 && desc.SampleCount != 2 && desc.SampleCount != 4 && desc.SampleCount != 8) ||
                (desc.SampleCount > 1 && (desc.MipLevels != 1 || desc.Depth > 1 ||
                    (desc.Usage & Usage::Storage) || (desc.Usage & Usage::HostTransfer))) ||
                ((desc.Usage & Usage::DepthStencil) && !depthFormat) ||
                (depthFormat && ((desc.Usage & Usage::ColorAttachment) || (desc.Usage & Usage::Storage))) ||
                (desc.Depth > 1 && (desc.ArrayLayers != 1 || desc.IsCubeMap || (desc.Usage & Usage::DepthStencil))) ||
                (desc.IsCubeMap && (desc.Width != desc.Height || desc.ArrayLayers != 6 || desc.SampleCount != 1))) return false;

            minimumBytes = 0;
            uint32_t w = desc.Width, h = desc.Height, d = desc.Depth;
            for (uint32_t mip = 0; mip < desc.MipLevels; ++mip) {
                uint64_t bytes = w;
                if (!Multiply(bytes, h, bytes) || !Multiply(bytes, d, bytes) ||
                    !Multiply(bytes, desc.ArrayLayers, bytes) || !Multiply(bytes, desc.SampleCount, bytes) ||
                    !Multiply(bytes, bytesPerTexel, bytes) || !Add(minimumBytes, bytes, minimumBytes)) return false;
                w = std::max(1u, w >> 1); h = std::max(1u, h >> 1); d = std::max(1u, d >> 1);
            }
            return true;
        }

        bool BufferDescription(const RHI::RHIBufferDesc& desc) {
            if (!desc.Size || (static_cast<uint32_t>(desc.Usage) & ~127u) ||
                static_cast<uint32_t>(desc.MemoryLocation) > static_cast<uint32_t>(RHI::RHIMemoryLocation::HostReadback) ||
                (desc.StructureStride && (desc.StructureStride % 4 || desc.StructureStride > 2048 ||
                    desc.Size % desc.StructureStride)) ||
                (desc.ByteAddress && (desc.Size % 4 || desc.StructureStride)) ||
                (desc.MemoryLocation == RHI::RHIMemoryLocation::HostReadback && desc.Usage != RHI::RHIBufferUsage::None)) return false;
            return true;
        }

        bool ValidUse(const Graph::ResourceDesc& resource, const Graph::Use& use) {
            using Usage = Graph::Usage;
            if ((use.Mode != Graph::Access::Read && use.Mode != Graph::Access::Write && use.Mode != Graph::Access::ReadWrite) ||
                (use.LoadOp != Graph::Load::None && use.LoadOp != Graph::Load::Load &&
                    use.LoadOp != Graph::Load::Clear && use.LoadOp != Graph::Load::Discard) ||
                (use.StoreOp != Graph::Store::Preserve && use.StoreOp != Graph::Store::Discard)) return false;
            const bool writes = use.Mode != Graph::Access::Read;
            const bool attachment = use.State == Usage::ColorAttachment || use.State == Usage::DepthAttachment;
            if ((use.LoadOp != Graph::Load::None && (!writes || !attachment)) ||
                (attachment && writes && use.LoadOp == Graph::Load::None) ||
                (!writes && use.StoreOp == Graph::Store::Discard)) return false;

            if (const auto* texture = std::get_if<RHI::RHITextureDesc>(&resource.Description)) {
                using Bits = RHI::RHITextureUsage;
                switch (use.State) {
                    case Usage::ShaderResource: return !writes && (texture->Usage & Bits::ShaderResource);
                    case Usage::Storage: return texture->Usage & Bits::Storage;
                    case Usage::ColorAttachment: return writes && (texture->Usage & Bits::ColorAttachment);
                    case Usage::DepthAttachment: return texture->Usage & Bits::DepthStencil;
                    case Usage::CopySource: return use.Mode == Graph::Access::Read && (texture->Usage & Bits::CopySource);
                    case Usage::CopyDest: return use.Mode == Graph::Access::Write && (texture->Usage & Bits::CopyDest);
                    case Usage::ResolveSource: return use.Mode == Graph::Access::Read && texture->SampleCount > 1 &&
                        (texture->Usage & Bits::CopySource);
                    case Usage::ResolveDest: return use.Mode == Graph::Access::Write && texture->SampleCount == 1 &&
                        (texture->Usage & Bits::CopyDest);
                    default: return false;
                }
            }
            const auto& buffer = std::get<RHI::RHIBufferDesc>(resource.Description);
            using Bits = RHI::RHIBufferUsage;
            auto has = [&](Bits flag) { return (buffer.Usage & flag) != Bits::None; };
            switch (use.State) {
                case Usage::ShaderResource: return !writes && has(Bits::ShaderResource);
                case Usage::Storage: return has(Bits::ShaderResourceStorage);
                case Usage::CopySource: return use.Mode == Graph::Access::Read;
                case Usage::CopyDest: return use.Mode == Graph::Access::Write;
                case Usage::VertexBuffer: return !writes && has(Bits::Vertex);
                case Usage::IndexBuffer: return !writes && has(Bits::Index);
                case Usage::ConstantBuffer: return !writes && has(Bits::Constant);
                case Usage::IndirectBuffer: return !writes && has(Bits::Argument);
                default: return false;
            }
        }
    }

    RenderTransientGraph::Resource RenderTransientGraph::AddResource(ResourceDesc resource) {
        if (m_Resources.size() >= UINT32_MAX) return UINT32_MAX;
        m_Resources.push_back(std::move(resource));
        return static_cast<Resource>(m_Resources.size() - 1);
    }

    RenderTransientGraph::PassId RenderTransientGraph::AddPass(Pass pass) {
        if (m_Passes.size() >= UINT32_MAX) return UINT32_MAX;
        m_Passes.push_back(std::move(pass));
        return static_cast<PassId>(m_Passes.size() - 1);
    }

    bool RenderTransientGraph::Compile(Plan& destination, std::string* error) const {
        if (error) error->clear();
        auto fail = [&](std::string message) {
            if (error) *error = std::move(message);
            return false;
        };
        Plan candidate;
        std::vector<CompatibilityKey> keys(m_Resources.size());
        std::vector<uint64_t> allocationSizes(m_Resources.size(), 0);
        std::unordered_set<std::string> names;
        for (Resource r = 0; r < m_Resources.size(); ++r) {
            const auto& desc = m_Resources[r];
            if (desc.Name.empty() || !names.insert(desc.Name).second)
                return fail("Duplicate or empty resource name: " + desc.Name);
            const bool texture = std::holds_alternative<RHI::RHITextureDesc>(desc.Description);
            if (desc.Type != Kind::Imported && desc.Type != Kind::TransientTexture && desc.Type != Kind::TransientBuffer)
                return fail("Invalid resource kind: " + desc.Name);
            if ((desc.Type == Kind::TransientTexture && !texture) || (desc.Type == Kind::TransientBuffer && texture))
                return fail("Resource kind/description mismatch: " + desc.Name);
            auto& key = keys[r];
            key.Type = desc.Type;
            uint64_t minimumBytes = 0;
            if (texture) {
                const auto& t = std::get<RHI::RHITextureDesc>(desc.Description);
                if (!TextureDescription(t, minimumBytes)) return fail("Invalid or overflowing texture description: " + desc.Name);
                key.Format = t.Format; key.Width = t.Width; key.Height = t.Height; key.Depth = t.Depth;
                key.MipLevels = t.MipLevels; key.ArrayLayers = t.ArrayLayers; key.SampleCount = t.SampleCount;
                key.IsCubeMap = t.IsCubeMap; key.UsageBits = static_cast<uint32_t>(t.Usage);
            } else {
                const auto& b = std::get<RHI::RHIBufferDesc>(desc.Description);
                if (!BufferDescription(b)) return fail("Invalid buffer description: " + desc.Name);
                minimumBytes = b.Size; key.MemoryLocation = b.MemoryLocation;
                key.StructureStride = b.StructureStride; key.ByteAddress = b.ByteAddress;
                key.UsageBits = static_cast<uint32_t>(b.Usage);
            }
            if (desc.Type == Kind::Imported) {
                if (desc.AllowAlias) return fail("Imported resources cannot allow aliasing: " + desc.Name);
                continue; // Imported memory requirements never enter the plan.
            }
            if (desc.ContentsDefined) return fail("Transient contents must start undefined: " + desc.Name);
            if (key.MemoryLocation != RHI::RHIMemoryLocation::Device)
                return fail("Only device-local transients are supported: " + desc.Name);
            const auto& mem = desc.Memory;
            if (!mem.SizeBytes || mem.SizeBytes < minimumBytes || !PowerOfTwo(mem.Alignment) || !PowerOfTwo(mem.Granularity))
                return fail("Invalid memory requirements: " + desc.Name);
            const uint64_t alignment = std::max(mem.Alignment, mem.Granularity);
            uint64_t padded;
            if (!Add(mem.SizeBytes, alignment - 1, padded)) return fail("Allocation alignment overflow: " + desc.Name);
            allocationSizes[r] = padded & ~(alignment - 1);
            key.SizeBytes = mem.SizeBytes; key.Alignment = mem.Alignment; key.Granularity = mem.Granularity;
            key.MemoryType = mem.MemoryType; key.MustBeDedicated = mem.MustBeDedicated;
            if (desc.AllowAlias) ++candidate.IgnoredAliasRequests;
        }

        names.clear();
        candidate.Dependencies.resize(m_Passes.size());
        std::vector<PassId> lastWriter(m_Resources.size(), UINT32_MAX);
        std::vector<std::vector<PassId>> readers(m_Resources.size());
        for (PassId p = 0; p < m_Passes.size(); ++p) {
            const auto& pass = m_Passes[p];
            if (pass.Name.empty() || !names.insert(pass.Name).second)
                return fail("Duplicate or empty pass name: " + pass.Name);
            if (pass.ExecutionQueue != Queue::Graphics) return fail("Unsupported queue: " + pass.Name);
            auto& dependencies = candidate.Dependencies[p];
            for (const auto dep : pass.Dependencies) {
                if (dep >= m_Passes.size()) return fail("Undefined dependency: " + pass.Name);
                dependencies.push_back(dep);
            }
            std::unordered_set<Resource> used;
            for (const auto& use : pass.Uses) {
                if (use.Id >= m_Resources.size()) return fail("Undefined resource: " + pass.Name);
                if (!used.insert(use.Id).second) return fail("Duplicate resource use: " + pass.Name);
                if (!ValidUse(m_Resources[use.Id], use)) return fail("Invalid resource use: " + pass.Name + "/" + m_Resources[use.Id].Name);
                if (lastWriter[use.Id] != UINT32_MAX) dependencies.push_back(lastWriter[use.Id]);
                if (use.Mode != Access::Read) {
                    dependencies.insert(dependencies.end(), readers[use.Id].begin(), readers[use.Id].end());
                    readers[use.Id].clear();
                    lastWriter[use.Id] = p;
                } else readers[use.Id].push_back(p);
            }
            std::sort(dependencies.begin(), dependencies.end());
            dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
        }

        std::vector<std::vector<PassId>> consumers(m_Passes.size());
        std::vector<size_t> indegree(m_Passes.size());
        std::priority_queue<PassId, std::vector<PassId>, std::greater<PassId>> ready;
        for (PassId p = 0; p < m_Passes.size(); ++p) {
            indegree[p] = candidate.Dependencies[p].size();
            for (auto dep : candidate.Dependencies[p]) consumers[dep].push_back(p);
            if (!indegree[p]) ready.push(p);
        }
        while (!ready.empty()) {
            const auto p = ready.top(); ready.pop();
            candidate.PassOrder.push_back(p);
            for (auto next : consumers[p]) if (--indegree[next] == 0) ready.push(next);
        }
        if (candidate.PassOrder.size() != m_Passes.size()) return fail("Cycle in transient graph");

        std::vector<bool> defined;
        for (const auto& resource : m_Resources) defined.push_back(resource.ContentsDefined);
        std::vector<uint32_t> first(m_Resources.size(), UINT32_MAX), last(m_Resources.size(), 0);
        for (uint32_t i = 0; i < candidate.PassOrder.size(); ++i) {
            const auto& pass = m_Passes[candidate.PassOrder[i]];
            for (const auto& use : pass.Uses) {
                const bool reads = use.Mode != Access::Write || use.LoadOp == Load::Load;
                if (reads && !defined[use.Id]) return fail("Read/LOAD before stored write: " + pass.Name + "/" + m_Resources[use.Id].Name);
                if (use.Mode != Access::Read) defined[use.Id] = use.StoreOp == Store::Preserve;
                first[use.Id] = std::min(first[use.Id], i); last[use.Id] = i;
            }
        }
        for (Resource r = 0; r < m_Resources.size(); ++r) {
            if (m_Resources[r].Type == Kind::Imported) continue;
            if (first[r] == UINT32_MAX) return fail("Unused transient resource: " + m_Resources[r].Name);
            candidate.Lifetimes.push_back({r, first[r], last[r], keys[r]});
            candidate.Allocations.push_back({r, static_cast<uint32_t>(candidate.Allocations.size()), allocationSizes[r]});
            if (!Add(candidate.RequestedBytes, keys[r].SizeBytes, candidate.RequestedBytes) ||
                !Add(candidate.PlannedAllocationBytes, allocationSizes[r], candidate.PlannedAllocationBytes))
                return fail("Transient byte statistics overflow");
        }
        candidate.PlannedPeakBytes = candidate.PlannedAllocationBytes;
        candidate.Diagnostic = "UnaliasedTransient: CPU plan only; no native allocation or alias barriers";
        if (candidate.IgnoredAliasRequests) candidate.Diagnostic += "; alias requests remain disabled";
        candidate.Resources = m_Resources;
        candidate.Passes = m_Passes;
        destination = std::move(candidate);
        return true;
    }
}
