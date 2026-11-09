#pragma once

#include <Axle/RenderService.hpp>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
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
    };

    struct RenderWorkerResult {
        uint64_t RequestId = 0;
        RenderResult Result;
        RenderWorkerState State = RenderWorkerState::Stopped;
    };

    // Owns the RenderService/RHIDevice calls on its dedicated thread. The
    // producer only submits immutable frame values and polls value results.
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

        RenderWorkerState GetState() const;
        uint64_t GetAcceptedFrames() const;
        uint64_t GetRejectedFrames() const;

    private:
        void ThreadMain(RenderWorkerConfig config);
        void SetState(RenderWorkerState state);

        mutable std::mutex m_Mutex;
        std::condition_variable m_Condition;
        std::deque<RenderWorkerFrame> m_Frames;
        std::deque<RenderWorkerResult> m_Results;
        std::thread m_Thread;
        RenderWorkerState m_State = RenderWorkerState::Stopped;
        RenderWorkerConfig m_Config{};
        bool m_StopRequested = false;
        uint64_t m_AcceptedFrames = 0;
        uint64_t m_RejectedFrames = 0;
        bool m_OwnsDevice = false;
    };

}
