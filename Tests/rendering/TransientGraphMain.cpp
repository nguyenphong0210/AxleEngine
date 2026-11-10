#include "RenderTestCommon.hpp"
#include <Axle/Log.hpp>
#include <Axle/RenderTransientFrame.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <array>
#include <cstring>
#include <thread>

using namespace Axle;
using namespace Axle::RHI;
using Graph = RenderTransientGraph;

struct Fixture {
    RHIBuffer Upload, BufferReadback, ImageReadback;
    RHITexture Output;
    nri::TextureDataLayoutDesc ImageLayout{};
    std::array<uint32_t, 64> Pattern{};
    Graph::Plan Plan;
    std::vector<RenderTransientFrame::Import> Imports;
    uint32_t TempBuffer = 0, TempImage = 1, Input = 2, Destination = 3, BufferResult = 4, ImageResult = 5;
    bool Init() {
        for (uint32_t i = 0; i < Pattern.size(); ++i) Pattern[i] = 0x12340000 + i;
        RENDER_CHECK(Upload.Init(RHIBufferDesc::Vertex(sizeof(Pattern), RHIMemoryLocation::HostUpload), Pattern.data()), "real imported upload");
        RHIBufferDesc readback; readback.Size = sizeof(Pattern); readback.MemoryLocation = RHIMemoryLocation::HostReadback;
        auto outputDesc = RHITextureDesc::RenderTarget2D(8, 8);
        outputDesc.Usage = outputDesc.Usage | RHITextureUsage::CopySource | RHITextureUsage::CopyDest;
        RENDER_CHECK(BufferReadback.Init(readback) && Output.Init(outputDesc), "external output generations");
        const auto& alignment = RHIDevice::Get().GetNRICore().GetDeviceDesc(*RHIDevice::Get().GetNRIDevice()).memoryAlignment;
        auto align = [](uint32_t n, uint32_t a) { return (n + a - 1) / a * a; };
        ImageLayout.rowPitch = align(8 * 4, alignment.uploadBufferTextureRow);
        ImageLayout.slicePitch = align(ImageLayout.rowPitch * 8, alignment.uploadBufferTextureSlice);
        readback.Size = ImageLayout.slicePitch;
        RENDER_CHECK(ImageReadback.Init(readback), "image readback with native pitch alignment");
        auto image = RHITextureDesc::RenderTarget2D(8, 8);
        image.Usage = image.Usage | RHITextureUsage::CopySource;
        Graph graph;
        Graph::ResourceDesc b; b.Name = "temporary buffer"; b.Type = Graph::Kind::TransientBuffer;
        b.Description = RHIBufferDesc::Vertex(sizeof(Pattern)); b.Memory = {sizeof(Pattern), 1}; graph.AddResource(b);
        Graph::ResourceDesc t; t.Name = "temporary image"; t.Type = Graph::Kind::TransientTexture;
        t.Description = image; t.Memory = {256, 1}; graph.AddResource(t);
        auto import = [&](std::string name, const RHITransientView& view, bool defined) {
            Graph::ResourceDesc desc; desc.Name = std::move(name); desc.Type = Graph::Kind::Imported;
            desc.Description = view.Description; desc.ContentsDefined = defined;
            Imports.push_back({graph.AddResource(std::move(desc)), view});
        };
        import("upload", MakeTransientView(Upload), true);
        import("output", MakeTransientView(Output), false);
        import("buffer readback", MakeTransientView(BufferReadback), false);
        import("image readback", MakeTransientView(ImageReadback), false);
        using U = Graph::Usage; using A = Graph::Access;
        graph.AddPass({"buffer upload", {{Input, U::CopySource}, {TempBuffer, U::CopyDest, A::Write}}});
        graph.AddPass({"color clear", {{TempImage, U::ColorAttachment, A::Write, Graph::Load::Clear}}});
        graph.AddPass({"export", {{TempImage, U::CopySource}, {Destination, U::CopyDest, A::Write},
            {TempBuffer, U::CopySource}, {BufferResult, U::CopyDest, A::Write}}});
        graph.AddPass({"readback", {{Destination, U::CopySource}, {ImageResult, U::CopyDest, A::Write}}});
        std::string error;
        RENDER_CHECK(graph.Compile(Plan, &error), "CPU plan for real native resources");
        return true;
    }
    bool Record(RenderTransientFrame& frame) {
        RENDER_CHECK(frame.Record(0, [&](RHICommandBuffer& cmd) {
            cmd.CopyBuffer(*frame.Get(TempBuffer)->Buffer, 0, *frame.Get(Input)->Buffer, 0, sizeof(Pattern));
            return true;
        }), "record transient buffer upload");
        RENDER_CHECK(frame.Record(1, [&](RHICommandBuffer& cmd) {
            nri::AttachmentDesc color{}; color.descriptor = frame.Get(TempImage)->RTV;
            color.loadOp = nri::LoadOp::CLEAR; color.storeOp = nri::StoreOp::STORE;
            color.clearValue.color.f = {0, 1, 0, 1};
            nri::RenderingDesc rendering{}; rendering.colors = &color; rendering.colorNum = 1;
            cmd.BeginRendering(rendering); cmd.EndRendering(); return true;
        }), "record graph-owned transient color target");
        RENDER_CHECK(frame.Record(2, [&](RHICommandBuffer& cmd) {
            cmd.CopyTexture(*frame.Get(Destination)->Texture, nullptr, *frame.Get(TempImage)->Texture, nullptr);
            cmd.CopyBuffer(*frame.Get(BufferResult)->Buffer, 0, *frame.Get(TempBuffer)->Buffer, 0, sizeof(Pattern)); return true;
        }), "record graph export to imported generations");
        RENDER_CHECK(frame.Record(3, [&](RHICommandBuffer& cmd) {
            nri::TextureRegionDesc region{}; region.width = region.height = 8; region.depth = 1;
            cmd.ReadbackTextureToBuffer(*frame.Get(ImageResult)->Buffer, ImageLayout, *frame.Get(Destination)->Texture, region);
            return true;
        }), "record graph readback");
        return true;
    }
    bool CheckPixels() {
        auto* bytes = static_cast<const unsigned char*>(ImageReadback.Map());
        RENDER_CHECK(bytes, "map actual image result");
        for (uint32_t y = 0; y < 8; ++y) for (uint32_t x = 0; x < 8; ++x) {
            const auto* pixel = bytes + y * ImageLayout.rowPitch + x * 4;
            RENDER_CHECK(pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0 && pixel[3] == 255, "all exported GPU pixels match green reference");
        }
        ImageReadback.Unmap();
        const auto* data = BufferReadback.Map();
        RENDER_CHECK(data && std::memcmp(data, Pattern.data(), sizeof(Pattern)) == 0, "all transient buffer bytes survive GPU copy/export");
        BufferReadback.Unmap(); return true;
    }
};

