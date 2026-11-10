#include <Axle/RenderWorker.hpp>

#include <Axle/RHI/RHIDevice.hpp>
#include <stdexcept>
#include <chrono>
#include "RenderWorkerCapture.hpp"

namespace Axle {
    namespace {
        uint64_t WorkerNowNs() {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }
        bool InvalidCapture(const RenderWorkerFrame& frame) {
            return frame.CaptureRgba8 && (frame.Request.Kind != RenderViewKind::Main ||
                frame.Request.PhysicsScene || frame.Request.ColorTarget || frame.Request.DepthTarget ||
                frame.Request.GLTargetFBO || frame.Request.ViewTargets || !frame.Request.Width || !frame.Request.Height ||
                uint64_t(frame.Request.Width) * frame.Request.Height > RenderWorkerMaxCaptureBytes / 4);
        }
    }

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
        if (m_Thread.joinable() || m_OwnsDevice || m_State != RenderWorkerState::Stopped || !config.QueueCapacity ||
            !m_Results.empty() || (m_Frames && !m_Frames->empty())) return false;
        try {
            if (!m_Frames) m_Frames = std::make_unique<FrameQueue>();
            m_Frames->reserve(config.QueueCapacity); m_Results.reserve(config.QueueCapacity);
            m_LastError.reserve(192);
        } catch (...) { return false; }
        m_Config = config;
        m_StopRequested = false;
        m_AcceptedFrames = 0;
        m_RejectedFrames = 0;
        m_InFlight = 0;
        m_LastError.clear();
        m_Results.clear();
        m_Frames->clear();
        ++m_StateEpoch;
        m_State = RenderWorkerState::Starting;
        try { m_Thread = std::thread(&RenderWorker::ThreadMain, this, config); }
        catch (...) { m_State = RenderWorkerState::Stopped; ++m_StateEpoch; return false; }
        return true;
    }

    void RenderWorker::CancelPendingLocked(std::unique_ptr<FrameQueue>& retired) {
        // Detach first, then move prebuilt terminal values into reserved slots.
        // No payload destruction or allocation occurs while admission is locked.
        retired.swap(m_Frames);
        if (!retired) return;
        for (auto& queued : *retired) {
            queued->Cancelled->State = m_State == RenderWorkerState::Failed ? m_State : RenderWorkerState::Draining;
            queued->Cancelled->StateEpoch = m_StateEpoch;
            queued->Cancelled->Timings.PublishedAtNs = WorkerNowNs();
            m_Results.push_back(std::move(queued->Cancelled));
        }
    }

    void RenderWorker::Stop() {
        std::unique_ptr<FrameQueue> retired;
        {
            std::unique_lock lock(m_Mutex);
            if (!m_Thread.joinable()) {
                if (m_OwnsDevice) {
                    if (!RHI::RHIDevice::Get().IsOwnerThread()) { m_State = RenderWorkerState::Failed; return; }
                    m_StopRequested = true; m_State = RenderWorkerState::Draining; ++m_StateEpoch;
                    lock.unlock();
                    RenderService::Get().Shutdown();
                    const bool closed = RHI::RHIDevice::Get().Shutdown();
                    lock.lock();
                    if (!closed) { m_State = RenderWorkerState::Failed; ++m_StateEpoch; return; }
                    m_OwnsDevice = false;
                }
                if (m_State != RenderWorkerState::Stopped) ++m_StateEpoch;
                m_State = RenderWorkerState::Stopped;
                return;
            }
            m_StopRequested = true;
            if (m_State != RenderWorkerState::Draining) ++m_StateEpoch;
            m_State = RenderWorkerState::Draining;
            CancelPendingLocked(retired);
        }
        m_Condition.notify_all();
        retired.reset();
        m_Thread.join();
        std::lock_guard lock(m_Mutex);
        const auto state = m_OwnsDevice ? RenderWorkerState::Failed : RenderWorkerState::Stopped;
        if (m_State != state) ++m_StateEpoch;
        m_State = state;
    }

    bool RenderWorker::Submit(RenderWorkerFrame frame) {
        std::lock_guard lock(m_Mutex);
        if (frame.Request.PhysicsScene || frame.Request.ColorTarget || frame.Request.DepthTarget ||
            frame.Request.GLTargetFBO || frame.Request.ViewTargets || InvalidCapture(frame)) {
            ++m_RejectedFrames;
            if (m_LastError.empty()) m_LastError = InvalidCapture(frame) ? "CPU image export requires a worker-owned main view within the RGBA8 byte limit" :
                "worker frames require worker-created offscreen targets; external targets and PhysicsScene are rejected";
            return false;
        }
        if (m_State != RenderWorkerState::Running || m_StopRequested || m_InFlight >= m_Config.QueueCapacity) {
            ++m_RejectedFrames;
            return false;
        }
        std::unique_ptr<QueuedFrame> queued;
        try {
            queued = std::make_unique<QueuedFrame>();
            queued->Result = std::make_unique<RenderWorkerResult>();
            queued->Cancelled = std::make_unique<RenderWorkerResult>();
            queued->Cancelled->RequestId = frame.RequestId;
            queued->Cancelled->Diagnostic = "render worker cancelled before completion";
            queued->Cancelled->Result.ErrorMessage = queued->Cancelled->Diagnostic;
        } catch (...) { ++m_RejectedFrames; return false; }
        queued->Frame = std::move(frame);
        queued->Result->Timings.Sequence = m_AcceptedFrames + 1;
        queued->Result->Timings.EnqueuedAtNs = WorkerNowNs();
        queued->Result->Timings.InFlightAtAdmission = static_cast<uint32_t>(m_InFlight + 1);
        queued->Cancelled->Timings = queued->Result->Timings;
        m_Frames->push_back(std::move(queued));
        ++m_AcceptedFrames;
        ++m_InFlight;
        m_Condition.notify_one();
        return true;
    }

    bool RenderWorker::Poll(RenderWorkerResult& result) {
        std::lock_guard lock(m_Mutex);
        if (m_Results.empty()) return false;
        result = std::move(*m_Results.front());
        m_Results.erase(m_Results.begin());
        if (m_InFlight) --m_InFlight;
        return true;
    }

    bool RenderWorker::RenderSynchronously(RenderWorkerFrame frame, RenderWorkerResult& result) {
        result = {};
        if (InvalidCapture(frame)) { result.Result.ErrorMessage = "Invalid synchronous RGBA8 export request"; return false; }
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
        result.RequestId = frame.RequestId;
        result.Timings.EnqueuedAtNs = result.Timings.StartedAtNs = WorkerNowNs();
        result.Result = service.RenderView(frame.World, frame.Request);
        result.Timings.RenderFinishedAtNs = WorkerNowNs();
        if (result.Result.Success && frame.CaptureRgba8 &&
            !Detail::CaptureWorkerRgba8(service, result.Image, result.Result.CompletionToken)) {
            result.Result.Success = false; result.Result.CompletionToken = {};
            result.Result.ErrorMessage = "synchronous RGBA8 export failed";
        }
        result.Timings.ExportFinishedAtNs = WorkerNowNs();
        result.State = GetState();
        result.StateEpoch = GetStateEpoch();
        result.Diagnostic = result.Result.ErrorMessage;
        result.Timings.PublishedAtNs = WorkerNowNs();
        return result.Result.Success;
    }

    void RenderWorker::ThreadMain(RenderWorkerConfig config) {
        RHI::RHIDevice* device = nullptr;
        RenderService* service = nullptr;
        std::unique_ptr<QueuedFrame> active;
        bool terminalPublished = false;
        try {
            device = &RHI::RHIDevice::Get();
            if (!config.InitializeDevice || device->IsInitialized()) {
                std::lock_guard lock(m_Mutex);
                m_LastError = !config.InitializeDevice ? "RenderWorker requires InitializeDevice=true to establish worker-thread RHI ownership" :
                    "RenderWorker cannot attach to an RHI initialized by another thread";
                m_State = RenderWorkerState::Failed; ++m_StateEpoch;
                return;
            }
            service = &RenderService::Get();
            if (!device->Init(config.Device)) {
                std::lock_guard lock(m_Mutex);
                m_LastError = "worker-thread RHI initialization failed";
                m_State = RenderWorkerState::Failed; ++m_StateEpoch;
                return;
            }
            m_OwnsDevice = true;
            if (!service->Init()) throw std::runtime_error("RenderService initialization failed on worker thread");
            SetState(RenderWorkerState::Running);
            for (;;) {
                std::unique_ptr<FrameQueue> retired;
                {
                    std::unique_lock lock(m_Mutex);
                    m_Condition.wait(lock, [&] { return m_StopRequested || (m_Frames && !m_Frames->empty()); });
                    if (m_StopRequested) {
                        CancelPendingLocked(retired);
                        break;
                    }
                    active = std::move(m_Frames->front()); m_Frames->erase(m_Frames->begin());
                    terminalPublished = false;
                }
                auto& result = *active->Result;
                result.RequestId = active->Frame.RequestId;
                result.Timings.StartedAtNs = WorkerNowNs();
                result.Result = service->RenderView(active->Frame.World, active->Frame.Request);
                result.Timings.RenderFinishedAtNs = WorkerNowNs();
                if (result.Result.Success && active->Frame.CaptureRgba8 &&
                    !Detail::CaptureWorkerRgba8(*service, result.Image, result.Result.CompletionToken)) {
                    result.Result.Success = false; result.Result.CompletionToken = {};
                    result.Result.ErrorMessage = "render worker RGBA8 export failed";
                }
                result.Timings.ExportFinishedAtNs = WorkerNowNs();
                const auto ownership = service->GetOwnershipReport();
                if (ownership.RHIState.State == RHI::RHIDeviceState::Lost) SetState(RenderWorkerState::DeviceLost);
                else if (ownership.RHIState.State == RHI::RHIDeviceState::SurfaceRebuildRequired) SetState(RenderWorkerState::SurfaceRebuildRequired);
                else if (ownership.RHIState.State == RHI::RHIDeviceState::Stopping) SetState(RenderWorkerState::Draining);
                else SetState(RenderWorkerState::Running);
                result.State = GetState(); result.StateEpoch = GetStateEpoch();
                result.Diagnostic = result.Result.ErrorMessage;
                const bool successfulProbe = result.Result.Success;
                bool stop = false;
                {
                    std::lock_guard lock(m_Mutex);
                    if (!result.Result.Success && m_LastError.empty()) m_LastError = result.Result.ErrorMessage;
                    result.Timings.PublishedAtNs = WorkerNowNs();
                    m_Results.push_back(std::move(active->Result)); terminalPublished = true;
                    if (successfulProbe) device->MarkRenderWorkerOwnershipSupported();
                    stop = m_State == RenderWorkerState::DeviceLost || m_State == RenderWorkerState::SurfaceRebuildRequired ||
                        m_State == RenderWorkerState::Draining;
                    if (stop) CancelPendingLocked(retired);
                }
                // Payloads (including plugin code leases) may query the worker.
                active.reset();
                if (stop) break;
            }
        } catch (...) {
            std::unique_ptr<FrameQueue> retired;
            std::lock_guard lock(m_Mutex);
            if (m_LastError.empty()) m_LastError = "render worker exception; pending work cancelled";
            m_State = RenderWorkerState::Failed; ++m_StateEpoch;
            if (active && !terminalPublished) {
                active->Cancelled->Timings = active->Result->Timings;
                active->Cancelled->State = m_State; active->Cancelled->StateEpoch = m_StateEpoch;
                active->Cancelled->Timings.PublishedAtNs = WorkerNowNs();
                m_Results.push_back(std::move(active->Cancelled));
            }
            CancelPendingLocked(retired);
        }
        active.reset();
        if (service) service->Shutdown();
        if (m_OwnsDevice && device) {
            if (!device->Shutdown()) {
                std::lock_guard lock(m_Mutex);
                if (m_LastError.empty()) m_LastError = "render worker shutdown retained unresolved native owners";
                m_State = RenderWorkerState::Failed; ++m_StateEpoch;
                return;
            }
            m_OwnsDevice = false;
        }
        SetState(RenderWorkerState::Stopped);
    }

}
