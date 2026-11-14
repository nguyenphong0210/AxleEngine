#pragma once

#include <Axle/RHI/RHIBarrier.hpp>
#include <Axle/RHI/RHIResource.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Axle {
    namespace RHI { class RHICommandBuffer; }

    // A sequential, whole-texture graph. Resources are borrowed, never allocated
    // here; recording pins their exact generations until queue completion. The
    // command journal owns first-use/native state across views and submissions.
    class RenderGraph {
    public:
        using Resource = uint32_t;
        enum class Access { Read, Write, ReadWrite };
        enum class Load { None, Load, Clear, Discard };
        enum class Store { Preserve, Discard };
        struct Use {
            Resource Texture;
            RHI::RHIResourceLayout Layout;
            Access Mode = Access::Read;
            Load LoadOp = Load::None;
            Store StoreOp = Store::Preserve;
        };

        Resource Import(std::string name, nri::Texture* texture, bool contentsDefined = false);
        void AddPass(std::string name, std::vector<Use> uses,
                     std::vector<std::string> dependencies = {});
        // When strictPhysicalResources is true, every resource used by a pass
        // must already have a native texture and live generation lease.
        bool Compile(bool strictPhysicalResources = false);
        bool BeginPass(std::string_view name, RHI::RHICommandBuffer& commands);
        bool IsComplete() const { return m_Compiled && m_NextPass == m_Passes.size(); }
        const std::string& GetError() const { return m_Error; }
        const std::vector<uint32_t>& GetDependencies(uint32_t pass) const { return m_Passes.at(pass).Dependencies; }

    private:
        struct Texture { std::string Name; nri::Texture* Native; bool Defined; RHI::RHIResourceLease Lease; };
        struct Pass {
            std::string Name;
            std::vector<Use> Uses;
            std::vector<std::string> Required;
            std::vector<uint32_t> Dependencies;
        };
        bool Fail(std::string error);
        std::vector<Texture> m_Textures;
        std::vector<Pass> m_Passes;
        std::string m_Error;
        size_t m_NextPass = 0;
        bool m_Compiled = false;
    };
}
