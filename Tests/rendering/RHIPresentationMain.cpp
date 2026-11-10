#include "RenderTestCommon.hpp"
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIPresentationContext.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <GLFW/glfw3.h>
#include <thread>
#include <chrono>
using namespace Axle;
using namespace Axle::RHI;

static bool Native(RHIType backend) {
    auto& device = RHIDevice::Get();
    RHIDeviceDesc desc; desc.PreferredBackend = backend; desc.AllowFallback = false; desc.EnableNRIValidation = true;
    desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    RENDER_CHECK(device.Init(desc) && !device.IsSoftwareAdapter() && device.GetBackend() == backend, "physical window presenter");
    WindowProps props; props.Width = 160; props.Height = 120; props.Maximized = false;
    props.Title = "OPT-04 shared presentation without ImGui";
    props.GraphicsBackend = backend == RHIType::Vulkan ? GraphicsAPI::Vulkan : GraphicsAPI::Direct3D12;
    Window window(props); RHIPresentationContext present;
    RENDER_CHECK(present.Init(window.GetNativeWindow()), "one real NRI window owner");
    for (int i = 0; i < 4; ++i)
        RENDER_CHECK(present.RenderFrame(160, 120, true) == RHIPresentOutcome::Presented, "standalone clear and present without ImGui");
    unsigned creates = 0;
    {
        ScopedRHIFault observe([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "CreateSwapChain") ++creates; return {};
        });
        RENDER_CHECK(present.RenderFrame(160, 120, false) == RHIPresentOutcome::Presented && !present.IsVSync(), "VSync off reaches reconfigured native presentation");
        RENDER_CHECK(present.RenderFrame(160, 120, true) == RHIPresentOutcome::Presented && present.IsVSync() && creates == 2, "VSync on/off uses the same swapchain policy");
    }
    glfwSetWindowSize(window.GetNativeWindow(), 224, 144); window.PollEvents();
    RENDER_CHECK(present.RenderFrame(224, 144, false) == RHIPresentOutcome::Presented, "real window resize rebuilds all frame/view storage");
    glfwIconifyWindow(window.GetNativeWindow());
    for (unsigned i = 0; i < 30 && !glfwGetWindowAttrib(window.GetNativeWindow(), GLFW_ICONIFIED); ++i) {
        window.PollEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    RENDER_CHECK(glfwGetWindowAttrib(window.GetNativeWindow(), GLFW_ICONIFIED), "native window is actually minimized");
    const auto count = present.GetPresentCount();
    RENDER_CHECK(present.RenderFrame(0, 0, false) == RHIPresentOutcome::Skipped && present.GetPresentCount() == count, "minimized frames do not acquire or present");
    glfwRestoreWindow(window.GetNativeWindow()); window.PollEvents();
    RENDER_CHECK(present.RenderFrame(224, 144, false) == RHIPresentOutcome::Presented, "restore resumes native frames");
    {
        ScopedRHIFault stale([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "AcquireNextTexture") return nri::Result::OUT_OF_DATE; return {};
        });
        RENDER_CHECK(present.RenderFrame(224, 144, false) == RHIPresentOutcome::RebuildRequired, "out-of-date acquisition requests shared rebuild");
    }
    RENDER_CHECK(present.RenderFrame(224, 144, false) == RHIPresentOutcome::Presented, "acquire recovery retains one owner");
    RENDER_CHECK(present.RenderFrame(224, 144, false, {}, [](RHICommandBuffer&, nri::Format) { return false; }) == RHIPresentOutcome::Failed,
        "abandoned view/UI recording cannot submit or present");
    RENDER_CHECK(present.RenderFrame(224, 144, false) == RHIPresentOutcome::Presented, "abandoned acquire is rebuilt before reuse");
    {
        ScopedRHIFault stale([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "QueuePresent") return nri::Result::OUT_OF_DATE; return {};
        });
        RENDER_CHECK(present.RenderFrame(224, 144, false) == RHIPresentOutcome::RebuildRequired, "accepted submit retains completion after out-of-date present");
    }
    RENDER_CHECK(present.RenderFrame(224, 144, false) == RHIPresentOutcome::Presented, "present recovery rebuilds behind completion");
    const auto completed = present.GetPresentCount();
    {
        ScopedRHIFault failed([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "QueueSubmit") return nri::Result::FAILURE; return {};
        });
        RENDER_CHECK(present.RenderFrame(224, 144, false) == RHIPresentOutcome::Failed && present.GetPresentCount() == completed,
            "failed shared submission never reaches presentation");
    }
    RENDER_CHECK(present.Shutdown(), "checked presenter teardown before native window destruction");
    return device.Shutdown();
}
int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting(); Log::Init(); bool ok = false;
    try {
        ok = true;
        const unsigned rounds = argc > 2 && std::string(argv[2]) == "stress" ? 6 : 1;
        for (unsigned round = 0; round < rounds && ok; ++round)
            ok = Native(argc > 1 && std::string(argv[1]) == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12);
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    RHIDevice::Get().Shutdown();
    ok = Axle::RenderTests::Check(GetRHIValidationErrorCount() == 0, "native validation including teardown", __FILE__, __LINE__) && ok;
    std::cout << "Shared presentation: " << (ok ? "PASS" : "FAIL") << '\n'; return ok ? 0 : 1;
}
