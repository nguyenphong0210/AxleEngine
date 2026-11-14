#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Axle {
    struct ResolvedSurface;
    enum class SurfaceEvaluation : uint8_t;
    struct MaterialSurfaceDiagnostic {
        uint32_t EntityID = 0;
        uint64_t Generation = 0, ShaderGeneration = 0;
        SurfaceEvaluation Evaluation{};
        uint32_t UnsupportedFeatures = 0;
        bool Valid = false;
        bool Fallback = false;
        std::string Reason;
    };

    enum class DiagnosticStatus : uint8_t { NotRun, Success, Fallback, Failed };
    enum class DiagnosticValidity : uint8_t { Unknown, Valid, Unavailable, Invalid };

    struct RenderPassDiagnostic {
        std::string Name;
        DiagnosticStatus Status = DiagnosticStatus::NotRun;
        DiagnosticValidity GPUValidity = DiagnosticValidity::Unknown;
        double CPUTimeMilliseconds = 0.0;
        double GPUTimeMilliseconds = 0.0;
        uint32_t DrawCalls = 0;
        uint32_t Dispatches = 0;
        std::string Reason;
    };

    struct RenderFrameDiagnostics {
        uint64_t FrameIndex = 0;
        uint64_t TargetGeneration = 0;
        uint64_t MaterialGeneration = 0;
        DiagnosticValidity GPUValidity = DiagnosticValidity::Unknown;
        double GPUTimeMilliseconds = 0.0;
        uint32_t DrawCalls = 0;
        uint32_t Dispatches = 0;
        std::vector<RenderPassDiagnostic> Passes;
        std::vector<MaterialSurfaceDiagnostic> Materials;
        std::vector<std::string> Fallbacks;
        std::vector<std::string> Errors;
    };

    class RenderDiagnostics {
    public:
        void BeginFrame(uint64_t frameIndex, uint64_t targetGeneration = 0);
        RenderPassDiagnostic& BeginPass(const std::string& name);
        void EndPass(DiagnosticStatus status = DiagnosticStatus::Success,
                     double cpuMilliseconds = 0.0,
                     double gpuMilliseconds = 0.0,
                     DiagnosticValidity gpuValidity = DiagnosticValidity::Unknown,
                     uint32_t drawCalls = 0,
                     uint32_t dispatches = 0,
                     std::string reason = {});
        void SetFrameGPUTime(double milliseconds, DiagnosticValidity validity);
        void AddFallback(std::string reason);
        void AddError(std::string reason);
        void SetCounters(uint32_t drawCalls, uint32_t dispatches);
        void AddMaterial(uint32_t entityID, const ResolvedSurface& surface);
        void SetMaterialGeneration(uint64_t generation) { m_Frame.MaterialGeneration = generation; }
        void SetFrameIndex(uint64_t frameIndex) { m_Frame.FrameIndex = frameIndex; }

        const RenderFrameDiagnostics& GetFrame() const { return m_Frame; }
        bool HasErrors() const { return !m_Frame.Errors.empty(); }
        bool IsGPUTimeValid() const { return m_Frame.GPUValidity == DiagnosticValidity::Valid; }

    private:
        RenderFrameDiagnostics m_Frame;
        size_t m_ActivePass = static_cast<size_t>(-1);
    };
}
