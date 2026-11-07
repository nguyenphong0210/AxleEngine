#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"
#include <Axle/RHI/RHIBindless.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIStreamer.hpp>
#include <algorithm>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <array>
#include <cstring>

using namespace Axle;
using namespace Axle::RHI;

static bool Policy() {
    RHIResourceService resources;
    RENDER_CHECK(resources.Open({}), "open CPU lifetime policy owner");
    int released = 0;
    auto leaf = resources.Publish(RHIResourceKind::Texture, "recorded image", {}, [&] { ++released; });
    const auto handle = leaf.GetHandle();
    auto image = resources.Publish(RHIResourceKind::DescriptorSet, "GUI registration", {}, [&] { ++released; }, {leaf});
    auto recording = resources.Pin(image);
    RENDER_CHECK(recording.IsLive() && resources.IsInUse(leaf) && resources.IsInUse(image), "recording pins transitive generations before submission");
    leaf.Reset(); image.Reset(); resources.CollectAfterDrain();
    RENDER_CHECK(released == 0 && resources.Acquire(handle).IsLive(), "a queue drain cannot retire CPU draw-list references");
    auto submitted = recording; recording.Reset();
    resources.CollectAfterDrain();
    RENDER_CHECK(released == 0, "transferred snapshot survives its original owner");
    submitted.Reset(); resources.CollectAfterDrain();
    RENDER_CHECK(released == 2 && !resources.Acquire(handle), "registration and nested texture retirement complete without an owner callback");
    auto retire = resources.GetDeferredReleaser();
    retire([retire, &released] { ++released; retire([&released] { ++released; }); });
    resources.CollectAfterDrain();
    RENDER_CHECK(released == 4, "nested retirement drains outside the pending-list iteration");
    resources.CloseAfterDrain();
    return true;
}

struct TextureCopy {
    RHIBuffer Readback;
    RHICommandBuffer Commands;
    nri::TextureDataLayoutDesc Layout{};
    bool Record(nri::Texture* texture) {
        auto& device = RHIDevice::Get();
        const auto& core = device.GetNRICore();
        const auto alignment = core.GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
        auto align = [](uint64_t value, uint64_t step) { return (value + step - 1) / step * step; };
        Layout.rowPitch = static_cast<uint32_t>(align(8 * 4, alignment.uploadBufferTextureRow));
        Layout.slicePitch = align(Layout.rowPitch * 8, alignment.uploadBufferTextureSlice);
        RHIBufferDesc desc; desc.Size = Layout.slicePitch; desc.MemoryLocation = RHIMemoryLocation::HostReadback;
        if (!Readback.Init(desc) || !Commands.Init() || !Commands.Begin() || !Commands.KeepAlive(Readback.AcquireLease())) return false;
        Commands.TransitionTexture(texture, RHIResourceLayout::ShaderResource, RHIResourceLayout::CopySource);
        nri::TextureRegionDesc region{}; region.width = region.height = 8; region.depth = 1;
        Commands.ReadbackTextureToBuffer(*Readback.GetNative(), Layout, *texture, region);
        Commands.TransitionTexture(texture, RHIResourceLayout::CopySource, RHIResourceLayout::ShaderResource);
        return Commands.End();
    }
    std::vector<uint8_t> Pixels() {
        auto* source = static_cast<uint8_t*>(Readback.Map());
        if (!source) return {};
        std::vector<uint8_t> pixels(8 * 8 * 4);
        for (unsigned y = 0; y < 8; ++y) std::memcpy(pixels.data() + y * 32, source + y * Layout.rowPitch, 32);
        Readback.Unmap(); return pixels;
    }
};