static bool PlacementOwnership() {
    auto& device = RHIDevice::Get(); const auto core = device.GetNRICore();
    const auto desc = RHITextureDesc::RenderTarget2D(8, 8);
    RHITransientRequirements queried;
    {
        unsigned allocations = 0;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "AllocateTextureMemory" || operation == "AllocateBufferMemory" || operation == "AllocateTransientMemory") {
                ++allocations; return nri::Result::FAILURE;
            } return {};
        });
        RENDER_CHECK(RHITransientResources::QueryRequirements(desc, queried) && allocations == 0,
            "query-only native probes allocate no backing memory");
    }
    nri::AllocateMemoryDesc allocation{}; allocation.size = queried.SizeBytes; allocation.type = queried.MemoryType;
    nri::Memory* backing = nullptr;
    RENDER_CHECK(core.AllocateMemory(*device.GetNRIDevice(), allocation, backing) == nri::Result::SUCCESS, "external placement backing");
    auto frees = std::make_shared<uint32_t>(0);
    auto memory = device.GetResources().Publish(RHIResourceKind::Memory, "placement ownership proof", {backing},
        [core, backing, frees] { ++*frees; core.FreeMemory(backing); });
    RENDER_CHECK(memory.IsLive(), "registered memory generation");
    RHITexture texture;
    RENDER_CHECK(!texture.InitPlaced(desc, backing, 1) && texture.InitPlaced(desc, backing), "placement rejects misalignment and binds valid offset");
    auto snapshot = texture.RetainGeneration();
    memory.Reset(); texture.Shutdown();
    RENDER_CHECK(snapshot.IsValid() && device.WaitIdle() && *frees == 0, "texture generation retains borrowed memory after owner release");
    snapshot.Shutdown();
    RENDER_CHECK(device.WaitIdle() && *frees == 1, "borrowed memory retires exactly once after final bound generation");
    RHITransientResources resources;
    RENDER_CHECK(resources.InitPlaced({desc}), "placed resource set");
    const auto previous = resources.Get(0)->Lease.GetHandle();
    {
        unsigned reached = 0;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "AllocateTransientMemory" && ++reached == 2) return nri::Result::FAILURE;
            return {};
        });
        RENDER_CHECK(!resources.InitPlaced({desc, desc}) && reached == 2 && resources.Get(0)->Lease.GetHandle() == previous,
            "partially allocated placement transaction preserves old set");
    }
    RENDER_CHECK(!resources.InitPlaced({desc, RHIBufferDesc::Vertex(256)}, {0, 0}) && resources.Get(0)->Lease.GetHandle() == previous,
        "incompatible texture/buffer pair cannot share a block");
    RENDER_CHECK(resources.InitPlaced({RHITextureDesc::DepthStencil2D(8, 8)}) && resources.Get(0)->DSV,
        "placed texture supports depth views");
    {
        bool reached = false;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "CreateTextureDSV") { reached = true; return nri::Result::FAILURE; } return {};
        });
        const auto old = resources.Get(0)->Lease.GetHandle();
        RENDER_CHECK(!resources.InitPlaced({RHITextureDesc::DepthStencil2D(8, 8)}) && reached &&
            resources.Get(0)->Lease.GetHandle() == old, "placed DSV failure rolls back without replacing prior depth");
    }
    return true;
}

