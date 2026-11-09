#include <Axle/RenderWorker.hpp>

#include <Axle/RHI/RHIDevice.hpp>

namespace Axle {

    RenderWorker::~RenderWorker() { Stop(); }

    void RenderWorker::SetState(RenderWorkerState state) {
        std::lock_guard lock(m_Mutex);
        if (m_StopRequested && state == RenderWorkerState::Running) state = RenderWorkerState::Draining;
        if (m_State != state) ++m_StateEpoch;
        m_State = state;
    }

    RenderWorkerState RenderWorker::GetState() const {
        std::lock_guard lock(m_Mutex);
        return m_State;
    }

    uint64_t RenderWorker::GetStateEpoch() const {
        std::lock_guard lock(m_Mutex);
        return m_StateEpoch;
    }

    std::string RenderWorker::GetLastError() const {
        std::lock_guard lock(m_Mutex);
        return m_LastError;
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
        if (m_Thread.joinable() || m_State != RenderWorkerState::Stopped || !config.QueueCapacity || !m_Results.empty()) return false;
        m_Config = config;
        m_StopRequested = false;
        m_AcceptedFrames = 0;
        m_RejectedFrames = 0;
        m_InFlight = 0;
        m_LastError.clear();
        m_Results.clear();
        m_Frames.clear();
        ++m_StateEpoch;
        m_State = RenderWorkerState::Starting;
        m_Thread = std::thread(&RenderWorker::ThreadMain, this, config);
        return true;
    }

    void RenderWorker::CancelPendingLocked(const char* diagnostic) {
        while (!m_Frames.empty()) {
            RenderWorkerResult result;
            result.RequestId = m_Frames.front().RequestId;
            result.Result.ErrorMessage = diagnostic;
            result.State = RenderWorkerState::Draining;
            result.StateEpoch = m_StateEpoch;
            result.Diagnostic = diagnostic;
            m_Frames.pop_front();
            if (m_Results.size() < m_Config.QueueCapacity) m_Results.push_back(std::move(result));
            else if (m_InFlight) --m_InFlight;
        }
    }

    void RenderWorker::Stop() {
        {
            std::lock_guard lock(m_Mutex);
            if (!m_Thread.joinable()) {
                if (m_OwnsDevice) {
                    RenderService::Get().Shutdown();
                    RHI::RHIDevice::Get().Shutdown();
                    m_OwnsDevice = false;
                }
                m_State = RenderWorkerState::Stopped;
                return;
            }
            m_StopRequested = true;
            CancelPendingLocked("render worker stopped before frame execution");
            if (m_State != RenderWorkerState::Draining) ++m_StateEpoch;
            m_State = RenderWorkerState::Draining;
        }
        m_Condition.notify_all();
        m_Thread.join();
        std::lock_guard lock(m_Mutex);
        m_State = RenderWorkerState::Stopped;
    }

    bool RenderWorker::Submit(RenderWorkerFrame frame) {
        std::lock_guard lock(m_Mutex);
        if (frame.Request.PhysicsScene || frame.Request.ColorTarget || frame.Request.DepthTarget ||
            frame.Request.GLTargetFBO || frame.Request.ViewTargets) {
            ++m_RejectedFrames;
            m_LastError = "worker frames require worker-created offscreen targets; external targets and PhysicsScene are rejected";
            return false;
        }
        if (m_State != RenderWorkerState::Running || m_StopRequested || m_InFlight >= m_Config.QueueCapacity) {
            ++m_RejectedFrames;
            return false;
        }
        m_Frames.push_back(std::move(frame));
        ++m_AcceptedFrames;
        ++m_InFlight;
        m_Condition.notify_one();
        return true;
    }

    bool RenderWorker::Poll(RenderWorkerResult& result) {
        std::lock_guard lock(m_Mutex);
        if (m_Results.empty()) return false;
        result = std::move(m_Results.front());
        m_Results.pop_front();
        if (m_InFlight) --m_InFlight;
        return true;
    }

    bool RenderWorker::RenderSynchronously(RenderWorkerFrame frame, RenderWorkerResult& result) {
        if (m_Thread.joinable()) Stop();
        const auto state = GetState();
        if (state != RenderWorkerState::Stopped && state != RenderWorkerState::Failed) return false;
        bool initializedFallbackDevice = false;
        auto& device = RHI::RHIDevice::Get();
        if (!device.IsInitialized()) {
            if (!device.Init(m_Config.Device)) return false;
            initializedFallbackDevice = true;
            m_OwnsDevice = true;
        }
        auto& service = RenderService::Get();
        if (!service.Init()) {
            if (initializedFallbackDevice) {
                device.Shutdown();
                m_OwnsDevice = false;
            }
            return false;
        }
        SetState(RenderWorkerState::Running);
        result.RequestId = frame.RequestId;
        result.Result = service.RenderView(frame.World, frame.Request);
        result.State = GetState();
        result.StateEpoch = GetStateEpoch();
        result.Diagnostic = result.Result.ErrorMessage;
        return result.Result.Success;
    }

    void RenderWorker::ThreadMain(RenderWorkerConfig config) {
        auto& device = RHI::RHIDevice::Get();
        if (!config.InitializeDevice) {
            std::lock_guard lock(m_Mutex);
            m_LastError = "RenderWorker requires InitializeDevice=true to establish worker-thread RHI ownership";
            m_State = RenderWorkerState::Failed;
            ++m_StateEpoch;
            return;
        }
        if (device.IsInitialized()) {
            std::lock_guard lock(m_Mutex);
            m_LastError = "RenderWorker cannot attach to an RHI initialized by another thread";
            m_State = RenderWorkerState::Failed;
            ++m_StateEpoch;
            return;
        }
        if (!device.Init(config.Device)) {
            std::lock_guard lock(m_Mutex);
            m_LastError = "worker-thread RHI initialization failed";
            m_State = RenderWorkerState::Failed;
            ++m_StateEpoch;
            return;
        }
        m_OwnsDevice = true;

        auto& service = RenderService::Get();
        if (!service.Init()) {
            device.Shutdown();
            m_OwnsDevice = false;
            std::lock_guard lock(m_Mutex);
            m_LastError = "RenderService initialization failed on worker thread";
            m_State = RenderWorkerState::Failed;
            ++m_StateEpoch;
            return;
        }
        SetState(RenderWorkerState::Running);

        for (;;) {
            RenderWorkerFrame frame;
            {
                std::unique_lock lock(m_Mutex);
                m_Condition.wait(lock, [&] { return m_StopRequested || !m_Frames.empty(); });
                if (m_StopRequested) {
                    CancelPendingLocked("render worker stopped before frame execution");
                    break;
                }
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
            result.StateEpoch = GetStateEpoch();
            result.Diagnostic = result.Result.ErrorMessage;
            const bool successfulProbe = result.Result.Success;
            {
                std::lock_guard lock(m_Mutex);
                if (!result.Result.Success) m_LastError = result.Result.ErrorMessage;
                if (m_Results.size() < m_Config.QueueCapacity) m_Results.push_back(std::move(result));
                if (successfulProbe) device.MarkRenderWorkerOwnershipSupported();
                if (m_State == RenderWorkerState::DeviceLost || m_State == RenderWorkerState::SurfaceRebuildRequired ||
                    m_State == RenderWorkerState::Draining) {
                    CancelPendingLocked("render worker cancelled pending frames after device state transition");
                    break;
                }
            }
        }

        service.Shutdown();
        if (m_OwnsDevice) device.Shutdown();
        m_OwnsDevice = false;
        SetState(RenderWorkerState::Stopped);
    }

}