static bool ReviewRegressions() {
    auto& device = RHIDevice::Get();
    struct DefaultCase { std::shared_ptr<RHITexture> (*Get)(); std::array<uint8_t, 4> Color; };
    const DefaultCase defaults[] = {
        {RHIMaterial::GetDefaultWhiteTexture, {255, 255, 255, 255}},
        {RHIMaterial::GetDefaultFlatNormalTexture, {128, 128, 255, 255}},
        {RHIMaterial::GetDefaultRoughnessTexture, {128, 128, 128, 255}},
        {RHIMaterial::GetDefaultBlackTexture, {0, 0, 0, 255}},
        {RHIMaterial::GetDefaultIrradianceCubemap, {128, 128, 128, 255}},
        {RHIMaterial::GetDefaultPrefilterCubemap, {128, 128, 128, 255}},
        {RHIMaterial::GetDefaultBRDFLUT, {255, 0, 0, 255}}
    };
    for (const auto& test : defaults) for (const auto* boundary : {"AllocateTextureMemory", "UploadData"}) {
        RHIMaterial::ReleaseDefaultTextures();
        RENDER_CHECK(device.WaitIdle(), "retire previous default before recoverable failure");
        bool reached = false;
        {
            ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == boundary) { reached = true; return nri::Result::OUT_OF_MEMORY; }
                return {};
            });
            RENDER_CHECK(!test.Get() && reached && device.CanCreateResources(), "FR-02: incomplete fallback is never published");
        }
        auto recovered = test.Get();
        RENDER_CHECK(recovered && recovered->IsValid() && test.Get() == recovered, "FR-02: retry publishes one complete default generation");
        const auto pixels = RenderTests::ReadTexture(*recovered);
        RENDER_CHECK(pixels.size() == recovered->GetWidth() * recovered->GetHeight() * 4, "FR-02: real default pixels are readable");
        for (size_t pixel = 0; pixel < pixels.size(); pixel += 4)
            RENDER_CHECK(std::equal(test.Color.begin(), test.Color.end(), pixels.begin() + pixel), "FR-02: retried default contains its intended GPU color");
    }
    RHIMaterial::ReleaseDefaultTextures();
    RENDER_CHECK(device.WaitIdle(), "release default fault fixtures");
    {
        RHIBindlessRegistry registry;
        bool reached = false;
        {
            ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == "UploadData") { reached = true; return nri::Result::OUT_OF_MEMORY; } return {};
            });
            RENDER_CHECK(!registry.Init(256) && reached && !registry.IsInitialized() && !registry.GetDescriptorPool().IsValid(),
                "FR-01/02: bindless startup rolls back incomplete fallback publication");
        }
        RENDER_CHECK(registry.Init(256) && registry.GetRegisteredCount() == 4, "bindless startup retries with complete defaults");
        RHITexture one, two, three;
        auto desc = RHITextureDesc::RenderTarget2D(8, 8);
        RENDER_CHECK(one.Init(desc) && two.Init(desc) && three.Init(desc), "real bindless replacement candidates");
        const auto first = registry.RegisterTexture(one);
        auto set = device.GetResources().AcquireNative(registry.GetDescriptorSet());
        auto recording = device.GetResources().Pin(set);
        RENDER_CHECK(first >= 4 && !registry.UnregisterTexture(first) && registry.GetRegisteredCount() == 5,
            "FR-01: pinned unregister explicitly rejects without freeing the CPU slot");
        RENDER_CHECK(registry.RegisterTexture(two) == RHIBindlessRegistry::FallbackWhiteIndex && registry.GetRegisteredCount() == 5,
            "FR-01: rejected descriptor write cannot publish a CPU index");
        recording.Reset();
        RENDER_CHECK(device.WaitIdle(), "finish descriptor-set retirement");
        const auto second = registry.RegisterTexture(two);
        RENDER_CHECK(second >= 4 && second != first && registry.GetRegisteredCount() == 6, "FR-01: retry actually binds a new available slot");
        RENDER_CHECK(registry.UnregisterTexture(first) && registry.RegisterTexture(three) == first, "FR-01: successful unregister permits slot reuse");
        auto current = device.GetResources().Pin(set);
        auto textures = device.GetResources().GetPinnedTextures(current);
        RENDER_CHECK(std::find(textures.begin(), textures.end(), one.GetNative()) == textures.end() &&
            std::find(textures.begin(), textures.end(), two.GetNative()) != textures.end() &&
            std::find(textures.begin(), textures.end(), three.GetNative()) != textures.end(), "FR-01: descriptor dependencies match the committed CPU slots");
        current.Reset(); set.Reset(); registry.Shutdown();
    }
    RHIMaterial::ReleaseDefaultTextures();
    RENDER_CHECK(device.WaitIdle(), "retire bindless regression resources");
    {
        RHIStreamer streamer;
        RENDER_CHECK(streamer.Init(), "real abandoned-batch streamer");
        for (unsigned frame = 0; frame < 16; ++frame) {
            RHIBuffer destination;
            const uint32_t data[4] = {frame, 2, 3, 4};
            RENDER_CHECK(destination.Init(RHIBufferDesc::Vertex(sizeof(data))), "abandoned destination");
            const auto handle = destination.GetHandle(); const auto batch = streamer.BeginCopyBatch();
            RENDER_CHECK(streamer.StreamBufferUpload(batch, destination, 0, data, sizeof(data)).IsValid(), "enqueue an unrecorded upload");
            destination.Shutdown();
            auto recording = device.GetResources().Pin(device.GetResources().AcquireNative(streamer.GetNative()));
            RENDER_CHECK(!streamer.EndFrame() && device.GetResources().Acquire(handle).IsLive(), "FR-03: failed frame retirement preserves pending destination ownership");
            recording.Reset();
            RENDER_CHECK(streamer.EndFrame() && device.WaitIdle() && !device.GetResources().Acquire(handle).IsLive(),
                "FR-03: native discard releases abandoned destinations every frame");
        }
    }
    return device.WaitIdle();
}

