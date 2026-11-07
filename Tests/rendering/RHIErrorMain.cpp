#include "RenderTestCommon.hpp"
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHISwapChain.hpp>
#include <Axle/Window.hpp>
#include <Axle/RHI/RHISampler.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHIStreamer.hpp>
#include <chrono>

using namespace Axle;
using namespace Axle::RHI;

static bool Policy() {
    auto& device = RHIDevice::Get();
    RENDER_CHECK(!device.ObserveResult(nri::Result::OUT_OF_MEMORY, "CreateTexture", "original"), "failure is returned to the caller");
    RENDER_CHECK(!device.ObserveResult(nri::Result::FAILURE, "CreateSampler", "secondary"), "secondary failure remains failure");
    const auto& error = device.GetFirstError();
    RENDER_CHECK(error && error->NativeResult == nri::Result::OUT_OF_MEMORY && error->Operation == "CreateTexture" && error->Resource == "original", "first structured error is preserved");
    return true;
}

static bool Start(RHIType backend) {
    RHIDeviceDesc desc;
    desc.PreferredBackend = backend; desc.AllowFallback = false; desc.Headless = true; desc.EnableNRIValidation = true;
    desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    return RHIDevice::Get().Init(desc) && !RHIDevice::Get().IsSoftwareAdapter() && RHIDevice::Get().GetBackend() == backend;
}

