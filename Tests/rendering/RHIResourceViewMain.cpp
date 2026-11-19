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
#include <algorithm>

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
    RHIViewTargets targets;
    RHIViewTargetDesc targetDesc; targetDesc.Width = targetDesc.Height = 8;
    RENDER_CHECK(targets.Configure(targetDesc) && targets.Validate(), "old epoch owns a complete view target generation");
    const auto oldTargetGeneration = targets.GetGeneration();
    const auto epoch = device.GetEpoch();
    RENDER_CHECK(device.Shutdown() && !ui.IsInitialized() && !ImGui::GetCurrentContext() && !oldMesh->IsValid(),
        "device closes the live native UI owner and all scene resources before native destruction");
    RENDER_CHECK(!targets.IsValid(), "old view targets become invalid when their physical device closes");
    RHIDeviceDesc desc; desc.PreferredBackend = backend; desc.AllowFallback = false; desc.EnableNRIValidation = true;
    desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    RENDER_CHECK(device.Init(desc) && device.GetEpoch() != epoch, "new physical device epoch");
    RENDER_CHECK(targets.Configure(targetDesc) && targets.Validate() &&
        targets.GetGeneration() != oldTargetGeneration,
        "same view wrapper publishes a fresh target generation in the new device epoch");
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
        if (!view.SetRenderOptions(settings)) return false;
        renderer.RenderScene(scene, camera, view.GetColorTexture(), view.GetDepthTexture(), settings, nullptr, view.GetTargets());
        return renderer.WasLastRenderSuccessful();
    };
    RENDER_CHECK(draw(), "baseline viewport configuration and render succeed");
    const auto originalPixels = ReadTexture(*view.GetColorTexture());
    RENDER_CHECK(originalPixels.size() == 64 * 64 * 4 && originalPixels[(32 * 64 + 32) * 4] > 20 && renderer.GetDrawCallCount() > 0, "actual mesh produces non-background pixels");
    {
        RENDER_CHECK(device.WaitIdle(), "drain baseline view before descriptor immutability probe");
        RHIFrameContext descriptorFrame;
        RENDER_CHECK(descriptorFrame.Init() && descriptorFrame.Begin(), "descriptor immutability probe frame begins");
        descriptorFrame.GetCommands()->SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.GetPipelineLayout());
        renderer.GetDescriptorPool().Bind(descriptorFrame.GetCommands()->GetNative());
        RHIDescriptorPool::BindDescriptorSet(descriptorFrame.GetCommands()->GetNative(), 0, renderer.GetDefaultDescriptorSet());
        RENDER_CHECK(!renderer.GetDescriptorPool().UpdateTexture(renderer.GetDefaultDescriptorSet(), 0, 0,
            RHIMaterial::GetDefaultIrradianceCubemap()->GetSRV()), "bound descriptor generation rejects in-flight mutation");
        RENDER_CHECK(descriptorFrame.End() && descriptorFrame.Submit() && device.WaitIdle(), "descriptor probe frame retires cleanly");
    }
    // Exercise the Editor-owned target path with the M14 default feature mix;
    // implicit targets in the environment fixture cannot catch this boundary.
    settings.EnableToneMapping = settings.EnableFXAA = true;
    settings.EnableGTAO = settings.EnableContactShadows = settings.EnableSSR = settings.EnableBloom = true;
    RENDER_CHECK(draw(), "M14 Editor feature mix renders with explicit viewport-owned targets");
    const auto targets = view.GetTargets();
    RENDER_CHECK(targets->GetGTAO() && targets->GetGTAO()->GetWidth() == 32 &&
        targets->GetGTAOResolved() && targets->GetGTAOResolved()->GetWidth() == 64 &&
        targets->GetGTAOResolved()->GetHeight() == 64 &&
        targets->GetHiZ(7) && targets->GetBloom(5), "viewport publishes half-res GTAO, full-res resolved GTAO, and complete Hi-Z/bloom levels");
    RENDER_CHECK(!ReadTexture(*view.GetColorTexture()).empty(), "M14 explicit viewport output can be read back");
    settings.EnableAutoExposure = settings.EnableDDGI = true;
    RENDER_CHECK(draw() && view.GetTargets()->GetExposure() && view.GetTargets()->GetDDGI(),
        "optional targets match renderer requirements even without an authored DDGI grid");
    // M15 S1.1: scene luminance must reach tone mapping through the GPU exposure
    // target while the authored exposure remains fixed at 1.0.
    auto savedScene = scene;
    scene = std::make_shared<Scene>();
    settings.EnableGTAO = settings.EnableContactShadows = settings.EnableSSR = settings.EnableBloom = false;
    settings.EnableDDGI = settings.EnableFXAA = false;
    settings.AutoExposureSpeed = 1000.0f;
    settings.Exposure = 1.0f;
    settings.EnableAutoExposure = false;
    settings.ClearColor = {2.0f, 2.0f, 2.0f, 1.0f};
    RENDER_CHECK(draw(), "manual bright reference renders");
    const auto manualBright = ReadTexture(*view.GetColorTexture());
    settings.EnableAutoExposure = true;
    RENDER_CHECK(draw(), "automatic bright reference renders");
    const auto automaticBright = ReadTexture(*view.GetColorTexture());
    settings.EnableAutoExposure = false;
    settings.ClearColor = {0.02f, 0.02f, 0.02f, 1.0f};
    RENDER_CHECK(draw(), "manual dark reference renders");
    const auto manualDark = ReadTexture(*view.GetColorTexture());
    settings.EnableAutoExposure = true;
    RENDER_CHECK(draw(), "automatic dark reference renders");
    const auto automaticDark = ReadTexture(*view.GetColorTexture());
    RENDER_CHECK(manualBright.size() == 64 * 64 * 4 && automaticBright.size() == manualBright.size() &&
                 manualDark.size() == manualBright.size() && automaticDark.size() == manualBright.size() &&
                 automaticBright[0] + 20 < manualBright[0] &&
                 automaticDark[0] > manualDark[0] + 20,
        "GPU auto exposure darkens a bright scene and lifts a dark scene at fixed authored exposure");
    std::cout << "  auto_exposure_gpu bright_manual=" << int(manualBright[0])
              << " bright_auto=" << int(automaticBright[0])
              << " dark_manual=" << int(manualDark[0])
              << " dark_auto=" << int(automaticDark[0]) << '\n';
    scene = savedScene;
    settings.ClearColor = {0, 0, 0, 1};
    settings.EnableGTAO = settings.EnableContactShadows = settings.EnableSSR = settings.EnableBloom = false;
    settings.EnableAutoExposure = settings.EnableDDGI = false;
    settings.EnableToneMapping = settings.EnableFXAA = false;
    RENDER_CHECK(draw() && !view.GetTargets()->GetGTAO() && !view.GetTargets()->GetGTAOResolved() &&
        !view.GetTargets()->GetHiZ(0) && !view.GetTargets()->GetBloom(0),
        "disabling effects replaces the complete target generation and recovers baseline");
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

