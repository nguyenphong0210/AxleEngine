#include <Axle/LocalShadowMap.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x "\n";return 1;}}while(0)
using namespace Axle;
int main() {
    RenderWorld world;RenderSpotLight spot;spot.Position={0,0,0};spot.Direction={0,0,-1};spot.Range=20;spot.OuterAngle=30;
    world.SpotLights.push_back(spot);SceneRenderSettings settings;LocalShadowFrame frame;
    CHECK(BuildLocalShadowFrame(world,{0,0,0},settings,frame));CHECK(frame.Views.size()==1&&frame.SpotViews[0]==0&&frame.PointViews[0]==-1);
    auto project=[&](glm::vec3 p){const auto clip=frame.Views[0].Matrix*glm::vec4(p,1);return glm::vec3(clip)/clip.w;};
    CHECK(std::abs(project({0,0,-spot.Shadows.NearPlane}).z)<1e-5&&std::abs(project({0,0,-20}).z-1)<1e-5);
    CHECK(std::abs(project({std::tan(glm::radians(30.f))*5,0,-5}).x-1)<1e-5);
    CHECK(ShadowIntersects(frame.Views[0].Matrix,glm::translate(glm::mat4(1),{0,0,-5}),{-1,-1,-1},{1,1,1}));
    CHECK(!ShadowIntersects(frame.Views[0].Matrix,glm::translate(glm::mat4(1),{100,0,-5}),{-1,-1,-1},{1,1,1}));
    world.AdditionalDirectionalLights.resize(2);RenderPointLight point;point.Radius=10;world.PointLights.push_back(point);
    CHECK(BuildLocalShadowFrame(world,{0,0,0},settings,frame));CHECK(frame.Views.size()==7&&frame.PointViews[2]>=0);
    const auto first=frame.PointViews[2];
    const glm::vec3 axes[]{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for(int f=0;f<6;++f) {
        const auto clip=frame.Views[first+f].Matrix*glm::vec4(axes[f],1);
        CHECK(clip.w>0&&std::abs(clip.x)<1e-6&&std::abs(clip.y)<1e-6&&clip.z>0&&clip.z<clip.w);
        CHECK(frame.Views[first+f].X+frame.Views[first+f].Size<=frame.AtlasSize&&frame.Views[first+f].Y+frame.Views[first+f].Size<=frame.AtlasSize);
    }
    world.PointLights.resize(5,point);world.SpotLights.resize(8,spot);
    CHECK(BuildLocalShadowFrame(world,{0,0,0},settings,frame));CHECK(frame.Views.size()==16&&!frame.Diagnostic.empty());
    size_t points=0,spots=0;for(int v:frame.PointViews)points+=v>=0;for(int v:frame.SpotViews)spots+=v>=0;CHECK(points==2&&spots==4);
    auto again=frame;CHECK(BuildLocalShadowFrame(world,{0,0,0},settings,again));CHECK(again.PointViews==frame.PointViews&&again.SpotViews==frame.SpotViews);
    world.PointLights.clear();world.SpotLights={spot};world.SpotLights[0].Shadows.CastShadows=false;
    CHECK(BuildLocalShadowFrame(world,{0,0,0},settings,frame)&&frame.Views.empty());
    world.SpotLights[0]=spot;world.SpotLights[0].Direction={0,0,0};
    CHECK(BuildLocalShadowFrame(world,{0,0,0},settings,frame)&&frame.Views.empty()&&!frame.Diagnostic.empty());
    world.SpotLights[0]=spot;world.SpotLights[0].Shadows.NearPlane=std::numeric_limits<float>::quiet_NaN();
    CHECK(BuildLocalShadowFrame(world,{0,0,0},settings,frame)&&frame.Views.empty());
    settings.LocalShadowResolution=100;CHECK(!BuildLocalShadowFrame(world,{0,0,0},settings,frame));
    settings.EnableShadows=false;CHECK(BuildLocalShadowFrame(world,{0,0,0},settings,frame)&&frame.Views.empty());
    std::cout<<"PASS local projection, six cube directions, ZO near/far, frustum culling, light indexing and bounded selection\n";
}
