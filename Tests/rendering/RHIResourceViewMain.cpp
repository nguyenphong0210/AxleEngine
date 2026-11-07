#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"
#include <Axle/Log.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Window.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <Axle/RHIViewportDescriptor.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHIObjectPicker.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

using namespace Axle;
using namespace Axle::RHI;
using Axle::RenderTests::ReadTexture;

static std::vector<uint8_t> ReadPinnedImage(nri::Texture* image) {
    auto& device = RHIDevice::Get(); const auto& core = device.GetNRICore();
    const auto alignment = core.GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
    auto align = [](uint64_t value, uint64_t step) { return (value + step - 1) / step * step; };
    nri::TextureDataLayoutDesc layout{};
    layout.rowPitch = static_cast<uint32_t>(align(32, alignment.uploadBufferTextureRow));
    layout.slicePitch = align(layout.rowPitch * 8, alignment.uploadBufferTextureSlice);
    RHIBufferDesc desc; desc.Size = layout.slicePitch; desc.MemoryLocation = RHIMemoryLocation::HostReadback;
    RHIBuffer readback; RHICommandBuffer commands;
    if (!readback.Init(desc) || !commands.Init() || !commands.Begin() || !commands.KeepAlive(readback.AcquireLease())) return {};
    commands.TransitionTexture(image, RHIResourceLayout::ShaderResource, RHIResourceLayout::CopySource);
    nri::TextureRegionDesc region{}; region.width = region.height = 8; region.depth = 1;
    commands.ReadbackTextureToBuffer(*readback.GetNative(), layout, *image, region);
    commands.TransitionTexture(image, RHIResourceLayout::CopySource, RHIResourceLayout::ShaderResource);
    if (!commands.End() || !commands.Submit() || !device.WaitIdle()) return {};
    auto* data = static_cast<uint8_t*>(readback.Map()); if (!data) return {};
    std::vector<uint8_t> pixels(256);
    for (unsigned y = 0; y < 8; ++y) std::memcpy(pixels.data() + y * 32, data + y * layout.rowPitch, 32);
    readback.Unmap(); return pixels;
}

static bool RecordedUIOwner(RHIType backend) {
    auto& device = RHIDevice::Get();
    auto ui = std::make_unique<ImGuiLayer>(backend == RHIType::Vulkan ? ImGuiBackendType::Vulkan : ImGuiBackendType::Direct3D12);
    ui->Init(nullptr);
    ImGui::GetIO().DisplaySize = ImVec2(64, 64); ImGui::GetIO().DeltaTime = 1.0f / 60;
    RHITexture source, target;
    RENDER_CHECK(source.Init(RHITextureDesc::RenderTarget2D(1, 1)) && target.Init(RHITextureDesc::RenderTarget2D(64, 64)), "recorded UI real textures");
    const uint8_t red[] = {255, 0, 0, 255};
    nri::TextureSubresourceUploadDesc sub{red, 1, 4, 4};
    nri::TextureUploadDesc upload{}; upload.texture = source.GetNative(); upload.subresources = &sub;
    upload.after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::ALL};
    RENDER_CHECK(device.UploadData(&upload, 1, nullptr, 0) == nri::Result::SUCCESS, "recorded UI red pixels");
    const auto sourceHandle = source.GetHandle();
    auto token = ui->RegisterTexture(source);
    RENDER_CHECK(token.IsValid(), "recorded UI leased image");
    ui->Begin();
    ImGui::GetForegroundDrawList()->AddImage(static_cast<ImTextureID>(token.GetID()), ImVec2(0, 0), ImVec2(64, 64));
    ImGui::Render();
    RHICommandBuffer commands;
    RENDER_CHECK(commands.Init() && commands.Begin(), "native UI recording");
    commands.TransitionTexture(target.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
    nri::AttachmentDesc color{}; color.descriptor = target.GetRTV(); color.loadOp = nri::LoadOp::CLEAR; color.storeOp = nri::StoreOp::STORE;
    nri::RenderingDesc rendering{}; rendering.colors = &color; rendering.colorNum = 1;
    commands.BeginRendering(rendering);
    RENDER_CHECK(ui->Render(&commands), "native UI bridge records with a backend generation pin");
    RENDER_CHECK(!ui->Render(&commands), "another recording cannot overwrite native UI frame storage");
    commands.EndRendering();
    commands.TransitionTexture(target.GetNative(), RHIResourceLayout::ColorAttachment, RHIResourceLayout::ShaderResource);
    RENDER_CHECK(commands.End(), "finish native UI recording");
    token.Reset(); source.Shutdown(); ui.reset();
    RENDER_CHECK(!ImGui::GetCurrentContext() && device.WaitIdle() && device.GetResources().Acquire(sourceHandle).IsLive(),
        "destroyed UI retains fonts, vertex data, descriptor arena and images in the unsubmitted command");
    RENDER_CHECK(commands.Submit() && device.WaitIdle(), "recorded UI remains executable after its original layer is destroyed");
    const auto pixels = ReadTexture(target);
    const auto center = (32 * 64 + 32) * 4;
    RENDER_CHECK(pixels.size() == 64 * 64 * 4 && pixels[center] > 240 && pixels[center + 1] < 8 && pixels[center + 2] < 8,
        "actual recorded native UI still samples the original red pixels");
    RENDER_CHECK(!device.GetResources().Acquire(sourceHandle), "completed native UI releases the retained image");
    return true;
}

