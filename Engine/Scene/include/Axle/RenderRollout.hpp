#pragma once
#include <string>
#include <vector>

namespace Axle {
    struct RenderFeatureRequests {
        bool Worker = false, ParallelRecording = false, TransientAliasing = false;
        bool AllowFallback = true;
        bool operator==(const RenderFeatureRequests&) const = default;
    };
    struct RenderProductRollout {
        RenderFeatureRequests Requested;
        bool CanStart = true, FellBack = false;
        bool Worker = false, ParallelRecording = false, TransientAliasing = false;
        std::vector<std::string> Reasons;
        std::string Summary;
    };
    // Transactional parsing; unrelated product arguments are ignored, unknown
    // --render-* requests fail. Strict mode wins regardless of argument order.
    bool ParseRenderFeatureArguments(int argc, char* const* argv, RenderFeatureRequests& output, std::string& error);
    // Product readiness is independent of hardware/probe capabilities. Current
    // consumers own UI/persistent targets synchronously; API proofs cannot enable
    // unimplemented product ownership/resource paths.
    RenderProductRollout EvaluateRenderProductRollout(const RenderFeatureRequests& requests);
}
