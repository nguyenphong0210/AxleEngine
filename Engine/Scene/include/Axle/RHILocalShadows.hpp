#pragma once
#include <Axle/RHIShadowDepthAtlas.hpp>
#include <Axle/PerspectiveCamera.hpp>
namespace Axle {
    struct alignas(256) RHILocalShadowConstants {
        glm::mat4 Matrices[16]{};
        glm::vec4 Tiles[16]{},Bias[16]{};
        glm::ivec4 LightViews[8]{}; // First 16 point GPU slots, then 16 spots. -1 = unshadowed.
        glm::vec4 Atlas{0}; // Reciprocal size, active view count.
        glm::vec4 Padding[7]{};
    };
    static_assert(sizeof(RHILocalShadowConstants)==1792);
    class RHILocalShadows {
    public:
        bool Init();
        void Shutdown();
        bool Prepare(const RenderWorld&,const PerspectiveCamera&,const SceneRenderSettings&,MaterialTextureCache&);
        bool Record(RHI::RHICommandBuffer& commands){return m_Depth.Record(commands);}
        void OnSubmitted(){m_Depth.OnSubmitted();}
        bool NeedsRender() const {return m_Depth.NeedsRender();}
        bool IsSupported() const {return m_Depth.IsSupported();}
        bool IsActive() const {return m_Depth.IsActive();}
        RHI::RHITexture* GetSampledAtlas() const {return m_Depth.GetSampledAtlas();}
        RHI::RHITexture* GetAtlas() const {return m_Depth.GetAtlas();}
        nri::Descriptor* GetSRV() const {return m_Depth.GetSRV();}
        nri::Descriptor* GetConstantsCBV() const {return m_Receiver.GetCBV();}
        const LocalShadowFrame& GetFrame() const {return m_Frame;}
        const std::string& GetDiagnostic() const {return m_Diagnostic;}
        uint32_t GetDrawCount() const {return m_Depth.GetDrawCount();}
    private:
        RHIShadowDepthAtlas m_Depth;
        RHI::RHIBuffer m_Receiver;
        RHILocalShadowConstants m_Constants;
        LocalShadowFrame m_Frame;
        std::string m_Diagnostic;
    };
}
