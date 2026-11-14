#include "RenderTestCommon.hpp"
#include <Axle/RenderPass.hpp>
#include <algorithm>
#include <stdexcept>

namespace {
    class TracedPass final : public Axle::IRenderPass {
    public:
        TracedPass(std::string name, std::vector<std::string>& events, std::shared_ptr<int> owner = {})
            : Name(std::move(name)), Events(events), Owner(std::move(owner)) {}
        const char* GetName() const override { return Name.c_str(); }
        Axle::RenderPassResult Configure(const Axle::RenderPassContext&) override {
            Events.push_back(Name + ":configure");
            if (ThrowAt == 0) throw std::runtime_error("configure interrupted");
            return ConfigureResult;
        }
        void Declare(Axle::RenderGraph& graph) override {
            Events.push_back(Name + ":declare");
            if (ThrowAt == 1) throw std::runtime_error("declare interrupted");
            if (InvalidRead) {
                const auto undefined = graph.Import("undefined", nullptr);
                graph.AddPass(Name, {{undefined, Axle::RHI::RHIResourceLayout::ShaderResource}});
            } else graph.AddPass(Name, {});
        }
        Axle::RenderPassResult Prepare(const Axle::RenderPassContext&) override {
            Events.push_back(Name + ":prepare");
            if (ThrowAt == 2) throw std::runtime_error("prepare interrupted");
            return PrepareResult;
        }
        Axle::RenderPassResult Record(const Axle::RenderPassContext&) override {
            Events.push_back(Name + ":record");
            if (ThrowAt == 3) throw std::runtime_error("record interrupted");
            return RecordResult;
        }
        void Finalize(const Axle::RenderPassContext&, const Axle::RenderPassResult& result) override {
            Events.push_back(Name + ":finalize:" + std::to_string(static_cast<int>(result.Status)));
        }
        Axle::RenderPassResult ConfigureResult, PrepareResult, RecordResult;
        bool InvalidRead = false;
        int ThrowAt = -1;
    private:
        std::string Name;
        std::vector<std::string>& Events;
        std::shared_ptr<int> Owner;
    };
}