static bool GUILeases(RHIType backend) {
    auto& device = RHIDevice::Get();
    WindowProps props; props.Title = "OPT-04 queued GUI texture generations"; props.Width = props.Height = 96; props.Maximized = false;
    props.GraphicsBackend = backend == RHIType::Vulkan ? GraphicsAPI::Vulkan : GraphicsAPI::Direct3D12;
    Window window(props);
    for (bool closeBeforeSubmit : {false, true}) {
        auto ui = std::make_unique<ImGuiLayer>(backend == RHIType::Vulkan ? ImGuiBackendType::Vulkan : ImGuiBackendType::Direct3D12);
        ui->Init(window.GetNativeWindow());
        auto presentation = ui->CreateRenderContext(window);
        auto registrar = ui->GetTextureRegistrar();
        auto retire = ui->GetDeferredReleaser();
        RHITexture image;
        RENDER_CHECK(image.Init(RHITextureDesc::RenderTarget2D(8, 8)), "actual GUI image generation");
        std::vector<uint8_t> expected(256);
        for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>(29 + i * 11);
        nri::TextureSubresourceUploadDesc sub{expected.data(), 1, 32, 256};
        nri::TextureUploadDesc upload{}; upload.texture = image.GetNative(); upload.subresources = &sub;
        upload.after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::ALL};
        RENDER_CHECK(device.UploadData(&upload, 1, nullptr, 0) == nri::Result::SUCCESS, "native GUI source pixels");
        const auto handle = image.GetHandle(); auto* original = image.GetNative();
        auto token = ui->RegisterTexture(image);
        RENDER_CHECK(token.IsValid(), "leased GUI registration");
        ui->Begin();
        const auto id = token.GetID();
        ImGui::Begin("Queued image"); ImGui::Image(static_cast<ImTextureID>(id), ImVec2(64, 64)); ImGui::End();
        token.Reset();
        RENDER_CHECK(image.Init(RHITextureDesc::RenderTarget2D(8, 8)), "reload wrapper after image publication");
        RENDER_CHECK(device.WaitIdle() && device.GetResources().Acquire(handle).IsLive(), "queue drain cannot release an image still in a CPU draw list");
        RENDER_CHECK(ReadPinnedImage(original) == expected, "queued image retains all original GPU pixels after replacement and registration-owner destruction");
        int retired = 0;
        retire([retire, &retired] { ++retired; retire([&retired] { ++retired; }); });
        RENDER_CHECK(retired == 0, "retirement waits for the CPU frame boundary");
        if (closeBeforeSubmit) ui.reset();
        else { ui->End(); RENDER_CHECK(presentation->Present(window), "shared window UI submission"); ui.reset(); }
        RENDER_CHECK(device.WaitIdle() && retired == 2 && !device.GetResources().Acquire(handle), "submit or abandon retires nested resources independently of the dead UI owner");
        retire([&retired] { ++retired; });
        RENDER_CHECK(device.WaitIdle() && retired == 3, "saved retirement handle remains safe after UI destruction");
        RENDER_CHECK(!registrar(image).IsValid(), "saved registration factory rejects a destroyed owner");
    }
    return RecordedUIOwner(backend);
}

