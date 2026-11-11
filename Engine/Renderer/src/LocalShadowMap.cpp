#include <Axle/LocalShadowMap.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
namespace Axle {
namespace {
bool Finite(glm::vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool Valid(const LocalShadowSettings& s,float range) {
    return std::isfinite(range)&&range>s.NearPlane&&std::isfinite(s.NearPlane)&&s.NearPlane>=.001f&&
        std::isfinite(s.Bias)&&s.Bias>=0&&s.Bias<=1&&std::isfinite(s.SlopeBias)&&s.SlopeBias>=0&&s.SlopeBias<=1&&
        std::isfinite(s.NormalBias)&&s.NormalBias>=0&&s.NormalBias<=1&&std::isfinite(s.FilterRadius)&&s.FilterRadius>=0&&s.FilterRadius<=2;
}
}
bool BuildLocalShadowFrame(const RenderWorld& world,const glm::vec3& camera,const SceneRenderSettings& settings,
    LocalShadowFrame& output,bool includePoints) {
    LocalShadowFrame frame;
    if(!settings.EnableShadows){output=std::move(frame);return true;}
    const auto resolution=settings.LocalShadowResolution;
    if(!Finite(camera)||resolution<128||resolution>2048||(resolution&(resolution-1))||
       settings.MaxShadowedPointLights>2||settings.MaxShadowedSpotLights>4||
       !std::isfinite(settings.LocalShadowDistance)||settings.LocalShadowDistance<0) {
        output=std::move(frame);output.Diagnostic="LOCAL_SHADOW_SETTINGS: invalid resolution, budget or distance";return false;
    }
    frame.AtlasSize=resolution*4;
    struct Candidate {size_t Index;float Distance;uint32_t ID;bool Point;};
    std::vector<Candidate> candidates;
    const auto prefix=std::min<size_t>(16,world.AdditionalDirectionalLights.size());
    for(size_t i=0;i<std::min<size_t>(16-prefix,world.PointLights.size());++i) {
        const auto& light=world.PointLights[i];if(!includePoints||!light.Shadows.CastShadows||light.Intensity<=0)continue;
        const float distance=glm::length(light.Position-camera);
        if(!Finite(light.Position)||!std::isfinite(light.Intensity)||!Valid(light.Shadows,light.Radius)) {frame.Diagnostic="LOCAL_SHADOW_LIGHT: invalid point settings; shadow disabled";continue;}
        if(distance<=light.Radius+settings.LocalShadowDistance)candidates.push_back({i,distance,light.EntityID,true});
    }
    for(size_t i=0;i<std::min<size_t>(16,world.SpotLights.size());++i) {
        const auto& light=world.SpotLights[i];if(!light.Shadows.CastShadows||light.Intensity<=0)continue;
        const float distance=glm::length(light.Position-camera);
        if(!Finite(light.Position)||!Finite(light.Direction)||!std::isfinite(glm::length(light.Direction))||glm::length(light.Direction)<1e-5f||!std::isfinite(light.Intensity)||
           !std::isfinite(light.OuterAngle)||!std::isfinite(light.InnerAngle)||light.OuterAngle<=0||light.OuterAngle>=89.5f||
           light.InnerAngle<0||light.InnerAngle>light.OuterAngle||!Valid(light.Shadows,light.Range)) {
            frame.Diagnostic="LOCAL_SHADOW_LIGHT: invalid spot settings; shadow disabled";continue;
        }
        if(distance<=light.Range+settings.LocalShadowDistance)candidates.push_back({i,distance,light.EntityID,false});
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
        if(a.Distance!=b.Distance)return a.Distance<b.Distance;if(a.ID!=b.ID)return a.ID<b.ID;return a.Index<b.Index;});
    uint32_t points=0,spots=0;
    const glm::vec3 directions[]{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const glm::vec3 ups[]{{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
    for(const auto& candidate:candidates) {
        const bool point=candidate.Point;
        if((point&&points>=settings.MaxShadowedPointLights)||(!point&&spots>=settings.MaxShadowedSpotLights)) {
            if(frame.Diagnostic.empty())frame.Diagnostic="LOCAL_SHADOW_BUDGET: excess lights remain unshadowed";continue;
        }
        const auto& shadow=point?world.PointLights[candidate.Index].Shadows:world.SpotLights[candidate.Index].Shadows;
        const auto position=point?world.PointLights[candidate.Index].Position:world.SpotLights[candidate.Index].Position;
        const float range=point?world.PointLights[candidate.Index].Radius:world.SpotLights[candidate.Index].Range;
        const auto first=static_cast<int>(frame.Views.size());
        if(point){frame.PointViews[prefix+candidate.Index]=first;++points;}else{frame.SpotViews[candidate.Index]=first;++spots;}
        const auto projection=glm::perspectiveRH_ZO(glm::radians(point?90.f:world.SpotLights[candidate.Index].OuterAngle*2),1.f,shadow.NearPlane,range);
        for(uint32_t face=0;face<(point?6u:1u);++face) {
            const auto direction=point?directions[face]:glm::normalize(world.SpotLights[candidate.Index].Direction);
            const auto up=point?ups[face]:(std::abs(direction.y)>.99f?glm::vec3(0,0,1):glm::vec3(0,1,0));
            const uint32_t slot=static_cast<uint32_t>(frame.Views.size());
            frame.Views.push_back({projection*glm::lookAtRH(glm::vec3(0),direction,up)*glm::translate(glm::mat4(1),-position),(slot%4)*resolution,(slot/4)*resolution,resolution});
            frame.Bias[slot]={shadow.Bias,shadow.SlopeBias,shadow.NormalBias,shadow.FilterRadius};
        }
    }
    output=std::move(frame);return true;
}
bool ShadowIntersects(const glm::mat4& light,const glm::mat4& model,const glm::vec3& minimum,const glm::vec3& maximum) {
    std::array<glm::vec4,8> clips;
    for(uint32_t i=0;i<8;++i) {
        clips[i]=light*model*glm::vec4(i&1?maximum.x:minimum.x,i&2?maximum.y:minimum.y,i&4?maximum.z:minimum.z,1);
        for(int c=0;c<4;++c)if(!std::isfinite(clips[i][c]))return true; // Conservative for unknown/deformed bounds.
    }
    for(int plane=0;plane<6;++plane) {
        bool outside=true;
        for(const auto& p:clips){const float v=plane==0?p.x+p.w:plane==1?p.w-p.x:plane==2?p.y+p.w:plane==3?p.w-p.y:plane==4?p.z:p.w-p.z;
            if(v>=0){outside=false;break;}}
        if(outside)return false;
    }
    return true;
}
}
