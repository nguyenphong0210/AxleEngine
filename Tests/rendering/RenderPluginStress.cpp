#include "RenderTestCommon.hpp"
#include <Axle/PluginRuntime.hpp>
#include <Axle/RenderSnapshotQueue.hpp>
#include <Axle/UUID.hpp>
#include <chrono>
#include <future>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

using namespace Axle;
using namespace Axle::PluginRuntime;
using namespace std::chrono_literals;

bool RunRenderPluginStress(const std::filesystem::path& library, const std::string& config) {
#if defined(_WIN32)
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
        NativePlugin plugin; RenderSnapshotQueue queue(2);
        RENDER_CHECK(plugin.Load(request, registry, error), "real DLL admission and registration");
        const auto native = GetModuleHandleW(request.ModulePath.c_str());
        const auto work = reinterpret_cast<void(*)(void*, void(*)(void*, unsigned))>(GetProcAddress(native, "ax_fixture_render_work"));
        const auto observe = reinterpret_cast<void(*)(std::thread::id*)>(GetProcAddress(native, "ax_fixture_observe_stop"));
        RENDER_CHECK(work && observe, "real plugin render-work and stop symbols");
        std::thread::id stopThread; observe(&stopThread);
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
        const bool entered = context.Entered.get_future().wait_for(15s) == std::future_status::ready;
        if (!entered) { context.Release.set_value(); task.wait(); }
        RENDER_CHECK(entered && context.Accepted && queue.GetStats().Depth == 1, "DLL callback really enqueues a protected render ticket while task is active");
        queue.Stop(); plugin.Stop(); context.Code.reset();
        const bool revoked = !plugin.IsActive() && registry.Size() == 0 && stopThread == std::thread::id{};
        const bool drained = queue.Drain() == 1 && stopThread == std::thread::id{} && GetModuleHandleW(request.ModulePath.c_str());
        context.Release.set_value();
        RENDER_CHECK(task.wait_for(15s) == std::future_status::ready, "admitted DLL task completes after queued work cancellation");
        task.get();
        RENDER_CHECK(revoked && drained && stopThread == std::thread::id{}, "logical stop revokes registration but neither ticket drain nor worker release unloads active code");
        NativePlugin::CollectRetiredModules();
        RENDER_CHECK(stopThread == std::this_thread::get_id() && !GetModuleHandleW(request.ModulePath.c_str()),
            "final Stop and actual FreeLibrary happen on owner only after task and ticket leases retire");
    }
    return true;
#else
    std::cerr << "Real DLL unload stress requires Windows in this fixture\n";
    return false;
#endif
}