static bool EpochOwnership(RHIType backend) {
    auto& device = RHIDevice::Get();
    WindowProps props; props.Title = "OPT-04 device ownership"; props.Width = props.Height = 96; props.Maximized = false;
    props.GraphicsBackend = backend == RHIType::Vulkan ? GraphicsAPI::Vulkan : GraphicsAPI::Direct3D12;
    Window window(props);
    ImGuiLayer ui(backend == RHIType::Vulkan ? ImGuiBackendType::Vulkan : ImGuiBackendType::Direct3D12);
    ui.Init(window.GetNativeWindow()); auto presentation = ui.CreateRenderContext(window); ui.Begin(); ui.End();
    RENDER_CHECK(presentation->Present(window), "shared epoch window frame");
    auto oldMesh = RHIMeshRegistry::Get().GetCube();
    RENDER_CHECK(oldMesh && oldMesh->IsValid(), "old epoch owns a real shared builtin");
    const auto epoch = device.GetEpoch();
    RENDER_CHECK(device.Shutdown() && !ui.IsInitialized() && !ImGui::GetCurrentContext() && !oldMesh->IsValid(),
        "device closes the live native UI owner and all scene resources before native destruction");
    RHIDeviceDesc desc; desc.PreferredBackend = backend; desc.AllowFallback = false; desc.EnableNRIValidation = true;
    desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    RENDER_CHECK(device.Init(desc) && device.GetEpoch() != epoch, "new physical device epoch");
    auto freshMesh = RHIMeshRegistry::Get().GetCube();
    RENDER_CHECK(freshMesh && freshMesh->IsValid() && freshMesh != oldMesh, "registry replaces stale migration caches with current-epoch resources");
    oldMesh.reset();
    presentation.reset(); ui.Init(window.GetNativeWindow()); presentation = ui.CreateRenderContext(window); ui.Begin(); ui.End();
    RENDER_CHECK(presentation->Present(window), "shared new-epoch window frame"); ui.Shutdown();
    RENDER_CHECK(freshMesh->IsValid() && device.WaitIdle(), "stale mesh/UI teardown cannot damage the new device");
    return true;
}

static bool UIErrors(RHIType backend) {
    auto& device = RHIDevice::Get();
    WindowProps props; props.Title = "OPT-04 UI failure ownership"; props.Width = props.Height = 96; props.Maximized = false;
    props.GraphicsBackend = backend == RHIType::Vulkan ? GraphicsAPI::Vulkan : GraphicsAPI::Direct3D12;
    Window window(props);
    auto ui = std::make_unique<ImGuiLayer>(backend == RHIType::Vulkan ? ImGuiBackendType::Vulkan : ImGuiBackendType::Direct3D12);
    ui->Init(window.GetNativeWindow());
    auto presentation = ui->CreateRenderContext(window);
    ui->Begin();
    ImGui::Begin("Failure ownership fixture"); ImGui::TextUnformatted("Native font and vertex storage"); ImGui::End();
    ui->End();
    RENDER_CHECK(presentation->Present(window), "shared UI warm-up frame");
    bool reached = false;
    {
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "QueueSubmit") { reached = true; return nri::Result::FAILURE; }
            return {};
        });
        ui->Begin();
        ImGui::Begin("Failure ownership fixture"); ImGui::TextUnformatted("Pending submission"); ImGui::End();
        ui->End();
        RENDER_CHECK(!presentation->Present(window), "shared UI failure propagates");
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
    auto presentation = ui.CreateRenderContext(window);
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
    auto draw = [&] {
        if (!view.SetRenderOptions(settings.MSAASamples, settings.EnableToneMapping, settings.EnableFXAA)) return;
        renderer.RenderScene(scene, camera, view.GetColorTexture(), view.GetDepthTexture(), settings, nullptr, view.GetTargets());
    };
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
    RENDER_CHECK(presentation->Present(window), "shared scene and UI window presentation");
    device.WaitIdle();
    return true;
}

