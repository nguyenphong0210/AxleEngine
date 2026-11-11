#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
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
bool UploadAlpha(const std::shared_ptr<RHITexture>& texture,bool transparent) {
    std::vector<uint8_t> pixels(16*16*4,255);
    if(transparent)for(size_t i=0;i<16*16;++i)pixels[i*4+3]=0;
    nri::TextureSubresourceUploadDesc sub{};sub.slices=pixels.data();sub.sliceNum=1;sub.rowPitch=64;sub.slicePitch=1024;
    nri::TextureUploadDesc upload{};upload.texture=texture->GetNative();upload.subresources=&sub;upload.planes=nri::PlaneBits::COLOR;
    upload.after={nri::AccessBits::SHADER_RESOURCE,nri::Layout::SHADER_RESOURCE,nri::StageBits::FRAGMENT_SHADER};
    return RHIDevice::Get().UploadData(&upload,1,nullptr,0)==nri::Result::SUCCESS;
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
    auto alpha=std::make_shared<RHITexture>();auto alphaDesc=RHITextureDesc::RenderTarget2D(16,16,RHITextureFormat::RGBA8_UNORM);
    alphaDesc.Usage=RHITextureUsage::ShaderResource;CHECK(alpha->Init(alphaDesc)&&UploadAlpha(alpha,false));
    auto cutout=std::make_shared<RHIMaterial>(cutoutDesc);world.Meshes[1].Material.RHIMaterialAsset=cutout;
    cutout->GetDesc().BaseColor.a=1;cutout->SetShadingModel(RHIShadingModel::TexturedPBR);cutout->SetTexture(RHITextureSlot::Albedo,alpha);
    CHECK(!render().empty()&&renderer.GetLocalShadows().GetDrawCount()>0);
    const auto solidDepth=Read(*renderer.GetLocalShadows().GetAtlas());CHECK(solidDepth!=cutoutDepth);
    CHECK(UploadAlpha(alpha,true)); // Same texture generation; cache must still redraw.
    CHECK(!render().empty()&&renderer.GetLocalShadows().GetDrawCount()>0);
    CHECK(Read(*renderer.GetLocalShadows().GetAtlas())==cutoutDepth);
    world.Meshes[1].Material.RHIMaterialAsset=material;CHECK(render()==on);
    // Lights, transforms and submitted generations invalidate the exact cache.
    world.SpotLights[0].Position.x+=1;CHECK(!render().empty()&&renderer.GetLocalShadows().GetDrawCount()>0);world.SpotLights[0]=spot;CHECK(render()==on);
    std::vector<RHIVertex3D> replacementVertices;std::vector<uint32_t> replacementIndices;
    RHIMesh::GenerateCube(.3f,replacementVertices,replacementIndices);CHECK(cube->Init(replacementVertices,replacementIndices));
    world.Meshes[1].NativeView=cube->CaptureView();CHECK(render()!=on&&renderer.GetLocalShadows().GetDrawCount()>0);
    RHIMesh::GenerateCube(2,replacementVertices,replacementIndices);CHECK(cube->Init(replacementVertices,replacementIndices));
    world.Meshes[1].NativeView=cube->CaptureView();CHECK(render()==on&&renderer.GetLocalShadows().GetDrawCount()>0);
    settings.LocalShadowResolution=512;bool injected=false;
    {ScopedRHIFault fault([&](std::string_view op)->std::optional<nri::Result>{if(op=="CreateTexture"&&!injected){injected=true;return nri::Result::OUT_OF_MEMORY;}return {};});
        renderer.RenderWorldView(world,camera,&color,&depth,settings);CHECK(injected&&!renderer.WasLastRenderSuccessful());}
    CHECK(renderer.GetLocalShadows().GetAtlas()->GetHandle()==handle);CHECK(!render().empty()&&renderer.GetLocalShadows().GetAtlas()->GetWidth()==2048);
    settings.EnableToneMapping=true;settings.EnableFXAA=true;settings.MSAASamples=2;CHECK(!render().empty());
    settings.EnableToneMapping=settings.EnableFXAA=false;settings.MSAASamples=1;settings.LocalShadowResolution=256;
    world.SpotLights.clear();RenderPointLight point;point.EntityID=29;point.Position=spot.Position;point.Intensity=4000;point.Radius=25;world.PointLights={point};
    settings.EnableShadows=false;const auto pointOff=render();CHECK(!pointOff.empty());
    settings.EnableShadows=true;const auto pointOn=render();CHECK(!pointOn.empty());
    CHECK(renderer.GetLocalShadows().GetFrame().Views.size()==6&&renderer.GetLocalShadows().GetFrame().PointViews[0]>=0);
    const auto pointDark=Darkened(pointOff,pointOn);std::cout<<"point shadow darkened pixels="<<pointDark<<"\n";CHECK(pointDark>20);
    CHECK(PPM("point-off.ppm",pointOff)&&PPM("point-on.ppm",pointOn));
    CHECK(render()==pointOn&&renderer.GetLocalShadows().GetDrawCount()==0);
    world.AdditionalDirectionalLights.resize(2);for(auto& light:world.AdditionalDirectionalLights){light.Intensity=light.AmbientIntensity=0;}
    CHECK(render()==pointOn&&renderer.GetLocalShadows().GetFrame().PointViews[2]>=0);world.AdditionalDirectionalLights.clear();
    world.PointLights[0].Shadows.CastShadows=false;CHECK(render()==pointOff);world.PointLights[0].Shadows.CastShadows=true;
    CHECK(render()==pointOn);
    // Real depth must be present in every cube projection, including the poles.
    const glm::vec3 axes[]{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    world.PointLights[0].Position={0,0,0};world.PointLights[0].Radius=10;world.Meshes.clear();
    auto marker=RHIMesh::CreateCube(.6f);CHECK(marker);
    for(auto axis:axes){auto instance=caster;instance.NativeView=marker->CaptureView();instance.WorldTransform=glm::translate(glm::mat4(1),axis*2.f);world.Meshes.push_back(instance);}
    settings.LocalShadowResolution=128;CHECK(!render().empty());
    const auto allFaces=Read(*renderer.GetLocalShadows().GetAtlas());CHECK(!allFaces.empty());
    for(const auto& view:renderer.GetLocalShadows().GetFrame().Views) {
        size_t written=0;for(uint32_t y=0;y<view.Size;++y)for(uint32_t x=0;x<view.Size;++x){float value;
            std::memcpy(&value,allFaces.data()+((view.Y+y)*512+view.X+x)*4,4);CHECK(std::isfinite(value)&&value>=0&&value<=1);written+=value<.99999f;}
        std::cout<<"point face depth pixels="<<written<<"\n";CHECK(written>0);
    }
    // A receiver straddling the +X/+Z selection boundary must be shadowed on
    // both sides. Put the camera between caster and receiver to expose the map.
    const auto diagonal=glm::normalize(glm::vec3(1,0,1));auto receiver=floor;receiver.NativeView=RHIMesh::CreatePlane(4,4,1,1)->CaptureView();
    receiver.WorldTransform=glm::translate(glm::mat4(1),diagonal*4.f)*glm::rotate(glm::mat4(1),glm::half_pi<float>(),glm::normalize(glm::cross(glm::vec3(0,1,0),-diagonal)));
    auto seam=caster;seam.NativeView=marker->CaptureView();seam.WorldTransform=glm::translate(glm::mat4(1),diagonal*2.f);world.Meshes={receiver,seam};
    const auto eye=diagonal*3.f+glm::vec3(0,.3f,0);camera.SetViewMatrix(glm::lookAtRH(eye,diagonal*4.f,glm::vec3(0,1,0)),eye);
    settings.EnableShadows=false;const auto seamOff=render();settings.EnableShadows=true;const auto seamOn=render();CHECK(!seamOff.empty()&&!seamOn.empty());
    size_t left=0,right=0;for(size_t i=0;i<seamOff.size();i+=4)if(int(seamOff[i])+seamOff[i+1]+seamOff[i+2]>int(seamOn[i])+seamOn[i+1]+seamOn[i+2]+30){if((i/4)%128<64)++left;else ++right;}
    std::cout<<"point seam darkened left="<<left<<" right="<<right<<"\n";CHECK(left>10&&right>10);
    CHECK(PPM("point-seam-off.ppm",seamOff)&&PPM("point-seam-on.ppm",seamOn));
    // The default/sky sets plus 4096 material sets must fit the forward pool
    // after adding the seventeenth texture binding. Keep raster work clipped.
    settings.EnableShadows=false;world.Meshes.assign(4096,floor);
    for(auto& instance:world.Meshes)instance.WorldTransform=glm::translate(glm::mat4(1),glm::vec3(10000));
    CHECK(!render().empty()&&renderer.GetRenderedEntityCount()==4096);
    {
        // Recording alone must never publish cached contents. Exercise the
        // real depth pass without invoking its successful-submit callback.
        RHILocalShadows pass;MaterialTextureCache textures;CHECK(pass.Init());
        world.Meshes={floor,caster};world.PointLights.clear();world.SpotLights={spot};settings.EnableShadows=true;
        CHECK(pass.Prepare(world,camera,settings,textures)&&pass.NeedsRender());
        {RHICommandBuffer abandoned;CHECK(abandoned.Init()&&abandoned.Begin());
            abandoned.TransitionTexture(pass.GetAtlas()->GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::DepthStencilWrite);
            CHECK(pass.Record(abandoned)&&abandoned.End());
            CHECK(abandoned.Reset());} // Drain/release recording pins before Prepare.
        CHECK(pass.Prepare(world,camera,settings,textures)&&pass.NeedsRender());
        {RHICommandBuffer submitted;CHECK(submitted.Init()&&submitted.Begin());
            submitted.TransitionTexture(pass.GetAtlas()->GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::DepthStencilWrite);
            CHECK(pass.Record(submitted));
            submitted.TransitionTexture(pass.GetAtlas()->GetNative(),RHIResourceLayout::DepthStencilWrite,RHIResourceLayout::ShaderResource);
            CHECK(submitted.End()&&submitted.Submit()&&device.WaitIdle());pass.OnSubmitted();}
        CHECK(pass.Prepare(world,camera,settings,textures)&&!pass.NeedsRender());pass.Shutdown();
    }
    CHECK(device.WaitIdle());renderer.Shutdown();CHECK(GetRHIValidationErrorCount()==0);
    std::cout<<"PASS Point/Spot pixels, six faces/seams, opt-out, cache, cutout and allocation retry\n";return true;
}
}
int main(int argc,char** argv){if(argc!=3)return 2;Output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(Output);
    try{return Native(std::string(argv[1])=="vulkan"?Axle::RHI::RHIType::Vulkan:Axle::RHI::RHIType::Direct3D12)&&Axle::RHI::GetRHIValidationErrorCount()==0?0:1;}
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
