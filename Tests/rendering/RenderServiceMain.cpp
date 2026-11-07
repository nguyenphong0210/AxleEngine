#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"
#include <Axle/RenderService.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/AssetThumbnailManager.hpp>
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
int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting(); Log::Init(); JobSystem::Init(2);
    const auto backend = argc > 1 && std::string(argv[1]) == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12;
    bool passed = false;
    try { passed = argc > 1 && std::string(argv[1]) == "gl" ? GLService() :
        argc > 2 && std::string(argv[2]) == "consumers" ? Thumbnails(backend) : Service(backend); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    RenderService::Get().Shutdown(); RHIMeshRegistry::Get().Shutdown(); RHIDevice::Get().Shutdown(); JobSystem::Shutdown();
    passed = passed && GetRHIValidationErrorCount() == 0;
    std::cout << "OPT05 RenderService " << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}
