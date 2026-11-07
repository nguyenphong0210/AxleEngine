#include "RenderTestCommon.hpp"
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHISwitcher.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/Window.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/Log.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <array>
#include <stdexcept>

using namespace Axle;
using namespace Axle::RHI;

static bool NativeReadiness(RHIType backend) {
    auto& device = RHIDevice::Get();
    RHIDeviceDesc desc;
    desc.PreferredBackend = backend;
    desc.AllowFallback = false;
    desc.EnableNRIValidation = true;
    desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    if (!desc.EnableValidation) {
        std::cout << "Graphics API validation: NOT RUN (layer unavailable); embedded NRI validation remains enabled\n";
        auto requiredValidation = desc;
        requiredValidation.EnableValidation = true;
        RENDER_CHECK(!device.Init(requiredValidation) && !device.IsBackendSelected() && !device.GetNRIDevice(), "missing requested validation rejects startup instead of silently downgrading");
    }
    RENDER_CHECK(device.Init(desc), "physical requested native backend initializes");
    RENDER_CHECK(device.IsNativeReady() && device.GetBackend() == backend && !device.IsSoftwareAdapter(), "native test never accepts fallback/software");
    RENDER_CHECK(!device.IsContextReady(), "native selection alone does not certify product context");
    device.Shutdown();
    for (const auto* boundary : {"CoreInterface", "HelperInterface", "StreamerInterface", "SwapChainInterface", "GetGraphicsQueue"}) {
        bool reached = false;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == boundary) { reached = true; return nri::Result::FAILURE; }
            return {};
        });
        RENDER_CHECK(!device.Init(desc), "required operation rejection fails strict startup");
        RENDER_CHECK(reached, "failure injection reached actual operation boundary after native creation");
        RENDER_CHECK(!device.IsBackendSelected() && !device.IsNativeReady() && !device.GetNRIDevice() && !device.GetGraphicsQueue(), "rejected candidate publishes nothing");
        RENDER_CHECK(!device.GetNRICore().CreateTexture, "no rejected interface table survives");
    }
    RENDER_CHECK(device.Init(desc), "startup recovers after all partial candidate rollbacks");
    device.Shutdown();
    return true;
}

static bool GLFrame(bool fallback, std::array<unsigned char, 64 * 64 * 4>& pixels) {
    ScopedRHIFault fault([](std::string_view operation) -> std::optional<nri::Result> {
        if (operation == "CreateDevice") return nri::Result::FAILURE;
        return {};
    });
    RHIBackendConfig config;
    config.PreferredBackend = fallback ? RHIType::Direct3D12 : RHIType::OpenGL;
    std::unique_ptr<Window> window;
    RENDER_CHECK(RHISwitcher::InitializeProduct(config, [&](GraphicsAPI api) {
        WindowProps props;
        props.Title = "OPT-04 GL equivalence probe";
        props.Width = 64; props.Height = 64; props.Maximized = false;
        props.GraphicsBackend = api;
        window = std::make_unique<Window>(props);
        return window->GetNativeWindow() && RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(glfwGetProcAddress));
    }), "complete GL product initializes");
    auto& device = RHIDevice::Get();
    RENDER_CHECK(device.IsBackendSelected() && device.IsContextReady() && !device.IsNativeReady(), "GL readiness is explicit and contains no native NRI device");
    {
        RHITexture nativeTexture;
        RHIBuffer nativeBuffer;
        RENDER_CHECK(!nativeTexture.Init({}) && !nativeTexture.IsValid(), "GL rejects native texture use safely");
        RENDER_CHECK(!nativeBuffer.Init({}) && !nativeBuffer.IsValid(), "GL rejects native buffer use safely");
        auto target = Framebuffer::Create({64, 64, 1, false});
        auto mesh = PrimitiveMesh::CreateCube();
        RENDER_CHECK(target && mesh, "real GL mesh and framebuffer exist");
        Shader shader("#version 450 core\nlayout(location=0) in vec3 p; void main(){gl_Position=vec4(p,1.0);}",
            "#version 450 core\nlayout(location=0) out vec4 c; void main(){c=vec4(0.75,0.25,0.125,1.0);}");
        RENDER_CHECK(shader.GetRendererID() != 0, "GL fixture shader compiled");
        target->Bind();
        RenderCommand::SetViewport(0, 0, 64, 64);
        RenderCommand::SetClearColor({0, 0, 0, 1});
        RenderCommand::Clear();
        shader.Bind(); RenderCommand::DrawIndexed(mesh);
        glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        RENDER_CHECK(glGetError() == GL_NO_ERROR, "GL rendering and readback are valid");
        RENDER_CHECK(pixels[(32 * 64 + 32) * 4] > 180 && pixels[0] == 0, "mesh pixels differ from clear background");
        target->Unbind();
    }
    device.Shutdown();
    window.reset();
    return true;
}

