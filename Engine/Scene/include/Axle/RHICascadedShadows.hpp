#pragma once
#include <Axle/CascadedShadowMap.hpp>
#include <Axle/RHIShadowDepthAtlas.hpp>
#include <Axle/RenderWorld.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/MaterialTextureCache.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHITransform.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>

namespace Axle {
    // b4/space0 in ForwardPBR; padding preserves the 256-byte CBV size rule.
    struct alignas(256) RHICascadeConstants {
        glm::mat4 Matrices[4]{glm::mat4(1),glm::mat4(1),glm::mat4(1),glm::mat4(1)};
        glm::vec4 Splits{0};
        glm::vec4 Atlas{0}; // cascade count, reciprocal atlas size, per-tile resolution, near clip
        glm::vec4 Bias{0};  // normalized base, normalized slope, world normal, PCF tap spacing
        glm::vec4 Padding[13]{};
    };
    static_assert(sizeof(RHICascadeConstants)==512);

    // Render-owner object. Prepare only after the owning view frame Begin has
    // waited for its preceding submission; command bindings pin exact generations.
    class RHICascadedShadows {
    public:
        bool Init();
        void Shutdown();
        bool Prepare(const RenderWorld&, const PerspectiveCamera&, const SceneRenderSettings&, MaterialTextureCache&);
        bool Record(RHI::RHICommandBuffer&);
        bool DeclareInputs(RenderGraph& graph, std::string_view pass) const { return m_Depth.DeclareInputs(graph, pass); }
        bool IsSupported() const { return m_Supported; }
        bool IsActive() const { return m_CPU.GetFrame().Valid && m_Constants.Atlas.x>0; }
        RHI::RHITexture* GetAtlas() { return m_Depth.GetAtlas(); }
        RHI::RHITexture* GetSampledAtlas() { return m_Depth.GetSampledAtlas(); }
        nri::Descriptor* GetSRV() const;
        nri::Descriptor* GetConstantsCBV() const { return m_Receiver.GetCBV(); }
        const RHICascadeConstants& GetConstants() const { return m_Constants; }
        const std::string& GetDiagnostic() const { return m_Diagnostic; }
        uint32_t GetDrawCount() const { return m_Depth.GetDrawCount(); }
    private:
        CascadedShadowMap m_CPU;
        RHICascadeConstants m_Constants;
        RHIShadowDepthAtlas m_Depth;
        RHI::RHIBuffer m_Receiver;
        std::string m_Diagnostic;
        bool m_Supported=false;
    };
}