static bool Native(RHIType backend) {
    auto& device = RHIDevice::Get();
    RENDER_CHECK(Start(backend), "strict physical backend");
    {
        RHITexture texture;
        auto desc = RHITextureDesc::RenderTarget2D(8, 8); desc.DebugName = "failure-name";
        ScopedRHIFault fault([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "AllocateTextureMemory") return nri::Result::OUT_OF_MEMORY;
            return {};
        });
        RENDER_CHECK(!texture.Init(desc) && !texture.IsValid(), "real wrapper then allocation rejection");
        const auto& error = device.GetFirstError();
        RENDER_CHECK(error && error->Backend == backend && error->Operation == "AllocateTextureMemory" && error->Resource == "failure-name", "native failure has backend/operation/resource");
        RENDER_CHECK(device.CanSubmit() && device.CanCreateResources(), "recoverable creation failure keeps the device usable");
    }
    RENDER_CHECK(device.Shutdown() && Start(backend), "fresh epoch for submit policy");
    {
        RHICommandBuffer commands;
        RENDER_CHECK(commands.Init(), "real recording recovery object");
        for (const auto* boundary : {"BeginCommandBuffer", "EndCommandBuffer"}) {
            if (std::string(boundary) == "EndCommandBuffer") RENDER_CHECK(commands.Begin(), "begin before injected end failure");
            bool reached = false;
            {
                ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                    if (operation == boundary) { reached = true; return nri::Result::FAILURE; }
                    return {};
                });
                const bool result = std::string(boundary) == "BeginCommandBuffer" ? commands.Begin() : commands.End();
                RENDER_CHECK(!result && reached && !commands.IsExecutable() && !commands.Submit(), "failed recording never becomes executable");
            }
            RENDER_CHECK(commands.Reset(), "failed/abandoned native recording is safely recreated");
        }
    }
    RENDER_CHECK(device.Shutdown() && Start(backend), "fresh epoch after recording recovery");
    {
        RHITexture texture;
        RHICommandBuffer commands;
        RENDER_CHECK(texture.Init(RHITextureDesc::RenderTarget2D(8, 8)) && commands.Init() && commands.Begin(), "actual resources and recording");
        RENDER_CHECK(!commands.Submit(), "unfinished command buffer cannot submit");
        RENDER_CHECK(!RHICommandBuffer::SubmitBatch(std::vector<RHICommandBuffer*>{&commands, nullptr}), "batch rejects invalid members instead of silently dropping them");
        RENDER_CHECK(commands.End(), "actual command buffer ends");
        {
            ScopedRHIFault fault([](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == "QueueSubmit") return nri::Result::FAILURE;
                return {};
            });
            RENDER_CHECK(!commands.Submit(), "failed submit propagates");
        }
        RENDER_CHECK(device.GetState() == RHIDeviceState::Stopping && !device.CanSubmit(), "ambiguous submit stops admission");
        unsigned retired = 0;
        texture.Shutdown();
        device.ReleaseNative([&] { ++retired; });
        RENDER_CHECK(retired == 0, "ambiguous submit retains cleanup owners");
        RENDER_CHECK(device.WaitIdle() && retired == 1, "checked native queue drain resolves retained owners");
        RENDER_CHECK(!commands.Submit() && !commands.Reset(), "draining does not silently resume a stopped device");
        const auto& error = device.GetFirstError();
        RENDER_CHECK(error && error->Operation == "QueueSubmit" && error->SubmissionAmbiguous, "submit ambiguity is explicit");
    }
    RENDER_CHECK(device.Shutdown() && Start(backend), "fresh epoch for timeout policy");
    {
        nri::Fence* fence = nullptr;
        const auto core = device.GetNRICore();
        RENDER_CHECK(core.CreateFence(*device.GetNRIDevice(), 0, fence) == nri::Result::SUCCESS, "real unsignaled fence");
        const auto start = std::chrono::steady_clock::now();
        RENDER_CHECK(!device.WaitForFence(fence, 1, 10), "unsignaled value reaches a bounded deadline");
        const auto elapsed = std::chrono::steady_clock::now() - start;
        RENDER_CHECK(elapsed < std::chrono::seconds(1) && device.GetFirstError() && device.GetFirstError()->TimedOut && !device.CanSubmit(), "timeout is structured and blocks reuse");
        core.DestroyFence(fence);
        {
            ScopedRHIFault fault([](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == "QueueDrain") return nri::Result::FAILURE;
                return {};
            });
            RENDER_CHECK(!device.Shutdown() && device.GetNRIDevice(), "unresolved cleanup retains the device instead of waiting forever or destroying owners");
        }
    }
    RENDER_CHECK(device.Shutdown() && Start(backend), "checked retry completes shutdown");
    {
        RHICommandBuffer commands;
        RENDER_CHECK(commands.Init() && commands.Begin() && commands.End(), "record for injected loss without resetting the GPU");
        ScopedRHIFault fault([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "QueueSubmit") return nri::Result::DEVICE_LOST;
            return {};
        });
        RENDER_CHECK(!commands.Submit() && device.GetState() == RHIDeviceState::Lost && !device.CanSubmit(), "loss blocks all further submissions");
        RHITexture rejected;
        RENDER_CHECK(!rejected.Init(RHITextureDesc::RenderTarget2D(8, 8)) && !rejected.IsValid(), "lost device rejects new resources");
        RHISampler sampler; RHIDescriptorPool pool; RHIStreamer streamer;
        RENDER_CHECK(!sampler.Init() && !pool.Init({}) && !streamer.Init(), "loss rejects all native resource helper creation");
        RENDER_CHECK(device.GetBackend() == backend && device.GetFirstError()->NativeResult == nri::Result::DEVICE_LOST, "loss does not silently fall back or overwrite its first error");
    }
    RENDER_CHECK(device.Shutdown(), "lost-device cleanup has no unsignaled fence wait");
    return true;
}

