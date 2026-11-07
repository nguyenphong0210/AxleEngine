#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"
#include <Axle/RenderService.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/AssetThumbnailManager.hpp>
#include <Axle/UI/Canvas2D.hpp>
#include <Axle/UI/UIFont.hpp>
#include <Axle/RHI/RHIPresentationContext.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <Axle/Window.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/FileSystem.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <chrono>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
using namespace Axle;
using namespace Axle::RHI;
using Axle::RenderTests::ReadTexture;

static bool Service(RHIType backend) {
    auto& device = RHIDevice::Get();
    RenderService service;
    RHIDeviceDesc init; init.PreferredBackend = backend; init.AllowFallback = false; init.Headless = true;
    init.EnableNRIValidation = init.EnableValidation = true;
    RENDER_CHECK(RHIDevice::IsGraphicsAPIValidationAvailable(backend), "actual API debug prerequisites required");
    for (int epoch = 0; epoch < 2; ++epoch) {
        RENDER_CHECK(device.Init(init) && device.GetBackend() == backend && !device.IsSoftwareAdapter(), "physical backend without fallback");
        {
            RENDER_CHECK(service.Init(), "service initializes per device epoch");
            Scene scene;
            auto entity = scene.CreateEntity("Arbitrary name");
            entity.AddComponent<MeshInstance3D>().AssetPath = "builtin://cube";
            auto& material = entity.AddComponent<MaterialComponent>();
            material.AlbedoColor = {1,0,0,1}; material.EmissionColor = {1,0,0}; material.EmissionIntensity = 1;
            ViewRequest request; request.Width = request.Height = 48;
            request.Settings.EnableSkybox = request.Settings.EnableShadows = false;
            request.Settings.ClearColor = {0,0,0,1};
            request.Camera = PerspectiveCamera(45,1,0.1f,100);
            request.Camera.SetViewMatrix(glm::lookAt(glm::vec3(0,0,3),glm::vec3(0),glm::vec3(0,1,0)),{0,0,3});
            auto world = ExtractRenderWorld(static_cast<const Scene&>(scene));
            RENDER_CHECK(!material.RHIMaterialAsset && !entity.GetComponent<MeshInstance3D>().RHIMeshAsset,
                "snapshot cannot publish GPU/material caches into authoring data");
            auto result = service.RenderView(world,request);
            RENDER_CHECK(result.Success && result.CompletionToken && result.CompletionToken.Epoch == device.GetEpoch() &&
                result.CompletionToken == device.GetLastSubmittedToken(), "service returns actual successful submission token");
            auto* target = service.GetDefaultMainColorTarget();
            RENDER_CHECK(target, "default main target");
            const auto red = ReadTexture(*target); const auto center = (24*48+24)*4;
            RENDER_CHECK(red.size() == 48*48*4 && red[center] > red[center+1]+30 && red[center] > red[center+2]+30,
                "actual service geometry has intended red material pixels");
            material.AlbedoColor = {0,1,0,1}; material.EmissionColor = {0,1,0};
            auto greenResult = service.RenderView(scene,request);
            RENDER_CHECK(greenResult.Success, "scene adapter uses shared snapshot path");
            const auto green = ReadTexture(*target);
            RENDER_CHECK(green[center+1] > green[center]+30, "later scene material edits change actual output");
            result = service.RenderView(world,request);
            RENDER_CHECK(result.Success && ReadTexture(*target) == red, "saved snapshot material remains immutable after scene edits");
            for (uint8_t samples : {uint8_t(1), uint8_t(4)}) for (bool tone : {false,true}) for (bool fxaa : {false,true}) {
                auto post = request;
                post.Settings.MSAASamples = samples; post.Settings.EnableToneMapping = tone; post.Settings.EnableFXAA = fxaa;
                result = service.RenderView(world,post);
                RENDER_CHECK(result.Success, "declared MSAA/tone/FXAA pass combination submits");
                auto pixels = ReadTexture(*service.GetDefaultMainColorTarget());
                RENDER_CHECK(pixels.size() == red.size() && pixels[center] > pixels[center+1]+20 && pixels[center] > pixels[center+2]+20,
                    "actual post-processing graph retains authored red geometry");
            }
            Scene empty;
            auto emptyRequest = request; emptyRequest.Settings.ClearColor = {0,0,1,1};
            RENDER_CHECK(service.RenderView(empty,emptyRequest).Success, "empty scene still records a clear/export view");
            auto cleared = ReadTexture(*service.GetDefaultMainColorTarget());
            RENDER_CHECK(cleared.size() == red.size() && cleared[center+2] > 245 && cleared[center] < 8,
                "empty views replace prior geometry with their declared clear");
            RENDER_CHECK(service.RenderView(world,request).Success, "return from optional/empty views");
            target = service.GetDefaultMainColorTarget();
            RENDER_CHECK(ReadTexture(*target) == red, "optional passes cannot leak into later default views");
            auto mutableGeometry=RHIMesh::CreateCube();
            auto& authoredMesh=entity.GetComponent<MeshInstance3D>();authoredMesh.RHIMeshAsset=mutableGeometry;
            auto frozenGeometry=ExtractRenderWorld(scene);
            RENDER_CHECK(service.RenderView(frozenGeometry,request).Success,"captured native geometry generation");
            const auto originalGeometry=ReadTexture(*target);
            std::vector<RHIVertex3D> smallVertices;std::vector<uint32_t> smallIndices;
            RHIMesh::GenerateCube(0.2f,smallVertices,smallIndices);
            RENDER_CHECK(mutableGeometry->Init(smallVertices,smallIndices)&&service.RenderView(scene,request).Success&&ReadTexture(*target)!=originalGeometry,
                "later mutable mesh wrapper replacement changes only later extracted views");
            mutableGeometry->Shutdown();authoredMesh.RHIMeshAsset.reset();
            RENDER_CHECK(service.RenderView(frozenGeometry,request).Success&&ReadTexture(*target)==originalGeometry,
                "saved snapshot keeps exact native geometry after wrapper replacement and shutdown");
            RENDER_CHECK(service.RenderView(world,request).Success,"restore original world after generation probe");
            const auto old = target->GetHandle();
            auto resized = request; resized.Width = resized.Height = 49;
            bool reached = false;
            {
                ScopedRHIFault fault([&](std::string_view operation)->std::optional<nri::Result>{
                    if (operation == "CreateTexture") { reached = true; return nri::Result::OUT_OF_MEMORY; } return {};
                });
                result = service.RenderView(world,resized);
            }
            RENDER_CHECK(reached && !result.Success && !result.CompletionToken && service.GetDefaultMainColorTarget()->GetHandle() == old,
                "failed target replacement preserves last good generation and reports failure");
            auto zero = request; zero.Width = 0;
            const auto prior = device.GetLastSubmittedToken();
            RENDER_CHECK(!service.RenderView(world,zero).Success && device.GetLastSubmittedToken() == prior, "zero extent never submits or allocates");
            material.CustomShaderDiagnostic="Current owner diagnostic";
            RENDER_CHECK(!service.RenderView(scene,zero).Success && material.CustomShaderDiagnostic=="Current owner diagnostic",
                "rejected scene request cannot publish stale feedback from a previous view");
            auto privateRequest = request; privateRequest.Kind = RenderViewKind::Thumbnail;
            RENDER_CHECK(!service.RenderView(world,privateRequest).Success, "private views cannot borrow shared main targets");
            {
                ScopedRHIFault fault([](std::string_view operation)->std::optional<nri::Result>{
                    if (operation == "QueueSubmit") return nri::Result::FAILURE; return {};
                });
                result = service.RenderView(world,request);
            }
            RENDER_CHECK(!result.Success && !result.CompletionToken, "submit failure cannot fabricate view success or completion");
            RENDER_CHECK(device.GetState() == RHIDeviceState::Stopping && !service.RenderView(world,request).Success, "ambiguous submit failure stops work until a checked restart");
            RENDER_CHECK(device.WaitIdle(), "completed service work");
        }
        RHIMeshRegistry::Get().Shutdown();
        RENDER_CHECK(device.Shutdown() && !service.IsInitialized(), "device close hook retires service before device; same object can reopen next epoch");
    }
    return true;
}
static std::filesystem::path ThumbnailFixture(const std::string& mode) {
    auto root = std::filesystem::path("build/agent-runs/OPT-05/consumer-fixtures") /
        (mode + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    AssemblyData data; data.IsValid = true; data.Name = "Authored thumbnail";
    data.RootNode.HasMesh = true; data.RootNode.Mesh.AssetPath = "builtin://cube";
    data.RootNode.HasMaterial = true; data.RootNode.Material.AlbedoColor = {1,0,0,1};
    data.RootNode.Material.EmissionColor = {1,0,0}; data.RootNode.Material.EmissionIntensity = 1;
    AssemblyNode child; child.HasMesh = true; child.Mesh.AssetPath = "builtin://sphere";
    child.Transform.Translation = {1.2f,0,0}; child.Transform.Scale = glm::vec3(0.35f);
    data.RootNode.Children.push_back(child);
    const auto path = root/"thumbnail.axle";
    return AssemblySerializer::SaveAssemblyData(path,data) ? path : std::filesystem::path{};
}
static bool Thumbnails(RHIType backend) {
    auto& device = RHIDevice::Get();
    RHIDeviceDesc init; init.PreferredBackend = backend; init.Headless = true; init.AllowFallback = false;
    init.EnableValidation = init.EnableNRIValidation = true;
    RENDER_CHECK(device.Init(init) && device.GetBackend() == backend && !device.IsSoftwareAdapter(), "physical thumbnail backend");
    {
        ImGuiLayer ui(backend == RHIType::Vulkan ? ImGuiBackendType::Vulkan : ImGuiBackendType::Direct3D12); ui.Init(nullptr);
        ImGui::GetIO().DisplaySize = {96,96}; ImGui::GetIO().DeltaTime = 1.0f/60;
        AssetThumbnailManager manager; manager.SetImGuiLayer(&ui);
        const auto fixture = ThumbnailFixture(backend == RHIType::Vulkan ? "vulkan" : "dx12");
        RENDER_CHECK(!fixture.empty(), "authored builtin hierarchy fixture saved");
        RENDER_CHECK(manager.GetOrCreateThumbnail(fixture) == 0, "thumbnail generation is bounded by OnUpdate");
        manager.OnUpdate();
        ui.Begin();
        const auto image = manager.GetOrCreateThumbnail(fixture);
        RENDER_CHECK(image && manager.GetCachedMeta(fixture) && manager.GetCachedMeta(fixture)->SubPartCount == 2, "real builtin assembly thumbnail and metadata");
        ImGui::GetForegroundDrawList()->AddImage(static_cast<ImTextureID>(image),{0,0},{96,96});
        manager.InvalidateThumbnail(fixture);
        ImGui::Render();
        RHITexture output; RHICommandBuffer commands;
        RENDER_CHECK(output.Init(RHITextureDesc::RenderTarget2D(96,96)) && commands.Init() && commands.Begin(), "thumbnail draw target");
        commands.TransitionTexture(output.GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ColorAttachment);
        nri::AttachmentDesc attachment{}; attachment.descriptor = output.GetRTV(); attachment.loadOp = nri::LoadOp::CLEAR; attachment.storeOp = nri::StoreOp::STORE;
        nri::RenderingDesc rendering{}; rendering.colors = &attachment; rendering.colorNum = 1;
        commands.BeginRendering(rendering); RENDER_CHECK(ui.Render(&commands), "queued thumbnail records after cache invalidation");
        commands.EndRendering(); commands.TransitionTexture(output.GetNative(),RHIResourceLayout::ColorAttachment,RHIResourceLayout::ShaderResource);
        RENDER_CHECK(commands.End() && commands.Submit() && device.WaitIdle(), "queued thumbnail submits real geometry");
        const auto pixels = ReadTexture(output); size_t red = 0;
        for (size_t i=0;i+3<pixels.size();i+=4) if (pixels[i] > pixels[i+1]+25 && pixels[i] > pixels[i+2]+25) ++red;
        RENDER_CHECK(red > 100, "actual red thumbnail pixels survive invalidate-before-submit");
        ui.End(); manager.Clear();
        RenderService::Get().Shutdown();
    }
    RHIMeshRegistry::Get().Shutdown(); return device.Shutdown();
}
static bool GLService() {
    WindowProps props; props.Title = "OPT05 GL service validation"; props.Width = props.Height = 96; props.Maximized = false;
    props.GraphicsBackend = GraphicsAPI::OpenGL; Window window(props);
    RENDER_CHECK(RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(glfwGetProcAddress)), "real GL context");
    {
        ImGuiLayer ui(ImGuiBackendType::OpenGL); ui.Init(window.GetNativeWindow());
        AssetThumbnailManager manager; manager.SetImGuiLayer(&ui);
        const auto fixture = ThumbnailFixture("gl");
        RENDER_CHECK(!fixture.empty() && manager.GetOrCreateThumbnail(fixture) == 0, "queued GL thumbnail fixture");
        manager.OnUpdate(); ui.Begin();
        const auto image = static_cast<GLuint>(manager.GetOrCreateThumbnail(fixture));
        RENDER_CHECK(image && glIsTexture(image), "GL thumbnail attachment still has a framebuffer owner");
        std::vector<uint8_t> pixels(128*128*4); glGetTextureImage(image,0,GL_RGBA,GL_UNSIGNED_BYTE,static_cast<GLsizei>(pixels.size()),pixels.data());
        size_t red=0; for(size_t i=0;i<pixels.size();i+=4) if(pixels[i]>pixels[i+1]+25 && pixels[i]>pixels[i+2]+25) ++red;
        RENDER_CHECK(red>100, "GL thumbnail uses actual material geometry pixels");
        ImGui::GetForegroundDrawList()->AddImage(static_cast<ImTextureID>(image),{0,0},{96,96});
        manager.InvalidateThumbnail(fixture);
        RENDER_CHECK(glIsTexture(image), "GL invalidation retains attachment through current UI frame");
        ui.End(); RENDER_CHECK(!glIsTexture(image), "completed GL UI releases retired framebuffer attachment");
        RenderService::Get().Shutdown();
    }
    return glGetError() == GL_NO_ERROR;
}
static void EmitCanvas(Canvas2D& canvas) {
    canvas.Init(); canvas.BeginCanvas({128,128});
    UIStyle red; red.BackgroundColor={1,0,0,0.5f}; red.BorderWidth=0;
    canvas.PushClipRect({16,16,16,16}); canvas.DrawRect({8,8,40,40},red); canvas.PopClipRect();
    auto image=std::make_shared<DecodedImage>(); image->Mips.push_back({1,1,{0,0,255,255}});
    ImageSourceData source; source.SourceKey=source.RequestKey="canvas-blue";source.Revision=1;source.Image=image;
    canvas.DrawSprite({64,8,24,24},canvas.RegisterImage(source));
    canvas.DrawTextString("AX",std::make_shared<UIFont>(),{8,60,100,32},32,{1,1,1,1});
    for(int level=0;level<3;++level) {
        canvas.DrawRadialArc({96,float(4+level*32),24,24},0,360,level*0.5f,4,{0,1,0,1},{1,0,0,1});
        canvas.DrawRadialArc({72,float(36+level*32),24,24},90,270,level*0.5f,4,{0,1,0,1},{1,0,0,1});
    }
    canvas.EndCanvas();
}
static bool CheckCanvas(const std::vector<uint8_t>& pixels, bool flip) {
    RENDER_CHECK(pixels.size()==128*128*4,"Canvas readback extent");
    auto at=[&](unsigned x,unsigned y){return ((flip?127-y:y)*128+x)*4;};
    const auto inside=at(24,24),outside=at(12,12),sprite=at(72,16);
    std::cout << "Canvas pixels inside=" << unsigned(pixels[inside]) << "," << unsigned(pixels[inside+1]) << "," << unsigned(pixels[inside+2]) << " outside=" << unsigned(pixels[outside]) << " sprite=" << unsigned(pixels[sprite+2]) << "\n";
    RENDER_CHECK(pixels[inside]>110 && pixels[inside]<150 && pixels[inside+1]<8,"alpha-blended red rectangle pixels");
    RENDER_CHECK(pixels[outside]<8 && pixels[outside+1]<8,"partial clip removes pixels outside scissor");
    RENDER_CHECK(pixels[sprite+2]>245 && pixels[sprite]<8,"authored sprite texture sampled by GPU");
    size_t white=0;for(unsigned y=60;y<110;++y)for(unsigned x=8;x<70;++x){auto p=at(x,y);if(pixels[p]>150&&pixels[p+1]>150&&pixels[p+2]>150)++white;}
    RENDER_CHECK(white>30 && white<900,"font atlas produces glyph strokes and transparent background, not solid quads");
    auto redAt=[&](unsigned x,unsigned y){const auto p=at(x,y);return pixels[p]>220&&pixels[p+1]<8;};
    auto greenAt=[&](unsigned x,unsigned y){const auto p=at(x,y);return pixels[p+1]>220&&pixels[p]<8;};
    RENDER_CHECK(redAt(115,23),"zero-value full circle keeps a visible track");
    RENDER_CHECK(greenAt(108,58)&&redAt(108,38),"half-value full circle splits fill and track");
    RENDER_CHECK(greenAt(108,70),"full-value full circle fills the complete ring");
    RENDER_CHECK(redAt(74,48)&&greenAt(77,87)&&redAt(77,73)&&greenAt(74,112),
        "nonzero-start arcs preserve relative zero/half/full fill extents");
    const auto outsideArc=at(94,112);
    RENDER_CHECK(pixels[outsideArc]<8&&pixels[outsideArc+1]<8&&pixels[outsideArc+2]<8,"partial arcs stay transparent outside their span");
    return true;
}
static bool NativeCanvas(RHIType backend) {
    auto& device=RHIDevice::Get();RHIDeviceDesc desc;desc.PreferredBackend=backend;desc.AllowFallback=false;
    desc.EnableValidation=desc.EnableNRIValidation=true;
    RENDER_CHECK(device.Init(desc)&&device.GetBackend()==backend&&!device.IsSoftwareAdapter(),"physical Canvas fixture");
    {
        WindowProps props;props.Title="OPT05 Runtime image presentation";props.Width=160;props.Height=128;props.Maximized=false;
        props.GraphicsBackend=backend==RHIType::Vulkan?GraphicsAPI::Vulkan:GraphicsAPI::Direct3D12;
        Window window(props);
        RHITexture target; RENDER_CHECK(target.Init(RHITextureDesc::RenderTarget2D(128,128)),"Canvas target");
        RHICommandBuffer clear;RENDER_CHECK(clear.Init()&&clear.Begin(),"Canvas clear recording");
        clear.TransitionTexture(target.GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ColorAttachment);
        nri::AttachmentDesc color{};color.descriptor=target.GetRTV();color.loadOp=nri::LoadOp::CLEAR;color.storeOp=nri::StoreOp::STORE;
        color.clearValue.color.f={0,0,0,1};nri::RenderingDesc rendering{};rendering.colors=&color;rendering.colorNum=1;
        clear.BeginRendering(rendering);clear.EndRendering();clear.TransitionTexture(target.GetNative(),RHIResourceLayout::ColorAttachment,RHIResourceLayout::ShaderResource);
        RENDER_CHECK(clear.End()&&clear.Submit()&&device.WaitIdle(),"real Canvas background clear");
        Canvas2D canvas;EmitCanvas(canvas);
        RENDER_CHECK(canvas.GetStats().DrawCalls==0&&canvas.Submit(&target)&&canvas.GetStats().DrawCalls>=4,"only actual Canvas submission reports draws");
        RENDER_CHECK(CheckCanvas(ReadTexture(target),false),"native Canvas text/sprite/clip/blend output");
        const auto beforeClip=ReadTexture(target);
        Canvas2D clipped;clipped.Init();clipped.BeginCanvas({128,128});
        clipped.PushClipRect({200,200,20,20});clipped.DrawRect({200,200,20,20},UIStyle{});clipped.EndCanvas();
        RENDER_CHECK(clipped.Submit(&target)&&clipped.GetStats().DrawCalls==0&&ReadTexture(target)==beforeClip,
            "fully clipped native Canvas succeeds without draws or pixel changes");clipped.Shutdown();
        RHIPresentationContext presenter;RENDER_CHECK(presenter.Init(window.GetNativeWindow()),"ImGui-free presentation owner");
        for(unsigned i=0;i<7;++i) {
            const auto result=presenter.RenderFrame(i<3?160:173,i<3?128:131,false,{}, {},target.GetNative());
            RENDER_CHECK(result==RHIPresentOutcome::Presented,"sampled image presentation handles RGBA/BGRA, scaling and odd resize");
        }
        RENDER_CHECK(presenter.GetPresentCount()==7&&presenter.Shutdown(),"actual repeated image presentation completion");
        canvas.Shutdown();
    }
    return device.Shutdown();
}
static bool GLCanvas() {
    WindowProps props;props.Title="OPT05 GL Canvas";props.Width=props.Height=128;props.Maximized=false;props.GraphicsBackend=GraphicsAPI::OpenGL;
    Window window(props);RENDER_CHECK(RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(glfwGetProcAddress)),"GL Canvas context");
    auto target=Framebuffer::Create({128,128,1,false});target->Bind();glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    GLuint priorBuffer=0,priorTexture=0;
    glCreateBuffers(1,&priorBuffer);glBindBuffer(GL_ARRAY_BUFFER,priorBuffer);
    glCreateTextures(GL_TEXTURE_2D,1,&priorTexture);glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,priorTexture);
    glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT,GL_FUNC_SUBTRACT);
    glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);glEnable(GL_STENCIL_TEST);glStencilFunc(GL_NEVER,0,0xff);
    glEnable(GL_FRAMEBUFFER_SRGB);
    Canvas2D canvas;EmitCanvas(canvas);RENDER_CHECK(canvas.Submit()&&canvas.GetStats().DrawCalls>=4,"GL Canvas submits actual geometry");
    GLint active=0,texture=0,buffer=0,equation=0;GLboolean mask[4]{};
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);glGetIntegerv(GL_BLEND_EQUATION_RGB,&equation);glGetBooleanv(GL_COLOR_WRITEMASK,mask);
    RENDER_CHECK(active==GL_TEXTURE3&&texture==priorTexture&&buffer==priorBuffer&&equation==GL_FUNC_REVERSE_SUBTRACT&&
        !mask[0]&&mask[1]&&!mask[2]&&mask[3]&&glIsEnabled(GL_STENCIL_TEST)&&glIsEnabled(GL_FRAMEBUFFER_SRGB),
        "GL Canvas restores caller bindings, blending, write masks and tests, including lazy image initialization");
    std::vector<uint8_t> pixels(128*128*4);glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    RENDER_CHECK(CheckCanvas(pixels,true),"GL Canvas glyph/sprite/clip/blend readback");canvas.Shutdown();
    Canvas2D clipped;clipped.Init();clipped.BeginCanvas({128,128});
    clipped.PushClipRect({200,200,20,20});clipped.DrawRect({200,200,20,20},UIStyle{});clipped.EndCanvas();
    RENDER_CHECK(clipped.Submit()&&clipped.GetStats().DrawCalls==0,"fully clipped GL Canvas succeeds with zero draws");
    std::vector<uint8_t> afterClip(pixels.size());glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,afterClip.data());
    RENDER_CHECK(afterClip==pixels,"clipped GL Canvas leaves destination untouched");clipped.Shutdown();
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_STENCIL_TEST);glDisable(GL_FRAMEBUFFER_SRGB);
    glDeleteBuffers(1,&priorBuffer);glDeleteTextures(1,&priorTexture);
    target->Unbind();target.reset();return glGetError()==GL_NO_ERROR;
}
int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting(); Log::Init(); JobSystem::Init(2);
    const auto backend = argc > 1 && std::string(argv[1]) == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12;
    bool passed = false;
    try { passed = argc > 2 && std::string(argv[2]) == "canvas" ? (std::string(argv[1])=="gl" ? GLCanvas() : NativeCanvas(backend)) :
        argc > 1 && std::string(argv[1]) == "gl" ? GLService() :
        argc > 2 && std::string(argv[2]) == "consumers" ? Thumbnails(backend) : Service(backend); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    RenderService::Get().Shutdown(); RHIMeshRegistry::Get().Shutdown(); RHIDevice::Get().Shutdown(); JobSystem::Shutdown();
    passed = passed && GetRHIValidationErrorCount() == 0;
    std::cout << "OPT05 RenderService " << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}