static bool StorageMipViews() {
    auto& device = RHIDevice::Get();
    // Per-mip storage views must also shrink the W slice range of a volume.
    {
        RHITextureDesc volumeDesc;
        volumeDesc.Width = volumeDesc.Height = volumeDesc.Depth = 8;
        volumeDesc.MipLevels = 4;
        volumeDesc.Format = RHITextureFormat::RGBA16_FLOAT;
        volumeDesc.Usage = RHITextureUsage::Storage;
        volumeDesc.DebugName = "MipStorageVolume";
        RHITexture volume;
        RENDER_CHECK(volume.Init(volumeDesc), "3D storage mip UAVs use valid shrinking slice ranges under native validation");
        for (uint32_t mip = 0; mip < volumeDesc.MipLevels; ++mip)
            RENDER_CHECK(volume.GetMipUAV(mip), "3D storage exposes a valid UAV for every mip down to depth one");
    }

    // M15-U5-16.1 / 16.2: Multi-mip cubemap storage allocation, per-mip SRV/UAV views, and fault rollback.
    {
        RHITexture cubeStorage;
        RENDER_CHECK(cubeStorage.Init(RHITextureDesc::CubemapStorage(32, 5, RHITextureFormat::RGBA16_FLOAT, "TestCubePrefilter")),
            "multi-mip HDR cubemap storage creation");
        RENDER_CHECK(cubeStorage.IsValid() && cubeStorage.GetMipCount() == 5 && cubeStorage.GetDesc().IsCubeMap &&
                     cubeStorage.GetDesc().ArrayLayers == 6 && cubeStorage.GetSRV() != nullptr && cubeStorage.GetUAV() != nullptr,
            "multi-mip cubemap exposes 5 mips, 6 cube layers, whole-cube SRV, and mip-0 UAV");
        for (uint32_t mip = 0; mip < 5; ++mip) {
            RENDER_CHECK(cubeStorage.GetMipSRV(mip) != nullptr && cubeStorage.GetMipUAV(mip) != nullptr,
                "every mip level exposes distinct SRV and storage array UAV descriptors");
        }
        RENDER_CHECK(cubeStorage.GetMipSRV(5) == nullptr && cubeStorage.GetMipUAV(5) == nullptr,
            "out-of-range mip views return null cleanly");
        RHITexture retained = cubeStorage.RetainGeneration();
        const auto cubeHandle = cubeStorage.GetHandle();
        cubeStorage.Shutdown();
        RENDER_CHECK(retained.IsValid() && retained.GetMipUAV(4) != nullptr && device.GetResources().Acquire(cubeHandle).IsLive(),
            "RetainGeneration preserves per-mip cubemap views independently");
        retained.Shutdown();
        RENDER_CHECK(!device.GetResources().Acquire(cubeHandle).IsLive(), "releasing retained multi-mip cubemap frees views and texture");

        uint32_t uavCount = 0;
        bool uavFaultHit = false;
        RHITexture failedCube;
        {
            ScopedRHIFault fault([&](std::string_view op) -> std::optional<nri::Result> {
                if (op == "CreateTextureUAV") {
                    ++uavCount;
                    if (uavCount == 3 && !uavFaultHit) {
                        uavFaultHit = true;
                        return nri::Result::OUT_OF_MEMORY;
                    }
                }
                return {};
            });
            RENDER_CHECK(!failedCube.Init(RHITextureDesc::CubemapStorage(32, 5, RHITextureFormat::RGBA16_FLOAT, "FailedCubePrefilter")) &&
                         uavFaultHit && !failedCube.IsValid(),
                "mid-mip UAV creation fault cleans up partial cubemap resources without leaks");
        }
    }

    // M15-U6-01.1 / 01.2 / 07.5: Native 3D froxel StorageTexture3D SRV+UAV, QueryMemoryRequirements,
    // tier scaling (Low..Ultra + 160x90x64 reference), resize, invalid input & fault isolation.
    {
        RHITexture volume3D;
        const auto volDesc = RHITextureDesc::StorageTexture3D(
            16u, 12u, 8u, RHITextureFormat::RGBA16_FLOAT, "TestFroxelStorage3D");
        RENDER_CHECK(volume3D.Init(volDesc) && volume3D.IsValid() &&
                     volume3D.GetWidth() == 16u && volume3D.GetHeight() == 12u && volume3D.GetDepth() == 8u &&
                     volume3D.GetSRV() != nullptr && volume3D.GetUAV() != nullptr,
            "M15-U6-01.1: StorageTexture3D allocates 3D RGBA16F volume with valid SRV and UAV");
        nri::MemoryDesc memDesc{};
        RENDER_CHECK(RHITexture::QueryMemoryRequirements(volDesc, memDesc) &&
                     memDesc.size >= 16ull * 12ull * 8ull * 8ull,
            "M15-U6-07.5: 3D texture memory query reports non-zero device allocation >= logical bytes");

        RHITexture retained3D = volume3D.RetainGeneration();
        const auto volHandle = volume3D.GetHandle();
        volume3D.Shutdown();
        RENDER_CHECK(retained3D.IsValid() && retained3D.GetSRV() && retained3D.GetUAV() &&
                     device.GetResources().Acquire(volHandle).IsLive(),
            "M15-U6-01.1: RetainGeneration preserves 3D SRV/UAV after wrapper shutdown");
        retained3D.Shutdown();
        RENDER_CHECK(!device.GetResources().Acquire(volHandle).IsLive(),
            "M15-U6-01.1: releasing retained 3D volume frees views and texture");

        RHIViewTargets froxelTargets;
        RHIViewTargetDesc vtDesc;
        vtDesc.Width = 1920u;
        vtDesc.Height = 1080u;
        vtDesc.EnableVolumetricFog = true;
        vtDesc.FroxelQuality = 2u; // High: 160x90x64
        RENDER_CHECK(froxelTargets.Configure(vtDesc) && froxelTargets.Validate(),
            "M15-U6-01.1 / 01.2: RHIViewTargets configures 1080p High froxel targets");
        for (const RHIViewTarget slot : {
                 RHIViewTarget::FroxelScattering, RHIViewTarget::FroxelIntegrated,
                 RHIViewTarget::FroxelHistory0, RHIViewTarget::FroxelHistory1 }) {
            auto* tex = froxelTargets.Get(slot);
            RENDER_CHECK(tex && tex->IsValid() &&
                         tex->GetWidth() == 160u && tex->GetHeight() == 90u && tex->GetDepth() == 64u &&
                         tex->GetFormat() == RHITextureFormat::RGBA16_FLOAT &&
                         tex->GetSRV() != nullptr && tex->GetUAV() != nullptr,
                "M15-U6-01.1 / 01.2: 1080p High tier allocates exact 160x90x64 3D RGBA16F volumes");
        }

        const auto genHigh = froxelTargets.GetGeneration();
        vtDesc.FroxelQuality = 0u; // Low: 80x45x32
        RENDER_CHECK(froxelTargets.Configure(vtDesc) && froxelTargets.Validate() &&
                     froxelTargets.GetGeneration() != genHigh &&
                     froxelTargets.GetFroxelScattering()->GetWidth() == 80u &&
                     froxelTargets.GetFroxelScattering()->GetHeight() == 45u &&
                     froxelTargets.GetFroxelScattering()->GetDepth() == 32u,
            "M15-U6-01.2: switching froxel quality tier publishes resized 80x45x32 generation");

        const auto genLow = froxelTargets.GetGeneration();
        auto badTier = vtDesc;
        badTier.FroxelQuality = 9u;
        auto badMsaa = vtDesc;
        badMsaa.Samples = 4u;
        RENDER_CHECK(!froxelTargets.Configure(badTier) && !froxelTargets.Configure(badMsaa) &&
                     froxelTargets.GetGeneration() == genLow && froxelTargets.Validate(),
            "M15-U6-01.1 / 01.2: invalid tier or MSAA froxel request preserves last-good generation");

        bool froxelOomHit = false;
        {
            ScopedRHIFault fault([&](std::string_view op) -> std::optional<nri::Result> {
                if (op == "CreateTexture" && ScopedRHIOperation::Resource() ==
                    "ViewTarget_" + std::to_string(static_cast<unsigned>(RHIViewTarget::FroxelHistory1))) {
                    froxelOomHit = true;
                    return nri::Result::OUT_OF_MEMORY;
                }
                return {};
            });
            auto resized = vtDesc;
            resized.Width = 1280u;
            resized.Height = 720u;
            resized.FroxelQuality = 2u;
            RENDER_CHECK(!froxelTargets.Configure(resized) && froxelOomHit &&
                         froxelTargets.GetGeneration() == genLow && froxelTargets.Validate() &&
                         froxelTargets.GetFroxelScattering()->GetWidth() == 80u,
                "M15-U6-01.1: OOM during froxel resize preserves prior validated 3D target generation");
        }

        vtDesc.EnableVolumetricFog = false;
        RENDER_CHECK(froxelTargets.Configure(vtDesc) && froxelTargets.Validate() &&
                     !froxelTargets.GetFroxelScattering() && !froxelTargets.GetFroxelIntegrated() &&
                     !froxelTargets.GetFroxelHistory(0) && !froxelTargets.GetFroxelHistory(1),
            "M15-U6-01.1: disabling volumetric fog allocates zero 3D froxel targets");
    }
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
    RENDER_CHECK(renderer.WasLastRenderSuccessful(), "second explicit view render succeeds");
    const auto secondPixels = ReadTexture(*second.GetColorTexture());
    RENDER_CHECK(!secondPixels.empty(), "second view produced actual GPU pixels");
    render.MSAASamples = 4; render.EnableToneMapping = true; render.ClearColor = {0.6f, 0.2f, 0.1f, 1};
    renderer.RenderScene(scene, camera, first.GetColorTexture(), first.GetDepthTexture(), render, nullptr, first.GetTargets());
    RENDER_CHECK(renderer.WasLastRenderSuccessful(), "first explicit HDR/MSAA view render succeeds");
    RENDER_CHECK(ReadTexture(*second.GetColorTexture()) == secondPixels, "rendering another sized HDR/MSAA view preserves the first preview's pixels");
    const auto beforeResize = first.GetTargets()->GetGeneration();
    RENDER_CHECK(first.Resize(12, 10) && first.GetTargets()->GetGeneration() != beforeResize &&
        first.GetTargets()->Validate() && first.GetTargets()->Get(RHIViewTarget::OpaqueColor)->GetWidth() == 12 &&
        first.GetTargets()->Get(RHIViewTarget::OpaqueColor)->GetHeight() == 10,
        "resize publishes a complete new per-view target generation");
    RENDER_CHECK(first.Resize(8, 8) && first.GetTargets()->Validate() &&
        first.GetTargets()->GetGeneration() != beforeResize &&
        ReadTexture(*second.GetColorTexture()) == secondPixels,
        "restoring size keeps independent view pixels and a valid generation");
    const std::vector<RHIViewTarget> required = {
        RHIViewTarget::Color, RHIViewTarget::Depth, RHIViewTarget::MSAAColor, RHIViewTarget::MSAADepth,
        RHIViewTarget::HDR, RHIViewTarget::FXAA, RHIViewTarget::OpaqueColor, RHIViewTarget::OpaqueDepth,
        RHIViewTarget::ParticleDepth, RHIViewTarget::History0, RHIViewTarget::History1
    };
    std::vector<RHIResourceHandle> handles;
    for (unsigned slot = 0; slot < static_cast<unsigned>(RHIViewTarget::Count); ++slot) {
        const auto target = static_cast<RHIViewTarget>(slot);
        auto* texture = first.GetTargets()->Get(target);
        const bool enabled = std::find(required.begin(), required.end(), target) != required.end();
        RENDER_CHECK(enabled ? texture && texture->IsValid() && texture->GetWidth() == 8 && texture->GetHeight() == 8 : !texture,
            "exact enabled target set belongs to the same view size; disabled effects allocate no targets");
        handles.push_back(texture ? texture->GetHandle() : RHIResourceHandle{});
    }
    RENDER_CHECK(second.GetTargets()->Get(RHIViewTarget::MSAAColor)->GetDesc().SampleCount == 2 &&
        second.GetTargets()->Get(RHIViewTarget::History0)->GetWidth() == 24 && !second.GetTargets()->Get(RHIViewTarget::History1), "views do not share MSAA/history storage");
    const auto gui = first.GetImTextureID();
    const auto firstGeneration = first.GetTargets()->GetGeneration();
    RENDER_CHECK(firstGeneration != 0 && first.GetTargets()->Validate(), "published target generation validates its complete resource contract");
    bool reached = false;
    {
        ScopedRHIFault fail([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "CreateTexture" && ScopedRHIOperation::Resource() ==
                "ViewTarget_" + std::to_string(static_cast<unsigned>(RHIViewTarget::History1))) { reached = true; return nri::Result::OUT_OF_MEMORY; }
            return {};
        });
        RENDER_CHECK(!first.SetHDR(true) && reached && first.GetImTextureID() == gui && !first.IsHDR(), "late history allocation failure preserves the complete set and GUI binding");
    }
    for (unsigned slot = 0; slot < handles.size(); ++slot) {
        auto* texture = first.GetTargets()->Get(static_cast<RHIViewTarget>(slot));
        RENDER_CHECK((texture ? texture->GetHandle() : RHIResourceHandle{}) == handles[slot], "no partial target publication");
    }
    RENDER_CHECK(first.GetTargets()->GetGeneration() == firstGeneration && first.GetTargets()->Validate(),
        "late allocation failure preserves the last-good generation identity and complete contract");
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
    const auto externalHandle = source->GetHandle(); auto externalLease = external.GetColorTexture()->AcquireLease();
    auto* externalNative = source->GetNative();
    source.reset();
    RENDER_CHECK(external.GetColorTexture()->IsValid() && external.GetColorTexture()->GetNative() == externalNative && device.WaitIdle(), "external wrapper destruction cannot invalidate the view's retained generation");
    ui.Begin();
    ImGui::GetForegroundDrawList()->AddImage(static_cast<ImTextureID>(first.GetImTextureID()), ImVec2(0, 0), ImVec2(48, 48));
    ImGui::GetForegroundDrawList()->AddImage(static_cast<ImTextureID>(external.GetImTextureID()), ImVec2(48, 0), ImVec2(96, 48));
    auto oldColorLease = first.GetColorTexture()->AcquireLease();
    auto* oldColor = first.GetColorTexture()->GetNative(); const auto oldHandle = first.GetColorTexture()->GetHandle();
    RENDER_CHECK(first.SetHDR(true) && first.SetRenderOptions(2, true, false) && first.GetDesc().HistoryCount == 2 && first.Resize(12, 12), "rapid HDR/MSAA/size changes publish complete candidates and retain history policy");
    RENDER_CHECK(first.GetTargets()->GetGeneration() != firstGeneration && first.GetTargets()->Validate(), "replacement publishes a new validated target generation");
    first.Shutdown(); external.Shutdown();
    RENDER_CHECK(device.WaitIdle() && ReadPinnedImage(oldColor) == red && ReadPinnedImage(externalNative) == blue,
        "queued images retain original pixels across whole-view replacement/close and external-owner destruction");
    oldColorLease = {}; externalLease = {};
    RENDER_CHECK(second.GetColorTexture()->GetWidth() == 24 && second.GetTargets()->Get(RHIViewTarget::MSAAColor)->GetDesc().SampleCount == 2,
        "closing another preview cannot resize this view");
    ui.End(); RENDER_CHECK(presentation->Present(window) && device.WaitIdle(), "shared presentation consumes retained view images");
    const bool oldLive = device.GetResources().Acquire(oldHandle).IsLive();
    const bool externalLive = device.GetResources().Acquire(externalHandle).IsLive();
    RENDER_CHECK(!oldLive, "completed frame releases the retired color generation");
    RENDER_CHECK(!externalLive, "completed frame releases the retired external generation");

    // M15-U6-01.1 / 01.2: RHIViewportDescriptor SetRenderOptions allocates and releases 3D froxel targets
    {
        SceneRenderSettings fogSettings;
        fogSettings.EnableVolumetricFog = true;
        fogSettings.FogQuality = FroxelFogQuality::Low;
        RENDER_CHECK(second.SetRenderOptions(fogSettings) &&
                     second.GetTargets()->GetFroxelScattering() &&
                     second.GetTargets()->GetFroxelIntegrated() &&
                     second.GetTargets()->GetFroxelHistory(0) &&
                     second.GetTargets()->GetFroxelHistory(1) &&
                     second.GetTargets()->GetFroxelScattering()->GetDepth() == 32u,
            "M15-U6-01.1 / 01.2: RHIViewportDescriptor SetRenderOptions allocates Low 3D froxel targets");

        fogSettings.FogQuality = FroxelFogQuality::High;
        RENDER_CHECK(second.SetRenderOptions(fogSettings) &&
                     second.GetTargets()->GetFroxelScattering()->GetDepth() == 64u,
            "M15-U6-01.2: RHIViewportDescriptor SetRenderOptions updates 3D froxel targets when tier changes");

        fogSettings.EnableVolumetricFog = false;
        RENDER_CHECK(second.SetRenderOptions(fogSettings) &&
                     !second.GetTargets()->GetFroxelScattering() &&
                     !second.GetTargets()->GetFroxelHistory(1),
            "M15-U6-01.1: RHIViewportDescriptor releases 3D froxel targets when volumetric fog is disabled");
    }

    // M15-U5-46.3: RHIViewTargets planar reflection color/depth lifecycle, resize & fault isolation
    {
        RHIViewTargets planarTargets;
        RHIViewTargetDesc planarDesc;
        planarDesc.Width = 32;
        planarDesc.Height = 20;
        planarDesc.EnableDepth = true;
        planarDesc.EnablePlanarReflection = true;
        planarDesc.PlanarResolutionScale = 0.5f;
        RENDER_CHECK(planarTargets.Configure(planarDesc) && planarTargets.Validate() &&
                     planarTargets.GetPlanarColor() && planarTargets.GetPlanarColor()->IsValid() &&
                     planarTargets.GetPlanarColor()->GetWidth() == 16 && planarTargets.GetPlanarColor()->GetHeight() == 10 &&
                     planarTargets.GetPlanarColor()->GetFormat() == RHITextureFormat::RGBA16_FLOAT &&
                     planarTargets.GetPlanarColor()->GetRTV() && planarTargets.GetPlanarColor()->GetSRV() &&
                     planarTargets.GetPlanarDepth() && planarTargets.GetPlanarDepth()->IsValid() &&
                     planarTargets.GetPlanarDepth()->GetWidth() == 16 && planarTargets.GetPlanarDepth()->GetHeight() == 10 &&
                     planarTargets.GetPlanarDepth()->GetDSV(),
            "M15-U5-46.3: RHIViewTargets allocates scaled PlanarColor (RGBA16F) and PlanarDepth (D32F)");

        const auto planarGenBeforeResize = planarTargets.GetGeneration();
        planarDesc.Width = 19;
        planarDesc.Height = 11;
        RENDER_CHECK(planarTargets.Configure(planarDesc) && planarTargets.Validate() &&
                     planarTargets.GetGeneration() != planarGenBeforeResize &&
                     planarTargets.GetPlanarColor()->GetWidth() == 10 && planarTargets.GetPlanarColor()->GetHeight() == 6 &&
                     planarTargets.GetPlanarDepth()->GetWidth() == 10 && planarTargets.GetPlanarDepth()->GetHeight() == 6,
            "M15-U5-46.3: resizing planar view targets scales dimensions cleanly and publishes new generation");

        const auto goodGen = planarTargets.GetGeneration();
        auto badScale = planarDesc;
        badScale.PlanarResolutionScale = 0.1f;
        RENDER_CHECK(!planarTargets.Configure(badScale) && planarTargets.GetGeneration() == goodGen && planarTargets.Validate(),
            "M15-U5-46.3: invalid planar scale preserves last-good generation");

        bool planarOomHit = false;
        {
            ScopedRHIFault fault([&](std::string_view op) -> std::optional<nri::Result> {
                if (op == "CreateTexture" && ScopedRHIOperation::Resource() ==
                    "ViewTarget_" + std::to_string(static_cast<unsigned>(RHIViewTarget::PlanarDepth))) {
                    planarOomHit = true;
                    return nri::Result::OUT_OF_MEMORY;
                }
                return {};
            });
            auto resized = planarDesc;
            resized.Width = 24;
            resized.Height = 16;
            RENDER_CHECK(!planarTargets.Configure(resized) && planarOomHit &&
                         planarTargets.GetGeneration() == goodGen && planarTargets.Validate() &&
                         planarTargets.GetPlanarColor()->GetWidth() == 10,
                "M15-U5-46.3: planar OOM fault preserves previous validated target generation");
        }
    }

    return StorageMipViews();
}

int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting();
    Log::Init();
    JobSystem::Init(2);
    RHIDeviceDesc desc;
    desc.PreferredBackend = argc > 1 && std::string(argv[1]) == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12;
    desc.AllowFallback = false; desc.EnableNRIValidation = true;
    desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(desc.PreferredBackend);
    const bool storageOnly = argc > 2 && std::string(argv[2]) == "storage";
    desc.Headless = storageOnly;
    auto& device = RHIDevice::Get();
    bool ok = device.Init(desc) && device.GetBackend() == desc.PreferredBackend && !device.IsSoftwareAdapter();
    try { if (ok) ok = storageOnly ? StorageMipViews() : argc > 2 && std::string(argv[2]) == "targets" ? TargetSets(desc.PreferredBackend) :
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
