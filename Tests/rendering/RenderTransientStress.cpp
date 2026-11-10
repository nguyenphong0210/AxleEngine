#include "RenderTestCommon.hpp"
#include <Axle/RenderTransientFrame.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>

using namespace Axle;
using namespace Axle::RHI;
namespace {
    using G = RenderTransientGraph;
    using Mode = RenderTransientFrame::AllocationMode;
    struct Readback {
        RHIBuffer Upload, Image, Words;
        nri::TextureDataLayoutDesc Layout{};
        uint32_t Width = 0, Height = 0;
        std::vector<uint32_t> Pattern;
        RHICompletionToken Completion;
        std::array<RHIResourceHandle, 4> Handles;
        bool Check() {
            auto* data = static_cast<const uint8_t*>(Image.Map());
            if (!data) return false;
            bool valid = true;
            for (uint32_t i = 0; i < 2; ++i) for (uint32_t y = 0; y < Height; ++y) for (uint32_t x = 0; x < Width; ++x) {
                const auto* p = data + i * Layout.slicePitch + y * Layout.rowPitch + x * 4;
                valid &= p[0] == (i ? 0 : 255) && p[1] == (i ? 255 : 0) && p[2] == 0 && p[3] == 255;
            }
            Image.Unmap();
            const auto* words = Words.Map(); valid &= words && std::memcmp(words, Pattern.data(), Pattern.size() * 4) == 0;
            Words.Unmap(); return valid;
        }
    };
    struct Fixture {
        G::Plan Plan;
        std::vector<RenderTransientFrame::Import> Imports;
        Readback Output;
        uint64_t BufferBytes = 0;
        bool Init(uint32_t seed) {
            Output.Width = 8 + seed % 3 * 8; Output.Height = 8 + seed % 2 * 8;
            BufferBytes = 256 + seed % 4 * 64;
            Output.Pattern.resize(BufferBytes / 2);
            for (uint32_t i = 0; i < Output.Pattern.size(); ++i) Output.Pattern[i] = seed * 0x10001u + i * 13u;
            if (!Output.Upload.Init(RHIBufferDesc::Vertex(BufferBytes * 2, RHIMemoryLocation::HostUpload))) return false;
            auto* mapped = Output.Upload.Map(); if (!mapped) return false;
            std::memcpy(mapped, Output.Pattern.data(), BufferBytes * 2); Output.Upload.Unmap();
            const auto& alignment = RHIDevice::Get().GetNRICore().GetDeviceDesc(*RHIDevice::Get().GetNRIDevice()).memoryAlignment;
            auto align = [](uint32_t n, uint32_t a) { return (n + a - 1) / a * a; };
            Output.Layout.rowPitch = align(Output.Width * 4, alignment.uploadBufferTextureRow);
            Output.Layout.slicePitch = align(Output.Layout.rowPitch * Output.Height, alignment.uploadBufferTextureSlice);
            RHIBufferDesc readback; readback.MemoryLocation = RHIMemoryLocation::HostReadback;
            readback.Size = Output.Layout.slicePitch * 2; if (!Output.Image.Init(readback)) return false;
            readback.Size = BufferBytes * 2; if (!Output.Words.Init(readback)) return false;
            G graph;
            for (uint32_t i = 0; i < 4; ++i) {
                G::ResourceDesc r; r.Name = "scratch " + std::to_string(i); r.AllowAlias = true; r.Memory = {BufferBytes, 1};
                r.Type = i < 2 ? G::Kind::TransientTexture : G::Kind::TransientBuffer;
                auto texture = RHITextureDesc::RenderTarget2D(Output.Width, Output.Height);
                texture.Usage = texture.Usage | RHITextureUsage::CopySource;
                r.Description = i < 2 ? RHITransientDesc{texture} : RHITransientDesc{RHIBufferDesc::Vertex(BufferBytes)};
                r.Memory.SizeBytes = i < 2 ? uint64_t(Output.Width) * Output.Height * 4 : BufferBytes;
                graph.AddResource(r);
            }
            for (const auto& view : {MakeTransientView(Output.Upload), MakeTransientView(Output.Image), MakeTransientView(Output.Words)}) {
                G::ResourceDesc r; r.Name = "import " + std::to_string(Imports.size()); r.Description = view.Description;
                r.ContentsDefined = Imports.empty(); Imports.push_back({graph.AddResource(r), view});
            }
            using U = G::Usage; using A = G::Access;
            for (uint32_t i = 0; i < 2; ++i) {
                graph.AddPass({"clear " + std::to_string(i), {{i, U::ColorAttachment, A::Write, G::Load::Clear},
                    {i + 2, U::CopyDest, A::Write}, {4, U::CopySource}}, i ? std::vector<uint32_t>{1} : std::vector<uint32_t>{}});
                graph.AddPass({"export " + std::to_string(i), {{i, U::CopySource}, {i + 2, U::CopySource}, {5, U::CopyDest, A::Write}, {6, U::CopyDest, A::Write}}});
            }
            std::string error;
            if (!graph.Compile(Plan, &error)) { std::cerr << error << '\n'; return false; }
            return true;
        }
        bool Record(RenderTransientFrame& frame, uint32_t pass, RHIResourceLease marker = {}) {
            return frame.Record(pass, [&](RHICommandBuffer& commands) {
                if (marker && !commands.KeepAlive(marker)) return false;
                const auto i = pass / 2;
                if (!(pass % 2)) {
                    nri::AttachmentDesc attachment{}; attachment.descriptor = frame.Get(i)->RTV;
                    attachment.loadOp = nri::LoadOp::CLEAR; attachment.storeOp = nri::StoreOp::STORE;
                    attachment.clearValue.color.f = i ? nri::Color32f{0, 1, 0, 1} : nri::Color32f{1, 0, 0, 1};
                    nri::RenderingDesc rendering{}; rendering.colors = &attachment; rendering.colorNum = 1;
                    commands.BeginRendering(rendering); commands.EndRendering();
                    commands.CopyBuffer(*frame.Get(i + 2)->Buffer, 0, *frame.Get(4)->Buffer, i * BufferBytes, BufferBytes);
                } else {
                    nri::TextureRegionDesc region{}; region.width = Output.Width; region.height = Output.Height; region.depth = 1;
                    auto layout = Output.Layout; layout.offset = i * Output.Layout.slicePitch;
                    commands.ReadbackTextureToBuffer(*frame.Get(5)->Buffer, layout, *frame.Get(i)->Texture, region);
                    commands.CopyBuffer(*frame.Get(6)->Buffer, i * BufferBytes, *frame.Get(i + 2)->Buffer, 0, BufferBytes);
                }
                return true;
            });
        }
    };
}

