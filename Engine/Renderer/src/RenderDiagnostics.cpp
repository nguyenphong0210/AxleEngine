#include <Axle/RenderDiagnostics.hpp>
#include <Axle/ResolvedSurface.hpp>
#include <utility>

namespace Axle {
    void RenderDiagnostics::BeginFrame(uint64_t frameIndex, uint64_t targetGeneration) {
        const auto previousGPUValidity = m_Frame.GPUValidity;
        const double previousGPUTime = m_Frame.GPUTimeMilliseconds;
        m_Frame = {};
        m_Frame.FrameIndex = frameIndex;
        m_Frame.TargetGeneration = targetGeneration;
        // Timestamp readback completes one submission later. Carry the
        // previous submission's result into the new frame instead of losing
        // it when the frame-local pass list is reset.
        m_Frame.GPUValidity = previousGPUValidity;
        m_Frame.GPUTimeMilliseconds = previousGPUTime;
        m_ActivePass = static_cast<size_t>(-1);
    }

    RenderPassDiagnostic& RenderDiagnostics::BeginPass(const std::string& name) {
        m_Frame.Passes.push_back({});
        m_ActivePass = m_Frame.Passes.size() - 1;
        auto& pass = m_Frame.Passes.back();
        pass.Name = name;
        pass.Status = DiagnosticStatus::NotRun;
        return pass;
    }

    void RenderDiagnostics::EndPass(DiagnosticStatus status, double cpuMilliseconds, double gpuMilliseconds,
                                    DiagnosticValidity gpuValidity, uint32_t drawCalls, uint32_t dispatches,
                                    std::string reason) {
        if (m_ActivePass == static_cast<size_t>(-1) || m_ActivePass >= m_Frame.Passes.size()) return;
        auto& pass = m_Frame.Passes[m_ActivePass];
        pass.Status = status;
        pass.CPUTimeMilliseconds = cpuMilliseconds;
        pass.GPUTimeMilliseconds = gpuMilliseconds;
        pass.GPUValidity = gpuValidity;
        pass.DrawCalls = drawCalls;
        pass.Dispatches = dispatches;
        pass.Reason = std::move(reason);
        m_ActivePass = static_cast<size_t>(-1);
    }

    void RenderDiagnostics::SetFrameGPUTime(double milliseconds, DiagnosticValidity validity) {
        m_Frame.GPUTimeMilliseconds = validity == DiagnosticValidity::Valid ? milliseconds : 0.0;
        m_Frame.GPUValidity = validity;
    }
    void RenderDiagnostics::AddFallback(std::string reason) { m_Frame.Fallbacks.push_back(std::move(reason)); }
    void RenderDiagnostics::AddError(std::string reason) { m_Frame.Errors.push_back(std::move(reason)); }
    void RenderDiagnostics::SetCounters(uint32_t drawCalls, uint32_t dispatches) {
        m_Frame.DrawCalls = drawCalls;
        m_Frame.Dispatches = dispatches;
    }
    void RenderDiagnostics::AddMaterial(uint32_t entityID, const ResolvedSurface& surface) {
        m_Frame.Materials.push_back({entityID, surface.Generation, surface.ShaderGeneration,
            surface.Evaluation, surface.UnsupportedFeatures, surface.Valid, surface.Fallback, surface.Diagnostic});
    }
}
