#include "RenderTestCommon.hpp"
#include <Axle/PluginRuntime.hpp>
#include <Axle/RenderSnapshotQueue.hpp>
#include <Axle/UUID.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <array>
#include <atomic>
#include <cstring>
#include <chrono>
#include <future>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

using namespace Axle;
using namespace Axle::PluginRuntime;
using namespace std::chrono_literals;

bool RunRenderPluginStress(const std::filesystem::path& library, const std::string& config, RHI::RHIType backend) {
#if defined(_WIN32)
    using namespace RHI;
    auto& device = RHIDevice::Get();
    const bool gpu = backend != RHIType::None;
    if (gpu) {
        RHIDeviceDesc init; init.PreferredBackend = backend; init.AllowFallback = false; init.Headless = true;
        init.EnableNRIValidation = init.EnableValidation = true;
        RENDER_CHECK(RHIDevice::IsGraphicsAPIValidationAvailable(backend) && device.Init(init) && !device.IsSoftwareAdapter(), "physical DLL/GPU lifetime fixture");
    }
    const auto root = std::filesystem::temp_directory_path() / "AxleEngine" / ("RenderPluginStress-" + std::to_string(static_cast<uint64_t>(Axle::UUID{})));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path Root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(Root, ec); } } cleanup{root};
    Plugin::ArtifactIdentity identity;
    identity.PluginId = identity.ModuleName = "fixture.counter"; identity.Configuration = config;
    identity.SdkFingerprint = "fixture-sdk"; identity.ToolchainFingerprint = "fixture-toolchain";
    identity.Fingerprint = "render-lifetime-stress"; identity.SourceRoot = root;
    LoadRequest request{std::filesystem::absolute(library), root / "render.receipt", identity};
    std::string error;
    RENDER_CHECK(Plugin::WriteArtifactReceipt(identity, request.ModulePath, request.ReceiptPath, error), "validated real DLL receipt");
    RegistrationRegistry registry;
    for (unsigned cycle = 0; cycle < 32; ++cycle) {
        auto stopThread = std::make_shared<std::thread::id>();
        auto scopedRequest = request; scopedRequest.ServiceLifetime = stopThread;
        NativePlugin plugin; RenderSnapshotQueue queue(2);
        RENDER_CHECK(plugin.Load(scopedRequest, registry, error), "real DLL admission and registration");
        const auto native = GetModuleHandleW(request.ModulePath.c_str());
        const auto work = reinterpret_cast<void(*)(void*, void(*)(void*, unsigned))>(GetProcAddress(native, "ax_fixture_render_work"));
        const auto observe = reinterpret_cast<void(*)(std::thread::id*)>(GetProcAddress(native, "ax_fixture_observe_stop"));
        RENDER_CHECK(work && observe, "real plugin render-work and stop symbols");
        observe(stopThread.get());
        RHICommandBuffer commands; RHIBuffer upload, readback;
        RHICompletionToken completion; RHIResourceLease gpuCode;
        auto gpuCleanup = std::make_shared<std::atomic<unsigned>>(0);
        const std::array<uint32_t, 4> words{cycle, 0xC1106001, 0x12345678, 0xabcdef01};
        if (gpu) {
            RHIBufferDesc read; read.Size = sizeof(words); read.MemoryLocation = RHIMemoryLocation::HostReadback;
            RENDER_CHECK(upload.Init(RHIBufferDesc::Vertex(sizeof(words), RHIMemoryLocation::HostUpload)) && readback.Init(read), "actual DLL-owned GPU workload");
            auto* data = upload.Map(); RENDER_CHECK(data, "host upload write"); std::memcpy(data, words.data(), sizeof(words)); upload.Unmap();
            gpuCode = device.GetResources().Publish(RHIResourceKind::Memory, "DLL executable owner", {},
                [code = plugin.GetCodeLease(), gpuCleanup, work] {
                    work(gpuCleanup.get(), +[](void* counter, unsigned value) {
                        if (value == 0xC1106001u) ++*static_cast<std::atomic<unsigned>*>(counter);
                    });
                });
            RENDER_CHECK(commands.Init() && commands.Begin() && commands.KeepAlive(gpuCode), "real submission pins plugin cleanup code independently of CPU task");
            commands.CopyBuffer(*readback.GetNative(), 0, *upload.GetNative(), 0, sizeof(words));
            RENDER_CHECK(commands.End() && commands.Submit(), "GPU work accepted before logical module stop");
            completion = device.GetLastSubmittedToken(); gpuCode.Reset(); commands.Shutdown(); upload.Shutdown();
        }
        struct Context {
            RenderSnapshotQueue& Queue;
            std::shared_ptr<void> Code;
            std::promise<void> Entered, Release;
            std::shared_future<void> Open = Release.get_future().share();
            bool Accepted = false;
        } context{queue, plugin.GetCodeLease()};
        auto emit = +[](void* user, unsigned value) {
            auto& c = *static_cast<Context*>(user);
            RenderSnapshotTicket ticket; ticket.SimulationTick = value; ticket.ProtectedRequest = true; ticket.LeaseAnchor = c.Code;
            c.Accepted = c.Queue.Push(std::move(ticket)) == RenderSnapshotPushResult::Accepted;
            c.Entered.set_value(); c.Open.wait();
        };
        // Explicit admission retains code before the task can execute its DLL callback.
        auto task = std::async(std::launch::async, [&, code = plugin.GetCodeLease()]() mutable {
            work(&context, emit); code.reset(); NativePlugin::CollectRetiredModules();
        });
        struct OpenOnExit { std::promise<void>& Release; bool Open = false; ~OpenOnExit() { if (!Open) Release.set_value(); } } open{context.Release};
        const bool entered = context.Entered.get_future().wait_for(15s) == std::future_status::ready;
        if (!entered) { context.Release.set_value(); open.Open = true; task.wait(); }
        RENDER_CHECK(entered && context.Accepted && queue.GetStats().Depth == 1, "DLL callback really enqueues a protected render ticket while task is active");
        queue.Stop(); plugin.Stop(); context.Code.reset();
        const bool revoked = !plugin.IsActive() && registry.Size() == 0 && *stopThread == std::thread::id{};
        const bool drained = queue.Drain() == 1 && *stopThread == std::thread::id{} && GetModuleHandleW(request.ModulePath.c_str());
        context.Release.set_value(); open.Open = true;
        RENDER_CHECK(task.wait_for(15s) == std::future_status::ready, "admitted DLL task completes after queued work cancellation");
        task.get();
        RENDER_CHECK(revoked && drained && *stopThread == std::thread::id{}, "logical stop revokes registration but neither ticket drain nor worker release unloads active code");
        if (gpu) {
            NativePlugin::CollectRetiredModules();
            RENDER_CHECK(*stopThread == std::thread::id{} && gpuCleanup->load() == 0, "CPU task/ticket completion cannot release a GPU-pinned module");
            {
                ScopedRHIFault busy([](std::string_view op) -> std::optional<nri::Result> {
                    if (op == "WaitFence") return nri::Result::NOT_READY; return {};
                });
                RENDER_CHECK(!device.WaitForCompletion(completion, 0) && !readback.Map() && gpuCleanup->load() == 0,
                    "unobserved GPU completion retains module and mapped storage guard");
            }
            RENDER_CHECK(device.WaitForCompletion(completion) && gpuCleanup->load() == 1, "checked GPU completion invokes retained DLL cleanup exactly once");
            const auto* data = readback.Map(); const bool exact = data && std::memcmp(data, words.data(), sizeof(words)) == 0;
            readback.Unmap(); RENDER_CHECK(exact, "actual DLL workload GPU bytes remain correct through logical stop");
        }
        NativePlugin::CollectRetiredModules();
        RENDER_CHECK(*stopThread == std::this_thread::get_id() && !GetModuleHandleW(request.ModulePath.c_str()),
            "final Stop and actual FreeLibrary happen on owner only after task and ticket leases retire");
    }
    return !gpu || device.Shutdown();
#else
    std::cerr << "Real DLL unload stress requires Windows in this fixture\n";
    return false;
#endif
}
