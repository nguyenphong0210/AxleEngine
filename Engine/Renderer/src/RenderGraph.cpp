#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Axle {
    RenderGraph::Resource RenderGraph::Import(std::string name, nri::Texture* texture, bool defined) {
        m_Compiled = false;
        // Different roles may alias the same external output (no HDR/FXAA/MSAA).
        if (texture) for (Resource i = 0; i < m_Textures.size(); ++i)
            if (m_Textures[i].Native == texture) return i;
        auto lease = texture ? RHI::RHIDevice::Get().GetResources().AcquireNative(texture) : RHI::RHIResourceLease{};
        m_Textures.push_back({std::move(name), texture, defined, std::move(lease)});
        return static_cast<Resource>(m_Textures.size() - 1);
    }

    void RenderGraph::AddPass(std::string name, std::vector<Use> uses, std::vector<std::string> dependencies) {
        m_Compiled = false;
        m_Passes.push_back({std::move(name), std::move(uses), std::move(dependencies), {}});
    }

    bool RenderGraph::Fail(std::string error) {
        m_Error = std::move(error);
        m_Compiled = false;
        return false;
    }

    bool RenderGraph::Compile() {
        m_Compiled = false; m_Error.clear(); m_NextPass = 0;
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
                const bool reads = use.Mode != Access::Write || use.LoadOp == Load::Load;
                const bool writes = use.Mode != Access::Read;
                const bool attachment = use.Layout == RHI::RHIResourceLayout::ColorAttachment ||
                                        use.Layout == RHI::RHIResourceLayout::DepthStencilWrite;
                if (use.Layout == RHI::RHIResourceLayout::Undefined) return Fail(pass.Name + ": undefined layout");
                const bool readLayout = use.Layout == RHI::RHIResourceLayout::ShaderResource ||
                    use.Layout == RHI::RHIResourceLayout::CopySource || use.Layout == RHI::RHIResourceLayout::ResolveSource ||
                    use.Layout == RHI::RHIResourceLayout::DepthStencilRead;
                const bool writeLayout = use.Layout == RHI::RHIResourceLayout::CopyDest || use.Layout == RHI::RHIResourceLayout::ResolveDest;
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
            names.emplace(pass.Name, p);
        }
        m_Compiled = true;
        return true;
    }

    bool RenderGraph::BeginPass(std::string_view name, RHI::RHICommandBuffer& commands) {
        if (!m_Compiled || m_NextPass >= m_Passes.size() || m_Passes[m_NextPass].Name != name)
            return Fail("Pass recording differs from compiled order: " + std::string(name));
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
}
