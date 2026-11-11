#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; return false; } } while (0)
namespace {
using namespace Axle;
using namespace Axle::RHI;
std::filesystem::path Output;

// Flat RGBE is a legal Radiance encoding. A small generated fixture keeps the
// authored source, orientation and values above one explicit in this test.
bool HDR(const char* name, glm::vec3 color) {
    std::ofstream file(Output/name, std::ios::binary);
    file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 32 +X 64\n";
    int exponent = 0;
    const float scale = std::frexp(std::max({color.x,color.y,color.z}), &exponent) * 256 /
        std::max({color.x,color.y,color.z});
    const unsigned char pixel[]{static_cast<unsigned char>(color.x*scale), static_cast<unsigned char>(color.y*scale),
        static_cast<unsigned char>(color.z*scale), static_cast<unsigned char>(exponent+128)};
    for (int i=0; i<64*32; ++i) file.write(reinterpret_cast<const char*>(pixel), 4);
    return bool(file);
}
std::vector<uint8_t> Read(RHITexture& texture, uint32_t bpp=4, uint32_t face=0, uint32_t mip=0) {
    const auto width=std::max(1u,texture.GetWidth()>>mip), height=std::max(1u,texture.GetHeight()>>mip);
    const auto pitch=(width*bpp+255)&~255u, size=(pitch*height+511)&~511u;
    RHIBufferDesc desc; desc.Size=size; desc.MemoryLocation=RHIMemoryLocation::HostReadback;
    RHIBuffer buffer; RHICommandBuffer commands;
    if (!buffer.Init(desc)||!commands.Init()||!commands.Begin()) return {};
    nri::TextureDataLayoutDesc layout{}; layout.rowPitch=pitch; layout.slicePitch=size;
    nri::TextureRegionDesc region{}; region.width=width; region.height=height; region.depth=1;
    region.layerOffset=face; region.mipOffset=mip;
    commands.ReadbackTextureToBuffer(*buffer.GetNative(),layout,*texture.GetNative(),region);
    commands.TransitionTexture(texture.GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ShaderResource);
    if (!commands.End()||!commands.Submit()||!RHIDevice::Get().WaitIdle()) return {};
    auto* data=static_cast<uint8_t*>(buffer.Map()); if (!data) return {};
    std::vector<uint8_t> result(size_t(width)*height*bpp);
    for (uint32_t y=0;y<height;++y) std::memcpy(result.data()+size_t(y)*width*bpp,data+size_t(y)*pitch,width*bpp);
    buffer.Unmap(); return result;
}
bool PPM(const char* name,const std::vector<uint8_t>& image,uint32_t width) {
    std::ofstream file(Output/name,std::ios::binary); file<<"P6\n"<<width<<" "<<width<<"\n255\n";
    for(size_t i=0;i<image.size();i+=4) file.write(reinterpret_cast<const char*>(image.data()+i),3);
    return bool(file);
}
bool ConstantCube(RHITexture& texture,glm::vec3 expected) {
    for(uint32_t face=0;face<6;++face) for(uint32_t mip=0;mip<texture.GetDesc().MipLevels;++mip) {
        const auto bytes=Read(texture,16,face,mip); CHECK(!bytes.empty());
        for(size_t i=0;i<bytes.size();i+=16) {
            glm::vec4 pixel; std::memcpy(&pixel,bytes.data()+i,16);
            CHECK(glm::length(glm::vec3(pixel)-expected)<1e-4f&&pixel.w==1);
        }
    }
    return true;
}
bool Native(RHIType backend) {
    JobSystem::Init(1);
    struct Jobs { ~Jobs(){ScriptHotReloadManager::Get().Shutdown();JobSystem::Shutdown();} } jobs;
    auto& device=RHIDevice::Get(); RHIDeviceDesc init; init.PreferredBackend=backend;
    init.Headless=true; init.AllowFallback=false; init.EnableNRIValidation=true;
    init.EnableValidation=RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    CHECK(device.Init(init)&&device.GetBackend()==backend&&!device.IsSoftwareAdapter());
    struct Close { ~Close(){RHIDevice::Get().WaitIdle();RHIDevice::Get().Shutdown();} } close;
    const auto oldRoot=FileSystem::GetAssetDirectory(); FileSystem::SetAssetDirectory(Output);
    struct Root { std::filesystem::path Value; ~Root(){FileSystem::SetAssetDirectory(Value);} } root{oldRoot};
    CHECK(HDR("red.hdr",{2,.25f,.125f})&&HDR("blue.hdr",{.125f,.25f,2}));
    TextureDescriptor authored; authored.Type="CubemapHDR"; authored.Source="red.hdr";
    CHECK(TextureDescriptor::Save((Output/"sky.axleasset").string(),authored));
    RHISceneRenderer renderer; CHECK(renderer.Init(device,false));
    auto& environment=renderer.GetEnvironment(); EnvironmentBakeSettings bake;
    bake.EnvironmentSize=16; bake.IrradianceSize=4; bake.PrefilterSize=16; bake.PrefilterMips=5; bake.BRDFSize=16; bake.Samples=64;
    CHECK(environment.SetBakeSettings(bake));
    constexpr uint32_t width=128;
    RHITexture color,depth; CHECK(color.Init(RHITextureDesc::RenderTarget2D(width,width)));
    CHECK(depth.Init(RHITextureDesc::DepthStencil2D(width,width)));
    auto sphere=RHIMesh::CreateSphere(1); CHECK(sphere);
    RHIMaterialDesc materialDesc; materialDesc.CullMode=CullMode::None;
    materialDesc.BaseColor=glm::vec4(.8f,.8f,.8f,1); materialDesc.Metallic=1; materialDesc.Roughness=.2f;
    auto material=std::make_shared<RHIMaterial>(materialDesc);
    RenderWorld world; RenderMeshInstance mesh; mesh.EntityID=1; mesh.NativeView=sphere->CaptureView();
    mesh.Material.RHIMaterialAsset=material; world.Meshes.push_back(mesh);
    world.Skybox.Authored=world.Skybox.Enabled=true; world.Skybox.Type=1; world.Skybox.HDRPath="sky.axleasset";
    PerspectiveCamera camera(55,1,.1f,100);
    camera.SetViewMatrix(glm::lookAtRH(glm::vec3(0,0,5),glm::vec3(0),glm::vec3(0,1,0)),glm::vec3(0,0,5));
    SceneRenderSettings settings; settings.EnableSkybox=true; settings.EnableShadows=false;
    settings.EnableParticles=false; settings.EnableToneMapping=false; settings.EnableStudioLightLobes=false;
    auto render=[&]{renderer.RenderWorldView(world,camera,&color,&depth,settings);
        return renderer.WasLastRenderSuccessful()?Read(color):std::vector<uint8_t>{};};
    auto red=render(); CHECK(red.size()==width*width*4&&environment.Ready());
    CHECK(environment.BakeCount()==1&&environment.UploadCount()==1&&environment.Diagnostic().empty());
    const auto first=environment.Ready()->Radiance->GetHandle();
    CHECK(ConstantCube(*environment.Ready()->Radiance,{2,.25f,.125f}));
    CHECK(ConstantCube(*environment.Ready()->Irradiance,{2,.25f,.125f}));
    CHECK(ConstantCube(*environment.Ready()->Prefilter,{2,.25f,.125f}));
    const size_t center=(width/2*width+width/2)*4;
    CHECK(red[0]>red[2]+40&&red[center]>red[center+2]+40); // sky and metallic specular receive HDR
    CHECK(PPM("red.ppm",red,width));
    CHECK(render()==red&&environment.BakeCount()==1&&environment.UploadCount()==1);
    // Exposure affects illumination and sky, without rebuilding immutable maps.
    world.Skybox.Intensity=.25f; auto dim=render(); CHECK(!dim.empty()&&dim[0]<red[0]&&dim[center]<red[center]);
    CHECK(environment.BakeCount()==1); world.Skybox.Intensity=1;
    authored.Exposure=.5f; CHECK(TextureDescriptor::Save((Output/"sky.axleasset").string(),authored));
    CHECK(!render().empty()&&environment.SourceExposure()==.5f&&environment.BakeCount()==1);
    authored.Exposure=1; CHECK(TextureDescriptor::Save((Output/"sky.axleasset").string(),authored));
    // Corrupting the same source retains its exact last-good GPU generation.
    CHECK(FileSystem::WriteTextFile(Output/"red.hdr","broken")); CHECK(render()==red);
    CHECK(environment.Ready()->Radiance->GetHandle()==first&&!environment.Diagnostic().empty());
    CHECK(HDR("red.hdr",{.125f,.25f,2})); auto blue=render(); CHECK(!blue.empty()&&blue[0]+40<blue[2]&&blue[center]+40<blue[center+2]);
    CHECK(environment.UploadCount()==2&&environment.Ready()->Radiance->GetHandle()!=first);
    CHECK(PPM("blue.ppm",blue,width));
    // A valid descriptor redirected to a missing source must not borrow red/blue.
    authored.Source="missing.hdr"; CHECK(TextureDescriptor::Save((Output/"sky.axleasset").string(),authored));
    auto fallback=render(); CHECK(!fallback.empty()&&!environment.Ready()&&!environment.Diagnostic().empty());
    CHECK(fallback[center]<5&&fallback[center+1]<5&&fallback[center+2]<5); // diffuse-only fallback on a pure metal
    material->GetDesc().Metallic=0; auto diffuse=render(); CHECK(!diffuse.empty()&&diffuse[center]>20);
    material->GetDesc().Metallic=1;
    // Upload failure leaves the old same-source generation available and retries.
    authored.Source="red.hdr"; CHECK(TextureDescriptor::Save((Output/"sky.axleasset").string(),authored));
    CHECK(render()==blue); auto previous=environment.Ready()->Radiance->GetHandle();
    CHECK(HDR("red.hdr",{4,.25f,.125f})); bool injected=false;
    { ScopedRHIFault fault([&](std::string_view op)->std::optional<nri::Result>{
        if(op=="CreateTexture"&&!injected){injected=true;return nri::Result::OUT_OF_MEMORY;} return {};});
      CHECK(render()==blue&&injected&&environment.Ready()->Radiance->GetHandle()==previous); }
    auto restored=render(); CHECK(!restored.empty()&&restored[0]>red[0]&&environment.UploadCount()==3);
    // Disabling IBL leaves the visible sky but removes fake metal reflections.
    renderer.SetUseIBL(false); auto disabled=render(); CHECK(!disabled.empty()&&disabled[0]==restored[0]&&disabled[center]<5);
    renderer.SetUseIBL(true); CHECK(render()==restored);
    world.Skybox.Enabled=false; CHECK(!render().empty()&&!environment.Ready());
    world.Skybox.Enabled=true; CHECK(render()==restored&&environment.UploadCount()==3);
    settings.EnableToneMapping=true; settings.EnableFXAA=true; settings.MSAASamples=2;
    CHECK(!render().empty()&&environment.Ready());
    CHECK(device.WaitIdle()); renderer.Shutdown(); CHECK(environment.CacheSize()==0);
    CHECK(GetRHIValidationErrorCount()==0);
    std::cout<<"PASS HDR native subresources, sky/specular/diffuse, cache, exposure, reload, fallback and upload retry\n";
    return true;
}
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    Output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(Output);
    try { return Native(std::string(argv[1])=="vulkan"?RHIType::Vulkan:RHIType::Direct3D12)&&GetRHIValidationErrorCount()==0?0:1; }
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