static bool Windowed(RHIType backend) {
    auto& device = RHIDevice::Get();
    for (const auto* rejection : {"none", "QueueSubmit", "QueuePresent"}) {
        RHIDeviceDesc desc; desc.PreferredBackend = backend; desc.AllowFallback = false; desc.EnableNRIValidation = true;
        RENDER_CHECK(device.Init(desc) && !device.IsSoftwareAdapter(), "physical windowed device");
        {
            WindowProps props; props.Title = "OPT-04 checked submit/present"; props.Width = props.Height = 96; props.Maximized = false;
            props.GraphicsBackend = backend == RHIType::Vulkan ? GraphicsAPI::Vulkan : GraphicsAPI::Direct3D12;
            Window window(props);
            RHISurface surface;
            RHISwapChain swapchain;
            RENDER_CHECK(surface.Init(window) && swapchain.Init(surface) && swapchain.BeginFrame(), "actual native surface and image acquisition");
            {
                RHICommandBuffer commands;
                RENDER_CHECK(commands.Init(swapchain.GetCurrentCommandAllocator()) && commands.Begin(), "surface-owned command allocator");
                auto* backbuffer = swapchain.GetCurrentTexture();
                nri::TextureViewDesc view{}; view.texture = backbuffer;
                view.type = nri::TextureView::COLOR_ATTACHMENT;
                view.format = device.GetNRICore().GetTextureDesc(*view.texture).format;
                nri::Descriptor* rtv = nullptr;
                RENDER_CHECK(device.GetNRICore().CreateTextureView(view, rtv) == nri::Result::SUCCESS, "real backbuffer view");
                commands.TransitionTexture(backbuffer, RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
                nri::AttachmentDesc color{}; color.descriptor = rtv; color.loadOp = nri::LoadOp::CLEAR; color.storeOp = nri::StoreOp::STORE;
                color.clearValue.color.f = {0.1f, 0.5f, 0.2f, 1};
                nri::RenderingDesc rendering{}; rendering.colors = &color; rendering.colorNum = 1;
                commands.BeginRendering(rendering); commands.EndRendering();
                commands.TransitionTexture(backbuffer, RHIResourceLayout::ColorAttachment, RHIResourceLayout::Present);
                RENDER_CHECK(commands.End(), "backbuffer recording ends");
                bool reached = false, presented = false;
                {
                    ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                        if (operation == "QueuePresent") presented = true;
                        if (operation == rejection) { reached = true; return operation == "QueuePresent" ? nri::Result::OUT_OF_DATE : nri::Result::FAILURE; }
                        return {};
                    });
                    const nri::CommandBuffer* native = commands.GetNative();
                    const bool success = swapchain.PresentFrame(&native, 1);
                    if (std::string(rejection) == "none") RENDER_CHECK(success && !reached && swapchain.GetFrameCount() == 1, "real native clear/submit/present succeeds");
                    else RENDER_CHECK(!success && reached, "requested surface failure propagates");
                }
                if (std::string(rejection) == "QueueSubmit") {
                    RENDER_CHECK(!presented && swapchain.GetFrameCount() == 0 && !device.CanSubmit(), "failed submit never presents or publishes an unsignaled completion value");
                } else {
                    RENDER_CHECK(swapchain.GetFrameCount() == 1 && device.WaitForFence(swapchain.GetFrameFence(), 1), "successful submission retains its actual completion even when presentation rejects");
                }
                RENDER_CHECK(device.WaitIdle(), "bounded checked drain before native view teardown");
                device.GetNRICore().DestroyDescriptor(rtv);
            }
            if (std::string(rejection) == "QueuePresent") {
                RENDER_CHECK(swapchain.NeedsResize() && device.GetState() == RHIDeviceState::SurfaceRebuildRequired, "out-of-date is a surface rebuild state");
                RENDER_CHECK(swapchain.Resize(96, 96) && device.CanSubmit(), "checked surface recreation restores admission");
            }
        }
        RENDER_CHECK(device.Shutdown(), "native surface shutdown is bounded after all paths");
    }
    return true;
}

int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting();
    Log::Init();
    bool ok = Policy();
    const std::string mode = argc > 1 ? argv[1] : "policy";
    if (ok && mode != "policy") {
        const auto backend = mode.find("vulkan") != std::string::npos ? RHIType::Vulkan : RHIType::Direct3D12;
        ok = mode.find("window") != std::string::npos ? Windowed(backend) : Native(backend);
    }
    RHIDevice::Get().Shutdown();
    ok = Axle::RenderTests::Check(Axle::RHI::GetRHIValidationErrorCount() == 0,
        "native validation reported no errors, including teardown", __FILE__, __LINE__) && ok;
    std::cout << "RHIError: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
