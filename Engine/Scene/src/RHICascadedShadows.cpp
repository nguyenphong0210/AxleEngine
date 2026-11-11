#include <Axle/RHICascadedShadows.hpp>
#include <algorithm>
#include <cmath>
namespace Axle {
using namespace RHI;
bool RHICascadedShadows::Init() {
    if(!m_Receiver.Init(RHIBufferDesc::Constant(sizeof(m_Constants),RHIMemoryLocation::HostUpload,"CascadeReceiver"))||!m_Depth.Init())return false;
    m_Supported=m_Depth.IsSupported();return m_Receiver.UpdateData(&m_Constants,sizeof(m_Constants));
}
void RHICascadedShadows::Shutdown(){m_Depth.Shutdown();m_Receiver.Shutdown();m_Constants={};m_Supported=false;m_Diagnostic.clear();}
nri::Descriptor* RHICascadedShadows::GetSRV() const{return m_Depth.GetSRV();}
bool RHICascadedShadows::Prepare(const RenderWorld& world,const PerspectiveCamera& camera,const SceneRenderSettings& settings,MaterialTextureCache& textures) {
    m_Constants={};m_Diagnostic.clear();std::vector<ShadowDepthView> views;
    auto disabled=[&]{return m_Depth.Prepare(world,textures,views,0)&&m_Receiver.UpdateData(&m_Constants,sizeof(m_Constants));};
    if(!settings.EnableShadows||!world.DirectionalLight||!world.DirectionalLight->CastsShadows)return disabled();
    if(!m_Supported){m_Diagnostic="CSM_UNSUPPORTED_FORMAT: D32 depth sampling/attachment is unavailable; shadows disabled.";return disabled();}
    if(camera.GetNearClip()<=0){m_Diagnostic="CSM_CAMERA_RANGE: non-positive camera near clip has no positive-depth cascade policy; shadows disabled.";return disabled();}
    const auto& light=*world.DirectionalLight;
    CascadedShadowMapSettings config;
    config.CascadeCount=settings.ShadowCascadeCount;config.Resolution=light.ShadowQuality==0?1024:light.ShadowQuality==2?4096:2048;
    config.NearClip=camera.GetNearClip();config.FarClip=camera.GetFarClip();config.MaxDistance=std::min(light.MaxShadowDistance,config.FarClip);config.SplitLambda=light.CascadeSplitLambda;
    config.DepthBias=light.ShadowBias;config.SlopeBias=light.ShadowSlopeBias;config.NormalBias=light.ShadowNormalBias;
    if(!std::isfinite(light.MaxShadowDistance)||!std::isfinite(light.ShadowFilterRadius)||light.ShadowFilterRadius<0||!m_CPU.Configure(config,&m_Diagnostic)||!m_CPU.Update(camera.GetViewMatrix(),camera.GetProjectionMatrixZO(),light.Direction,&m_Diagnostic)) {
        if(m_Diagnostic.empty())m_Diagnostic="CSM_INVALID_SETTINGS: shadow distance/filter must be finite and non-negative.";return false;
    }
    const auto size=config.Resolution*2;
    m_Constants.Atlas={config.CascadeCount,1.f/size,config.Resolution,config.NearClip};
    m_Constants.Bias={config.DepthBias,config.SlopeBias,config.NormalBias,std::min(light.ShadowFilterRadius,2.f)};
    for(uint32_t c=0;c<config.CascadeCount;++c){const auto& cascade=m_CPU.GetCascade(c);m_Constants.Matrices[c]=cascade.LightViewProjection;m_Constants.Splits[c]=cascade.FarDistance;views.push_back({cascade.LightViewProjection,(c%2)*config.Resolution,(c/2)*config.Resolution,config.Resolution});}
    if(!m_Receiver.UpdateData(&m_Constants,sizeof(m_Constants)))return false;
    const bool success=m_Depth.Prepare(world,textures,views,size);m_Diagnostic=m_Depth.GetDiagnostic();
    if(m_Diagnostic.starts_with("SHADOW_"))m_Diagnostic.replace(0,7,"CSM_");
    return success;
}
bool RHICascadedShadows::Record(RHICommandBuffer& commands){return m_Depth.Record(commands);}
}
