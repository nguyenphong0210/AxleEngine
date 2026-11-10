#pragma once
#include <Axle/RenderTransientGraph.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHITransientResources.hpp>
#include <functional>
#include <memory>

namespace Axle {
    // Owns one pending transaction and the last successfully submitted generation.
    // The device's submission pins independently keep resources alive until GPU
    // completion, including after replacement or destruction of this wrapper.
    class RenderTransientFrame {
    public:
        enum class AllocationMode { Committed, PlacedUnaliased, AliasingIfValidated };
        struct Import { RenderTransientGraph::Resource Id; RHI::RHITransientView View; };
        // Trusted owner callback: record only declared work; do not end, reset,
        // submit or retain the borrowed command wrapper beyond this call.
        using Recorder = std::function<bool(RHI::RHICommandBuffer&)>;
        RenderTransientFrame();
        ~RenderTransientFrame();
        RenderTransientFrame(const RenderTransientFrame&) = delete;
        RenderTransientFrame& operator=(const RenderTransientFrame&) = delete;
        bool Prepare(const RenderTransientGraph::Plan& declarations, const std::vector<Import>& imports = {},
                     AllocationMode mode = AllocationMode::Committed);
        bool Record(RenderTransientGraph::PassId pass, const Recorder& recorder);
        bool Submit();
        bool Abandon();
        // Borrow only for declared work in the current callback. Aliased views
        // are unavailable outside their declared pass; external outputs must be
        // imported persistent generations. Never export/capture an aliased view.
        const RHI::RHITransientView* Get(RenderTransientGraph::Resource resource) const;
        const RenderTransientGraph::Plan* GetPlan() const;
        uint64_t GetPublishedGeneration() const { return m_Generation; }
        RHI::RHICompletionToken GetCompletion() const { return m_Completion; }
        bool IsPoisoned() const;
        const std::string& GetError() const { return m_Error; }
    private:
        struct Storage;
        std::unique_ptr<Storage> m_Pending, m_Published;
        std::string m_Error;
        uint64_t m_Generation = 0;
        RHI::RHICompletionToken m_Completion;
        bool Fail(std::string error);
    };
}
