#pragma once
#include <Axle/CascadedShadowMap.hpp>
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
        bool IsSupported() const { return m_Supported; }
        bool IsActive() const { return m_CPU.GetFrame().Valid && m_Constants.Atlas.x>0; }
        RHI::RHITexture* GetAtlas() { return m_Atlas.get(); }
        RHI::RHITexture* GetSampledAtlas() { return IsActive()?m_Atlas.get():m_DisabledAtlas.get(); }
        nri::Descriptor* GetSRV() const;
        nri::Descriptor* GetConstantsCBV() const { return m_Receiver.GetCBV(); }
        const RHICascadeConstants& GetConstants() const { return m_Constants; }
        const std::string& GetDiagnostic() const { return m_Diagnostic; }
        uint32_t GetDrawCount() const { return m_DrawCount; }
    private:
        bool ReleaseUnusedOwners();
        struct Draw {
            std::shared_ptr<const RHI::RHIMeshView> Mesh;
            RHI::RHIObjectPushConstants Object;
            RHI::RHIBuffer Material;
            std::shared_ptr<RHI::RHITexture> Albedo;
            nri::DescriptorSet* Set=nullptr;
            RHI::RHIResourceRef<nri::Pipeline> Pipeline;
        };
        CascadedShadowMap m_CPU;
        RHICascadeConstants m_Constants;
        RHI::RHIBuffer m_Receiver;
        std::array<RHI::RHIBuffer,4> m_Cameras;
        std::unique_ptr<RHI::RHITexture> m_Atlas;
        std::unique_ptr<RHI::RHITexture> m_DisabledAtlas;
        RHI::RHIPipelineLayout m_Layout;
        RHI::RHIDescriptorPool m_Pool;
        std::array<RHI::RHIResourceRef<nri::Pipeline>,3> m_Pipelines;
        std::vector<Draw> m_Draws;
        std::string m_Diagnostic;
        bool m_Supported=false;
        uint32_t m_DrawCount=0;
        size_t m_CasterCount=0;
    };
}
