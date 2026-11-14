#include <Axle/RenderPass.hpp>
#include <algorithm>

namespace Axle {

    CallbackRenderPass::CallbackRenderPass(std::string name, Callback prepare, Callback record,
                                           DeclareCallback declare, Callback configure)
        : m_Name(std::move(name)), m_Configure(std::move(configure)), m_Prepare(std::move(prepare)),
          m_Record(std::move(record)), m_Declare(std::move(declare)) {}

    void RenderPassScheduler::Clear() {
        m_Passes.clear();
        m_Disabled.clear();
        m_Diagnostic.clear();
    }

    void RenderPassScheduler::Add(std::unique_ptr<IRenderPass> pass) {
        if (pass) {
            m_Passes.push_back(std::move(pass));
            m_Disabled.push_back(false);
        }
    }

    std::vector<std::string> RenderPassScheduler::GetPassNames() const {
        std::vector<std::string> names;
        names.reserve(m_Passes.size());
        for (const auto& pass : m_Passes) names.emplace_back(pass->GetName());
        return names;
    }

    template<class Fn>
    RenderPassResult RenderPassScheduler::Run(Fn&& fn, const RenderPassContext& context, bool configuring) {
        m_Diagnostic.clear();
        RenderPassResult aggregate;
        for (size_t i = 0; i < m_Passes.size(); ++i) {
            if (m_Disabled[i]) continue;
            const auto& pass = m_Passes[i];
            auto result = fn(*pass, context);
            if (result.Status == RenderPassStatus::Disabled && !configuring)
                result = RenderPassResult::Failed("Disabled is only valid during Configure, before graph declaration");
            if (!result) {
                m_Diagnostic = std::string(pass->GetName()) + ": " + result.Diagnostic;
                return result;
            }
            if (result.Status == RenderPassStatus::Disabled) m_Disabled[i] = true;
            if (result.Status == RenderPassStatus::Fallback) aggregate = result;
        }
        return aggregate;
    }

    RenderPassResult RenderPassScheduler::Configure(const RenderPassContext& context) {
        std::fill(m_Disabled.begin(), m_Disabled.end(), false);
        return Run([](IRenderPass& pass, const RenderPassContext& ctx) { return pass.Configure(ctx); }, context, true);
    }

    void RenderPassScheduler::Declare(RenderGraph& graph) {
        for (size_t i = 0; i < m_Passes.size(); ++i)
            if (!m_Disabled[i]) m_Passes[i]->Declare(graph);
    }

    RenderPassResult RenderPassScheduler::Prepare(const RenderPassContext& context) {
        return Run([](IRenderPass& pass, const RenderPassContext& ctx) { return pass.Prepare(ctx); }, context);
    }

    RenderPassResult RenderPassScheduler::Record(const RenderPassContext& context) {
        return Run([](IRenderPass& pass, const RenderPassContext& ctx) { return pass.Record(ctx); }, context);
    }

    void RenderPassScheduler::Finalize(const RenderPassContext& context, const RenderPassResult& result) {
        for (const auto& pass : m_Passes) pass->Finalize(context, result);
    }

    RenderPassResult RenderPassScheduler::Execute(const RenderPassContext& context, bool strictPhysicalResources) {
        RenderPassResult result = RenderPassResult::Failed("pass execution did not complete");
        auto finish = [&](RenderPassScheduler*) {
            Finalize(context, result);
            m_Passes.clear();
            m_Disabled.clear();
        };
        std::unique_ptr<RenderPassScheduler, decltype(finish)> cleanup(this, finish);
        if (!context.Graph) {
            m_Diagnostic = "missing render graph";
            return result = RenderPassResult::Failed(m_Diagnostic);
        }
        RenderPassResult aggregate;
        auto runPhase = [&](auto&& phase) {
            // Finalize must see failure if the callback exits by exception.
            result = RenderPassResult::Failed("pass execution interrupted");
            result = phase();
            if (result.Status == RenderPassStatus::Fallback) aggregate = result;
            return static_cast<bool>(result);
        };
        if (!runPhase([&] { return Configure(context); })) return result;
        result = RenderPassResult::Failed("graph declaration interrupted");
        Declare(*context.Graph);
        if (!context.Graph->Compile(strictPhysicalResources)) {
            m_Diagnostic = "graph compile: " + context.Graph->GetError();
            return result = RenderPassResult::Failed(m_Diagnostic);
        }
        if (!runPhase([&] { return Prepare(context); }) || !runPhase([&] { return Record(context); })) return result;
        if (context.Commands && !context.Graph->IsComplete()) {
            m_Diagnostic = "recording did not consume the complete render graph";
            return result = RenderPassResult::Failed(m_Diagnostic);
        }
        return result = aggregate;
    }

}
