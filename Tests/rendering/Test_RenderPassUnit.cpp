#include "RenderTestCommon.hpp"
#include <Axle/RenderPass.hpp>

bool RunRenderPassUnitTests() {
    using namespace Axle;
    std::cout << "  [RenderPass] Running pass lifecycle and orchestration tests...\n";

    RenderPassScheduler scheduler;
    std::vector<std::string> events;
    auto make = [&](const char* name) {
        const std::string passName = name;
        return std::make_unique<CallbackRenderPass>(
            passName,
            [&, passName](const RenderPassContext&) { events.emplace_back(passName + ":prepare"); return RenderPassResult{}; },
            [&, passName](const RenderPassContext&) { events.emplace_back(passName + ":record"); return RenderPassResult{}; },
            [passName](RenderGraph& graph) { graph.AddPass(passName + ":declare", {}); });
    };
    scheduler.Add(make("Opaque"));
    scheduler.Add(make("Post"));
    const auto names = scheduler.GetPassNames();
    RENDER_CHECK(names.size() == 2 && names[0] == "Opaque" && names[1] == "Post",
        "scheduler preserves explicit pass order");

    RenderPassContext context;
    RENDER_CHECK(static_cast<bool>(scheduler.Configure(context)), "all passes configure successfully");
    RENDER_CHECK(static_cast<bool>(scheduler.Prepare(context)), "all passes prepare successfully");
    RenderGraph graph;
    scheduler.Declare(graph);
    RENDER_CHECK(graph.Compile(), "pass declarations compile through the graph boundary");
    RENDER_CHECK(static_cast<bool>(scheduler.Record(context)), "all passes record successfully");
    scheduler.Finalize(context, {});
    RENDER_CHECK(events.size() == 4 && events[0] == "Opaque:prepare" && events[1] == "Post:prepare" &&
                 events[2] == "Opaque:record" && events[3] == "Post:record",
        "prepare and record phases follow the same deterministic order");

    events.clear();
    RENDER_CHECK(static_cast<bool>(scheduler.RecordOne("Post", context)) &&
                 events.size() == 1 && events[0] == "Post:record",
        "named group recording can be dispatched at the live renderer boundary");

    RenderPassScheduler failing;
    failing.Add(std::make_unique<CallbackRenderPass>("Failing", [](const RenderPassContext&) {
        return RenderPassResult::Failed("synthetic failure");
    }));
    const auto result = failing.Prepare(context);
    RENDER_CHECK(!result && failing.GetDiagnostic().find("Failing") != std::string::npos,
        "failed pass stops the schedule and preserves pass diagnostic");

    std::cout << "  [RenderPass] Pass lifecycle and orchestration tests passed.\n";
    return true;
}