bool RunRenderTransientStress(RHIType backend, const std::string& terminal) {
    auto& device = RHIDevice::Get();
    RHIResourceHandle stale; RHICompletionToken staleCompletion;
    uint64_t created = 0; auto retired = std::make_shared<std::atomic<uint64_t>>(0);
    uint32_t random = 0xC110600u;
    const unsigned iterations = terminal == "normal" ? 1024 : 64;
    for (unsigned epoch = 0; epoch < 3; ++epoch) {
        RHIDeviceDesc init; init.PreferredBackend = backend; init.AllowFallback = false; init.Headless = true;
        init.EnableNRIValidation = init.EnableValidation = true;
        RENDER_CHECK(RHIDevice::IsGraphicsAPIValidationAvailable(backend) && device.Init(init) && !device.IsSoftwareAdapter(), "strict physical native stress with validation");
        RENDER_CHECK(!device.GetResources().Acquire(stale) && !device.WaitForCompletion(staleCompletion, 0) &&
            !device.GetCapabilities().SupportsTransientResourceAliasing, "new epoch rejects old handles/completion and resets alias capability");
        RENDER_CHECK(RHITransientResources::ProbeAliasing().Supported, "each fresh epoch proves native aliasing again");
        std::deque<Readback> pending;
        auto drain = [&] {
            if (pending.empty()) return true;
            {
                ScopedRHIFault busy([](std::string_view op) -> std::optional<nri::Result> {
                    if (op == "WaitFence") return nri::Result::NOT_READY; return {};
                });
                if (device.WaitForCompletion(pending.back().Completion, 0) || pending.back().Words.Map() ||
                    !device.GetResources().Acquire(pending.back().Handles[0]).IsLive()) return false;
            }
            if (!device.WaitForCompletion(pending.back().Completion)) return false;
            for (auto& output : pending) {
                if (!output.Check()) return false;
                for (const auto handle : output.Handles) if (device.GetResources().Acquire(handle)) return false;
                stale = output.Handles[0]; staleCompletion = output.Completion;
            }
            pending.clear(); return true;
        };
        for (unsigned iteration = 0; iteration < iterations; ++iteration) {
            if (pending.size() == 3) RENDER_CHECK(drain(), "bounded three-frame batch verifies pixels/words and retirement before reuse");
            random = random * 1664525u + 1013904223u;
            Fixture fixture; RENDER_CHECK(fixture.Init(random >> 16), "size-varying texture/buffer/import declarations");
            const auto mode = iteration % 3 == 0 ? Mode::Committed : iteration % 3 == 1 ? Mode::PlacedUnaliased : Mode::AliasingIfValidated;
            RenderTransientFrame frame;
            if (iteration % 7 == 0) {
                bool hit = false;
                ScopedRHIFault fail([&](std::string_view op) -> std::optional<nri::Result> {
                    if (op == "CreateTextureRTV") { hit = true; return nri::Result::OUT_OF_MEMORY; } return {};
                });
                RENDER_CHECK(!frame.Prepare(fixture.Plan, fixture.Imports, mode) && hit && !frame.GetPublishedGeneration(), "native creation failure cannot publish a partial graph");
            }
            RENDER_CHECK(frame.Prepare(fixture.Plan, fixture.Imports, mode), "fresh candidate after recoverable allocation failure");
            if (iteration % 11 == 0) {
                RENDER_CHECK(!frame.Record(0, [](auto&) -> bool { throw std::runtime_error("abandoned stress pass"); }) &&
                    !frame.Submit() && frame.Abandon() && frame.Prepare(fixture.Plan, fixture.Imports, mode), "callback failure rolls back recording before reuse");
            }
            if (mode == Mode::AliasingIfValidated && iteration % 5 == 0) {
                RENDER_CHECK(fixture.Record(frame, 0) && fixture.Record(frame, 1), "record first alias occupant before activation failure");
                bool hit = false;
                {
                    ScopedRHIFault fail([&](std::string_view op) -> std::optional<nri::Result> {
                        if (op == "TransientAliasBarrier") { hit = true; return nri::Result::FAILURE; } return {};
                    });
                    RENDER_CHECK(!fixture.Record(frame, 2) && hit && !frame.Submit(), "failed alias activation cannot submit partial occupants");
                }
                RENDER_CHECK(frame.Abandon() && frame.Prepare(fixture.Plan, fixture.Imports, mode), "alias failure is recoverable before submit");
            }
            if (mode == Mode::AliasingIfValidated) RENDER_CHECK(frame.GetDiagnostics().AliasEventCount == 2, "proven stress mode actually aliases both classes");
            for (const auto& life : frame.GetPlan()->Lifetimes) {
                // Packed getters expose only the current pass; native handles for
                // later occupants are captured while recording their declared use.
                if (const auto* view = frame.Get(life.Id)) fixture.Output.Handles[life.Id] = view->Lease.GetHandle();
            }
            auto marker = device.GetResources().Publish(RHIResourceKind::Memory, "stress retirement", {}, [retired] { ++*retired; }); ++created;
            for (uint32_t pass = 0; pass < 4; ++pass) {
                const auto occupant = pass / 2;
                if (!(pass % 2)) for (const auto id : {occupant, occupant + 2}) fixture.Output.Handles[id] = frame.Get(id)->Lease.GetHandle();
                RENDER_CHECK(fixture.Record(frame, pass, marker), "actual GPU clear/copy/readback workload");
                if (pass == 0 && iteration % 9 == 0) {
                    const auto old = fixture.Output.Upload.GetHandle();
                    RENDER_CHECK(fixture.Output.Upload.Init(RHIBufferDesc::Vertex(fixture.BufferBytes * 2, RHIMemoryLocation::HostUpload)) &&
                        fixture.Output.Upload.GetHandle() != old && device.GetResources().Acquire(old).IsLive(), "import reload while recording retains the exact old native generation");
                }
            }
            RENDER_CHECK(frame.Submit(), "stress submission publishes a real completion");
            fixture.Output.Completion = frame.GetCompletion();
            fixture.Imports.clear(); marker.Reset();
            pending.push_back(std::move(fixture.Output));
        }
        RENDER_CHECK(drain(), "all delayed native readbacks match and transient generations retire");
        RENDER_CHECK(device.WaitIdle() && retired->load() == created, "every submitted retirement marker releases exactly once");
        if (terminal != "normal") {
            Fixture fixture; RENDER_CHECK(fixture.Init(epoch + 1), "terminal failure fixture");
            RenderTransientFrame frame;
            RENDER_CHECK(frame.Prepare(fixture.Plan, fixture.Imports, Mode::AliasingIfValidated), "terminal alias candidate");
            for (uint32_t p = 0; p < 4; ++p) RENDER_CHECK(fixture.Record(frame, p), "terminal recording");
            const auto before = device.GetLastSubmittedToken();
            {
                ScopedRHIFault fail([&](std::string_view op) -> std::optional<nri::Result> {
                    if (op == "QueueSubmit") return terminal == "lost" ? nri::Result::DEVICE_LOST : nri::Result::FAILURE;
                    return {};
                });
                RENDER_CHECK(!frame.Submit() && frame.IsPoisoned() && !frame.Abandon() && !frame.Submit() &&
                    device.GetLastSubmittedToken() == before, "terminal submit cannot publish completion or recycle ambiguous alias storage");
            }
            RENDER_CHECK(!device.CanSubmit() && (terminal != "lost" || device.GetState() == RHIDeviceState::Lost), "simulated loss/failed submit stops new admission");
            // Injection happened before native submission, after a checked drain.
            RENDER_CHECK(device.Shutdown(), "bounded teardown resolves simulated terminal epoch");
        } else RENDER_CHECK(device.Shutdown(), "normal stress epoch teardown");
        RENDER_CHECK(!device.GetResources().Acquire(stale), "closed epoch rejects retired identities");
    }
    std::cout << "Transient stress: " << iterations * 3 << " frames, 3 epochs, seed=0xC110600; retirement markers " << retired->load() << '/' << created << '\n';
    return true;
}
