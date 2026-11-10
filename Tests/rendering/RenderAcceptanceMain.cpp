#include "RenderTestCommon.hpp"
#include <Axle/RenderService.hpp>
#include <Axle/Log.hpp>
#include <filesystem>

using namespace Axle;
static bool Policy() {
    RenderFeatureRequests request;
    const auto normal = EvaluateRenderProductRollout(request);
    RENDER_CHECK(normal.CanStart && !normal.FellBack && !normal.Worker && !normal.ParallelRecording && !normal.TransientAliasing,
        "product defaults preserve current synchronous/persistent behavior");
    for (unsigned mask = 1; mask < 8; ++mask) {
        request.Worker = mask & 1; request.ParallelRecording = mask & 2; request.TransientAliasing = mask & 4;
        request.AllowFallback = true;
        const auto fallback = EvaluateRenderProductRollout(request);
        request.AllowFallback = false;
        const auto strict = EvaluateRenderProductRollout(request);
        RENDER_CHECK(fallback.CanStart && fallback.FellBack && !fallback.Reasons.empty() && !fallback.Worker && !fallback.TransientAliasing &&
            !strict.CanStart && !strict.FellBack && fallback.Reasons == strict.Reasons,
            "unimplemented product paths have explicit fallback or strict rejection, never inferred from API proof");
    }
    char app[] = "app", worker[] = "--render-thread=worker", parallel[] = "--render-parallel-recording=on";
    char alias[] = "--render-transient-aliasing=on", strict[] = "--strict-render-features", bad[] = "--render-thread=typo";
    char* valid[]{app, worker, parallel, alias, strict};
    request = {}; std::string error;
    RENDER_CHECK(ParseRenderFeatureArguments(5, valid, request, error) && request.Worker && request.ParallelRecording &&
        request.TransientAliasing && !request.AllowFallback && error.empty(), "shared parser preserves strict feature requests");
    const auto old = request; char* invalid[]{app, bad};
    RENDER_CHECK(!ParseRenderFeatureArguments(2, invalid, request, error) && request == old && !error.empty(), "invalid CLI cannot mutate prior policy");
    RenderService service;
    RENDER_CHECK(!service.ConfigureProductArguments(5, valid, error) && !service.GetProductRollout().CanStart && !service.Init(),
        "strict product request fails before service/native initialization");
    RENDER_CHECK(service.ConfigureProductArguments(4, valid, error) && service.GetProductRollout().FellBack &&
        !RHI::RHIDevice::Get().IsInitialized(), "fallback selection itself performs no GPU work");
    return true;
}
bool RunRenderAcceptanceMeasure(RHI::RHIType backend, const std::filesystem::path& output);
int main(int argc, char** argv) {
    RenderTests::ConfigureFailureReporting(); Log::Init();
    bool ok = false;
    try {
        const std::string mode = argc > 1 ? argv[1] : "policy";
        ok = mode == "policy" ? Policy() : (mode == "dx12" || mode == "vulkan") && argc == 3 &&
            RunRenderAcceptanceMeasure(mode == "vulkan" ? RHI::RHIType::Vulkan : RHI::RHIType::Direct3D12, argv[2]);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; }
    RHI::RHIDevice::Get().Shutdown();
    ok = RenderTests::Check(RHI::GetRHIValidationErrorCount() == 0, "validation including acceptance teardown", __FILE__, __LINE__) && ok;
    std::cout << "OPT11 acceptance: " << (ok ? "PASS" : "FAIL") << '\n'; return ok ? 0 : 1;
}
