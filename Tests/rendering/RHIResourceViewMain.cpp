#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"
#include <Axle/Log.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Window.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <Axle/RHIViewportDescriptor.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHIObjectPicker.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

using namespace Axle;
using namespace Axle::RHI;
using Axle::RenderTests::ReadTexture;

static bool UIErrors(RHIType backend) {
    auto& device = RHIDevice::Get();
    WindowProps props; props.Title = "OPT-04 UI failure ownership"; props.Width = props.Height = 96; props.Maximized = false;
    props.GraphicsBackend = backend == RHIType::Vulkan ? GraphicsAPI::Vulkan : GraphicsAPI::Direct3D12;
    Window window(props);
    auto ui = std::make_unique<ImGuiLayer>(backend == RHIType::Vulkan ? ImGuiBackendType::Vulkan : ImGuiBackendType::Direct3D12);
    ui->Init(window.GetNativeWindow());
    ui->Begin();
    ImGui::Begin("Failure ownership fixture"); ImGui::TextUnformatted("Native font and vertex storage"); ImGui::End();
    ui->End();
    bool reached = false;
    {
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "QueueSubmit") { reached = true; return nri::Result::FAILURE; }
            return {};
        });
        ui->Begin();
        ImGui::Begin("Failure ownership fixture"); ImGui::TextUnformatted("Pending submission"); ImGui::End();
        ui->End();
    }
    RENDER_CHECK(reached && !ui->IsOperational() && !device.CanSubmit(), "native UI submit rejection stops product admission");
    unsigned retired = 0;
    {
        ScopedRHIFault fault([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "QueueDrain") return nri::Result::FAILURE;
            return {};
        });
        ui->Shutdown();
        ui.reset();
        device.ReleaseNative([&] { ++retired; });
        RENDER_CHECK(retired == 0 && !ImGui::GetCurrentContext(), "destroyed UI layer retains its independent native context and callback owners");
    }
    RENDER_CHECK(device.WaitIdle() && retired == 1, "checked retry retires native UI after the layer has already been destroyed");
    return true;
}

