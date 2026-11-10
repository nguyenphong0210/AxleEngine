#include <Axle/RenderRollout.hpp>
#include <string_view>

namespace Axle {
    bool ParseRenderFeatureArguments(int argc, char* const* argv, RenderFeatureRequests& output, std::string& error) {
        auto candidate = output;
        if (argc < 0 || (argc && !argv)) { error = "Invalid render argument array"; return false; }
        auto toggle = [](std::string_view value, bool& target) {
            if (value != "on" && value != "off") return false;
            target = value == "on"; return true;
        };
        for (int i = 1; i < argc; ++i) {
            if (!argv[i]) { error = "Null render argument"; return false; }
            const std::string_view arg(argv[i]);
            if (arg == "--strict-render-features") { candidate.AllowFallback = false; continue; }
            bool valid = true;
            if (arg.starts_with("--render-thread=")) {
                const auto value = arg.substr(std::string_view("--render-thread=").size());
                valid = value == "sync" || value == "worker";
                if (valid) candidate.Worker = value == "worker";
            } else if (arg.starts_with("--render-parallel-recording=")) {
                valid = toggle(arg.substr(std::string_view("--render-parallel-recording=").size()), candidate.ParallelRecording);
            } else if (arg.starts_with("--render-transient-aliasing=")) {
                valid = toggle(arg.substr(std::string_view("--render-transient-aliasing=").size()), candidate.TransientAliasing);
            } else if (arg.starts_with("--render-")) valid = false;
            if (!valid) { error = "Invalid or unknown render feature argument: " + std::string(arg); return false; }
        }
        output = candidate; error.clear(); return true;
    }
    RenderProductRollout EvaluateRenderProductRollout(const RenderFeatureRequests& requests) {
        RenderProductRollout result; result.Requested = requests;
        if (requests.Worker) result.Reasons.push_back("Product worker ownership is deferred: UI/presentation/resource consumers still use the synchronous owner; offscreen worker API proof is separate.");
        if (requests.ParallelRecording) result.Reasons.push_back("Product native parallel recording is deferred: worker allocator/command recording and measured product benefit are not accepted; primary batches alone are insufficient.");
        if (requests.TransientAliasing) result.Reasons.push_back("Product transient aliasing is deferred: current scene/history/UI targets are persistent/imported; the proven graph API does not migrate those lifetimes.");
        result.CanStart = result.Reasons.empty() || requests.AllowFallback;
        result.FellBack = !result.Reasons.empty() && requests.AllowFallback;
        result.Summary = "RENDER_PRODUCT_POLICY mode=synchronous worker=off parallel_recording=off transient_aliasing=off";
        result.Summary += result.CanStart ? result.FellBack ? " fallback=explicit" : " fallback=none" : " startup=rejected";
        return result;
    }
}
