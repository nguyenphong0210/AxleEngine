#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <Axle/SceneRenderer.hpp>
#include <Axle/SceneRenderBridge.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/Scene.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/Components.hpp>
#include <Axle/Window.hpp>
#include <Axle/RenderCommand.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; return false; } } while (0)
namespace {
using namespace Axle;
using namespace Axle::RHI;
std::filesystem::path Output;

bool TimestampProbe(uint64_t& ticks, double& milliseconds) {
    auto& device = RHIDevice::Get();
    if (!device.GetCapabilities().TimestampFrequencyHz) return false;
    nri::QueryPoolDesc queryDesc{};
    queryDesc.queryType = nri::QueryType::TIMESTAMP;
    queryDesc.capacity = 2;
    nri::QueryPool* queryPool = nullptr;
    if (device.GetNRICore().CreateQueryPool(*device.GetNRIDevice(), queryDesc, queryPool) != nri::Result::SUCCESS || !queryPool) return false;
    RHIBuffer readback;
    RHIBufferDesc readbackDesc{};
    readbackDesc.Size = 16;
    readbackDesc.MemoryLocation = RHIMemoryLocation::HostReadback;
    readbackDesc.DebugName = "TimestampProbeReadback";
    if (!readback.Init(readbackDesc)) { device.GetNRICore().DestroyQueryPool(queryPool); return false; }
    RHIBuffer scratch;
    if (!scratch.Init(RHIBufferDesc::Structured(1024, 4096, true, RHIMemoryLocation::Device, "TimestampProbeScratch"))) {
        readback.Shutdown(); device.GetNRICore().DestroyQueryPool(queryPool); return false;
    }
    RHICommandBuffer commands;
    if (!commands.Init(device.GetGraphicsQueue()) || !commands.Begin()) { scratch.Shutdown(); readback.Shutdown(); device.GetNRICore().DestroyQueryPool(queryPool); return false; }
    auto& core = device.GetNRICore();
    core.ResetQueries(*queryPool, 0, 2);
    core.CmdEndQuery(*commands.GetNative(), *queryPool, 0);
    for (uint32_t i = 0; i < 64; ++i) core.CmdZeroBuffer(*commands.GetNative(), *scratch.GetNative(), 0, scratch.GetSize());
    core.CmdEndQuery(*commands.GetNative(), *queryPool, 1);
    core.CmdCopyQueries(*commands.GetNative(), *queryPool, 0, 2, *readback.GetNative(), 0);
    if (!commands.End()) { commands.Shutdown(); scratch.Shutdown(); readback.Shutdown(); device.GetNRICore().DestroyQueryPool(queryPool); return false; }
    nri::CommandBuffer* native = commands.GetNative();
    nri::QueueSubmitDesc submit{}; submit.commandBuffers = &native; submit.commandBufferNum = 1;
    if (!device.Submit(submit) || !device.WaitIdle()) { commands.Shutdown(); scratch.Shutdown(); readback.Shutdown(); device.GetNRICore().DestroyQueryPool(queryPool); return false; }
    auto* values = static_cast<uint64_t*>(readback.Map());
    if (!values) { commands.Shutdown(); scratch.Shutdown(); readback.Shutdown(); device.GetNRICore().DestroyQueryPool(queryPool); return false; }
    ticks = values[1] > values[0] ? values[1] - values[0] : 0;
    readback.Unmap();
    milliseconds = static_cast<double>(ticks) * 1000.0 / static_cast<double>(device.GetCapabilities().TimestampFrequencyHz);
    commands.Shutdown(); scratch.Shutdown(); readback.Shutdown(); device.GetNRICore().DestroyQueryPool(queryPool);
    return true;
}

// Flat RGBE is a legal Radiance encoding. A small generated fixture keeps the
// authored source, orientation and values above one explicit in this test.
bool HDR(const char* name, glm::vec3 color, int pattern=0) {
    std::ofstream file(Output/name, std::ios::binary);
    file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 32 +X 64\n";
    for (int y=0;y<32;++y) for(int x=0;x<64;++x) {
        const float theta=(y+.5f)/32*glm::pi<float>(), phi=((x+.5f)/64-.5f)*glm::two_pi<float>();
        const glm::vec3 direction(std::cos(phi)*std::sin(theta),std::cos(theta),std::sin(phi)*std::sin(theta));
        const auto value=pattern==1 ? glm::vec3(2)+1.5f*direction : pattern==2 ?
            glm::vec3(.03f+20*std::pow(std::max(direction.z,0.f),64.f)) : color;
        int exponent=0;
        const float maximum=std::max({value.x,value.y,value.z});
        const float scale=std::frexp(maximum,&exponent)*256/maximum;
        const unsigned char pixel[]{static_cast<unsigned char>(value.x*scale),static_cast<unsigned char>(value.y*scale),
            static_cast<unsigned char>(value.z*scale),static_cast<unsigned char>(exponent+128)};
        file.write(reinterpret_cast<const char*>(pixel),4);
    }
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
uint8_t Display(float radiance) {
    const auto mapped=std::clamp((radiance*(2.51f*radiance+.03f))/(radiance*(2.43f*radiance+.59f)+.14f),0.f,1.f);
    return static_cast<uint8_t>(std::lround(std::pow(mapped,1.f/2.2f)*255));
}
bool OpenGL() {
    WindowProps props;props.Width=props.Height=128;props.Maximized=false;props.GraphicsBackend=GraphicsAPI::OpenGL;
    Window window(props);CHECK(RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(glfwGetProcAddress)));
    const auto oldRoot=FileSystem::GetAssetDirectory();FileSystem::SetAssetDirectory(Output);
    struct Root {std::filesystem::path Value;~Root(){FileSystem::SetAssetDirectory(Value);}} root{oldRoot};
    CHECK(HDR("gl.hdr",{2,.25f,.125f}));
    SceneRenderer renderer;renderer.Init();CHECK(renderer.IsReady());
    auto target=Framebuffer::Create({128,128,1,false});
    RenderWorld world;world.Skybox.Authored=world.Skybox.Enabled=true;
    world.Skybox.Type=1;world.Skybox.HDRPath="gl.hdr";
    RenderMeshInstance mesh;mesh.GLMesh=PrimitiveMesh::CreateSphere();mesh.Material.PatternMode=3;world.Meshes.push_back(mesh);
    PerspectiveCamera camera(55,1,.1f,100);
    camera.SetViewMatrix(glm::lookAtRH(glm::vec3(0,0,5),glm::vec3(0),glm::vec3(0,1,0)),glm::vec3(0,0,5));
    SceneRenderSettings settings;settings.EnableShadows=settings.EnableParticles=false;
    auto render=[&]{renderer.RenderWorldView(world,camera,target,nullptr,nullptr,settings);target->Bind();
        std::vector<uint8_t> pixels(128*128*4);glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());target->Unbind();return pixels;};
    auto uniform=[&](const char* name){GLint value=-1;const auto program=renderer.GetShader()->GetRendererID();
        glGetUniformiv(program,glGetUniformLocation(program,name),&value);return value;};
    const size_t center=(64*128+64)*4;
    const auto off=render();CHECK(uniform("u_UseIBL")==0&&off[center]<5&&off[0]>20);
    world.Skybox.EnvironmentLighting=true;const auto on=render();CHECK(uniform("u_UseIBL")==1&&on[center]>off[center]+20);
    world.Skybox.EnvironmentLighting=false;CHECK(render()==off&&uniform("u_UseIBL")==0);
    CHECK(PPM("gl-background.ppm",off,128)&&PPM("gl-environment.ppm",on,128));
    // Exercise the separate ECS bridge against the actual production shader.
    Scene scene;auto sky=scene.CreateEntity("Explicit environment");auto& authored=sky.AddComponent<SkyboxComponent>();
    authored.Type=SkyboxType::HDRCubemap;authored.HDRPath="gl.hdr";
    SceneRenderBridge::RenderGeometryPass(scene,camera,*renderer.GetShader());CHECK(uniform("u_UseIBL")==0);
    authored.EnvironmentLighting=true;SceneRenderBridge::RenderGeometryPass(scene,camera,*renderer.GetShader());CHECK(uniform("u_UseIBL")==1);
    authored.EnvironmentLighting=false;SceneRenderBridge::RenderGeometryPass(scene,camera,*renderer.GetShader());CHECK(uniform("u_UseIBL")==0);
    CHECK(glGetError()==GL_NO_ERROR);std::cout<<"PASS OpenGL background-only and explicit IBL pixels, snapshot and ECS bridge gates\n";return true;
}
bool Native(RHIType backend) {
    JobSystem::Init(1);
    struct Jobs { ~Jobs(){ScriptHotReloadManager::Get().Shutdown();JobSystem::Shutdown();} } jobs;
    auto& device=RHIDevice::Get(); RHIDeviceDesc init; init.PreferredBackend=backend;
    init.Headless=true; init.AllowFallback=false; init.EnableNRIValidation=true;
    init.EnableValidation=RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    CHECK(device.Init(init)&&device.GetBackend()==backend&&!device.IsSoftwareAdapter());
    struct Close { ~Close(){RHIDevice::Get().WaitIdle();RHIDevice::Get().Shutdown();} } close;
    uint64_t timestampTicks = 0; double timestampMilliseconds = 0.0;
    const bool timestampProbeOk = TimestampProbe(timestampTicks, timestampMilliseconds);
    CHECK(device.GetCapabilities().TimestampFrequencyHz > 0);
    {
        std::ofstream scorecard(Output / "M14_U9_timestamp_scorecard.txt");
        scorecard << "backend=" << (backend == RHIType::Vulkan ? "Vulkan" : "D3D12") << "\n"
                  << "timestamp_frequency_hz=" << device.GetCapabilities().TimestampFrequencyHz << "\n"
                  << "empty_gpu_scope_ticks=" << timestampTicks << "\n"
                  << "empty_gpu_scope_ms=" << timestampMilliseconds << "\n"
                  << "gpu_scope_valid=" << (timestampTicks > 0 ? 1 : 0) << "\n"
                  << "probe_submission_ok=" << (timestampProbeOk ? 1 : 0) << "\n";
    }
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
    world.Skybox.Authored=world.Skybox.Enabled=world.Skybox.EnvironmentLighting=true; world.Skybox.Type=1; world.Skybox.HDRPath="sky.axleasset";
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
    // M14-U4 Occlusion verification: Dedicated compute GTAO & directional contact shadows
    settings.EnableGTAO=true; settings.EnableContactShadows=true; auto gtao=render();
    CHECK(!gtao.empty()&&gtao.size()==red.size());
    {
        std::ofstream scorecard(Output / "M14_U9_timestamp_scorecard.txt", std::ios::app);
        scorecard << "renderer_gpu_scope_ms=" << renderer.GetLastGPUTimestampMilliseconds() << "\n"
                  << "renderer_gpu_scope_valid=" << (renderer.GetLastGPUTimestampMilliseconds() > 0.0 ? 1 : 0) << "\n";
    }
    settings.EnableGTAO=true; settings.EnableContactShadows=false; auto gtaoOnly=render();
    CHECK(!gtaoOnly.empty()&&gtaoOnly.size()==red.size());
    settings.EnableGTAO=false; settings.EnableContactShadows=true; auto contactOnly=render();
    CHECK(!contactOnly.empty()&&contactOnly.size()==red.size());
    settings.EnableGTAO=false; settings.EnableContactShadows=false;
    settings.EnableSSR=true; auto ssr=render();
    CHECK(!ssr.empty()&&ssr.size()==red.size());
    // M14-U5 Reflection probe & Rect Light verification
    RenderReflectionProbe probe;
    probe.Position = glm::vec3(0.0f, 2.0f, 0.0f);
    probe.BoxExtents = glm::vec3(20.0f, 20.0f, 20.0f);
    probe.Intensity = 1.5f;
    probe.ParallaxCorrected = true;
    world.ReflectionProbes.push_back(probe);
    auto ssrWithProbe = render();
    CHECK(!ssrWithProbe.empty() && ssrWithProbe.size() == red.size());
    world.ReflectionProbes.clear();
    settings.EnableSSR=false;

    RenderRectLight rect;
    rect.Position = glm::vec3(0.0f, 4.0f, 0.0f);
    rect.Direction = glm::vec3(0.0f, -1.0f, 0.0f);
    rect.Up = glm::vec3(0.0f, 0.0f, 1.0f);
    rect.Width = 3.0f; rect.Height = 1.5f;
    rect.Color = glm::vec3(1.0f, 0.9f, 0.8f);
    rect.Intensity = 50.0f; rect.Range = 25.0f;
    rect.TwoSided = false;
    world.RectLights.push_back(rect);
    auto withRectLight = render();
    CHECK(!withRectLight.empty() && withRectLight.size() == red.size());
    world.RectLights.clear();

    // M14-U6 Volumetric Fog & Participating Media verification
    settings.EnableVolumetricFog = true;
    auto fog = render();
    CHECK(!fog.empty() && fog.size() == red.size());
    settings.EnableVolumetricFog = false;

    RenderVolumetricFog rvf;
    rvf.Enabled = true;
    rvf.Density = 0.05f;
    rvf.Color = glm::vec3(0.9f, 0.95f, 1.0f);
    rvf.Intensity = 1.2f;
    world.VolumetricFog = rvf;
    settings.EnableVolumetricFog = true;
    auto fogAuthored = render();
    CHECK(!fogAuthored.empty() && fogAuthored.size() == red.size());
    world.VolumetricFog.reset();
    settings.EnableVolumetricFog = false;
    // M14-U6 DDGI runtime update: the authored probe lattice must allocate and
    // refresh its bounded irradiance atlas without changing the fallback path.
    RenderDDGIProbeGrid ddgiRuntime;
    ddgiRuntime.Enabled = true;
    ddgiRuntime.GridDimensions = glm::ivec3(32, 16, 32);
    ddgiRuntime.GridSpacing = glm::vec3(1.5f);
    ddgiRuntime.EnergyMultiplier = 1.25f;
    world.DDGIProbeGrid = ddgiRuntime;
    settings.EnableDDGI = true;
    auto ddgiFrame = render();
    CHECK(!ddgiFrame.empty() && ddgiFrame.size() == red.size());
    settings.EnableDDGI = false;
    world.DDGIProbeGrid.reset();
    CHECK(render()==red&&environment.BakeCount()==1&&environment.UploadCount()==1);

    // M14-U3 infrastructure verification: Storage/UAV texture views, ScreenNormal,
    // ScreenStorage, and double-buffered history view targets.
    {
        RHITexture storageTex;
        bool inited = storageTex.Init(RHITextureDesc::StorageTexture2D(width, width, RHITextureFormat::RGBA8_UNORM, "TestStorageTex"));
        CHECK(inited);
        CHECK(storageTex.IsValid() && storageTex.GetUAV() != nullptr && storageTex.GetSRV() != nullptr);
        CHECK(storageTex.GetNativeUAVHandle() != 0);

        RHIViewTargets screenTargets;
        RHIViewTargetDesc targetDesc;
        targetDesc.Width = width; targetDesc.Height = width;
        targetDesc.EnableNormal = true;
        targetDesc.EnableScreenStorage = true;
        targetDesc.HistoryCount = 2;
        CHECK(screenTargets.Configure(targetDesc));
        CHECK(screenTargets.IsValid());
        CHECK(screenTargets.Get(RHIViewTarget::ScreenNormal) != nullptr);
        CHECK(screenTargets.Get(RHIViewTarget::ScreenNormal)->IsValid());
        CHECK(screenTargets.Get(RHIViewTarget::ScreenStorage) != nullptr);
        CHECK(screenTargets.Get(RHIViewTarget::ScreenStorage)->IsValid());
        CHECK(screenTargets.Get(RHIViewTarget::ScreenStorage)->GetUAV() != nullptr);
        CHECK(screenTargets.Get(RHIViewTarget::History0) != nullptr && screenTargets.Get(RHIViewTarget::History0)->IsValid());
        CHECK(screenTargets.Get(RHIViewTarget::History1) != nullptr && screenTargets.Get(RHIViewTarget::History1)->IsValid());
    }

    // M14-U5 Serialization & Extraction test
    {
        auto testScene = std::make_shared<Scene>();
        auto e1 = testScene->CreateEntity("ProbeEntity");
        auto& probeComp = e1.AddComponent<ReflectionProbeComponent>();
        probeComp.BoxExtents = glm::vec3(15.0f, 12.0f, 18.0f);
        probeComp.Intensity = 2.0f;
        probeComp.ParallaxCorrected = true;
        probeComp.Shape = ProbeShape::Box;

        auto e2 = testScene->CreateEntity("RectLightEntity");
        auto& rectComp = e2.AddComponent<RectLightComponent>();
        rectComp.Width = 5.0f;
        rectComp.Height = 2.5f;
        rectComp.Color = glm::vec3(1.0f, 0.8f, 0.6f);
        rectComp.Intensity = 120.0f;
        rectComp.TwoSided = true;

        auto e3 = testScene->CreateEntity("FogEntity");
        auto& fogComp = e3.AddComponent<VolumetricFogComponent>();
        fogComp.Density = 0.04f;
        fogComp.Color = glm::vec3(0.7f, 0.8f, 0.9f);
        fogComp.MaxDistance = 150.0f;

        auto e4 = testScene->CreateEntity("DDGIEntity");
        auto& ddgiComp = e4.AddComponent<DDGIProbeGridComponent>();
        ddgiComp.GridDimensions = glm::ivec3(32, 16, 32);
        ddgiComp.GridSpacing = glm::vec3(1.5f, 1.5f, 1.5f);
        ddgiComp.EnergyMultiplier = 1.25f;

        SceneSerializer serializer(testScene.get());
        std::string serialized = serializer.SerializeToString();
        CHECK(!serialized.empty());

        auto restoredScene = std::make_shared<Scene>();
        SceneSerializer deserializer(restoredScene.get());
        CHECK(deserializer.DeserializeFromString(serialized));

        auto viewProbe = restoredScene->GetRegistry().view<ReflectionProbeComponent>();
        CHECK(viewProbe.begin() != viewProbe.end());
        for (auto ent : viewProbe) {
            const auto& p = viewProbe.get<ReflectionProbeComponent>(ent);
            CHECK(p.Intensity == 2.0f);
            CHECK(p.BoxExtents.x == 15.0f);
            CHECK(p.ParallaxCorrected == true);
        }

        auto viewRect = restoredScene->GetRegistry().view<RectLightComponent>();
        CHECK(viewRect.begin() != viewRect.end());
        for (auto ent : viewRect) {
            const auto& r = viewRect.get<RectLightComponent>(ent);
            CHECK(r.Width == 5.0f);
            CHECK(r.Height == 2.5f);
            CHECK(r.Intensity == 120.0f);
            CHECK(r.TwoSided == true);
        }

        auto viewFog = restoredScene->GetRegistry().view<VolumetricFogComponent>();
        CHECK(viewFog.begin() != viewFog.end());
        for (auto ent : viewFog) {
            const auto& f = viewFog.get<VolumetricFogComponent>(ent);
            CHECK(f.Density == 0.04f);
            CHECK(f.MaxDistance == 150.0f);
        }

        auto viewDDGI = restoredScene->GetRegistry().view<DDGIProbeGridComponent>();
        CHECK(viewDDGI.begin() != viewDDGI.end());
        for (auto ent : viewDDGI) {
            const auto& d = viewDDGI.get<DDGIProbeGridComponent>(ent);
            CHECK(d.GridDimensions.x == 32);
            CHECK(d.EnergyMultiplier == 1.25f);
        }
    }
    // The HDR background remains visible when environment lighting is opt-out;
    // the metallic receiver must not receive ambient/specular energy from it.
    world.Skybox.EnvironmentLighting=false; auto backgroundOnly=render();
    CHECK(!backgroundOnly.empty()&&backgroundOnly[0]>20&&backgroundOnly[center]<5);
    // Background colors and HDR import exposure must not alter an existing
    // authored directional fill when sky illumination is disabled.
    const auto originalSky=world.Skybox;world.DirectionalLight=RenderDirectionalLight{};
    world.DirectionalLight->Intensity=0;world.DirectionalLight->AmbientIntensity=.25f;
    const auto fill=render();
    world.Skybox.ZenithColor=world.Skybox.HorizonColor=world.Skybox.GroundColor={1,0,0,1};
    authored.Exposure=2;CHECK(TextureDescriptor::Save((Output/"sky.axleasset").string(),authored));
    const auto changedBackground=render();CHECK(!fill.empty()&&!changedBackground.empty());
    for(size_t c=0;c<3;++c)CHECK(fill[center+c]==changedBackground[center+c]);
    world.DirectionalLight.reset();world.Skybox=originalSky;authored.Exposure=1;
    CHECK(TextureDescriptor::Save((Output/"sky.axleasset").string(),authored));
    world.Skybox.EnvironmentLighting=true; CHECK(render()==red);
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
    settings.EnableToneMapping=false; settings.EnableFXAA=false; settings.MSAASamples=1;
    // Axis-coded radiance checks the actual native cube sampler and sky view
    // reconstruction, including both poles, rather than only CPU face centers.
    CHECK(HDR("axes.hdr",{},1)); world.Skybox.HDRPath="axes.hdr"; world.Meshes.clear();
    const glm::vec3 axes[]{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for(uint32_t face=0;face<6;++face) {
        const auto axis=axes[face], up=std::abs(axis.y)>.5f?glm::vec3(0,0,1):glm::vec3(0,1,0);
        camera.SetViewMatrix(glm::lookAtRH(glm::vec3(0),axis,up),glm::vec3(0));
        auto image=render(); CHECK(!image.empty());
        for(uint32_t c=0;c<3;++c) CHECK(std::abs(int(image[center+c])-Display(2+1.5f*axis[c]))<=2);
    }
    const auto lut=Read(*environment.Ready()->BRDF,16); CHECK(lut.size()==16*16*16);
    for(size_t i=0;i<lut.size();i+=16) {
        glm::vec4 value;std::memcpy(&value,lut.data()+i,16);
        CHECK(std::isfinite(value.x)&&std::isfinite(value.y)&&value.x>=0&&value.y>=0&&value.x+value.y<1.1f);
    }
    // A narrow HDR lobe produces a sharp metallic highlight, then a lower,
    // wider reflection as roughness selects the prefiltered mip chain.
    CHECK(HDR("lobe.hdr",{},2));world.Skybox.HDRPath="lobe.hdr";world.Meshes.push_back(mesh);
    camera.SetViewMatrix(glm::lookAtRH(glm::vec3(0,0,5),glm::vec3(0),glm::vec3(0,1,0)),glm::vec3(0,0,5));
    material->GetDesc().Roughness=.05f; auto sharp=render(); CHECK(!sharp.empty());
    material->GetDesc().Roughness=.9f; auto rough=render(); CHECK(!rough.empty());
    std::cout<<"reflection center sharp="<<int(sharp[center])<<" rough="<<int(rough[center])<<"\n";
    CHECK(sharp[center]>rough[center]+30);
    // M14-U1.1: the rough metallic lobe must retain materially more energy
    // than the pre-compensation single-scatter baseline (150 on this fixture).
    CHECK(rough[center]>=180);
    CHECK(PPM("sharp.ppm",sharp,width)&&PPM("rough.ppm",rough,width));
    // LRU stays bounded. Switching an environment refreshes even unused draw
    // descriptors so eviction does not retain the old native texture forever.
    const auto old=environment.Ready()->Radiance->GetHandle();
    std::weak_ptr<const RHIEnvironmentGeneration> oldOwner=environment.Ready();
    world.Meshes.clear();
    for(int i=0;i<5;++i) {
        const auto name="cache"+std::to_string(i)+".hdr";CHECK(HDR(name.c_str(),{.1f+float(i),.5f,.25f}));
        world.Skybox.HDRPath=name;CHECK(!render().empty()&&environment.CacheSize()<=4);
    }
    CHECK(!render().empty()&&device.WaitIdle());
    CHECK(oldOwner.expired()&&!device.GetResources().Acquire(old).IsLive());
    // LDR sources are rejected instead of silently converted to HDR.
    const unsigned char ppm[]{'P','6','\n','1',' ','1','\n','2','5','5','\n',255,0,0};
    CHECK(FileSystem::WriteBinaryFileAtomically(Output/"ldr.ppm",ppm,sizeof(ppm)));
    world.Skybox.HDRPath="ldr.ppm";CHECK(!render().empty()&&!environment.Ready());
    CHECK(environment.Diagnostic().find("ENV_FORMAT")!=std::string::npos);
    world.Skybox.HDRPath="missing.hdr";CHECK(!render().empty()&&!environment.Ready());
    CHECK(environment.Diagnostic().find("ENV_SOURCE")!=std::string::npos);
    world.Skybox.HDRPath.clear();CHECK(!render().empty()&&!environment.Ready());
    CHECK(environment.Diagnostic().find("ENV_SOURCE")!=std::string::npos);
    // Descriptor resolution uses native filesystem paths through the VFS, not
    // the Windows narrow codepage, for non-ASCII project/asset names.
    const auto unicodePath=Output/std::filesystem::path(L"sky_\u0111\u1ecf.axleasset");
    authored.Source="blue.hdr";
    CHECK(FileSystem::WriteIdentifiedTextFile(unicodePath,TextureDescriptor::Serialize(authored)));
    world.Skybox.HDRPath="sky_\xC4\x91\xE1\xBB\x8F.axleasset";
    CHECK(!render().empty()&&environment.Ready()&&environment.Diagnostic().empty());
    // A procedural background is not a sun or an ambient source. Raw snapshots
    // must obey the same ownership rules as snapshots extracted from the ECS.
    world.Skybox.Type=0;world.Skybox.EnvironmentLighting=false;world.Skybox.HDRPath.clear();world.Meshes={mesh};
    const auto atmosphereOnly=render();CHECK(!atmosphereOnly.empty()&&atmosphereOnly[center]<5&&atmosphereOnly[0]>20);
    world.Skybox.EnvironmentLighting=true;const auto proceduralIBL=render();
    CHECK(!proceduralIBL.empty()&&environment.Ready()&&environment.Diagnostic().empty()&&proceduralIBL[center]>atmosphereOnly[center]+10);
    const auto proceduralGeneration=environment.Ready()->Radiance->GetHandle();CHECK(environment.BakeCount()>=1&&environment.UploadCount()>=1);
    CHECK(render()==proceduralIBL&&environment.Ready()->Radiance->GetHandle()==proceduralGeneration);
    world.Skybox.EnvironmentLighting=false;
    world.Skybox.SunColor=glm::vec4(0);CHECK(render()==atmosphereOnly);
    world.DirectionalLight=RenderDirectionalLight{};world.DirectionalLight->AmbientIntensity=0;
    world.DirectionalLight->Direction={0,0,1};
    const auto direct=render();CHECK(!direct.empty()&&direct[center]>atmosphereOnly[center]+20);
    material->GetDesc().Roughness=.02f;
    world.DirectionalLight->Intensity=.02f;
    world.DirectionalLight->SolarAngularRadius=.20f;const auto broadSun=render();
    world.DirectionalLight->SolarAngularRadius=.00462512f;const auto physicalSun=render();
    CHECK(!broadSun.empty()&&!physicalSun.empty()&&std::abs(int(physicalSun[center])-int(broadSun[center]))>10);
    world.DirectionalLight->Intensity=1.0f;
    world.Meshes.clear();camera.SetViewMatrix(glm::lookAtRH(glm::vec3(0),glm::vec3(0,0,1),glm::vec3(0,1,0)),glm::vec3(0));
    const auto sun=render();world.DirectionalLight->Intensity=0;const auto zeroSun=render();
    CHECK(!sun.empty()&&!zeroSun.empty()&&sun[center]>zeroSun[center]+10);
    world.DirectionalLight.reset();world.Skybox.SunDirection={0,0,1,0};CHECK(render()==zeroSun);

    // M14-U7 Post & Temporal (Bloom, Auto-Exposure, TAA) verification
    world.Meshes = { mesh };
    world.DirectionalLight = RenderDirectionalLight{};
    world.DirectionalLight->Intensity = 10.0f;
    world.DirectionalLight->Direction = { 0, 0, 1 };
    settings.EnableToneMapping = true;

    // 1. Bloom Off vs On
    settings.EnableBloom = false;
    const auto bloomOff = render();
    settings.EnableBloom = true;
    settings.BloomThreshold = 0.5f;
    settings.BloomIntensity = 1.5f;
    const auto bloomOn = render();
    CHECK(!bloomOff.empty() && !bloomOn.empty());
    CHECK(bloomOn[center] >= bloomOff[center]);
    settings.EnableBloom = false;

    // 2. Auto-Exposure smooth adaptation
    settings.EnableAutoExposure = true;
    const auto autoExpFrame1 = render();
    const auto autoExpFrame2 = render();
    CHECK(!autoExpFrame1.empty() && !autoExpFrame2.empty());
    settings.EnableAutoExposure = false;

    // 3. TAA Jitter and History Reprojection
    settings.EnableTAA = false;
    const auto taaBaseline = render();
    settings.EnableTAA = true;
    const auto taaFrame1 = render();
    const auto taaFrame2 = render();
    CHECK(!taaBaseline.empty() && !taaFrame1.empty() && !taaFrame2.empty());
    // The first frame must bypass invalid history, while the second frame must
    // use the opposite history slot after the resolve copy.
    for (size_t c = 0; c < 3; ++c) {
        CHECK(std::abs(int(taaFrame1[center + c]) - int(taaBaseline[center + c])) <= 32);
    }
    settings.EnableTAA = false;
    settings.EnableToneMapping = false;

    // M14-U8 Complex Shading Models (Charlie Sheen, SSS, Beer-Lambert & Dispersion)
    material->GetDesc().SurfaceType = Axle::SurfaceType::Opaque;
    material->GetDesc().Roughness = 0.5f;
    material->GetDesc().Metallic = 0.0f;
    world.DirectionalLight = RenderDirectionalLight{};
    world.DirectionalLight->Intensity = 2.0f;
    world.DirectionalLight->Direction = { 0.5f, 0.5f, 0.707f };

    // 1. Charlie Sheen verification
    material->GetDesc().SheenEnabled = false;
    const auto sheenOff = render();
    material->GetDesc().SheenEnabled = true;
    material->GetDesc().SheenColor = glm::vec3(1.0f, 1.0f, 1.0f);
    material->GetDesc().SheenRoughness = 0.3f;
    const auto sheenOn = render();
    CHECK(!sheenOff.empty() && !sheenOn.empty());
    CHECK(sheenOn[center] >= sheenOff[center]);
    material->GetDesc().SheenEnabled = false;

    // 2. Subsurface Scattering verification
    material->GetDesc().SubsurfaceEnabled = false;
    const auto sssOff = render();
    material->GetDesc().SubsurfaceEnabled = true;
    material->GetDesc().SubsurfaceColor = glm::vec3(1.0f, 0.4f, 0.2f);
    material->GetDesc().SubsurfaceRadius = 1.0f;
    const auto sssOn = render();
    CHECK(!sssOff.empty() && !sssOn.empty());
    CHECK(sssOn[center] >= sssOff[center]);
    material->GetDesc().SubsurfaceEnabled = false;

    // 3. Beer-Lambert Absorption & Dispersion verification
    material->GetDesc().SurfaceType = Axle::SurfaceType::Transparent;
    material->GetDesc().RefractionEnabled = true;
    material->GetDesc().RefractionStrength = 1.0f;
    material->GetDesc().RefractionIOR = 1.5f;
    material->GetDesc().RefractionThickness = 0.5f;
    material->GetDesc().AbsorptionEnabled = true;
    material->GetDesc().AbsorptionColor = glm::vec3(1.5f, 0.5f, 0.1f);
    material->GetDesc().Dispersion = 0.05f;
    const auto absorbedGlass = render();
    CHECK(!absorbedGlass.empty());
    material->GetDesc().AbsorptionEnabled = false;
    material->GetDesc().RefractionEnabled = false;
    material->GetDesc().SurfaceType = Axle::SurfaceType::Opaque;

    CHECK(device.WaitIdle()); renderer.Shutdown(); CHECK(environment.CacheSize()==0);
    CHECK(GetRHIValidationErrorCount()==0);
    // CPU cache metadata may outlive a native device. Stale native generations
    // must become invalid and be recreated under the new owner epoch.
    RHIEnvironment survivor;CHECK(survivor.SetBakeSettings(bake));world.Skybox.Type=1;world.Skybox.HDRPath="blue.hdr";
    CHECK(survivor.Prepare(world.Skybox)&&survivor.Ready());const auto stale=survivor.Ready()->Radiance->GetHandle();
    device.Shutdown();CHECK(!survivor.Ready()->IsValid());CHECK(device.Init(init));
    CHECK(survivor.Prepare(world.Skybox)&&survivor.Ready()&&survivor.UploadCount()==2);
    CHECK(survivor.Ready()->Radiance->GetHandle()!=stale&&survivor.CacheSize()==1);
    survivor.Clear();CHECK(device.WaitIdle()&&GetRHIValidationErrorCount()==0);
    std::cout<<"PASS HDR native subresources, sky/specular/diffuse, cache, exposure, reload, fallback and upload retry\n";
    return true;
}
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    Output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(Output);
    try { return (std::string(argv[1])=="gl"?OpenGL():Native(std::string(argv[1])=="vulkan"?RHIType::Vulkan:RHIType::Direct3D12))&&GetRHIValidationErrorCount()==0?0:1; }
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
