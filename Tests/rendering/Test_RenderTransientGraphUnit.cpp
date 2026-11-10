#include "RenderTestCommon.hpp"
#include <Axle/RenderTransientGraph.hpp>
#include <algorithm>
#include <random>

namespace Axle::RenderTests {
    namespace {
        using Graph = RenderTransientGraph;
        using Usage = Graph::Usage;
        using Access = Graph::Access;

        Graph::ResourceDesc Texture(std::string name, bool allowAlias = false) {
            Graph::ResourceDesc desc;
            desc.Name = std::move(name); desc.Type = Graph::Kind::TransientTexture;
            auto texture = RHI::RHITextureDesc::RenderTarget2D(4, 4);
            texture.Usage = texture.Usage | RHI::RHITextureUsage::CopySource | RHI::RHITextureUsage::CopyDest;
            desc.Description = texture;
            desc.Memory = {64, 64, 64, 3, false};
            desc.AllowAlias = allowAlias;
            return desc;
        }
        Graph::ResourceDesc Buffer(std::string name, uint64_t bytes = 64, uint64_t alignment = 64) {
            Graph::ResourceDesc desc;
            desc.Name = std::move(name); desc.Type = Graph::Kind::TransientBuffer;
            desc.Description = RHI::RHIBufferDesc::Structured(bytes / 4, 4, true);
            desc.Memory = {bytes, alignment, 1, 4, false};
            return desc;
        }
        Graph::Use Clear(Graph::Resource id) {
            return {id, Usage::ColorAttachment, Access::Write, Graph::Load::Clear};
        }
        Graph::Use CopyWrite(Graph::Resource id) { return {id, Usage::CopyDest, Access::Write}; }

        bool SamePlan(const Graph::Plan& a, const Graph::Plan& b) {
            return a.PassOrder == b.PassOrder && a.Dependencies == b.Dependencies &&
                a.Lifetimes == b.Lifetimes && a.Allocations == b.Allocations &&
                a.RequestedBytes == b.RequestedBytes && a.PlannedAllocationBytes == b.PlannedAllocationBytes &&
                a.PlannedPeakBytes == b.PlannedPeakBytes && a.IgnoredAliasRequests == b.IgnoredAliasRequests &&
                a.Diagnostic == b.Diagnostic && a.Resources.size() == b.Resources.size() && a.Passes.size() == b.Passes.size();
        }