bool RunRenderPassUnitTests() {
    using namespace Axle;
    std::cout << "  [RenderPass] Running pass lifecycle and orchestration tests...\n";
    RenderPassScheduler scheduler;
    std::vector<std::string> events;
    RenderPassContext context;
    RenderGraph graph;
    context.Graph = &graph;
    auto owner = std::make_shared<int>(42);
    std::weak_ptr<int> lifetime = owner;
    scheduler.Add(std::make_unique<TracedPass>("Opaque", events, owner));
    scheduler.Add(std::make_unique<TracedPass>("Post", events, owner));
    owner.reset();
    RENDER_CHECK(scheduler.GetPassNames() == std::vector<std::string>({"Opaque", "Post"}),
        "schedule preserves explicit order");
    const auto success = scheduler.Execute(context);
    RENDER_CHECK(success.Status == RenderPassStatus::Supported && scheduler.Size() == 0 && lifetime.expired(),
        "successful execution releases every frame capture before returning");
    RENDER_CHECK(events == std::vector<std::string>({
        "Opaque:configure", "Post:configure", "Opaque:declare", "Post:declare",
        "Opaque:prepare", "Post:prepare", "Opaque:record", "Post:record",
        "Opaque:finalize:0", "Post:finalize:0"}),
        "all lifecycle phases use the same order and finalize once");

    for (int phase = 0; phase < 4; ++phase) {
        events.clear(); graph.Reset();
        owner = std::make_shared<int>(phase); lifetime = owner;
        auto first = std::make_unique<TracedPass>("Failing", events, owner);
        if (phase == 0) first->ConfigureResult = RenderPassResult::Failed("configure failure");
        if (phase == 1) first->InvalidRead = true;
        if (phase == 2) first->PrepareResult = RenderPassResult::Failed("prepare failure");
        if (phase == 3) first->RecordResult = RenderPassResult::Failed("record failure");
        scheduler.Add(std::move(first));
        scheduler.Add(std::make_unique<TracedPass>("Later", events, owner));
        owner.reset();
        const auto result = scheduler.Execute(context);
        RENDER_CHECK(!result && scheduler.Size() == 0 && lifetime.expired() && !scheduler.GetDiagnostic().empty(),
            "configure/compile/prepare/record failures retain diagnostics and release captures");
        RENDER_CHECK(events.size() >= 3 && events[events.size()-2] == "Failing:finalize:3" &&
            events.back() == "Later:finalize:3", "failure finalizes every adapter exactly once");
        RENDER_CHECK(std::find(events.begin(), events.end(), "Later:record") == events.end(),
            "failed execution never records subsequent passes");
        if (phase < 3) RENDER_CHECK(std::find(events.begin(), events.end(), "Failing:record") == events.end(),
            "preparation and graph rejection record no commands");
    }

    for (int phase = 0; phase < 4; ++phase) {
        events.clear(); graph.Reset();
        owner = std::make_shared<int>(phase); lifetime = owner;
        auto throwing = std::make_unique<TracedPass>("Throwing", events, owner);
        throwing->ThrowAt = phase;
        scheduler.Add(std::move(throwing));
        owner.reset();
        bool caught = false;
        try { scheduler.Execute(context); } catch (const std::runtime_error&) { caught = true; }
        RENDER_CHECK(caught && scheduler.Size() == 0 && lifetime.expired() && events.back() == "Throwing:finalize:3",
            "exceptions in every lifecycle phase finalize as failed and release captures");
    }
    for (bool inPrepare : {false, true}) {
        events.clear(); graph.Reset();
        auto lateDisabled = std::make_unique<TracedPass>("LateDisabled", events);
        (inPrepare ? lateDisabled->PrepareResult : lateDisabled->RecordResult) = {RenderPassStatus::Disabled, "too late"};
        scheduler.Add(std::move(lateDisabled));
        RENDER_CHECK(!scheduler.Execute(context) && scheduler.GetDiagnostic().find("LateDisabled") != std::string::npos &&
            events.back() == "LateDisabled:finalize:3", "late disabling cannot skip already-declared graph work");
    }

    events.clear(); graph.Reset();
    auto disabled = std::make_unique<TracedPass>("Disabled", events);
    disabled->ConfigureResult = {RenderPassStatus::Disabled, "feature disabled"};
    disabled->InvalidRead = true;
    scheduler.Add(std::move(disabled));
    auto fallback = std::make_unique<TracedPass>("Fallback", events);
    fallback->ConfigureResult = RenderPassResult::Fallback("portable path");
    scheduler.Add(std::move(fallback));
    const auto fallbackResult = scheduler.Execute(context);
    RENDER_CHECK(fallbackResult.Status == RenderPassStatus::Fallback && fallbackResult.Diagnostic == "portable path",
        "fallback result survives later successful lifecycle phases");
    RENDER_CHECK(events == std::vector<std::string>({
        "Disabled:configure", "Fallback:configure", "Fallback:declare", "Fallback:prepare", "Fallback:record",
        "Disabled:finalize:2", "Fallback:finalize:2"}),
        "disabled passes skip declare/prepare/record, while fallback remains executable");

    graph.Reset(); owner = std::make_shared<int>(7); lifetime = owner;
    scheduler.Add(std::make_unique<CallbackRenderPass>("Adapter", CallbackRenderPass::Callback{},
        [owner](const RenderPassContext&) { return RenderPassResult{}; },
        [](RenderGraph& g) { g.AddPass("Adapter", {}); }));
    owner.reset();
    RENDER_CHECK(static_cast<bool>(scheduler.Execute(context)) && lifetime.expired(),
        "legacy callback adapter releases borrowed frame owners after execution");
    graph.Reset();
    bool recordedInvalidInputs = false;
    scheduler.Add(std::make_unique<CallbackRenderPass>("PreparedInputs",
        [](const RenderPassContext& ctx) {
            ctx.Graph->DeclareRead("declared", nullptr);
            return RenderPassResult{};
        },
        [&](const RenderPassContext&) { recordedInvalidInputs = true; return RenderPassResult{}; },
        [](RenderGraph& g) { g.AddPass("declared", {}); }));
    RENDER_CHECK(!scheduler.Execute(context) && !recordedInvalidInputs,
        "preparation input errors are compiled and rejected before any recording");
    context.Graph = nullptr;
    scheduler.Add(std::make_unique<CallbackRenderPass>("MissingGraph"));
    RENDER_CHECK(!scheduler.Execute(context) && scheduler.Size() == 0 &&
        scheduler.GetDiagnostic() == "missing render graph", "missing graph rejects and clears frame schedule");
    std::cout << "  [RenderPass] Pass lifecycle and orchestration tests passed.\n";
    return true;
}
