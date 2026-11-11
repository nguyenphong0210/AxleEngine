#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RenderService.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <Axle/FileSystem.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x "\n";return false;}}while(0)
namespace {
using namespace Axle;using namespace Axle::RHI;
std::filesystem::path Output;
bool PPM(const char* name,const std::vector<uint8_t>& image,uint32_t width) {
    std::ofstream file(Output/name,std::ios::binary);file<<"P6\n"<<width<<" "<<width<<"\n255\n";
    for(size_t i=0;i<image.size();i+=4)file.write(reinterpret_cast<const char*>(image.data()+i),3);
    return bool(file);
}
std::vector<uint8_t> Read(RHITexture& texture) {
    const uint32_t width=texture.GetWidth(),height=texture.GetHeight();
    RHIBufferDesc desc;desc.Size=uint64_t(width)*height*4;desc.MemoryLocation=RHIMemoryLocation::HostReadback;
    RHIBuffer read;RHICommandBuffer commands;
    if(!read.Init(desc)||!commands.Init()||!commands.Begin())return {};
    nri::TextureDataLayoutDesc layout{};layout.rowPitch=width*4;layout.slicePitch=width*height*4;
    nri::TextureRegionDesc region{};region.width=width;region.height=height;region.depth=1;
    commands.ReadbackTextureToBuffer(*read.GetNative(),layout,*texture.GetNative(),region);
    commands.TransitionTexture(texture.GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ShaderResource);
    if(!commands.End()||!commands.Submit()||!RHIDevice::Get().WaitIdle())return {};
    auto* data=read.Map();if(!data)return {};
    std::vector<uint8_t> result(desc.Size);std::memcpy(result.data(),data,result.size());read.Unmap();return result;
}
bool UploadAlpha(const std::shared_ptr<RHITexture>& texture,bool holes) {
    std::vector<uint8_t> pixels(16*16*4,255);
    if(holes)for(size_t i=0;i<16*16;++i) if(i%16<8)pixels[i*4+3]=0;
    nri::TextureSubresourceUploadDesc sub{};sub.slices=pixels.data();sub.sliceNum=1;sub.rowPitch=64;sub.slicePitch=1024;
    nri::TextureUploadDesc upload{};upload.texture=texture->GetNative();upload.subresources=&sub;upload.planes=nri::PlaneBits::COLOR;
    upload.after={nri::AccessBits::SHADER_RESOURCE,nri::Layout::SHADER_RESOURCE,nri::StageBits::FRAGMENT_SHADER};
    return RHIDevice::Get().UploadData(&upload,1,nullptr,0)==nri::Result::SUCCESS;
}
size_t WrittenDepth(const std::vector<uint8_t>& bytes) {
    size_t count=0;for(size_t i=0;i<bytes.size();i+=4){float value;std::memcpy(&value,bytes.data()+i,4);if(value<1)++count;}return count;
}
size_t Darkened(const std::vector<uint8_t>& off,const std::vector<uint8_t>& on) {
    if(off.size()!=on.size())return 0;size_t count=0;
    for(size_t i=0;i<off.size();i+=4)if(int(off[i])+off[i+1]+off[i+2]>int(on[i])+on[i+1]+on[i+2]+30)++count;
    return count;
}
bool Native(RHIType backend) {
    JobSystem::Init(1);
    struct Jobs {~Jobs(){ScriptHotReloadManager::Get().Shutdown();JobSystem::Shutdown();}} jobs;
    auto& device=RHIDevice::Get();RHIDeviceDesc init;init.PreferredBackend=backend;init.Headless=true;init.AllowFallback=false;
    init.EnableNRIValidation=true;init.EnableValidation=RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    CHECK(device.Init(init)&&device.GetBackend()==backend&&!device.IsSoftwareAdapter());
    struct Close {~Close(){RenderService::Get().Shutdown();RHIDevice::Get().WaitIdle();RHIDevice::Get().Shutdown();}} close;
    RHISceneRenderer renderer;CHECK(renderer.Init(device,false));CHECK(renderer.GetCascadedShadows().IsSupported());
    constexpr uint32_t width=128;RHITexture color,depth;
    CHECK(color.Init(RHITextureDesc::RenderTarget2D(width,width))&&depth.Init(RHITextureDesc::DepthStencil2D(width,width)));
    auto plane=RHIMesh::CreatePlane(30,30,1,1),cube=RHIMesh::CreateCube(2);
    CHECK(plane&&cube);
    RHIMaterialDesc desc;desc.CullMode=CullMode::None;desc.BaseColor=glm::vec4(.8f,.8f,.8f,1);desc.Roughness=.8f;
    auto material=std::make_shared<RHIMaterial>(desc);
    RenderWorld world;RenderMeshInstance floor;floor.EntityID=1;floor.NativeView=plane->CaptureView();floor.Material.RHIMaterialAsset=material;floor.CastsShadows=false;
    RenderMeshInstance caster=floor;caster.EntityID=2;caster.NativeView=cube->CaptureView();caster.CastsShadows=true;
    caster.WorldTransform=glm::translate(glm::mat4(1),glm::vec3(0,1.1f,0));
    world.Meshes={floor,caster};world.DirectionalLight=RenderDirectionalLight{};
    auto& sun=*world.DirectionalLight;sun.Direction=glm::normalize(glm::vec3(.7f,1,.3f));sun.ShadowQuality=0;
    sun.MaxShadowDistance=40;sun.CascadeSplitLambda=.65f;sun.ShadowSlopeBias=.002f;sun.ShadowNormalBias=.015f;
    PerspectiveCamera camera(55,1,.1f,100);
    camera.SetViewMatrix(glm::lookAtRH(glm::vec3(7,8,12),glm::vec3(0,0,0),glm::vec3(0,1,0)),glm::vec3(7,8,12));
    SceneRenderSettings settings;settings.EnableSkybox=false;settings.EnableParticles=false;settings.EnableToneMapping=false;
    settings.EnableStudioLightLobes=false;
    auto render=[&] {
        renderer.RenderWorldView(world,camera,&color,&depth,settings);
        if(!renderer.WasLastRenderSuccessful()){std::cerr<<renderer.GetCascadedShadows().GetDiagnostic()<<"\n";return std::vector<uint8_t>{};}
        return Read(color);
    };
    settings.EnableShadows=false;auto off=render();CHECK(off.size()==width*width*4);
    settings.EnableShadows=true;auto on=render();CHECK(on.size()==off.size());
    CHECK(renderer.GetCascadedShadows().GetDrawCount()==4);CHECK(renderer.GetRenderedEntityCount()==2);
    const auto dark=Darkened(off,on);std::cout<<"shadow darkened pixels="<<dark<<"\n";CHECK(dark>20);
    CHECK(PPM("off.ppm",off,width)&&PPM("on.ppm",on,width));
    // Every cascade receives real geometry, not just a cleared depth allocation.
    const auto originalMeshes=world.Meshes;
    const auto constants=renderer.GetCascadedShadows().GetConstants();
    const auto inverseView=glm::inverse(camera.GetViewMatrix());
    for(uint32_t c=0;c<4;++c) {
        auto marker=caster;marker.EntityID=10+c;
        float near=c==0?camera.GetNearClip():constants.Splits[c-1],far=constants.Splits[c];
        marker.WorldTransform=inverseView*glm::translate(glm::mat4(1),glm::vec3(0,0,-(near+far)*.5f))*
            glm::scale(glm::mat4(1),glm::vec3(std::max(.05f,(far-near)*.05f)));
        world.Meshes.push_back(marker);
    }
    CHECK(!render().empty());
    auto atlasBytes=Read(*renderer.GetCascadedShadows().GetAtlas());CHECK(!atlasBytes.empty());
    const auto resolution=uint32_t(renderer.GetCascadedShadows().GetConstants().Atlas.z);
    for(uint32_t c=0;c<4;++c) {
        uint32_t written=0;
        for(uint32_t y=0;y<resolution;++y)for(uint32_t x=0;x<resolution;++x) {
            float d;const auto index=((c/2*resolution+y)*(resolution*2)+c%2*resolution+x)*4;
            std::memcpy(&d,atlasBytes.data()+index,4);CHECK(std::isfinite(d)&&d>=0&&d<=1);if(d<.99999f)++written;
        }
        std::cout<<"cascade "<<c<<" depth pixels="<<written<<"\n";CHECK(written>0);
    }
    world.Meshes=originalMeshes;
    world.Meshes[1].CastsShadows=false;auto noCaster=render();CHECK(noCaster==off);
    world.Meshes[1].CastsShadows=true;sun.CastsShadows=false;CHECK(render()==off);sun.CastsShadows=true;
    world.DirectionalLight.reset();CHECK(!render().empty()&&!renderer.GetCascadedShadows().IsActive());
    // Restore light after resetting the optional (avoid retaining a dead reference).
    world.DirectionalLight=RenderDirectionalLight{};
    auto& restored=*world.DirectionalLight;restored.Direction=glm::normalize(glm::vec3(.7f,1,.3f));restored.MaxShadowDistance=40;
    restored.ShadowQuality=0;restored.CascadeSplitLambda=.65f;restored.ShadowSlopeBias=.002f;restored.ShadowNormalBias=.015f;
    settings.ShadowCascadeCount=3;CHECK(!render().empty()&&renderer.GetCascadedShadows().GetDrawCount()==3);
    settings.ShadowCascadeCount=4;
    // Cutout caster with zero alpha must leave the receiver identical to no shadow.
    auto cutoutDesc=desc;cutoutDesc.SurfaceType=SurfaceType::Cutout;cutoutDesc.BaseColor.a=0;
    auto cutout=std::make_shared<RHIMaterial>(cutoutDesc);world.Meshes[1].Material.RHIMaterialAsset=cutout;
    CHECK(!render().empty());
    const auto cutoutDepth=Read(*renderer.GetCascadedShadows().GetAtlas());
    for(size_t i=0;i<cutoutDepth.size();i+=4){float value;std::memcpy(&value,cutoutDepth.data()+i,4);CHECK(value==1);}
    // A real texture's alpha and authored UV/cutoff must affect shadow depth.
    auto alpha=std::make_shared<RHITexture>();auto alphaDesc=RHITextureDesc::RenderTarget2D(16,16,RHITextureFormat::RGBA8_UNORM);
    alphaDesc.Usage=RHITextureUsage::ShaderResource;CHECK(alpha->Init(alphaDesc)&&UploadAlpha(alpha,false));
    cutout->GetDesc().BaseColor.a=1;cutout->SetShadingModel(RHIShadingModel::TexturedPBR);
    cutout->SetTexture(RHITextureSlot::Albedo,alpha);
    CHECK(!render().empty());auto solidDepth=WrittenDepth(Read(*renderer.GetCascadedShadows().GetAtlas()));
    CHECK(solidDepth>0&&device.WaitIdle()&&UploadAlpha(alpha,true));
    CHECK(!render().empty());auto holesDepth=WrittenDepth(Read(*renderer.GetCascadedShadows().GetAtlas()));
    CHECK(holesDepth>0&&holesDepth<solidDepth);
    std::cout<<"cutout depth solid="<<solidDepth<<" holes="<<holesDepth<<"\n";
    CHECK(PPM("cutout.ppm",Read(color),width));
    world.Meshes[1].Material.RHIMaterialAsset=material;
    // Bias is applied only to sun visibility, not ambient or unrelated lights.
    restored.ShadowBias=1;CHECK(render()==off);restored.ShadowBias=.0012f;
    CHECK(Darkened(off,render())>20);
    auto transparentDesc=desc;transparentDesc.SurfaceType=SurfaceType::Transparent;
    auto transparent=std::make_shared<RHIMaterial>(transparentDesc);
    world.Meshes[0].Material.RHIMaterialAsset=transparent;
    settings.EnableSkybox=true;world.Skybox.Authored=world.Skybox.Enabled=true;
    settings.EnableShadows=false;auto skyOff=render();CHECK(!skyOff.empty());
    settings.EnableShadows=true;auto skyOn=render();CHECK(Darkened(skyOff,skyOn)>20);
    CHECK(PPM("sky-transparent.ppm",skyOn,width));
    settings.EnableSkybox=false;world.Skybox.Enabled=world.Skybox.Authored=false;world.Meshes[0].Material.RHIMaterialAsset=material;
    settings.EnableToneMapping=true;settings.EnableFXAA=true;settings.MSAASamples=2;
    CHECK(!render().empty()&&renderer.GetCascadedShadows().IsActive());
    settings.EnableToneMapping=false;settings.EnableFXAA=false;settings.MSAASamples=1;
    // A mesh with no native material remains opaque regardless of color alpha;
    // the push-constant union's legacy sun defaults must not turn it into cutout.
    const auto nativeMaterial=world.Meshes[1].Material;
    world.Meshes[1].Material.RHIMaterialAsset.reset();world.Meshes[1].Material.AlbedoColor.a=0;
    world.Meshes[1].Material.PatternMode=3; // Solid: old accidental cutout writes zero depth.
    CHECK(!render().empty()&&WrittenDepth(Read(*renderer.GetCascadedShadows().GetAtlas()))>0);
    world.Meshes[1].Material=nativeMaterial;
    // Transactional atlas replacement failure must reject the view, preserve the
    // previous generation, and permit a later successful retry.
    const auto atlasHandle=renderer.GetCascadedShadows().GetAtlas()->GetHandle();restored.ShadowQuality=1;
    bool injected=false;
    {ScopedRHIFault fault([&](std::string_view op)->std::optional<nri::Result>{if(op=="CreateTexture"&&!injected){injected=true;return nri::Result::OUT_OF_MEMORY;}return {};});
        renderer.RenderWorldView(world,camera,&color,&depth,settings);CHECK(injected&&!renderer.WasLastRenderSuccessful());}
    CHECK(renderer.GetCascadedShadows().GetAtlas()->GetHandle()==atlasHandle);
    CHECK(!render().empty()&&renderer.GetCascadedShadows().GetAtlas()->GetWidth()==4096);
    restored.ShadowQuality=2;CHECK(!render().empty()&&renderer.GetCascadedShadows().GetAtlas()->GetWidth()==8192);
    const auto ultraHandle=renderer.GetCascadedShadows().GetAtlas()->GetHandle();
    settings.EnableShadows=false;CHECK(!render().empty());
    CHECK(renderer.GetCascadedShadows().GetDrawCount()==0&&renderer.GetCascadedShadows().GetSampledAtlas()->GetWidth()==2);
    CHECK(renderer.GetCascadedShadows().GetAtlas()->GetHandle()==ultraHandle);
    settings.EnableShadows=true;
    restored.ShadowQuality=0;CHECK(!render().empty());
    CHECK(color.Init(RHITextureDesc::RenderTarget2D(64,64))&&depth.Init(RHITextureDesc::DepthStencil2D(64,64)));
    CHECK(render().size()==64*64*4);
    CHECK(color.Init(RHITextureDesc::RenderTarget2D(width,width))&&depth.Init(RHITextureDesc::DepthStencil2D(width,width)));
    CHECK(!render().empty());
    settings.ShadowCascadeCount=2;renderer.RenderWorldView(world,camera,&color,&depth,settings);CHECK(!renderer.WasLastRenderSuccessful());settings.ShadowCascadeCount=4;
    CHECK(!render().empty());
    auto originalMaterial=world.Meshes[1].Material;
    world.Meshes[1].Material.UseCustomShader=true;
    CHECK(!render().empty()&&renderer.GetCascadedShadows().GetDrawCount()==0);
    CHECK(renderer.GetCascadedShadows().GetDiagnostic().find("CSM_CUSTOM_SHADER")!=std::string::npos);
    world.Meshes[1].CastsShadows=false;CHECK(!render().empty());
    CHECK(renderer.GetCascadedShadows().GetDiagnostic().find("CSM_CUSTOM_SHADER")!=std::string::npos);
    world.Meshes[1].CastsShadows=true;world.Meshes[1].Material=originalMaterial;
    camera.SetOrthographic(20,1,-100,100);CHECK(!render().empty()&&!renderer.GetCascadedShadows().IsActive());
    CHECK(renderer.GetCascadedShadows().GetDiagnostic().find("CSM_CAMERA_RANGE")!=std::string::npos);
    CHECK(device.WaitIdle());
    // Removed scene owners must not survive in the high-watermark caster cache
    // or its descriptor dependencies once the preceding submission is drained.
    camera.SetPerspective(55,1,.1f,100);
    {
        RHICascadedShadows pass;MaterialTextureCache textureCache;RHIFrameContext frame;
        CHECK(pass.Init()&&frame.Init());
        RenderWorld temporary;temporary.DirectionalLight=restored;
        std::weak_ptr<const RHIMeshView> viewOwner;std::weak_ptr<RHITexture> textureOwner;
        RHIResourceHandle oldTexture;
        {
            auto texture=std::make_shared<RHITexture>();CHECK(texture->Init(alphaDesc)&&UploadAlpha(texture,false));
            auto temporaryDesc=desc;temporaryDesc.ShadingModel=RHIShadingModel::TexturedPBR;
            auto temporaryMaterial=std::make_shared<RHIMaterial>(temporaryDesc);temporaryMaterial->SetTexture(RHITextureSlot::Albedo,texture);
            auto instance=caster;instance.NativeView=cube->CaptureView();instance.Material.RHIMaterialAsset=temporaryMaterial;
            temporary.Meshes.push_back(instance);viewOwner=instance.NativeView;textureOwner=texture;oldTexture=texture->GetHandle();
        }
        CHECK(frame.Begin()&&pass.Prepare(temporary,camera,settings,textureCache));
        auto& commands=*frame.GetCommands();
        commands.TransitionTexture(pass.GetAtlas()->GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::DepthStencilWrite);
        CHECK(pass.Record(commands)&&frame.End()&&frame.Submit()&&device.WaitIdle());
        temporary.Clear();CHECK(frame.Begin());
        CHECK(pass.Prepare(temporary,camera,settings,textureCache));
        CHECK(viewOwner.expired()&&textureOwner.expired()&&!device.GetResources().Acquire(oldTexture).IsLive());
        CHECK(frame.Abandon());pass.Shutdown();
    }
    renderer.Shutdown();
    camera.SetPerspective(55,1,.1f,100);
    ViewRequest request;request.Width=request.Height=width;request.Camera=camera;request.Settings=settings;
    request.ColorTarget=&color;request.DepthTarget=&depth;
    auto& service=RenderService::Get();auto product=service.RenderView(world,request);
    CHECK(product.Success&&product.ErrorMessage.empty()&&service.GetActiveCapabilities().SupportsCascadedShadows);
    CHECK(Darkened(off,Read(color))>20);
    request.Settings.EnableShadows=false;product=service.RenderView(world,request);CHECK(product.Success&&product.ErrorMessage.empty());
    CHECK(Read(color)==off);
    service.Shutdown();
    CHECK(GetRHIValidationErrorCount()==0);
    std::cout<<"PASS native cascade depth, shadow images, flags, cutout, resize/failure and camera policy\n";return true;
}
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    Output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(Output);
    try {bool ok=Native(std::string(argv[1])=="vulkan"?Axle::RHI::RHIType::Vulkan:Axle::RHI::RHIType::Direct3D12);
        return ok&&Axle::RHI::GetRHIValidationErrorCount()==0?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
