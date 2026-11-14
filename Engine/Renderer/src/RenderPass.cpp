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

    RenderPassResult RenderPassScheduler::RecordOne(std::string_view name, const RenderPassContext& context) {
        m_Diagnostic.clear();
        for (const auto& pass : m_Passes) {
            if (name != pass->GetName()) continue;
            const auto result = pass->Record(context);
            if (!result) m_Diagnostic = std::string(pass->GetName()) + ": " + result.Diagnostic;
            return result;
        }
        m_Diagnostic = "unknown pass group: " + std::string(name);
        return RenderPassResult::Failed(m_Diagnostic);
    }

    bool RenderPassScheduler::SetRecordCallback(std::string_view name, CallbackRenderPass::Callback callback) {
        for (const auto& pass : m_Passes) {
            if (name != pass->GetName()) continue;
            auto* callbackPass = dynamic_cast<CallbackRenderPass*>(pass.get());
            if (!callbackPass) return false;
            callbackPass->SetRecordCallback(std::move(callback));
            return true;
        }
        return false;
    }

    void RenderPassScheduler::Finalize(const RenderPassContext& context, const RenderPassResult& result) {
        for (const auto& pass : m_Passes) pass->Finalize(context, result);
    }

}