static bool TargetSets(RHIType backend) {
    auto& device = RHIDevice::Get();
    WindowProps props; props.Title = "OPT-04 per-view target ownership"; props.Width = props.Height = 96; props.Maximized = false;
    props.GraphicsBackend = backend == RHIType::Vulkan ? GraphicsAPI::Vulkan : GraphicsAPI::Direct3D12;
    Window window(props); ImGuiLayer ui(backend == RHIType::Vulkan ? ImGuiBackendType::Vulkan : ImGuiBackendType::Direct3D12);
    ui.Init(window.GetNativeWindow()); auto presentation = ui.CreateRenderContext(window);
    RHIViewportDescriptor first, second, external;
    RHIViewportDesc desc; desc.Width = desc.Height = 8; desc.ColorFormat = RHITextureFormat::RGBA8_UNORM;
    RENDER_CHECK(first.Init(&ui, desc) && first.SetRenderOptions(4, true, true, 2), "complete color/depth/MSAA/post-process/history generation");
    desc.Width = 24; desc.Height = 16;
    RENDER_CHECK(second.Init(&ui, desc) && second.SetRenderOptions(2, false, true, 1), "independent differently sized preview generation");
    RHISceneRenderer renderer;
    RENDER_CHECK(renderer.Init(device), "one renderer consumes independent view generations");
    auto scene = std::make_shared<Scene>();
    PerspectiveCamera camera(45, 1, 0.1f, 100);
    SceneRenderSettings render;
    render.EnableSkybox = render.EnableShadows = render.EnableStudioLightLobes = render.EnableParticles = false;
    render.MSAASamples = 2; render.EnableFXAA = true; render.ClearColor = {0.1f, 0.3f, 0.6f, 1};
    renderer.RenderScene(scene, camera, second.GetColorTexture(), second.GetDepthTexture(), render, nullptr, second.GetTargets());
    const auto secondPixels = ReadTexture(*second.GetColorTexture());
    RENDER_CHECK(!secondPixels.empty(), "second view produced actual GPU pixels");
    render.MSAASamples = 4; render.EnableToneMapping = true; render.ClearColor = {0.6f, 0.2f, 0.1f, 1};
    renderer.RenderScene(scene, camera, first.GetColorTexture(), first.GetDepthTexture(), render, nullptr, first.GetTargets());
    RENDER_CHECK(ReadTexture(*second.GetColorTexture()) == secondPixels, "rendering another sized HDR/MSAA view preserves the first preview's pixels");
    std::vector<RHIResourceHandle> handles;
    for (unsigned slot = 0; slot < static_cast<unsigned>(RHIViewTarget::Count); ++slot) {
        auto* texture = first.GetTargets()->Get(static_cast<RHIViewTarget>(slot));
        RENDER_CHECK(texture && texture->IsValid() && texture->GetWidth() == 8, "all required targets belong to the same view size");
        handles.push_back(texture->GetHandle());
    }
    RENDER_CHECK(second.GetTargets()->Get(RHIViewTarget::MSAAColor)->GetDesc().SampleCount == 2 &&
        second.GetTargets()->Get(RHIViewTarget::History0)->GetWidth() == 24 && !second.GetTargets()->Get(RHIViewTarget::History1), "views do not share MSAA/history storage");
    const auto gui = first.GetImTextureID();
    bool reached = false;
    {
        ScopedRHIFault fail([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "CreateTexture" && ScopedRHIOperation::Resource() == "ViewTarget_10") { reached = true; return nri::Result::OUT_OF_MEMORY; }
            return {};
        });
        RENDER_CHECK(!first.SetHDR(true) && reached && first.GetImTextureID() == gui && !first.IsHDR(), "late history allocation failure preserves the complete set and GUI binding");
    }
    for (unsigned slot = 0; slot < handles.size(); ++slot)
        RENDER_CHECK(first.GetTargets()->Get(static_cast<RHIViewTarget>(slot))->GetHandle() == handles[slot], "no partial target publication");
    RENDER_CHECK(!first.Resize(0, 0) && !first.SetRenderOptions(3, true, true, 2) && first.IsValid(), "zero size and unsupported samples preserve last good view");
    std::vector<uint8_t> red(256), blue(256);
    for (unsigned i = 0; i < 64; ++i) { red[i * 4] = red[i * 4 + 3] = 255; blue[i * 4 + 2] = blue[i * 4 + 3] = 255; }
    auto upload = [&](RHITexture& texture, const std::vector<uint8_t>& pixels) {
        nri::TextureSubresourceUploadDesc sub{pixels.data(), 1, 32, 256};
        nri::TextureUploadDesc data{}; data.texture = texture.GetNative(); data.subresources = &sub;
        data.after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::ALL};
        return device.UploadData(&data, 1, nullptr, 0) == nri::Result::SUCCESS;
    };
    RENDER_CHECK(upload(*first.GetColorTexture(), red), "queued view source pixels");
    auto source = std::make_unique<RHITexture>();
    RENDER_CHECK(source->Init(RHITextureDesc::RenderTarget2D(8, 8)) && upload(*source, blue) && external.BindExternalTexture(&ui, source.get()), "explicit external generation snapshot");
    const auto externalHandle = source->GetHandle(); auto* externalNative = source->GetNative();
    source.reset();
    RENDER_CHECK(external.GetColorTexture()->IsValid() && external.GetColorTexture()->GetNative() == externalNative && device.WaitIdle(), "external wrapper destruction cannot invalidate the view's retained generation");
    ui.Begin();
    ImGui::GetForegroundDrawList()->AddImage(static_cast<ImTextureID>(first.GetImTextureID()), ImVec2(0, 0), ImVec2(48, 48));
    ImGui::GetForegroundDrawList()->AddImage(static_cast<ImTextureID>(external.GetImTextureID()), ImVec2(48, 0), ImVec2(96, 48));
    auto* oldColor = first.GetColorTexture()->GetNative(); const auto oldHandle = first.GetColorTexture()->GetHandle();
    RENDER_CHECK(first.SetHDR(true) && first.SetRenderOptions(2, true, false) && first.GetDesc().HistoryCount == 2 && first.Resize(12, 12), "rapid HDR/MSAA/size changes publish complete candidates and retain history policy");
    first.Shutdown(); external.Shutdown();
    RENDER_CHECK(device.WaitIdle() && ReadPinnedImage(oldColor) == red && ReadPinnedImage(externalNative) == blue,
        "queued images retain original pixels across whole-view replacement/close and external-owner destruction");
    RENDER_CHECK(second.GetColorTexture()->GetWidth() == 24 && second.GetTargets()->Get(RHIViewTarget::MSAAColor)->GetDesc().SampleCount == 2,
        "closing another preview cannot resize this view");
    ui.End(); RENDER_CHECK(presentation->Present(window) && device.WaitIdle(), "shared presentation consumes retained view images");
    RENDER_CHECK(!device.GetResources().Acquire(oldHandle) && !device.GetResources().Acquire(externalHandle), "completed frame releases both retired view generations");
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
    try { if (ok) ok = argc > 2 && std::string(argv[2]) == "targets" ? TargetSets(desc.PreferredBackend) :
        argc > 2 && std::string(argv[2]) == "leases" ? GUILeases(desc.PreferredBackend) :
        argc > 2 && std::string(argv[2]) == "epoch" ? EpochOwnership(desc.PreferredBackend) :
        argc > 2 && std::string(argv[2]) == "errors" ? UIErrors(desc.PreferredBackend) : Run(desc.PreferredBackend); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; ok = false; }
    RHIMeshRegistry::Get().Shutdown();
    device.Shutdown();
    JobSystem::Shutdown();
    ok = Axle::RenderTests::Check(Axle::RHI::GetRHIValidationErrorCount() == 0,
        "native validation reported no errors, including teardown", __FILE__, __LINE__) && ok;
    std::cout << "RHIResourceView: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
