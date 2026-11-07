#include "RenderTestCommon.hpp"
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIFrameContext.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIStreamer.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <array>
#include <cstring>

using namespace Axle;
using namespace Axle::RHI;

static bool Native(RHIType backend) {
    auto& device = RHIDevice::Get();
    RHIDeviceDesc init; init.PreferredBackend = backend; init.AllowFallback = false; init.Headless = true;
    init.EnableNRIValidation = true; init.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    RENDER_CHECK(device.Init(init) && device.GetBackend() == backend && !device.IsSoftwareAdapter(), "physical frame arenas");
    std::array<RHIFrameContext, 2> frames;
    std::array<RHIBuffer, 2> readbacks;
    RHIBufferDesc readbackDesc; readbackDesc.Size = 64; readbackDesc.MemoryLocation = RHIMemoryLocation::HostReadback;
    for (size_t i = 0; i < frames.size(); ++i)
        RENDER_CHECK(frames[i].Init(1024, 64) && readbacks[i].Init(readbackDesc), "bounded slot storage");
    for (uint32_t cycle = 0; cycle < 6; ++cycle) {
        auto& frame = frames[cycle % frames.size()]; auto& readback = readbacks[cycle % frames.size()];
        RENDER_CHECK(frame.Begin(), "completed slot may wrap and begin again");
        const std::array<uint32_t, 2> first{0x12345678u + cycle, 0xabcdef01u}, second{0xdeadbeefu, 0x45678901u + cycle};
        auto a = frame.WriteUpload(first.data(), sizeof(first)), b = frame.WriteUpload(second.data(), sizeof(second));
        auto uniformA = frame.WriteUniform(first.data(), sizeof(first)), uniformB = frame.WriteUniform(second.data(), sizeof(second));
        RENDER_CHECK(a && b && uniformA && uniformB && b.Offset >= a.Offset + a.Size &&
            uniformB.Offset >= uniformA.Offset + uniformA.Size, "multiple views receive disjoint upload and uniform ranges");
        RENDER_CHECK(!frame.WriteUpload(first.data(), 65), "arena pressure rejects overflow without wrapping onto live bytes");
        frame.GetCommands()->CopyBuffer(*readback.GetNative(), 0, *a.Buffer, a.Offset, a.Size);
        frame.GetCommands()->CopyBuffer(*readback.GetNative(), 16, *b.Buffer, b.Offset, b.Size);
        frame.GetCommands()->CopyBuffer(*readback.GetNative(), 32, *uniformA.Buffer, uniformA.Offset, uniformA.Size);
        frame.GetCommands()->CopyBuffer(*readback.GetNative(), 48, *uniformB.Buffer, uniformB.Offset, uniformB.Size);
        RENDER_CHECK(frame.End() && frame.Submit() && frame.GetCompletion(), "seal host writes and publish one slot completion");
        RENDER_CHECK(!frame.WriteUpload(first.data(), sizeof(first)), "submitted data cannot be overwritten");
        if (cycle == 0) {
            ScopedRHIFault busy([](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == "WaitFence") return nri::Result::NOT_READY; return {};
            });
            RENDER_CHECK(!frame.Begin(0) && !frame.WriteUpload(second.data(), sizeof(second)), "deliberately busy slot cannot reset or overwrite its arenas");
        }
        RENDER_CHECK(device.WaitForCompletion(frame.GetCompletion()), "bounded slot completion");
        const auto* bytes = static_cast<const unsigned char*>(readback.Map());
        RENDER_CHECK(bytes && std::memcmp(bytes, first.data(), sizeof(first)) == 0 &&
            std::memcmp(bytes + 16, second.data(), sizeof(second)) == 0 &&
            std::memcmp(bytes + 32, first.data(), sizeof(first)) == 0 &&
            std::memcmp(bytes + 48, second.data(), sizeof(second)) == 0, "GPU consumed original upload and uniform bytes from both view ranges");
        readback.Unmap();
    }
    {
        RHIStreamer streamer; RHIStreamerDesc desc;
        desc.ConstantBufferSize = (std::max)(8u, device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment.constantBufferOffset) * 2; desc.HostDataCapacity = 16;
        RENDER_CHECK(streamer.Init(desc), "small production streamer ring");
        const uint64_t a = 0x123456789abcdef0ull, b = 0x3456789abcdef012ull;
        const auto first = streamer.StreamConstant(a), second = streamer.StreamConstant(b);
        auto* host = streamer.StreamHostData(&a, sizeof(a), 1);
        RENDER_CHECK(first == 0 && second >= first + sizeof(a) && streamer.StreamConstant(b) == UINT32_MAX &&
            host && !streamer.StreamHostData(&b, 17, 1), "streamer pressure cannot wrap onto published ranges");
        RHICommandBuffer copy;
        RENDER_CHECK(copy.Init() && copy.Begin(), "streamer original byte readback");
        auto* backing = streamer.GetConstantBuffer();
        RENDER_CHECK(backing, "managed streamer backing generation");
        copy.CopyBuffer(*readbacks[0].GetNative(), 0, *backing, first, sizeof(a));
        copy.CopyBuffer(*readbacks[0].GetNative(), 16, *backing, second, sizeof(b));
        RENDER_CHECK(!streamer.EndFrame() && std::memcmp(host, &a, sizeof(a)) == 0, "unsubmitted recording prevents streamer slot rotation and CPU block reclamation");
        RENDER_CHECK(copy.End() && copy.Submit() && device.WaitIdle(), "consume original ring ranges");
        const auto* bytes = static_cast<const unsigned char*>(readbacks[0].Map());
        RENDER_CHECK(bytes && std::memcmp(bytes, &a, sizeof(a)) == 0 && std::memcmp(bytes + 16, &b, sizeof(b)) == 0,
            "GPU confirms a refused wrap did not overwrite either constant range"); readbacks[0].Unmap();
        RENDER_CHECK(streamer.EndFrame() && streamer.StreamConstant(b) == 0, "checked frame retirement permits safe ring wrap");
    }
    auto& frame = frames[0];
    RENDER_CHECK(frame.Begin(), "abandoned frame");
    auto old = frame.GetCommands()->GetNative();
    {
        ScopedRHIFault failed([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "CreateBuffer") return nri::Result::OUT_OF_MEMORY; return {};
        });
        RENDER_CHECK(!frame.Init(1024, 64) && frame.GetCommands()->GetNative() == old, "partial slot construction preserves its complete old generation");
    }
    RENDER_CHECK(frame.Abandon() && frame.Begin() && frame.End(), "abandon releases pins and permits a clean recording");
    {
        ScopedRHIFault failed([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "QueueSubmit") return nri::Result::FAILURE; return {};
        });
        RENDER_CHECK(!frame.Submit() && !frame.GetCompletion(), "failed frame never advertises successful completion");
    }
    RENDER_CHECK(device.WaitIdle() && frame.Abandon(), "failed frame storage survives until checked drain");
    return device.Shutdown();
}
int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting(); Log::Init(); bool ok = false;
    try { ok = Native(argc > 1 && std::string(argv[1]) == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    RHIDevice::Get().Shutdown();
    ok = Axle::RenderTests::Check(GetRHIValidationErrorCount() == 0, "native validation including teardown", __FILE__, __LINE__) && ok;
    std::cout << "Frame contexts: " << (ok ? "PASS" : "FAIL") << '\n'; return ok ? 0 : 1;
}