static bool Native(RHIType backend, bool failSubmit, bool placed) {
    const auto allocation = placed ? RenderTransientFrame::AllocationMode::PlacedUnaliased : RenderTransientFrame::AllocationMode::Committed;
    auto& device = RHIDevice::Get();
    RHIDeviceDesc init; init.PreferredBackend = backend; init.AllowFallback = false; init.Headless = true;
    init.EnableNRIValidation = true; init.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    RENDER_CHECK(device.Init(init) && !device.IsSoftwareAdapter(), "physical transient fixture and validation");
    if (placed) RENDER_CHECK(PlacementOwnership(), "placed resource ownership and rollback proof");
    Fixture fixture; RENDER_CHECK(fixture.Init(), "native graph fixture");
    RenderTransientFrame frame;
    RENDER_CHECK(frame.Prepare(fixture.Plan, fixture.Imports, allocation) && frame.GetPublishedGeneration() == 0,
        "native allocation stays pending before submission");
    const auto actualBytes = frame.GetPlan()->RequestedBytes;
    RENDER_CHECK(actualBytes >= fixture.Plan.RequestedBytes, "native plan uses queried requirements");
    auto submittedLease = frame.Get(fixture.TempImage)->Lease;
    RENDER_CHECK(fixture.Record(frame), "record native graph");
    if (failSubmit) {
        ScopedRHIFault fault([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "QueueSubmit") return nri::Result::FAILURE; return {};
        });
        RENDER_CHECK(!frame.Submit() && frame.IsPoisoned() && !frame.Abandon() && !frame.Submit() &&
            frame.GetPublishedGeneration() == 0 && device.GetResources().IsInUse(submittedLease),
            "ambiguous submit quarantines transitive generations and blocks retry");
    } else {
        RENDER_CHECK(frame.Submit() && frame.GetPublishedGeneration() == 1 && frame.GetCompletion(), "publish only after successful submit");
        RENDER_CHECK(device.WaitForCompletion(frame.GetCompletion()) && fixture.CheckPixels(), "real transient readback after checked completion");
        const auto old = frame.Get(fixture.TempImage)->Lease.GetHandle();
        const std::vector<const char*> boundaries = placed ?
            std::vector<const char*>{"CreateBuffer", "AllocateTransientMemory", "BindBufferMemory", "CreateTexture", "BindTextureMemory", "CreateTextureSRV", "CreateTextureRTV", "CreateCommandBuffer"} :
            std::vector<const char*>{"CreateBuffer", "AllocateBufferMemory", "BindBufferMemory", "CreateTexture", "AllocateTextureMemory", "BindTextureMemory", "CreateTextureSRV", "CreateTextureRTV", "CreateCommandBuffer"};
        for (const auto* boundary : boundaries) {
            unsigned reached = 0;
            ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == boundary) { ++reached; return nri::Result::FAILURE; } return {};
            });
            RENDER_CHECK(!frame.Prepare(fixture.Plan, fixture.Imports, allocation) && reached == 1 &&
                frame.GetPublishedGeneration() == 1 && frame.Get(fixture.TempImage)->Lease.GetHandle() == old,
                "every native candidate failure preserves published generation");
        }
        RENDER_CHECK(frame.Prepare(fixture.Plan, fixture.Imports, allocation) &&
            !frame.Record(1, [](auto&) { return true; }) && !frame.Submit() && frame.Abandon() &&
            frame.Get(fixture.TempImage)->Lease.GetHandle() == old, "wrong pass order abandons only candidate");
        RENDER_CHECK(frame.Prepare(fixture.Plan, fixture.Imports, allocation) &&
            !frame.Record(0, [](auto&) -> bool { throw std::runtime_error("injected callback"); }) &&
            !frame.Submit() && frame.Abandon(), "callback exception cannot publish partial graph");
        auto mismatched = fixture.Imports; mismatched[0].View.Description = RHIBufferDesc::Vertex(4);
        RENDER_CHECK(!frame.Prepare(fixture.Plan, mismatched, allocation) && frame.GetPublishedGeneration() == 1, "mismatched import rejects");
        auto forged = fixture.Plan;
        forged.Resources[fixture.Input].Description = mismatched[0].View.Description;
        RENDER_CHECK(!frame.Prepare(forged, mismatched, allocation), "matching forged metadata cannot override actual imported native description");
        bool wrongOwner = true;
        std::thread thread([&] { wrongOwner = frame.Prepare(fixture.Plan, fixture.Imports, allocation); }); thread.join();
        RENDER_CHECK(!wrongOwner, "wrong-owner native allocation rejects");
        RENDER_CHECK(frame.Prepare(fixture.Plan, fixture.Imports, allocation) && fixture.Record(frame) && frame.Submit() &&
            frame.GetPublishedGeneration() == 2 && device.WaitForCompletion(frame.GetCompletion()) && fixture.CheckPixels(),
            "replacement generation renders after rollback tests");
        RHIResourceLease retained;
        RHICompletionToken token;
        {
            RenderTransientFrame temporary;
            RENDER_CHECK(temporary.Prepare(fixture.Plan, fixture.Imports, allocation) && fixture.Record(temporary), "lifetime fixture recording");
            retained = temporary.Get(fixture.TempImage)->Lease;
            RENDER_CHECK(temporary.Submit(), "lifetime fixture submit"); token = temporary.GetCompletion();
        }
        RENDER_CHECK(retained.IsLive() && device.WaitForCompletion(token), "submission retains generation after graph wrapper destruction");
        std::cout << "Committed transient bytes: " << actualBytes << '\n';
    }
    RENDER_CHECK(device.WaitIdle(), "transient fixture bounded drain");
    RENDER_CHECK(device.Shutdown() && !frame.Get(fixture.TempImage), "closed device epoch invalidates graph views");
    return true;
}

int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting(); Log::Init();
    const std::string mode = argc > 1 ? argv[1] : "policy";
    bool ok = true;
    try {
        if (mode == "policy") {
            RenderTransientFrame frame; Graph::Plan empty;
            ok = !frame.Prepare(empty) && !frame.Submit() && !frame.Get(0);
        } else ok = Native(mode.find("vulkan") == 0 ? RHIType::Vulkan : RHIType::Direct3D12,
            mode.find("fail_submit") != std::string::npos, mode.find("placed") != std::string::npos);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; ok = false; }
    RHIDevice::Get().Shutdown();
    ok = Axle::RenderTests::Check(GetRHIValidationErrorCount() == 0, "native validation including transient teardown", __FILE__, __LINE__) && ok;
    std::cout << "Transient graph: " << (ok ? "PASS" : "FAIL") << '\n'; return ok ? 0 : 1;
}
