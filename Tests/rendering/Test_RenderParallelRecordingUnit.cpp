#include "RenderTestCommon.hpp"
#include <Axle/RHI/RHIParallelRecording.hpp>

namespace Axle::RenderTests {

    bool RunRenderParallelRecordingUnitTests() {
        std::cout << "  [ParallelRecording] Running C11-04 probe and CPU fallback tests...\n";
        RHI::RHIDeviceCapabilities capabilities;
        capabilities.ActiveBackend = RHI::RHIType::Direct3D12;
        const auto probe = RHI::ProbeParallelRecording(capabilities);
        RENDER_CHECK(probe.Mode == RHI::RHIParallelRecordingMode::PrimaryBatch &&
            probe.PrimaryBatchSubmission && !probe.WorkerRecording && !probe.SecondaryMerge,
            "modern RHI selects independent primary batches while keeping worker leases gated");

        RHI::RHIRecordingPlan plan;
        plan.Passes = {
            {10, {}, true},
            {20, {10}, true},
            {30, {10}, true},
            {40, {20, 30}, false}
        };
        const auto levels = plan.BuildLevels();
        RENDER_CHECK(levels.size() == 3 && levels[0] == std::vector<uint32_t>{10} &&
            levels[1] == std::vector<uint32_t>({20, 30}) && levels[2] == std::vector<uint32_t>{40},
            "recording plan computes deterministic topological levels");
        const auto batches = RHI::BuildCpuPreparationBatches(plan, 2);
        RENDER_CHECK(batches.size() == 4 && batches[0].PassIds == std::vector<uint32_t>{10} &&
            batches[1].PassIds == std::vector<uint32_t>{20} && batches[2].PassIds == std::vector<uint32_t>{30} &&
            batches[3].PassIds == std::vector<uint32_t>{40},
            "CPU preparation fallback partitions independent work across worker lanes and serializes unsafe passes");
        const auto serialBatches = RHI::BuildCpuPreparationBatches(plan, 0);
        RENDER_CHECK(serialBatches.size() == 3 && serialBatches[1].PassIds == std::vector<uint32_t>({20, 30}) &&
            serialBatches[2].PassIds == std::vector<uint32_t>{40},
            "zero-worker fallback retains a valid owner-serial plan");

        RHI::RHIRecordingPlan cycle;
        cycle.Passes = {{1, {2}, true}, {2, {1}, true}};
        std::string diagnostic;
        RENDER_CHECK(cycle.BuildLevels().empty() && !cycle.Validate(&diagnostic) && !diagnostic.empty(),
            "cyclic recording plans are rejected with a diagnostic");
        RHI::RHIRecordingPlan missing;
        missing.Passes = {{1, {99}, true}};
        RENDER_CHECK(!missing.Validate(&diagnostic), "missing recording dependencies are rejected");
        RHI::RHIRecordingPlan duplicate;
        duplicate.Passes = {{1, {}, true}, {1, {}, true}};
        RENDER_CHECK(!duplicate.Validate(&diagnostic), "duplicate recording pass ids are rejected");
        std::cout << "  [ParallelRecording] All C11-04 probe/fallback tests passed.\n";
        return true;
    }

}