        bool TestPlansAndLifetimes() {
            Graph graph;
            const auto a = graph.AddResource(Texture("A", true));
            const auto b = graph.AddResource(Texture("B", true));
            auto imported = Texture("external");
            imported.Type = Graph::Kind::Imported; imported.ContentsDefined = true;
            imported.Memory = {UINT64_MAX, 0, 0, 0, false}; // not allocator input
            const auto external = graph.AddResource(imported);
            const auto buffer = graph.AddResource(Buffer("buffer"));
            graph.AddPass({"clear A", {Clear(a), CopyWrite(buffer)}});
            graph.AddPass({"sample A", {{a}, {external}, {buffer, Usage::Storage, Access::ReadWrite}}});
            graph.AddPass({"clear B", {Clear(b)}, {1}});
            graph.AddPass({"sample B", {{b}, {buffer, Usage::ShaderResource}}, {2}});
            Graph::Plan plan;
            std::string error = "old error";
            RENDER_CHECK(graph.Compile(plan, &error) && error.empty(), "transient texture/buffer graph compiles without a device");
            RENDER_CHECK(plan.PassOrder == std::vector<uint32_t>({0, 1, 2, 3}) && plan.Lifetimes.size() == 3,
                "stable pass order and imported resource exclusion");
            RENDER_CHECK(plan.Lifetimes[0].Id == a && plan.Lifetimes[0].FirstPass == 0 && plan.Lifetimes[0].LastPass == 1 &&
                plan.Lifetimes[1].Id == b && plan.Lifetimes[1].FirstPass == 2 && plan.Lifetimes[1].LastPass == 3 &&
                plan.Lifetimes[2].Id == buffer && plan.Lifetimes[2].FirstPass == 0 && plan.Lifetimes[2].LastPass == 3,
                "lifetimes include all reads/writes as closed execution-order intervals");
            RENDER_CHECK(plan.Allocations.size() == 3 && plan.Allocations[0].Block == 0 &&
                plan.Allocations[1].Block == 1 && plan.Allocations[2].Block == 2,
                "both overlapping and disjoint lifetimes use distinct unaliased blocks");
            RENDER_CHECK(plan.RequestedBytes == 192 && plan.PlannedAllocationBytes == 192 && plan.PlannedPeakBytes == 192 &&
                plan.IgnoredAliasRequests == 2 && plan.Diagnostic.find("disabled") != std::string::npos,
                "unaliased statistics exclude imports and explicitly report ignored alias requests");
            RENDER_CHECK(plan.Lifetimes[0].Key.MemoryType == 3 && plan.Lifetimes[2].Key.MemoryType == 4 &&
                plan.Lifetimes[0].Key != plan.Lifetimes[2].Key,
                "compatibility retains resource kind, description and memory metadata");
            Graph::Plan again;
            RENDER_CHECK(graph.Compile(again) && SamePlan(plan, again), "repeated compile is deterministic");
            const auto snapshot = plan;
            graph.AddPass({"bad", {{99}}});
            RENDER_CHECK(!graph.Compile(plan, &error) && !error.empty() && SamePlan(plan, snapshot),
                "failed compilation preserves the last candidate plan");
            RENDER_CHECK(plan.Passes.size() == 4 && plan.Resources[0].Name == "A",
                "published CPU values own their descriptions independently of graph edits");

            Graph touching;
            const auto left = touching.AddResource(Texture("left"));
            const auto right = touching.AddResource(Texture("right"));
            touching.AddPass({"first", {Clear(left)}});
            touching.AddPass({"boundary", {{left}, Clear(right)}});
            RENDER_CHECK(touching.Compile(plan) && plan.Lifetimes[0].LastPass == plan.Lifetimes[1].FirstPass &&
                plan.Allocations[0].Block != plan.Allocations[1].Block, "resources touching in one pass have overlapping closed lifetimes");

            Graph forward;
            forward.AddPass({"consumer", {}, {2, 2}});
            forward.AddPass({"independent", {}});
            forward.AddPass({"producer", {}});
            RENDER_CHECK(forward.Compile(plan) && plan.PassOrder == std::vector<uint32_t>({1, 2, 0}) &&
                plan.Dependencies[0] == std::vector<uint32_t>{2}, "Kahn ordering permits forward references and deduplicates edges");
            Graph empty;
            RENDER_CHECK(empty.Compile(plan) && plan.PassOrder.empty() && plan.Allocations.empty() && plan.PlannedPeakBytes == 0,
                "empty graph has a valid zero-allocation plan");
            return true;
        }

        bool TestHazards() {
            Graph graph;
            const auto id = graph.AddResource(Buffer("shared"));
            graph.AddPass({"initialize", {CopyWrite(id)}});
            graph.AddPass({"reader1", {{id, Usage::Storage}}});
            graph.AddPass({"reader2", {{id, Usage::Storage}}});
            graph.AddPass({"writer", {{id, Usage::Storage, Access::Write}}, {0}});
            graph.AddPass({"read-modify-write", {{id, Usage::Storage, Access::ReadWrite}}});
            Graph::Plan plan;
            RENDER_CHECK(graph.Compile(plan), "RAW/WAR/WAW graph with read-modify-write is valid");
            RENDER_CHECK(plan.Dependencies[1] == std::vector<uint32_t>{0} &&
                plan.Dependencies[2] == std::vector<uint32_t>{0} &&
                plan.Dependencies[3] == std::vector<uint32_t>({0, 1, 2}) &&
                plan.Dependencies[4] == std::vector<uint32_t>{3}, "hazards preserve all prior readers and never add a self-edge for read-write");
            return true;
        }

        bool Reject(const std::vector<Graph::ResourceDesc>& resources, const std::vector<Graph::Pass>& passes) {
            Graph graph;
            for (const auto& resource : resources) graph.AddResource(resource);
            for (const auto& pass : passes) graph.AddPass(pass);
            Graph::Plan plan; plan.RequestedBytes = 777; plan.Diagnostic = "preserved";
            std::string error;
            return !graph.Compile(plan, &error) && !error.empty() && plan.RequestedBytes == 777 && plan.Diagnostic == "preserved";
        }

