#include <Axle/RHILocalShadows.hpp>
namespace Axle {
using namespace RHI;
bool RHILocalShadows::Init(){return m_Receiver.Init(RHIBufferDesc::Constant(sizeof(m_Constants),RHIMemoryLocation::HostUpload,"LocalShadowReceiver"))&&m_Depth.Init();}
void RHILocalShadows::Shutdown(){m_Depth.Shutdown();m_Receiver.Shutdown();m_Frame={};m_Constants={};m_Diagnostic.clear();}
bool RHILocalShadows::Prepare(const RenderWorld& world,const PerspectiveCamera& camera,const SceneRenderSettings& settings,MaterialTextureCache& textures) {
    m_Constants={};for(auto& group:m_Constants.LightViews)group=glm::ivec4(-1);
    // Point projections are enabled in the following verified checkpoint.
    if(!BuildLocalShadowFrame(world,camera.GetPosition(),settings,m_Frame,false)){m_Diagnostic=m_Frame.Diagnostic;return false;}
    m_Diagnostic=m_Frame.Diagnostic;
    if(!m_Depth.IsSupported()){m_Frame={};m_Diagnostic="LOCAL_SHADOW_FORMAT: D32 depth sampling unsupported; lights remain unshadowed";}
    for(size_t i=0;i<m_Frame.Views.size();++i) {
        const auto& view=m_Frame.Views[i];m_Constants.Matrices[i]=view.Matrix;
        m_Constants.Tiles[i]={float(view.X)/m_Frame.AtlasSize,float(view.Y)/m_Frame.AtlasSize,float(view.Size)/m_Frame.AtlasSize,0};
        m_Constants.Bias[i]=m_Frame.Bias[i];
    }
    for(size_t i=0;i<16;++i){m_Constants.LightViews[i/4][i%4]=m_Frame.PointViews[i];m_Constants.LightViews[4+i/4][i%4]=m_Frame.SpotViews[i];}
    if(!m_Frame.Views.empty())m_Constants.Atlas={1.f/m_Frame.AtlasSize,float(m_Frame.Views.size()),0,0};
    if(!m_Receiver.UpdateData(&m_Constants,sizeof(m_Constants)))return false;
    if(!m_Depth.Prepare(world,textures,m_Frame.Views,m_Frame.AtlasSize,true,true)){m_Diagnostic=m_Depth.GetDiagnostic();return false;}
    if(!m_Depth.GetDiagnostic().empty()){if(!m_Diagnostic.empty())m_Diagnostic+="; ";m_Diagnostic+=m_Depth.GetDiagnostic();}
    return true;
}
}
