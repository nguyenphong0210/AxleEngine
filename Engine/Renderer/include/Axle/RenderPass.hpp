#pragma once

#include <Axle/PerspectiveCamera.hpp>
#include <Axle/RenderGraph.hpp>
#include <Axle/RenderWorld.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <Axle/ViewContext.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Axle {

    enum class RenderPassStatus : uint8_t {
        Supported,
        Disabled,
        Fallback,
        Failed
    };

    struct RenderPassResult {
        RenderPassStatus Status = RenderPassStatus::Supported;
        std::string Diagnostic;
        explicit operator bool() const { return Status != RenderPassStatus::Failed; }
        static RenderPassResult Failed(std::string diagnostic) { return {RenderPassStatus::Failed, std::move(diagnostic)}; }
        static RenderPassResult Fallback(std::string diagnostic) { return {RenderPassStatus::Fallback, std::move(diagnostic)}; }
    };

    struct RenderPassCapabilities {
        bool NativeRHI = false;
        bool Compute = false;
        bool StorageTextures = false;
        bool Timestamps = false;
        bool RayTracing = false;
    };

    struct RenderPassContext {
        const RenderWorld* World = nullptr;
        const PerspectiveCamera* Camera = nullptr;
        const SceneRenderSettings* Settings = nullptr;
        ViewContext* View = nullptr;
        RenderGraph* Graph = nullptr;
        RHI::RHICommandBuffer* Commands = nullptr;
        RenderPassCapabilities Capabilities;
    };

    class IRenderPass {
    public:
        virtual ~IRenderPass() = default;
        virtual const char* GetName() const = 0;
        virtual RenderPassResult Configure(const RenderPassContext& context) = 0;
        virtual void Declare(RenderGraph& graph) = 0;
        virtual RenderPassResult Prepare(const RenderPassContext& context) = 0;
        virtual RenderPassResult Record(const RenderPassContext& context) = 0;
        virtual void Finalize(const RenderPassContext& context, const RenderPassResult& result) = 0;
    };

    // Adapter used while the existing renderer body is migrated group by group.
    // The scheduler owns ordering and result handling; the callback owns only the
    // legacy implementation details of one named group.
    class CallbackRenderPass final : public IRenderPass {
    public:
        using Callback = std::function<RenderPassResult(const RenderPassContext&)>;
        using DeclareCallback = std::function<void(RenderGraph&)>;

        CallbackRenderPass(std::string name, Callback prepare = {}, Callback record = {}, DeclareCallback declare = {});
        const char* GetName() const override { return m_Name.c_str(); }
        RenderPassResult Configure(const RenderPassContext&) override { return {}; }
        void Declare(RenderGraph& graph) override { if (m_Declare) m_Declare(graph); }
        RenderPassResult Prepare(const RenderPassContext& context) override { return m_Prepare ? m_Prepare(context) : RenderPassResult{}; }
        RenderPassResult Record(const RenderPassContext& context) override { return m_Record ? m_Record(context) : RenderPassResult{}; }
        void Finalize(const RenderPassContext&, const RenderPassResult&) override {}

    private:
        std::string m_Name;
        Callback m_Prepare;
        Callback m_Record;
        DeclareCallback m_Declare;
    };

    class RenderPassScheduler {
    public:
        void Clear();
        void Add(std::unique_ptr<IRenderPass> pass);
        size_t Size() const { return m_Passes.size(); }
        std::vector<std::string> GetPassNames() const;
        RenderPassResult Configure(const RenderPassContext& context);
        void Declare(RenderGraph& graph);
        RenderPassResult Prepare(const RenderPassContext& context);
        RenderPassResult Record(const RenderPassContext& context);
        // Execute one named group at its real recording boundary.  The
        // renderer may keep a legacy body behind the adapter while migration
        // proceeds, but ordering/result propagation remains scheduler-owned.
        RenderPassResult RecordOne(std::string_view name, const RenderPassContext& context);
        void Finalize(const RenderPassContext& context, const RenderPassResult& result);
        const std::string& GetDiagnostic() const { return m_Diagnostic; }

    private:
        template<class Fn> RenderPassResult Run(Fn&& fn, const RenderPassContext& context);
        std::vector<std::unique_ptr<IRenderPass>> m_Passes;
        std::string m_Diagnostic;
    };

}