        bool TestInvalidGraphs() {
            const auto t = Texture("image");
            const Graph::Pass clear{"clear", {Clear(0)}};
            RENDER_CHECK(Reject({}, {{"cycle A", {}, {1}}, {"cycle B", {}, {0}}}) &&
                Reject({}, {{"self", {}, {0}}}) && Reject({}, {{"missing", {}, {3}}}),
                "cycles, self-edges and undefined dependencies fail without modifying output");
            RENDER_CHECK(Reject({}, {{"same", {}}, {"same", {}}}) && Reject({}, {{"", {}}}) &&
                Reject({t, t}, {clear}) && Reject({}, {{"missing resource", {{0}}}}), "invalid names and IDs are rejected");
            RENDER_CHECK(Reject({t}, {{"duplicate", {Clear(0), Clear(0)}}}) &&
                Reject({t}, {{"compute", {Clear(0)}, {}, Graph::Queue::Compute}}) &&
                Reject({t}, {{"copy queue", {Clear(0)}, {}, Graph::Queue::Copy}}), "duplicate uses and unsupported queues are rejected");
            RENDER_CHECK(Reject({t}, {{"read first", {{0}}}}) &&
                Reject({t}, {{"load first", {{0, Usage::ColorAttachment, Access::Write, Graph::Load::Load}}}}) &&
                Reject({t}, {{"discard", {{0, Usage::ColorAttachment, Access::Write, Graph::Load::Clear, Graph::Store::Discard}}},
                    {"read discarded", {{0}}}}), "undefined, LOAD-first and discarded contents cannot be read");
            RENDER_CHECK(Reject({t}, {{"no load op", {{0, Usage::ColorAttachment, Access::Write}}}}) &&
                Reject({t}, {{"shader write", {{0, Usage::ShaderResource, Access::Write}}}}) &&
                Reject({t}, {{"vertex texture", {{0, Usage::VertexBuffer}}}}) &&
                Reject({Buffer("buffer")}, {{"color buffer", {Clear(0)}}}), "resource kind, layout/access and attachment declarations are checked");
            RENDER_CHECK(Reject({t}, {clear, {"discard read", {{0, Usage::ShaderResource, Access::Read, Graph::Load::None, Graph::Store::Discard}}}}) &&
                Reject({t}, {{"enum", {{0, static_cast<Usage>(255), Access::Write}}}}) &&
                Reject({t}, {{"access enum", {{0, Usage::Storage, static_cast<Access>(255)}}}}) &&
                Reject({t}, {}), "read discard, unknown enums and unused transients are rejected");
            auto bad = t; bad.Type = Graph::Kind::TransientBuffer;
            RENDER_CHECK(Reject({bad}, {clear}), "descriptor variant must match resource kind");
            bad = t; bad.ContentsDefined = true;
            RENDER_CHECK(Reject({bad}, {clear}), "transient contents cannot be declared preinitialized");
            bad = t; bad.Type = Graph::Kind::Imported; bad.AllowAlias = true;
            RENDER_CHECK(Reject({bad}, {clear}), "imported resources cannot opt into aliasing");
            bad = Buffer("upload"); std::get<RHI::RHIBufferDesc>(bad.Description).MemoryLocation = RHI::RHIMemoryLocation::HostUpload;
            RENDER_CHECK(Reject({bad}, {{"write", {CopyWrite(0)}}}), "first transient allocator is device-local only");
            auto restricted = t; std::get<RHI::RHITextureDesc>(restricted.Description).Usage = RHI::RHITextureUsage::ShaderResource;
            RENDER_CHECK(Reject({restricted}, {clear}), "pass use must be permitted by resource usage flags");
            return true;
        }

