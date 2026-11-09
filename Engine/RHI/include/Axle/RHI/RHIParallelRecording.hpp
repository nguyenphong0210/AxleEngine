#pragma once

#include <Axle/RHI/RHICommon.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Axle::RHI {

    enum class RHIParallelRecordingMode {
        Serial,
        CpuPreparation,
        NativeSecondary
    };

    struct RHIParallelRecordingProbeResult {
        RHIParallelRecordingMode Mode = RHIParallelRecordingMode::Serial;
        bool AllocatorIsolation = false;
        bool SecondaryMerge = false;
        std::string Diagnostic;
    };

    // C11-04 probe. The pinned engine wrapper exposes primary command buffers
    // and batch submit, but no portable secondary execute/merge operation.
    // Therefore native parallel recording remains disabled until that contract
    // is proved; CPU preparation is the honest fallback.
    RHIParallelRecordingProbeResult ProbeParallelRecording(const RHIDeviceCapabilities& capabilities);

    struct RHIRecordingPass {
        uint32_t Id = 0;
        std::vector<uint32_t> Dependencies;
        bool ThreadSafe = true;
    };

    struct RHIRecordingPlan {
        std::vector<RHIRecordingPass> Passes;

        // Returns deterministic DAG levels. Cycles and missing dependencies
        // return an empty result.
        std::vector<std::vector<uint32_t>> BuildLevels() const;
        bool Validate(std::string* diagnostic = nullptr) const;
    };

    struct RHIRecordingBatch {
        uint32_t Level = 0;
        std::vector<uint32_t> PassIds;
    };

    // Stable CPU-preparation fallback. It partitions only thread-safe passes,
    // preserves pass order within a level, and never calls native RHI APIs.
    std::vector<RHIRecordingBatch> BuildCpuPreparationBatches(const RHIRecordingPlan& plan,
                                                               uint32_t workerCount);

}