static bool Run(RHIType backend) {
    WindowProps props;
    props.Title = "OPT-04 resource transaction probe"; props.Width = props.Height = 128; props.Maximized = false;
    props.GraphicsBackend = backend == RHIType::Vulkan ? GraphicsAPI::Vulkan : GraphicsAPI::Direct3D12;
    Window window(props);
    RENDER_CHECK(window.GetNativeWindow(), "real native window");
    ImGuiLayer ui(backend == RHIType::Vulkan ? ImGuiBackendType::Vulkan : ImGuiBackendType::Direct3D12);
    ui.Init(window.GetNativeWindow());
    RHIViewportDesc desc; desc.Width = desc.Height = 64; desc.ColorFormat = RHITextureFormat::RGBA8_UNORM;
    RHIViewportDescriptor view;
    RENDER_CHECK(view.Init(&ui, desc), "complete view and real GUI registration");
    auto* oldColor = view.GetColorTexture()->GetNative();
    auto* oldDepth = view.GetDepthTexture()->GetNative();
    const auto oldGUI = view.GetImTextureID();
    for (const auto* boundary : {"CreateTextureDSV", "RegisterViewportTexture"}) {
        bool reached = false;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == boundary) { reached = true; return nri::Result::FAILURE; }
            return {};
        });
        RENDER_CHECK(!view.Resize(96, 96) && reached && view.IsValid() && view.GetWidth() == 64 &&
            view.GetColorTexture()->GetNative() == oldColor && view.GetDepthTexture()->GetNative() == oldDepth && view.GetImTextureID() == oldGUI,
            "depth/registration failure preserves complete view and GUI binding");
    }
    RENDER_CHECK(!view.Resize(0, 64) && view.GetImTextureID() == oldGUI, "minimized extent does not replace view");
    RENDER_CHECK(!view.SetColorFormat(RHITextureFormat::D32_FLOAT) && view.GetColorFormat() == RHITextureFormat::RGBA8_UNORM, "failed format change preserves metadata");
    auto& device = RHIDevice::Get();
    RHISceneRenderer renderer;
    RENDER_CHECK(renderer.Init(device, false), "production renderer starts with validated resources");
    auto scene = std::make_shared<Scene>();
    std::vector<RHIVertex3D> vertices;
    std::vector<uint32_t> indices;
    RHIMesh::GenerateCube(1, vertices, indices);
    auto mesh = std::make_shared<RHIMesh>();
    RENDER_CHECK(mesh->Init(vertices, indices, "transaction cube"), "mesh generation before replacement");
    auto entity = scene->CreateEntity("transaction cube");
    auto& instance = entity.AddComponent<MeshInstance3D>();
    instance.RHIMeshAsset = mesh; instance.PatternMode = 3; instance.Color1 = instance.Color2 = {0.9f, 0.1f, 0.2f, 1};
    PerspectiveCamera camera(45, 1, 0.1f, 100);
    camera.SetViewMatrix(glm::lookAt(glm::vec3(0, 0, 4), glm::vec3(0), glm::vec3(0, 1, 0)), {0, 0, 4});
    SceneRenderSettings settings;
    settings.EnableSkybox = settings.EnableShadows = settings.EnableToneMapping = settings.EnableFXAA = false;
    settings.EnableParticles = false; settings.MSAASamples = 1; settings.ClearColor = {0, 0, 0, 1};
    auto draw = [&] { renderer.RenderScene(scene, camera, view.GetColorTexture(), view.GetDepthTexture(), settings); };
    draw();
    const auto originalPixels = ReadTexture(*view.GetColorTexture());
    RENDER_CHECK(originalPixels.size() == 64 * 64 * 4 && originalPixels[(32 * 64 + 32) * 4] > 20 && renderer.GetDrawCallCount() > 0, "actual mesh produces non-background pixels");
    {
        unsigned count = 0;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "MapBuffer" && ++count == 2) return nri::Result::FAILURE;
            return {};
        });
        RENDER_CHECK(!mesh->Init(vertices, indices, "failed replacement") && count == 2, "actual mesh replacement fails after real vertex upload");
    }
    draw();
    RENDER_CHECK(ReadTexture(*view.GetColorTexture()) == originalPixels, "old mesh remains drawable with identical pixels");
    settings.EnableToneMapping = settings.EnableFXAA = true; settings.MSAASamples = 4;
    draw();
    RENDER_CHECK(renderer.GetMSAAColorTarget() && renderer.GetMSAADepthTarget() && renderer.GetIntermediateHDRTarget() && renderer.GetIntermediateFXAATarget(), "all real MSAA/post-processing targets created");
    auto* msaaColor = renderer.GetMSAAColorTarget()->GetNative();
    auto* msaaDepth = renderer.GetMSAADepthTarget()->GetNative();
    settings.MSAASamples = 2;
    const auto beforeFailure = ReadTexture(*view.GetColorTexture());
    {
        bool reached = false;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "CreateTextureDSV") { reached = true; return nri::Result::FAILURE; }
            return {};
        });
        draw();
        RENDER_CHECK(reached && renderer.GetMSAAColorTarget()->GetNative() == msaaColor && renderer.GetMSAADepthTarget()->GetNative() == msaaDepth, "failed MSAA depth allocation preserves both old targets");
    }
    RENDER_CHECK(ReadTexture(*view.GetColorTexture()) == beforeFailure, "failed target set submits no invalid/mismatched rendering");
    draw();
    RENDER_CHECK(renderer.GetMSAAColorTarget()->GetDesc().SampleCount == 2 && renderer.GetMSAADepthTarget()->GetDesc().SampleCount == 2, "MSAA target set recovers together");
    RHIObjectPicker picker;
    RENDER_CHECK(picker.Init(device), "real picking pipeline");
    picker.Resize(32, 32);
    auto* oldPicking = picker.GetPickingTexture()->GetNative();
    auto* oldPickingDepth = picker.GetDepthTexture()->GetNative();
    {
        ScopedRHIFault fault([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "CreateTextureDSV") return nri::Result::FAILURE;
            return {};
        });
        picker.Resize(48, 48);
        RENDER_CHECK(picker.GetPickingTexture()->GetNative() == oldPicking && picker.GetDepthTexture()->GetNative() == oldPickingDepth, "picking pair replacement is atomic");
    }
    ui.Begin();
    ImGui::Image(static_cast<ImTextureID>(view.GetImTextureID()), ImVec2(64, 64));
    ui.End();
    device.WaitIdle();
    return true;
}

int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting();
    Log::Init();
    JobSystem::Init(2);
    RHIDeviceDesc desc;
    desc.PreferredBackend = argc > 1 && std::string(argv[1]) == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12;
    desc.AllowFallback = false; desc.EnableNRIValidation = true;
    desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(desc.PreferredBackend);
    auto& device = RHIDevice::Get();
    bool ok = device.Init(desc) && device.GetBackend() == desc.PreferredBackend && !device.IsSoftwareAdapter();
    try { if (ok) ok = argc > 2 && std::string(argv[2]) == "errors" ? UIErrors(desc.PreferredBackend) : Run(desc.PreferredBackend); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; ok = false; }
    RHIMeshRegistry::Get().Shutdown();
    device.Shutdown();
    JobSystem::Shutdown();
    std::cout << "RHIResourceView: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
