#include <Axle/RenderPass.hpp>

namespace Axle {

    CallbackRenderPass::CallbackRenderPass(std::string name, Callback prepare, Callback record, DeclareCallback declare)
        : m_Name(std::move(name)), m_Prepare(std::move(prepare)), m_Record(std::move(record)), m_Declare(std::move(declare)) {}

    void RenderPassScheduler::Clear() {
        m_Passes.clear();
        m_Diagnostic.clear();
    }

    void RenderPassScheduler::Add(std::unique_ptr<IRenderPass> pass) {
        if (pass) m_Passes.push_back(std::move(pass));
    }

    std::vector<std::string> RenderPassScheduler::GetPassNames() const {
        std::vector<std::string> names;
        names.reserve(m_Passes.size());
        for (const auto& pass : m_Passes) names.emplace_back(pass->GetName());
        return names;
    }

    template<class Fn>
    RenderPassResult RenderPassScheduler::Run(Fn&& fn, const RenderPassContext& context) {
        m_Diagnostic.clear();
        for (const auto& pass : m_Passes) {
            auto result = fn(*pass, context);
            if (!result) {
                m_Diagnostic = std::string(pass->GetName()) + ": " + result.Diagnostic;
                return result;
            }
        }
        return {};
    }

    RenderPassResult RenderPassScheduler::Configure(const RenderPassContext& context) {
        m_Diagnostic.clear();
        for (const auto& pass : m_Passes) {
            const auto result = pass->Configure(context);
            if (!result) {
                m_Diagnostic = std::string(pass->GetName()) + ": " + result.Diagnostic;
                return result;
            }
        }
        return {};
    }

    void RenderPassScheduler::Declare(RenderGraph& graph) {
        for (const auto& pass : m_Passes) pass->Declare(graph);
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

}
