#pragma once
#include <Axle/LocalShadowMap.hpp>
#include <Axle/MaterialTextureCache.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHITransform.hpp>
namespace Axle {
    // Shared depth caster implementation for cascades and local projections.
    // Prepare only after the owning view frame drains its preceding submission.
    class RHIShadowDepthAtlas {
    public:
        bool Init();
        void Shutdown();
        bool Prepare(const RenderWorld&,MaterialTextureCache&,const std::vector<ShadowDepthView>&,
                     uint32_t atlasSize,bool cull=false,bool cache=false);
        bool Record(RHI::RHICommandBuffer&);
        void OnSubmitted(){if(m_NeedsRender)m_LastKey=m_PendingKey;}
        bool IsSupported() const {return m_Supported;}
        bool IsActive() const {return !m_Views.empty();}
        bool NeedsRender() const {return IsActive()&&m_NeedsRender;}
        RHI::RHITexture* GetAtlas() const {return m_Atlas.get();}
        RHI::RHITexture* GetSampledAtlas() const {return IsActive()?m_Atlas.get():m_DisabledAtlas.get();}
        nri::Descriptor* GetSRV() const;
        uint32_t GetDrawCount() const {return m_DrawCount;}
        const std::string& GetDiagnostic() const {return m_Diagnostic;}
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
        std::unique_ptr<RHI::RHITexture> m_Atlas,m_DisabledAtlas;
        RHI::RHIPipelineLayout m_Layout;
        RHI::RHIDescriptorPool m_Pool;
        std::array<RHI::RHIResourceRef<nri::Pipeline>,3> m_Pipelines;
        std::array<RHI::RHIBuffer,16> m_Cameras;
        std::vector<ShadowDepthView> m_Views;
        std::array<std::vector<size_t>,16> m_Visible;
        std::vector<Draw> m_Draws;
        std::string m_Diagnostic,m_LastKey,m_PendingKey;
        size_t m_CasterCount=0;
        uint32_t m_DrawCount=0;
        bool m_Supported=false,m_NeedsRender=false;
    };
}
