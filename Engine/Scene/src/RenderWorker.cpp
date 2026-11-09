#include <Axle/RenderWorker.hpp>

#include <Axle/RHI/RHIDevice.hpp>

namespace Axle {

    RenderWorker::~RenderWorker() { Stop(); }

    void RenderWorker::SetState(RenderWorkerState state) {
        std::lock_guard lock(m_Mutex);
        m_State = state;
    }

    RenderWorkerState RenderWorker::GetState() const {
        std::lock_guard lock(m_Mutex);
        return m_State;
    }
    uint64_t RenderWorker::GetAcceptedFrames() const {
        std::lock_guard lock(m_Mutex);
        return m_AcceptedFrames;
    }
    uint64_t RenderWorker::GetRejectedFrames() const {
        std::lock_guard lock(m_Mutex);
        return m_RejectedFrames;
    }

    bool RenderWorker::Start(const RenderWorkerConfig& config) {
        std::lock_guard lock(m_Mutex);
        if (m_Thread.joinable() || m_State != RenderWorkerState::Stopped) return false;
        m_Config = config;
        m_StopRequested = false;
        m_AcceptedFrames = 0;
        m_RejectedFrames = 0;
        m_Results.clear();
        m_Frames.clear();
        m_State = RenderWorkerState::Starting;
        m_Thread = std::thread(&RenderWorker::ThreadMain, this, config);
        return true;
    }

    void RenderWorker::Stop() {
        {
            std::lock_guard lock(m_Mutex);
            if (!m_Thread.joinable()) {
                m_State = RenderWorkerState::Stopped;
                return;
            }
            m_StopRequested = true;
            m_State = RenderWorkerState::Draining;
        }
        m_Condition.notify_all();
        if (m_Thread.joinable()) m_Thread.join();
        {
            std::lock_guard lock(m_Mutex);
            m_State = RenderWorkerState::Stopped;
        }
    }

    bool RenderWorker::Submit(RenderWorkerFrame frame) {
        std::lock_guard lock(m_Mutex);
        if (m_State != RenderWorkerState::Running || m_StopRequested || m_Frames.size() >= m_Config.QueueCapacity) {
            ++m_RejectedFrames;
            return false;
        }
        m_Frames.push_back(std::move(frame));
        ++m_AcceptedFrames;
        m_Condition.notify_one();
        return true;
    }

    bool RenderWorker::Poll(RenderWorkerResult& result) {
        std::lock_guard lock(m_Mutex);
        if (m_Results.empty()) return false;
        result = std::move(m_Results.front());
        m_Results.pop_front();
        return true;
    }

    void RenderWorker::ThreadMain(RenderWorkerConfig config) {
        auto& device = RHI::RHIDevice::Get();
        if (config.InitializeDevice && !device.IsInitialized()) {
            if (!device.Init(config.Device)) {
                SetState(RenderWorkerState::Failed);
                return;
            }
            m_OwnsDevice = true;
        }

        auto& service = RenderService::Get();
        if (!service.Init()) {
            if (m_OwnsDevice) device.Shutdown();
            m_OwnsDevice = false;
            SetState(RenderWorkerState::Failed);
            return;
        }
        SetState(RenderWorkerState::Running);

        for (;;) {
            RenderWorkerFrame frame;
            {
                std::unique_lock lock(m_Mutex);
                m_Condition.wait(lock, [&] { return m_StopRequested || !m_Frames.empty(); });
                if (m_Frames.empty() && m_StopRequested) break;
                frame = std::move(m_Frames.front());
                m_Frames.pop_front();
            }

            RenderWorkerResult result;
            result.RequestId = frame.RequestId;
            result.Result = service.RenderView(frame.World, frame.Request);
            const auto ownership = service.GetOwnershipReport();
            if (ownership.RHIState.State == RHI::RHIDeviceState::Lost) SetState(RenderWorkerState::DeviceLost);
            else if (ownership.RHIState.State == RHI::RHIDeviceState::SurfaceRebuildRequired) SetState(RenderWorkerState::SurfaceRebuildRequired);
            else if (ownership.RHIState.State == RHI::RHIDeviceState::Stopping) SetState(RenderWorkerState::Draining);
            else SetState(RenderWorkerState::Running);
            result.State = GetState();
            {
                std::lock_guard lock(m_Mutex);
                m_Results.push_back(std::move(result));
            }
        }

        service.Shutdown();
        if (m_OwnsDevice) device.Shutdown();
        m_OwnsDevice = false;
        SetState(RenderWorkerState::Stopped);
    }

}