static bool VulkanWindowFailures() {
    auto& device = RHIDevice::Get();
    RHIDeviceDesc desc;
    desc.PreferredBackend = RHIType::Vulkan; desc.AllowFallback = false;
    desc.EnableNRIValidation = true;
    desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(RHIType::Vulkan);
    RENDER_CHECK(device.Init(desc) && !device.IsSoftwareAdapter(), "physical Vulkan device for real window failure probes");
    WindowProps props; props.Title = "OPT-04 Vulkan partial construction";
    props.Width = 96; props.Height = 64; props.Maximized = false; props.GraphicsBackend = GraphicsAPI::Vulkan;
    {
        Window window(props);
        RENDER_CHECK(window.GetNativeWindow(), "real native window exists before injected failure");
        for (const auto* boundary : {"CreateGuiDescriptorPool", "CreateSwapChain", "CreatePresentView",
            "CreateCommandAllocator", "CreateCommandBuffer", "CreateDescriptorPool", "CreateBuffer", "CreateBufferCBV"}) {
            bool reached = false, rejected = false;
            ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == boundary) { reached = true; return nri::Result::FAILURE; }
                return {};
            });
            ImGuiLayer ui(ImGuiBackendType::Vulkan);
            try {
                ui.Init(window.GetNativeWindow());
                auto presentation = ui.CreateRenderContext(window);
            } catch (const std::exception&) { rejected = true; ui.Shutdown(); }
            RENDER_CHECK(reached && rejected && !ui.IsInitialized(), "partial native UI/presentation composition rejects and rolls back");
            ui.Shutdown();
        }
        ImGuiLayer ui(ImGuiBackendType::Vulkan);
        ui.Init(window.GetNativeWindow());
        RENDER_CHECK(ui.IsInitialized(), "native UI recovers after every partial construction stage");
        auto presentation = ui.CreateRenderContext(window);
        ui.Begin(); ui.End();
        RENDER_CHECK(presentation->Present(window), "shared native UI recovers and presents after partial construction failures");
        presentation->Shutdown(); ui.Shutdown();
    }
    device.Shutdown();
    return true;
}

static bool Policy() {
    auto& device = RHIDevice::Get();
    ScopedRHIFault fault([](std::string_view operation) -> std::optional<nri::Result> {
        if (operation == "CreateDevice") return nri::Result::FAILURE;
        return {};
    });
    RHIBackendConfig config;
    config.AllowFallback = false;
    unsigned products = 0;
    RENDER_CHECK(!RHISwitcher::InitializeProduct(config, [&](GraphicsAPI) { ++products; return true; }), "strict failure cannot invoke GL product");
    RENDER_CHECK(products == 0 && !device.IsBackendSelected(), "strict failure has no selected backend");
    config.AllowFallback = true;
    RENDER_CHECK(RHISwitcher::InitializeProduct(config, [&](GraphicsAPI api) { ++products; return api == GraphicsAPI::OpenGL; }), "fallback chooses one complete GL candidate");
    RENDER_CHECK(products == 1 && !device.IsNativeReady(), "fallback does not expose native readiness");
    device.Shutdown();
    config.PreferredBackend = RHIType::OpenGL;
    RENDER_CHECK(!RHISwitcher::InitializeProduct(config, [](GraphicsAPI) { return false; }), "product bridge failure rejects GL selection");
    RENDER_CHECK(!device.IsBackendSelected() && !device.IsContextReady(), "product failure rolls device selection back");
    const char* argv[] = {"test", "--strict-rhi", "--rhi=auto"};
    RENDER_CHECK(!RHISwitcher::ParseCommandLine(3, const_cast<char**>(argv)).AllowFallback, "strict flag wins in either CLI order");
    return true;
}

int main(int argc, char** argv) {
    Log::Init();
    bool ok = false;
    try {
        const std::string mode = argc > 1 ? argv[1] : "policy";
        if (mode == "policy") ok = Policy();
        else if (mode == "vulkan-window") ok = VulkanWindowFailures();
        else if (mode == "gl") {
            std::array<unsigned char, 64 * 64 * 4> explicitPixels{}, fallbackPixels{};
            ok = GLFrame(false, explicitPixels) && GLFrame(true, fallbackPixels) && explicitPixels == fallbackPixels;
        } else ok = NativeReadiness(mode == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    RHIDevice::Get().Shutdown();
    ok = Axle::RenderTests::Check(Axle::RHI::GetRHIValidationErrorCount() == 0,
        "native validation reported no errors, including teardown", __FILE__, __LINE__) && ok;
    std::cout << "RHIFoundation: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
