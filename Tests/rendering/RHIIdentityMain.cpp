#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIResource.hpp>
#include <Axle/RHI/RHISampler.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHIStreamer.hpp>
#include <thread>

using namespace Axle;
using namespace Axle::RHI;
using namespace Axle::RenderTests;

static bool Policy() {
    RHIResourceService resources;
    unsigned devices = 0, released = 0;
    int identity = 0;
    RENDER_CHECK(resources.Open([&] { ++devices; }), "open a real resource-owner policy context");
    auto first = resources.Publish(RHIResourceKind::Texture, "generation A", {&identity}, [&] { ++released; });
    const auto old = first.GetHandle();
    RENDER_CHECK(first.IsLive() && resources.Acquire(old).IsLive(), "published generation resolves");
    auto retained = first;
    first.Reset();
    resources.CollectAfterDrain();
    RENDER_CHECK(released == 0, "another strong lease retains its immutable generation");
    retained.Reset();
    RENDER_CHECK(!resources.Acquire(old), "retired handle cannot resolve");
    auto second = resources.Publish(RHIResourceKind::Texture, "generation B", {&identity}, [&] { ++released; });
    RENDER_CHECK(second.GetHandle().Slot == old.Slot && second.GetHandle().Generation != old.Generation &&
        !resources.Acquire(old), "slot reuse changes generation and rejects the stale handle");
    resources.CollectAfterDrain();
    RENDER_CHECK(released == 1, "retired generation cleans up exactly once");
    auto workerLease = resources.Publish(RHIResourceKind::Buffer, "worker release", {}, [&] { ++released; });
    std::thread worker([lease = std::move(workerLease)]() mutable { lease.Reset(); }); worker.join();
    RENDER_CHECK(released == 1, "worker release cannot call native cleanup");
    resources.CollectAfterDrain();
    RENDER_CHECK(released == 2, "render owner collects worker retirement");
    const auto priorEpoch = resources.GetEpoch();
    resources.CloseAfterDrain();
    RENDER_CHECK(!second.IsLive() && released == 3 && devices == 1, "device close invalidates all surviving wrappers before destroying the device");
    RENDER_CHECK(resources.Open([&] { ++devices; }) && resources.GetEpoch() != priorEpoch &&
        !resources.Acquire(second.GetHandle()), "another device epoch rejects every old identity");
    second.Reset(); resources.CollectAfterDrain();
    RENDER_CHECK(released == 3, "stale destructor cannot destroy a new device resource");
    resources.CloseAfterDrain();
    return devices == 2;
}

static bool Native(RHIType backend) {
    auto& device = RHIDevice::Get();
    RHIDeviceDesc desc; desc.PreferredBackend = backend; desc.AllowFallback = false;
    desc.Headless = true; desc.EnableNRIValidation = true;
    desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    RENDER_CHECK(device.Init(desc) && device.GetBackend() == backend && !device.IsSoftwareAdapter(), "strict physical device");
    RHITexture texture;
    RHIBuffer buffer;
    RHISampler sampler;
    RHIPipelineLayout layout;
    RHIDescriptorPool pool;
    RHICommandBuffer commands;
    RHIStreamer streamer;
    RENDER_CHECK(texture.Init(RHITextureDesc::RenderTarget2D(8, 8)) &&
        buffer.Init(RHIBufferDesc::Constant(256)) && sampler.Init() && pool.Init() && commands.Init() && streamer.Init(), "actual resources belong to the current device");
    nri::DescriptorRangeDesc range{};
    range.descriptorType = nri::DescriptorType::TEXTURE; range.descriptorNum = 1; range.shaderStages = nri::StageBits::FRAGMENT_SHADER;
    nri::DescriptorSetDesc setDesc{}; setDesc.ranges = &range; setDesc.rangeNum = 1;
    nri::PipelineLayoutDesc layoutDesc{};
    layoutDesc.shaderStages = nri::StageBits::FRAGMENT_SHADER;
    layoutDesc.descriptorSets = &setDesc; layoutDesc.descriptorSetNum = 1;
    RENDER_CHECK(layout.Init(layoutDesc), "real native layout");
    auto* set = pool.AllocateDescriptorSet(layout, 0);
    RENDER_CHECK(set, "real descriptor set generation");
    pool.UpdateTexture(set, 0, 0, texture.GetSRV());
    auto setLease = device.GetResources().AcquireNative(set);
    const auto handle = texture.GetHandle();
    auto lease = texture.AcquireLease();
    auto* original = texture.GetNative();
    RENDER_CHECK(lease.IsLive() && device.GetResources().Acquire(handle).IsLive(), "wrapper exposes the shared resource contract");
    RENDER_CHECK(texture.Init(RHITextureDesc::RenderTarget2D(16, 16)) && texture.GetNative() != original &&
        device.GetResources().Acquire(handle).IsLive(), "replacement preserves a leased older generation");
    lease.Reset();
    RENDER_CHECK(device.GetResources().Acquire(handle).IsLive(), "descriptor set retains the exact texture/view generation");
    pool.Reset();
    RENDER_CHECK(setLease.IsLive() && device.GetResources().Acquire(handle).IsLive(), "arena replacement preserves a leased old descriptor set and its resources");
    setLease.Reset();
    RENDER_CHECK(device.WaitIdle(), "collect descriptor dependency retirement after drain");
    RENDER_CHECK(!device.GetResources().Acquire(handle), "old generation retires after its final lease");
    auto survive = texture.AcquireLease();
    std::vector<std::unique_ptr<RHIBuffer>> group;
    std::vector<RHIMemoryAllocation> chunks;
    RENDER_CHECK(RHIMemoryManager::AllocateBufferGroup({RHIBufferDesc::Vertex(256)}, group, chunks), "real grouped allocation joins the owner context");
    RENDER_CHECK(device.Shutdown(), "checked drain and resource teardown precede owning device destruction");
    RENDER_CHECK(!texture.IsValid() && !texture.GetNative() && !buffer.IsValid() && !buffer.GetNative() &&
        !sampler.IsValid() && !layout.IsValid() && !pool.IsValid() && !commands.IsValid() && !streamer.IsValid() && !survive.IsLive() &&
        !group.front()->IsValid() && !chunks.front().GetNative(),
        "every native wrapper becomes harmless when its device closes");
    RENDER_CHECK(device.Init(desc) && device.GetEpoch() != handle.Epoch, "reopen a distinct native device epoch");
    RHITexture fresh;
    RENDER_CHECK(fresh.Init(RHITextureDesc::RenderTarget2D(8, 8)), "new device can allocate independently");
    auto* freshNative = fresh.GetNative();
    texture.Shutdown(); buffer.Shutdown(); sampler.Shutdown(); layout.Shutdown(); pool.Shutdown(); commands.Shutdown(); streamer.Shutdown(); survive.Reset();
    group.clear(); RHIMemoryManager::FreeAllocatedChunks(chunks);
    RENDER_CHECK(fresh.IsValid() && fresh.GetNative() == freshNative && device.WaitIdle(), "old wrappers release without touching the new device");
    return device.Shutdown();
}

int main(int argc, char** argv) {
    ConfigureFailureReporting(); Log::Init();
    bool ok = false;
    try {
        const std::string mode = argc > 1 ? argv[1] : "policy";
        ok = Policy();
        if (ok && mode != "policy") ok = Native(mode == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    RHIDevice::Get().Shutdown();
    ok = Axle::RenderTests::Check(Axle::RHI::GetRHIValidationErrorCount() == 0,
        "native validation reported no errors, including teardown", __FILE__, __LINE__) && ok;
    std::cout << "ResourceIdentity: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
