#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x "\n";return false;}}while(0)
namespace {
using namespace Axle;using namespace Axle::RHI;
std::filesystem::path Output;
std::vector<uint8_t> Read(RHITexture& texture) {
    const auto width=texture.GetWidth(),height=texture.GetHeight();
    RHIBufferDesc desc;desc.Size=uint64_t(width)*height*4;desc.MemoryLocation=RHIMemoryLocation::HostReadback;
    RHIBuffer buffer;RHICommandBuffer commands;if(!buffer.Init(desc)||!commands.Init()||!commands.Begin())return {};
    nri::TextureDataLayoutDesc layout{};layout.rowPitch=width*4;layout.slicePitch=width*height*4;
    nri::TextureRegionDesc region{};region.width=width;region.height=height;region.depth=1;
    commands.ReadbackTextureToBuffer(*buffer.GetNative(),layout,*texture.GetNative(),region);
    commands.TransitionTexture(texture.GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ShaderResource);
    if(!commands.End()||!commands.Submit()||!RHIDevice::Get().WaitIdle())return {};
    auto* bytes=buffer.Map();if(!bytes)return {};std::vector<uint8_t> result(desc.Size);std::memcpy(result.data(),bytes,result.size());buffer.Unmap();return result;
}
size_t Darkened(const std::vector<uint8_t>& off,const std::vector<uint8_t>& on) {
    if(off.size()!=on.size())return 0;size_t n=0;for(size_t i=0;i<off.size();i+=4)if(int(off[i])+off[i+1]+off[i+2]>int(on[i])+on[i+1]+on[i+2]+30)++n;return n;
}
bool PPM(const char* name,const std::vector<uint8_t>& image) {
    std::ofstream file(Output/name,std::ios::binary);file<<"P6\n128 128\n255\n";
    for(size_t i=0;i<image.size();i+=4)file.write(reinterpret_cast<const char*>(image.data()+i),3);return bool(file);
}
bool Native(RHIType backend) {
    JobSystem::Init(1);struct Jobs{~Jobs(){ScriptHotReloadManager::Get().Shutdown();JobSystem::Shutdown();}}jobs;
    auto& device=RHIDevice::Get();RHIDeviceDesc init;init.PreferredBackend=backend;init.Headless=true;init.AllowFallback=false;
    init.EnableNRIValidation=true;init.EnableValidation=RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    CHECK(device.Init(init)&&device.GetBackend()==backend&&!device.IsSoftwareAdapter());
    struct Close{~Close(){RHIDevice::Get().WaitIdle();RHIDevice::Get().Shutdown();}}close;
    RHISceneRenderer renderer;CHECK(renderer.Init(device,false));RHITexture color,depth;
    CHECK(color.Init(RHITextureDesc::RenderTarget2D(128,128))&&depth.Init(RHITextureDesc::DepthStencil2D(128,128)));
    auto plane=RHIMesh::CreatePlane(20,20,1,1),cube=RHIMesh::CreateCube(2);CHECK(plane&&cube);
    RHIMaterialDesc desc;desc.CullMode=CullMode::None;desc.BaseColor={.8f,.8f,.8f,1};desc.Roughness=.8f;
    auto material=std::make_shared<RHIMaterial>(desc);
    RenderWorld world;RenderMeshInstance floor;floor.NativeView=plane->CaptureView();floor.CastsShadows=false;floor.Material.RHIMaterialAsset=material;
    auto caster=floor;caster.NativeView=cube->CaptureView();caster.CastsShadows=true;caster.WorldTransform=glm::translate(glm::mat4(1),glm::vec3(0,1.1f,0));world.Meshes={floor,caster};
    RenderSpotLight spot;spot.EntityID=17;spot.Position={-3,7,3};spot.Direction=glm::normalize(-spot.Position);spot.Range=25;spot.Intensity=4000;spot.InnerAngle=25;spot.OuterAngle=45;world.SpotLights={spot};
    PerspectiveCamera camera(55,1,.1f,100);camera.SetViewMatrix(glm::lookAtRH(glm::vec3(7,8,12),glm::vec3(0),glm::vec3(0,1,0)),glm::vec3(7,8,12));
    SceneRenderSettings settings;settings.EnableSkybox=settings.EnableParticles=false;settings.LocalShadowResolution=256;
    auto render=[&]{renderer.RenderWorldView(world,camera,&color,&depth,settings);if(!renderer.WasLastRenderSuccessful()){std::cerr<<renderer.GetLocalShadows().GetDiagnostic()<<"\n";return std::vector<uint8_t>{};}return Read(color);};
    settings.EnableShadows=false;const auto off=render();CHECK(off.size()==128*128*4);
    settings.EnableShadows=true;const auto on=render();CHECK(on.size()==off.size());
    CHECK(renderer.GetLocalShadows().GetFrame().Views.size()==1&&renderer.GetLocalShadows().GetDrawCount()>0);
    CHECK(PPM("spot-off.ppm",off)&&PPM("spot-on.ppm",on));
    const auto dark=Darkened(off,on);std::cout<<"spot shadow darkened pixels="<<dark<<"\n";CHECK(dark>20);
    CHECK(PPM("spot-off.ppm",off)&&PPM("spot-on.ppm",on));
    const auto handle=renderer.GetLocalShadows().GetAtlas()->GetHandle();CHECK(render()==on&&renderer.GetLocalShadows().GetDrawCount()==0);
    CHECK(renderer.GetLocalShadows().GetAtlas()->GetHandle()==handle);
    world.SpotLights[0].Shadows.CastShadows=false;CHECK(render()==off&&!renderer.GetLocalShadows().IsActive());
    world.SpotLights[0].Shadows.CastShadows=true;CHECK(render()==on);
    world.Meshes[1].CastsShadows=false;CHECK(render()==off);world.Meshes[1].CastsShadows=true;CHECK(render()==on);
    auto cutoutDesc=desc;cutoutDesc.SurfaceType=SurfaceType::Cutout;cutoutDesc.BaseColor.a=0;
    world.Meshes[1].Material.RHIMaterialAsset=std::make_shared<RHIMaterial>(cutoutDesc);CHECK(!render().empty());
    const auto cutoutDepth=Read(*renderer.GetLocalShadows().GetAtlas());CHECK(!cutoutDepth.empty());
    for(size_t i=0;i<cutoutDepth.size();i+=4){float value;std::memcpy(&value,cutoutDepth.data()+i,4);CHECK(value==1);}
    world.Meshes[1].Material.RHIMaterialAsset=material;CHECK(render()==on);
    // Lights, transforms and submitted generations invalidate the exact cache.
    world.SpotLights[0].Position.x+=1;CHECK(!render().empty()&&renderer.GetLocalShadows().GetDrawCount()>0);world.SpotLights[0]=spot;CHECK(render()==on);
    settings.LocalShadowResolution=512;bool injected=false;
    {ScopedRHIFault fault([&](std::string_view op)->std::optional<nri::Result>{if(op=="CreateTexture"&&!injected){injected=true;return nri::Result::OUT_OF_MEMORY;}return {};});
        renderer.RenderWorldView(world,camera,&color,&depth,settings);CHECK(injected&&!renderer.WasLastRenderSuccessful());}
    CHECK(renderer.GetLocalShadows().GetAtlas()->GetHandle()==handle);CHECK(!render().empty()&&renderer.GetLocalShadows().GetAtlas()->GetWidth()==2048);
    settings.EnableToneMapping=true;settings.EnableFXAA=true;settings.MSAASamples=2;CHECK(!render().empty());
    CHECK(device.WaitIdle());renderer.Shutdown();CHECK(GetRHIValidationErrorCount()==0);
    std::cout<<"PASS spot shadow pixels, opt-out, cache, invalidation, cutout and allocation retry\n";return true;
}
}
int main(int argc,char** argv){if(argc!=3)return 2;Output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(Output);
    try{return Native(std::string(argv[1])=="vulkan"?Axle::RHI::RHIType::Vulkan:Axle::RHI::RHIType::Direct3D12)&&Axle::RHI::GetRHIValidationErrorCount()==0?0:1;}
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