static bool Native(RHIType backend) {
    auto& device = RHIDevice::Get();
    RHIDeviceDesc desc; desc.PreferredBackend = backend; desc.AllowFallback = false;
    desc.Headless = true; desc.EnableNRIValidation = true;
    desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    RENDER_CHECK(device.Init(desc) && !device.IsSoftwareAdapter() && device.GetBackend() == backend, "strict physical lifetime fixture");
    RENDER_CHECK(ReviewRegressions() && device.WaitIdle(), "consolidated review regression checks");
    RHITexture image;
    RENDER_CHECK(image.Init(RHITextureDesc::RenderTarget2D(8, 8)), "real image generation");
    std::vector<uint8_t> expected(8 * 8 * 4);
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>(13 + i * 7);
    nri::TextureSubresourceUploadDesc sub{expected.data(), 1, 32, 256};
    nri::TextureUploadDesc upload{}; upload.texture = image.GetNative(); upload.subresources = &sub;
    upload.after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::ALL};
    RENDER_CHECK(device.UploadData(&upload, 1, nullptr, 0) == nri::Result::SUCCESS, "real source pixels");
    const auto original = image.GetHandle();
    TextureCopy copy;
    RENDER_CHECK(copy.Record(image.GetNative()), "record real GPU readback before replacing its source");
    RENDER_CHECK(image.Init(RHITextureDesc::RenderTarget2D(8, 8)) && device.WaitIdle() && device.GetResources().Acquire(original).IsLive(),
        "reload and unrelated queue drain preserve an unsubmitted recording's original pixels");
    RENDER_CHECK(copy.Commands.Submit(), "submit retained generation");
    const auto completion = device.GetLastSubmittedToken();
    copy.Commands.Shutdown();
    {
        ScopedRHIFault busy([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "WaitFence") return nri::Result::NOT_READY;
            return {};
        });
        RENDER_CHECK(!device.WaitForCompletion(completion, 0) && device.GetResources().Acquire(original).IsLive() && !copy.Readback.Map(),
            "an unobserved completion retains all generations and rejects mapped storage reuse");
    }
    RENDER_CHECK(device.WaitForCompletion(completion) && copy.Pixels() == expected, "completion reveals the actual original GPU pixels");
    RENDER_CHECK(!device.GetResources().Acquire(original), "completed work releases the old generation");
    RHITexture reused;
    RENDER_CHECK(reused.Init(RHITextureDesc::RenderTarget2D(8, 8)) && reused.GetHandle().Slot == original.Slot &&
        reused.GetHandle().Generation != original.Generation, "completed slot can be reused without reviving a stale handle");
    const auto abandoned = reused.GetHandle();
    RHICommandBuffer unsubmitted;
    RENDER_CHECK(unsubmitted.Init() && unsubmitted.Begin() && unsubmitted.KeepAlive(reused.AcquireLease()), "real abandoned recording");
    reused.Shutdown();
    RENDER_CHECK(device.WaitIdle() && device.GetResources().Acquire(abandoned).IsLive(), "CPU recording survives a drain");
    RENDER_CHECK(unsubmitted.Reset() && !device.GetResources().Acquire(abandoned), "abandon destroys commands before retiring nested dependencies");
    RHICommandBuffer failed;
    RENDER_CHECK(failed.Init() && failed.Begin() && failed.KeepAlive(image.AcquireLease()) && failed.End(), "failed-submit fixture");
    const auto retained = image.GetHandle();
    const auto before = device.GetLastSubmittedToken();
    {
        ScopedRHIFault fault([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "QueueSubmit") return nri::Result::FAILURE;
            return {};
        });
        RENDER_CHECK(!failed.Submit() && device.GetLastSubmittedToken() == before, "failed submit cannot publish completion");
    }
    image.Shutdown(); failed.Shutdown();
    RENDER_CHECK(device.GetResources().Acquire(retained).IsLive(), "ambiguous submit survives destruction of every original wrapper");
    RENDER_CHECK(device.WaitIdle() && !device.GetResources().Acquire(retained), "checked drain resolves ambiguous retirement");
    return device.Shutdown();
}

int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting(); Log::Init();
    bool ok = false;
    try {
        const std::string mode = argc > 1 ? argv[1] : "policy";
        ok = Policy();
        if (ok && mode != "policy") ok = Native(mode == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    RHIDevice::Get().Shutdown();
    ok = Axle::RenderTests::Check(GetRHIValidationErrorCount() == 0, "native validation including teardown", __FILE__, __LINE__) && ok;
    std::cout << "Retirement: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
