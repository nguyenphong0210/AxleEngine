#pragma once

#include <Axle/RenderService.hpp>
#include <condition_variable>
#include <cstdint>
#include <vector>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace Axle {

    enum class RenderWorkerState { Stopped, Starting, Running, SurfaceRebuildRequired, DeviceLost, Draining, Failed };

    struct RenderWorkerConfig {
        RHI::RHIDeviceDesc Device;
        uint32_t QueueCapacity = 3;
        bool InitializeDevice = true;
    };

    struct RenderWorkerFrame {
        RenderWorld World;
        ViewRequest Request;
        uint64_t RequestId = 0;
        // Explicit CPU image export only; ordinary frames add no capture waits.
        bool CaptureRgba8 = false;
        // CPU payload/module retention, matching RenderSnapshotTicket::LeaseAnchor.
        // Ends after recording or cancellation; native uses need independent GPU
        // submission leases (e.g. RenderDebugLine::OwnerLease).
        std::shared_ptr<const void> LeaseAnchor;
        // External targets and physics are rejected; targets are worker-created.
    };

    inline constexpr uint64_t RenderWorkerMaxCaptureBytes = 16ull * 1024 * 1024;
    struct RenderCpuImage {
        uint32_t Width = 0, Height = 0;
        std::vector<uint8_t> Rgba8;
    };
    struct RenderWorkerTimings {
        // Steady-clock nanoseconds within this process; CPU wall timings include
        // existing frame-slot retirement, not GPU timestamp measurements.
        uint64_t Sequence = 0, EnqueuedAtNs = 0, StartedAtNs = 0;
        uint64_t RenderFinishedAtNs = 0, ExportFinishedAtNs = 0, PublishedAtNs = 0;
        uint32_t InFlightAtAdmission = 0;
    };

    struct RenderWorkerResult {
        uint64_t RequestId = 0;
        RenderResult Result;
        RenderWorkerState State = RenderWorkerState::Stopped;
        uint64_t StateEpoch = 0;
        std::string Diagnostic;
        RenderWorkerTimings Timings;
        RenderCpuImage Image;
    };

    // Owns the RenderService/RHIDevice calls on its dedicated thread. The
    // producer only submits immutable frame values and polls value results.
    // Start/Stop/synchronous fallback are serialized lifecycle operations;
    // Submit/Poll/diagnostic queries may run concurrently with Stop.
    class RenderWorker {
    public:
        RenderWorker() = default;
        ~RenderWorker();

        RenderWorker(const RenderWorker&) = delete;
        RenderWorker& operator=(const RenderWorker&) = delete;

        bool Start(const RenderWorkerConfig& config = {});
        void Stop();
        bool Submit(RenderWorkerFrame frame);
        bool Poll(RenderWorkerResult& result);
        bool RenderSynchronously(RenderWorkerFrame frame, RenderWorkerResult& result);

        RenderWorkerState GetState() const;
        uint64_t GetStateEpoch() const;
        std::string GetLastError() const;
        uint64_t GetAcceptedFrames() const;
        uint64_t GetRejectedFrames() const;

    private:
        struct QueuedFrame {
            RenderWorkerFrame Frame;
            std::unique_ptr<RenderWorkerResult> Result, Cancelled;
        };
        using FrameQueue = std::vector<std::unique_ptr<QueuedFrame>>;
        void ThreadMain(RenderWorkerConfig config);
        void SetState(RenderWorkerState state);
        void CancelPendingLocked(std::unique_ptr<FrameQueue>& retired);

        mutable std::mutex m_Mutex;
        std::condition_variable m_Condition;
        // Capacity allocated at Start; cancellation/Stop never allocates.
        std::unique_ptr<FrameQueue> m_Frames;
        std::vector<std::unique_ptr<RenderWorkerResult>> m_Results;
        std::thread m_Thread;
        RenderWorkerState m_State = RenderWorkerState::Stopped;
        RenderWorkerConfig m_Config{};
        bool m_StopRequested = false;
        uint64_t m_AcceptedFrames = 0;
        uint64_t m_RejectedFrames = 0;
        uint64_t m_InFlight = 0;
        uint64_t m_StateEpoch = 0;
        std::string m_LastError;
        bool m_OwnsDevice = false;
    };

}
