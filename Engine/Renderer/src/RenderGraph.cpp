#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Axle {
    RenderGraph::~RenderGraph() { EndRecording(); }
    void RenderGraph::Reset() {
        EndRecording();
        m_Textures.clear(); m_Passes.clear(); m_Error.clear();
        m_NextPass = m_ActivePass = 0;
        m_Compiled = m_Sealed = false;
    }
    void RenderGraph::EndRecording() {
        if (m_Commands) RHI::RHIDevice::Get().GetResources().ClearAccessValidator(m_Commands, this);
        m_Commands = nullptr;
    }
    bool RenderGraph::DeclareBuffer(std::string_view name, RHI::RHIResourceLease lease, RHI::RHIAccess access, Access mode) {
        if (m_Sealed) return Fail("Buffer declaration after graph compile");
        auto pass = std::find_if(m_Passes.begin(), m_Passes.end(), [&](const auto& p) { return p.Name == name; });
        if (pass == m_Passes.end() || !lease.IsLive() || access == RHI::RHIAccess::None)
            return Fail(std::string(name) + ": invalid buffer input");
        for (auto& use : pass->Buffers) if (use.Lease.GetHandle() == lease.GetHandle()) {
            if (use.Mode != mode) return Fail(std::string(name) + ": conflicting buffer access");
            use.Access |= access;
            return true;
        }
        pass->Buffers.push_back({std::move(lease), access, mode});
        return true;
    }
    bool RenderGraph::DeclareRead(std::string_view name, const void* alias) {
        if (m_Sealed) return Fail("Input declaration after graph compile");
        auto& resources = RHI::RHIDevice::Get().GetResources();
        auto lease = resources.AcquireNative(alias);
        if (!lease.IsLive() || !resources.Pin(lease).IsLive()) return Fail(std::string(name) + ": input has no live generation");
        auto pass = std::find_if(m_Passes.begin(), m_Passes.end(), [&](const auto& p) { return p.Name == name; });
        if (pass == m_Passes.end()) return Fail(std::string(name) + ": input has no pass");
        for (const auto& input : resources.GetAccesses(lease)) {
            if (input.Texture) {
                const auto id = Import("ExternalInput", input.Texture, true);
                const auto existing = std::find_if(pass->Uses.begin(), pass->Uses.end(), [&](const auto& use) { return use.Texture == id; });
                if (existing == pass->Uses.end()) pass->Uses.push_back({id, RHI::RHIResourceLayout::ShaderResource});
                else if (existing->Layout != RHI::RHIResourceLayout::ShaderResource)
                    return Fail(std::string(name) + ": sampled input conflicts with declared texture use");
            } else if (input.Buffer && !DeclareBuffer(name, input.Resource, RHI::RHIAccess::ConstantBuffer)) return false;
        }
        return true;
    }
    bool RenderGraph::Validate(const RHI::RHIResourceAccess& input) {
        if (!m_Compiled || m_ActivePass >= m_Passes.size()) return Fail("Resource access outside a compiled pass");
        const auto& pass = m_Passes[m_ActivePass];
        if (input.Texture) {
            for (const auto& use : pass.Uses) {
                const auto& texture = m_Textures[use.Texture];
                if (texture.Lease.GetHandle() != input.Resource.GetHandle()) continue;
                if (input.Layout != nri::Layout::UNDEFINED && input.Layout != RHI::ToNRILayout(use.Layout))
                    return Fail(pass.Name + ": texture layout differs from declaration: " + texture.Name);
                return true;
            }
        } else if (input.Buffer) {
            for (const auto& use : pass.Buffers) if (use.Lease.GetHandle() == input.Resource.GetHandle()) {
                if ((static_cast<uint32_t>(input.Access) & ~static_cast<uint32_t>(RHI::ToNRIAccessBits(use.Access))) != 0)
                    return Fail(pass.Name + ": buffer access differs from declaration");
                return true;
            }
        } else return true; // pipeline/layout/pool/sampler lifetime has no physical access
        return Fail(pass.Name + ": undeclared physical resource generation");
    }

    RenderGraph::Resource RenderGraph::Import(std::string name, nri::Texture* texture, bool defined) {
        if (m_Sealed) { Fail("Import after graph compile"); return UINT32_MAX; }
        m_Compiled = false;
        // Different roles may alias the same external output (no HDR/FXAA/MSAA).
        if (texture) for (Resource i = 0; i < m_Textures.size(); ++i)
            if (m_Textures[i].Native == texture) return i;
        auto lease = texture ? RHI::RHIDevice::Get().GetResources().AcquireNative(texture) : RHI::RHIResourceLease{};
        m_Textures.push_back({std::move(name), texture, defined, std::move(lease)});
        return static_cast<Resource>(m_Textures.size() - 1);
    }

    void RenderGraph::AddPass(std::string name, std::vector<Use> uses, std::vector<std::string> dependencies) {
        if (m_Sealed) { Fail("Pass declaration after graph compile"); return; }
        m_Compiled = false;
        m_Passes.push_back({std::move(name), std::move(uses), std::move(dependencies), {}});
    }

    bool RenderGraph::Fail(std::string error) {
        m_Error = std::move(error);
        m_Compiled = false;
        return false;
    }

    bool RenderGraph::Compile(bool strictPhysicalResources, bool validateBindings) {
        m_ValidateBindings = validateBindings;
        if (!m_Error.empty()) return false;
        m_Compiled = false; m_NextPass = 0;
        struct BufferState { RHI::RHIResourceHandle Handle; int64_t Writer = -1; std::vector<uint32_t> Readers; };
        std::vector<BufferState> buffers;
        std::vector<bool> defined;
        std::vector<std::vector<uint32_t>> users(m_Textures.size());
        std::vector<int64_t> writers(m_Textures.size(), -1);
        for (const auto& texture : m_Textures) defined.push_back(texture.Defined);
        std::unordered_map<std::string, uint32_t> names;
        for (uint32_t p = 0; p < m_Passes.size(); ++p) {
            auto& pass = m_Passes[p]; pass.Dependencies.clear();
            if (pass.Name.empty() || names.count(pass.Name)) return Fail("Duplicate or empty pass name: " + pass.Name);
            auto depend = [&](uint32_t previous) {
                if (std::find(pass.Dependencies.begin(), pass.Dependencies.end(), previous) == pass.Dependencies.end())
                    pass.Dependencies.push_back(previous);
            };
            for (const auto& required : pass.Required) {
                auto found = names.find(required);
                if (found == names.end()) return Fail(pass.Name + ": dependency must precede its consumer: " + required);
                depend(found->second);
            }
            std::unordered_set<Resource> used;
            for (const auto& use : pass.Uses) {
                if (use.Texture >= m_Textures.size()) return Fail(pass.Name + ": undefined resource");
                if (!used.insert(use.Texture).second) return Fail(pass.Name + ": aliased or duplicate resource use");
                const auto& texture = m_Textures[use.Texture];
                if (strictPhysicalResources && (!texture.Native || !texture.Lease.IsLive()))
                    return Fail(pass.Name + ": resource has no live physical generation: " + texture.Name);
                const bool reads = use.Mode != Access::Write || use.LoadOp == Load::Load;
                const bool writes = use.Mode != Access::Read;
                const bool attachment = use.Layout == RHI::RHIResourceLayout::ColorAttachment ||
                                        use.Layout == RHI::RHIResourceLayout::DepthStencilWrite;
                if (use.Layout == RHI::RHIResourceLayout::Undefined) return Fail(pass.Name + ": undefined layout");
                const bool readLayout = use.Layout == RHI::RHIResourceLayout::ShaderResource ||
                    use.Layout == RHI::RHIResourceLayout::CopySource || use.Layout == RHI::RHIResourceLayout::ResolveSource ||
                    use.Layout == RHI::RHIResourceLayout::DepthStencilRead;
                const bool writeLayout = use.Layout == RHI::RHIResourceLayout::CopyDest || use.Layout == RHI::RHIResourceLayout::ResolveDest;
                if ((attachment || use.Layout == RHI::RHIResourceLayout::ShaderResourceStorage) && use.Mode == Access::Read)
                    return Fail(pass.Name + ": writable view requires explicit write access: " + texture.Name);
                if ((readLayout && use.Mode != Access::Read) || (writeLayout && use.Mode != Access::Write))
                    return Fail(pass.Name + ": layout/access mismatch: " + texture.Name);
                if ((use.LoadOp != Load::None && (!writes || !attachment)) ||
                    (writes && attachment && use.LoadOp == Load::None))
                    return Fail(pass.Name + ": invalid attachment load declaration: " + texture.Name);
                if (!writes && use.StoreOp == Store::Discard) return Fail(pass.Name + ": read cannot discard contents");
                if (reads && !defined[use.Texture]) return Fail(pass.Name + ": read/LOAD before stored write: " + texture.Name);
                if (writers[use.Texture] >= 0) depend(static_cast<uint32_t>(writers[use.Texture]));
                if (writes) {
                    for (auto prior : users[use.Texture]) depend(prior); // WAR as well as RAW/WAW.
                    users[use.Texture].clear(); writers[use.Texture] = p;
                    defined[use.Texture] = use.StoreOp == Store::Preserve;
                } else users[use.Texture].push_back(p);
            }
            for (const auto& use : pass.Buffers) {
                auto& resources = RHI::RHIDevice::Get().GetResources();
                bool physical = false;
                for (const auto& input : resources.GetAccesses(use.Lease)) if (input.Buffer) physical = true;
                if (!use.Lease.IsLive() || (strictPhysicalResources && !physical)) return Fail(pass.Name + ": missing buffer generation");
                const auto writes = RHI::RHIAccess::CopyDest | RHI::RHIAccess::ShaderResourceStorage;
                if ((use.Mode == Access::Read) == ((use.Access & writes) != RHI::RHIAccess::None))
                    return Fail(pass.Name + ": buffer layout/access mismatch");
                auto state = std::find_if(buffers.begin(), buffers.end(), [&](const auto& b) { return b.Handle == use.Lease.GetHandle(); });
                if (state == buffers.end()) { buffers.push_back({use.Lease.GetHandle()}); state = buffers.end() - 1; }
                if (state->Writer >= 0) depend(static_cast<uint32_t>(state->Writer));
                if (use.Mode == Access::Read) state->Readers.push_back(p);
                else { for (auto reader : state->Readers) depend(reader); state->Readers.clear(); state->Writer = p; }
            }
            names.emplace(pass.Name, p);
        }
        m_Compiled = true; m_Sealed = true;
        return true;
    }

    bool RenderGraph::BeginPass(std::string_view name, RHI::RHICommandBuffer& commands) {
        if (!m_Compiled || m_NextPass >= m_Passes.size() || m_Passes[m_NextPass].Name != name)
            return Fail("Pass recording differs from compiled order: " + std::string(name));
        m_ActivePass = m_NextPass;
        m_Commands = commands.GetNative();
        if (m_ValidateBindings && !RHI::RHIDevice::Get().GetResources().SetAccessValidator(m_Commands, this,
            [this](const RHI::RHIResourceAccess& input) { return Validate(input); }))
            return Fail(std::string(name) + ": command access scope unavailable");
        for (const auto& use : m_Passes[m_NextPass].Uses) {
            auto& texture = m_Textures[use.Texture];
            if (!texture.Native) return Fail(std::string(name) + ": missing physical texture: " + texture.Name);
            if (!texture.Lease.IsLive() || !commands.KeepAlive(texture.Lease))
                return Fail(std::string(name) + ": dead texture generation: " + texture.Name);
            // Undefined is a request to consult the device's transactional state
            // journal, not an instruction to discard an already written image.
            commands.TransitionTexture(texture.Native, RHI::RHIResourceLayout::Undefined, use.Layout);
            if (!commands.IsRecording())
                return Fail(std::string(name) + ": transition recording failed: " + texture.Name);
        }
        ++m_NextPass;
        return true;
    }

    bool RenderGraph::DeclareDescriptorRead(const void* descriptor, RHI::RHICommandBuffer& commands) {
        if (!m_Compiled || m_NextPass == 0 || m_NextPass > m_Passes.size() || !descriptor)
            return Fail("Descriptor read declared outside an active graph pass");
        // The graph owns the dependency/lifetime edge here.  Descriptor
        // resources may be declared while a graphics rendering scope is
        // active, so do not inject an implicit barrier from this helper;
        // texture layout transitions remain the graph's declared Use entries.
        auto lease = RHI::RHIDevice::Get().GetResources().AcquireNative(descriptor);
        if (!lease.IsLive() || !commands.KeepAlive(lease))
            return Fail("Descriptor read could not retain its transitive resource leases");
        return true;
    }
}