        bool TestRequirementsAndArithmetic() {
            auto t = Texture("texture");
            const Graph::Pass clear{"clear", {Clear(0)}};
            for (auto bytes : {uint64_t{0}, uint64_t{63}}) {
                auto bad = t; bad.Memory.SizeBytes = bytes;
                RENDER_CHECK(Reject({bad}, {clear}), "zero and undersized requirements reject");
            }
            for (auto alignment : {uint64_t{0}, uint64_t{3}}) {
                auto bad = t; bad.Memory.Alignment = alignment;
                RENDER_CHECK(Reject({bad}, {clear}), "alignment must be a nonzero power of two");
                bad = t; bad.Memory.Granularity = alignment;
                RENDER_CHECK(Reject({bad}, {clear}), "granularity must be a nonzero power of two");
            }
            auto bad = t; bad.Memory.SizeBytes = UINT64_MAX;
            RENDER_CHECK(Reject({bad}, {clear}), "alignment overflow rejects before creating a plan");
            bad = t; auto& image = std::get<RHI::RHITextureDesc>(bad.Description);
            image.Width = image.Height = image.Depth = UINT32_MAX;
            image.Usage = RHI::RHITextureUsage::ShaderResource;
            bad.Memory = {UINT64_MAX, 1, 1, 0, false};
            RENDER_CHECK(Reject({bad}, {{"write", {CopyWrite(0)}}}), "texture payload arithmetic overflow rejects");
            bad = t; std::get<RHI::RHITextureDesc>(bad.Description).Width = 0;
            RENDER_CHECK(Reject({bad}, {clear}), "zero-sized textures reject");
            bad = t; std::get<RHI::RHITextureDesc>(bad.Description).MipLevels = 4;
            RENDER_CHECK(Reject({bad}, {clear}), "mip count cannot exceed the dimension chain");
            bad = t; std::get<RHI::RHITextureDesc>(bad.Description).SampleCount = 3;
            RENDER_CHECK(Reject({bad}, {clear}), "invalid sample count rejects");
            bad = t; std::get<RHI::RHITextureDesc>(bad.Description).Format = RHI::RHITextureFormat::D32_FLOAT;
            RENDER_CHECK(Reject({bad}, {clear}), "depth formats cannot use color attachment descriptions");
            auto largeA = Buffer("A"); largeA.Memory = {UINT64_MAX, 1, 1, 0, false};
            auto largeB = Buffer("B"); largeB.Memory = {64, 1, 1, 0, false};
            RENDER_CHECK(Reject({largeA, largeB}, {{"write both", {CopyWrite(0), CopyWrite(1)}}}), "total requested byte overflow rejects");
            largeA.Memory = {UINT64_MAX - 127, 64, 1, 0, false};
            largeB.Memory = {65, 64, 1, 0, false};
            RENDER_CHECK(Reject({largeA, largeB}, {{"write both", {CopyWrite(0), CopyWrite(1)}}}), "aligned allocation total overflow rejects");

            Graph graph;
            t.Memory = {65, 64, 256, 7, true};
            graph.AddResource(t); graph.AddPass(clear);
            Graph::Plan plan;
            RENDER_CHECK(graph.Compile(plan) && plan.RequestedBytes == 65 && plan.PlannedAllocationBytes == 256 &&
                plan.Lifetimes[0].Key.Granularity == 256 && plan.Lifetimes[0].Key.MustBeDedicated,
                "rounding respects granularity and preserves dedicated allocation restrictions");
            return true;
        }

        bool TestGeneratedGraphs() {
            // Reproducible data exercises lifetimes and topology against a separate
            // interval oracle rather than only matching compiler examples.
            std::mt19937 random(0xC1105);
            for (uint32_t trial = 0; trial < 120; ++trial) {
                Graph graph;
                const uint32_t count = 2 + random() % 12;
                const uint32_t passCount = 5 + random() % 20;
                std::vector<uint32_t> starts(count), ends(count);
                for (uint32_t r = 0; r < count; ++r) {
                    starts[r] = random() % passCount;
                    ends[r] = starts[r] + random() % (passCount - starts[r]);
                    graph.AddResource(Texture("resource" + std::to_string(r), true));
                }
                for (uint32_t p = 0; p < passCount; ++p) {
                    Graph::Pass pass; pass.Name = "pass" + std::to_string(p);
                    if (p) pass.Dependencies.push_back(p - 1);
                    for (uint32_t r = 0; r < count; ++r) {
                        if (p == starts[r]) pass.Uses.push_back(Clear(r));
                        else if (p == ends[r]) pass.Uses.push_back({r});
                    }
                    graph.AddPass(std::move(pass));
                }
                Graph::Plan plan, again;
                RENDER_CHECK(graph.Compile(plan) && graph.Compile(again) && SamePlan(plan, again), "generated graphs compile deterministically");
                RENDER_CHECK(plan.Allocations.size() == count && plan.PlannedPeakBytes == uint64_t(count) * 64,
                    "generated baseline never claims memory savings from disjoint lifetimes");
                for (uint32_t r = 0; r < count; ++r) {
                    RENDER_CHECK(plan.Lifetimes[r].FirstPass == starts[r] && plan.Lifetimes[r].LastPass == ends[r] &&
                        plan.Allocations[r].Block == r, "generated lifetimes match declaration-derived interval oracle");
                }
                for (uint32_t p = 0; p < passCount; ++p) {
                    RENDER_CHECK(plan.PassOrder[p] == p, "generated explicit dependencies preserve serial execution");
                    for (auto dependency : plan.Dependencies[p])
                        RENDER_CHECK(dependency < p, "all generated hazard edges precede their consumer");
                }
            }
            return true;
        }
    }

    bool RunRenderTransientGraphUnitTests() {
        std::cout << "  [TransientGraph] Running C11-05.1 CPU planner contracts...\n";
        if (!TestPlansAndLifetimes() || !TestHazards() || !TestInvalidGraphs() ||
            !TestRequirementsAndArithmetic() || !TestGeneratedGraphs()) return false;
        std::cout << "  [TransientGraph] All planner contracts and 120 generated graphs passed.\n";
        return true;
    }
}
