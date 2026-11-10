#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"
#include <Axle/RenderWorker.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <glm/gtc/matrix_transform.hpp>

using namespace Axle;
using namespace Axle::RHI;
namespace {
    uint64_t Now() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    bool Wait(RenderWorker& worker, RenderWorkerResult& result) {
        const auto end = Now() + 20000000000ull;
        while (!worker.Poll(result)) { if (Now() > end) return false; std::this_thread::yield(); }
        return true;
    }
    bool Timing(const RenderWorkerResult& r) {
        const auto& t = r.Timings;
        return t.Sequence && t.EnqueuedAtNs && t.StartedAtNs >= t.EnqueuedAtNs && t.RenderFinishedAtNs >= t.StartedAtNs &&
            t.ExportFinishedAtNs >= t.RenderFinishedAtNs && t.PublishedAtNs >= t.ExportFinishedAtNs &&
            t.InFlightAtAdmission && t.InFlightAtAdmission <= 3;
    }
    void Distribution(std::ostream& out, const char* name, std::vector<uint64_t> values, bool comma = true) {
        std::sort(values.begin(), values.end());
        auto rank = [&](size_t percentile) { return values[(values.size() * percentile + 99) / 100 - 1]; };
        out << '"' << name << "\":{\"p50\":" << rank(50) << ",\"p95\":" << rank(95) << ",\"p99\":" << rank(99) << '}' << (comma ? ",\n" : "\n");
    }
    bool Ppm(const std::filesystem::path& path, const std::vector<uint8_t>& pixels) {
        std::ofstream f(path, std::ios::binary); f << "P6\n64 64\n255\n";
        for (size_t i = 0; i < pixels.size(); i += 4) f.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
        f.flush(); return f.good();
    }
}
bool RunRenderAcceptanceMeasure(RHIType backend, const std::filesystem::path& output) {
    JobSystem::Init(2);
    struct Jobs { ~Jobs() { JobSystem::Shutdown(); } } jobs;
    Scene scene; auto entity = scene.CreateEntity("Frozen OPT11 geometry");
    entity.AddComponent<MeshInstance3D>().AssetPath = "builtin://cube";
    auto& material = entity.AddComponent<MaterialComponent>();
    material.AlbedoColor = {1,0,0,1}; material.EmissionColor = {1,0,0}; material.EmissionIntensity = 1;
    const auto world = ExtractRenderWorld(static_cast<const Scene&>(scene));
    ViewRequest view; view.Width = view.Height = 64;
    view.Settings.EnableSkybox = view.Settings.EnableShadows = false;
    view.Camera = PerspectiveCamera(45,1,0.1f,100);
    view.Camera.SetViewMatrix(glm::lookAt(glm::vec3(0,0,3),glm::vec3(0),glm::vec3(0,1,0)),{0,0,3});
    auto& device = RHIDevice::Get();
    RHIDeviceDesc init; init.PreferredBackend = backend; init.AllowFallback = false; init.Headless = true;
    init.EnableValidation = init.EnableNRIValidation = true;
    RENDER_CHECK(RHIDevice::IsGraphicsAPIValidationAvailable(backend) && device.Init(init) && !device.IsSoftwareAdapter(), "physical validated serial reference");
    std::vector<uint64_t> serialRender, serialExport, workerRender, workerQueue, workerRoundtrip, workerExport;
    std::vector<uint8_t> golden;
    {
        RenderService service; RENDER_CHECK(service.Init(), "serial acceptance service");
        for (unsigned n = 0; n < 72; ++n) {
            const auto begin = Now(); const auto result = service.RenderView(world, view); const auto elapsed = Now() - begin;
            RENDER_CHECK(result.Success && result.CompletionToken && result.RenderedEntityCount, "serial fixture actually renders immutable geometry");
            if (n >= 8) serialRender.push_back(elapsed);
        }
        for (unsigned n = 0; n < 72; ++n) {
            const auto begin = Now(); auto pixels = RenderTests::ReadTexture(*service.GetDefaultMainColorTarget());
            if (n >= 8) serialExport.push_back(Now() - begin);
            RENDER_CHECK(pixels.size() == 64 * 64 * 4 && (golden.empty() || pixels == golden), "serial golden is complete and stable");
            golden = std::move(pixels);
        }
        const auto center = (32 * 64 + 32) * 4;
        RENDER_CHECK(golden[center] > golden[center + 1] + 30 && golden[center] > golden[center + 2] + 30, "matching blank images cannot pass parity");
        service.Shutdown();
    }
    RHIMeshRegistry::Get().Shutdown(); RENDER_CHECK(device.Shutdown(), "close serial owner before worker owns a new epoch");
    material.AlbedoColor = {0,1,0,1}; material.EmissionColor = {0,1,0};
    RenderWorker worker; RenderWorkerConfig config; config.Device = init; config.QueueCapacity = 3;
    RENDER_CHECK(worker.Start(config), "start dedicated native owner");
    const auto startupDeadline = Now() + 20000000000ull;
    while (worker.GetState() == RenderWorkerState::Starting && Now() < startupDeadline) std::this_thread::yield();
    RENDER_CHECK(worker.GetState() == RenderWorkerState::Running && !device.IsSoftwareAdapter() && device.GetBackend() == backend,
        "worker uses requested physical backend");
    auto frame = [&](uint64_t id, bool capture = false) {
        RenderWorkerFrame f; f.RequestId = id; f.World = world; f.Request = view; f.CaptureRgba8 = capture; return f;
    };
    auto invalid = frame(999, true); invalid.Request.Width = UINT32_MAX;
    RENDER_CHECK(!worker.Submit(std::move(invalid)), "export admission rejects oversized CPU output before native allocation");
    invalid = frame(999, true); invalid.Request.Kind = RenderViewKind::Thumbnail;
    RENDER_CHECK(!worker.Submit(std::move(invalid)), "export cannot read a stale main target for a different view kind");
    RenderWorkerResult result;
    for (unsigned n = 1; n <= 8; ++n) RENDER_CHECK(worker.Submit(frame(n)) && Wait(worker, result) && result.Result.Success && Timing(result), "worker warmup and monotonic timing domains");
    std::map<uint64_t, uint64_t> pending;
    uint64_t submitted = 0, completed = 0; const auto deadline = Now() + 30000000000ull;
    while (completed < 64 && Now() < deadline) {
        while (submitted < 64 && pending.size() < 3) {
            const uint64_t id = 9 + submitted; const auto begin = Now();
            RENDER_CHECK(worker.Submit(frame(id)), "bounded measured admission"); pending.emplace(id, begin); ++submitted;
        }
        if (!worker.Poll(result)) { std::this_thread::yield(); continue; }
        const auto found = pending.find(result.RequestId);
        RENDER_CHECK(found != pending.end() && result.Result.Success && result.Result.RenderedEntityCount && Timing(result) && result.Image.Rgba8.empty(),
            "ordinary measured frames return metadata with no implicit image export");
        workerRoundtrip.push_back(Now() - found->second);
        workerQueue.push_back(result.Timings.StartedAtNs - result.Timings.EnqueuedAtNs);
        workerRender.push_back(result.Timings.RenderFinishedAtNs - result.Timings.StartedAtNs);
        pending.erase(found); ++completed;
    }
    RENDER_CHECK(completed == 64 && pending.empty(), "every measured request has a terminal result before deadline");
    for (unsigned n = 0; n < 72; ++n) {
        RENDER_CHECK(worker.Submit(frame(73 + n, true)) && Wait(worker, result) && result.Result.Success && Timing(result) &&
            result.Image.Width == 64 && result.Image.Height == 64 && result.Image.Rgba8 == golden,
            "serial and worker exported pixels match exactly across owner/device epochs despite later scene edits");
        if (n >= 8) workerExport.push_back(result.Timings.ExportFinishedAtNs - result.Timings.RenderFinishedAtNs);
    }
    const auto accepted = worker.GetAcceptedFrames(), rejected = worker.GetRejectedFrames();
    worker.Stop();
    RENDER_CHECK(accepted == 144 && rejected == 2 && !device.IsInitialized() && result.Image.Rgba8 == golden,
        "CPU output survives worker shutdown without a native pointer or lost request");
    std::filesystem::create_directories(output);
    RENDER_CHECK(Ppm(output / "serial.ppm", golden) && Ppm(output / "worker.ppm", result.Image.Rgba8), "golden and worker image artifacts write and flush successfully");
    std::ofstream json(output / "measurements.json");
    json << "{\n\"backend\":\"" << (backend == RHIType::Vulkan ? "vulkan" : "dx12") << "\",\n\"clock\":\"cpu_steady_nanoseconds\",\n\"warmup\":8,\"samples\":64,\"capacity\":3,\n\"accepted\":" << accepted << ",\"rejected\":" << rejected << ",\"image_differing_bytes\":0,\n";
    Distribution(json, "serial_render_wall", serialRender); Distribution(json, "worker_render_wall", workerRender);
    Distribution(json, "worker_queue_age", workerQueue); Distribution(json, "worker_roundtrip", workerRoundtrip);
    Distribution(json, "serial_export_wall", serialExport); Distribution(json, "worker_export_wall", workerExport, false);
    json << "}\n";
    json.flush();
    RENDER_CHECK(json.good(), "actual measurements artifact writes successfully");
    std::cout << "Acceptance measurements: " << (output / "measurements.json") << '\n';
    return true;
}
