#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace Axle {

    struct JobSystemData {
        enki::TaskScheduler Scheduler;
        std::atomic<JobSystemState> State{JobSystemState::Stopped};
        u32 TotalThreads = 0;
        u32 DedicatedWorkerThreads = 0;

        std::mutex AdmissionMutex;

        std::mutex MainThreadMutex;
        std::vector<QueuedCompletion> MainThreadQueue;
        static constexpr size_t MaxMainThreadQueueCapacity = 16384;

        std::mutex ActiveTasksMutex;
        std::vector<std::shared_ptr<enki::ICompletable>> ActiveTasks;
    };

    static std::unique_ptr<JobSystemData> s_Data = nullptr;
    static std::thread::id s_OwnerThreadId{};
    static std::atomic<u32> s_Epoch{0};
    static thread_local u32 t_ActiveTaskDepth = 0;

    // --- JobHandle Implementation ---

    bool JobHandle::IsComplete() const {
        return !m_Task || m_Task->GetIsComplete();
    }

    void JobHandle::Wait() {
        if (m_Task) {
            JobSystem::Wait(m_Task.get());
        }
    }

    // --- JobSystem Implementation ---

    void JobSystem::Init(u32 numThreads) {
        std::thread::id callerId = std::this_thread::get_id();
        if (s_OwnerThreadId == std::thread::id()) {
            s_OwnerThreadId = callerId;
        } else if (callerId != s_OwnerThreadId) {
            AXLE_CORE_ERROR("JobSystem::Init() rejected: called from non-owner thread!");
            return;
        }

        if (s_Data && s_Data->State.load(std::memory_order_acquire) != JobSystemState::Stopped) {
            AXLE_CORE_WARN("JobSystem::Init() called while already initialized (state: {0})!",
                static_cast<int>(s_Data->State.load()));
            return;
        }

        u32 hwThreads = std::thread::hardware_concurrency();
        if (hwThreads == 0) hwThreads = 4;

        u32 totalThreads = (numThreads > 0) ? numThreads : hwThreads;
        u32 dedicatedWorkers = (totalThreads > 1) ? (totalThreads - 1) : 0;

        auto data = std::make_unique<JobSystemData>();
        data->TotalThreads = totalThreads;
        data->DedicatedWorkerThreads = dedicatedWorkers;
        data->Scheduler.Initialize(totalThreads);
        data->State.store(JobSystemState::Running, std::memory_order_release);

        s_Data = std::move(data);
        s_Epoch.fetch_add(1, std::memory_order_relaxed);

        AXLE_CORE_INFO("JobSystem initialized (epoch {0}) with {1} total scheduler threads ({2} dedicated worker threads, hardware concurrency: {3})",
            s_Epoch.load(), totalThreads, dedicatedWorkers, hwThreads);
    }

    void JobSystem::Shutdown() {
        std::thread::id callerId = std::this_thread::get_id();
        if (s_OwnerThreadId != std::thread::id() && callerId != s_OwnerThreadId) {
            AXLE_CORE_ERROR("JobSystem::Shutdown() rejected: called from non-owner thread!");
            return;
        }

        if (!s_Data || s_Data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) {
            return;
        }

        AXLE_CORE_INFO("JobSystem shutting down (epoch {0})...", s_Epoch.load());

        // 1. Quiescing: reject new application work under admission lock
        {
            std::lock_guard<std::mutex> lock(s_Data->AdmissionMutex);
            s_Data->State.store(JobSystemState::Quiescing, std::memory_order_release);
        }

        // 2. Draining: wait for all admitted tasks and internal children to complete, shut down enkiTS
        s_Data->State.store(JobSystemState::Draining, std::memory_order_release);
        s_Data->Scheduler.WaitforAllAndShutdown();

        // 3. Clear active tasks and queues
        {
            std::lock_guard<std::mutex> lock(s_Data->ActiveTasksMutex);
            s_Data->ActiveTasks.clear();
        }

        {
            std::lock_guard<std::mutex> lock(s_Data->MainThreadMutex);
            s_Data->MainThreadQueue.clear();
        }

        // 4. Stopped
        {
            std::lock_guard<std::mutex> lock(s_Data->AdmissionMutex);
            s_Data->State.store(JobSystemState::Stopped, std::memory_order_release);
            s_Data->TotalThreads = 0;
            s_Data->DedicatedWorkerThreads = 0;
        }

        s_Data.reset();
        AXLE_CORE_INFO("JobSystem shutdown complete.");
    }

    bool JobSystem::IsInitialized() {
        return s_Data != nullptr && s_Data->State.load(std::memory_order_acquire) == JobSystemState::Running;
    }

    JobSystemState JobSystem::GetState() {
        if (!s_Data) return JobSystemState::Stopped;
        return s_Data->State.load(std::memory_order_acquire);
    }

    u32 JobSystem::GetEpoch() {
        return s_Epoch.load(std::memory_order_relaxed);
    }

    u32 JobSystem::GetTotalThreadCount() {
        if (!s_Data || s_Data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) {
            return 0;
        }
        return s_Data->TotalThreads;
    }

    u32 JobSystem::GetDedicatedWorkerThreadCount() {
        if (!s_Data || s_Data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) {
            return 0;
        }
        return s_Data->DedicatedWorkerThreads;
    }

    u32 JobSystem::GetWorkerThreadCount() {
        return GetTotalThreadCount();
    }

    u32 JobSystem::GetCurrentThreadIndex() {
        if (!IsInitialized()) {
            return enki::NO_THREAD_NUM;
        }
        return s_Data->Scheduler.GetThreadNum();
    }

    bool JobSystem::IsMainThread() {
        return (s_OwnerThreadId != std::thread::id()) && (std::this_thread::get_id() == s_OwnerThreadId);
    }

    bool JobSystem::IsValidSchedulerContext() {
        if (IsMainThread()) return true;
        if (t_ActiveTaskDepth > 0) return true;
        if (!s_Data || s_Data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) return false;
        uint32_t threadNum = s_Data->Scheduler.GetThreadNum();
        return (threadNum != enki::NO_THREAD_NUM && threadNum < s_Data->Scheduler.GetNumTaskThreads());
    }

    enki::TaskScheduler& JobSystem::GetScheduler() {
        AXLE_CORE_ASSERT(s_Data && s_Data->State.load(std::memory_order_acquire) == JobSystemState::Running, "JobSystem not initialized!");
        if (!s_Data) {
            AXLE_CORE_ERROR("JobSystem::GetScheduler called while not initialized!");
            static enki::TaskScheduler s_DummyFallback;
            return s_DummyFallback;
        }
        return s_Data->Scheduler;
    }

    void JobSystem::RunPinnedTasks() {
        if (!s_Data) return;
        if (!IsMainThread()) {
            ReportInvalidContext("RunPinnedTasks");
            return;
        }

        // Run enkiTS pinned tasks assigned to this thread (thread 0)
        s_Data->Scheduler.RunPinnedTasks();

        // Drain deferred main thread completions for unified pump coverage
        PumpMainThreadCompletions();

        // Periodically clean up completed async task handles
        std::lock_guard<std::mutex> lock(s_Data->ActiveTasksMutex);
        if (!s_Data->ActiveTasks.empty()) {
            auto it = std::remove_if(s_Data->ActiveTasks.begin(), s_Data->ActiveTasks.end(),
                [](const std::shared_ptr<enki::ICompletable>& task) {
                    return task->GetIsComplete();
                });
            s_Data->ActiveTasks.erase(it, s_Data->ActiveTasks.end());
        }
    }

    JobHandle JobSystem::Dispatch(std::function<void()> func) {
        if (!func) return JobHandle();

        if (!IsValidSchedulerContext()) {
            ReportInvalidContext("Dispatch");
            return JobHandle();
        }

        if (!s_Data) {
            AXLE_CORE_WARN("JobSystem::Dispatch rejected: JobSystem stopped!");
            return JobHandle();
        }

        struct AsyncLambdaTask : public enki::ITaskSet {
            std::function<void()> TaskFunc;
            AsyncLambdaTask(std::function<void()> f)
                : enki::ITaskSet(1), TaskFunc(std::move(f)) {}

            void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
                (void)range; (void)threadnum;
                JobSystem::IncrementActiveTaskDepth();
                if (TaskFunc) TaskFunc();
                JobSystem::DecrementActiveTaskDepth();
            }
        };

        auto task = std::make_shared<AsyncLambdaTask>(std::move(func));

        {
            std::lock_guard<std::mutex> lock(s_Data->AdmissionMutex);
            JobSystemState state = s_Data->State.load(std::memory_order_acquire);
            bool admit = (state == JobSystemState::Running) ||
                         ((state == JobSystemState::Quiescing || state == JobSystemState::Draining) && t_ActiveTaskDepth > 0);
            if (!admit) {
                AXLE_CORE_WARN("JobSystem::Dispatch rejected: scheduler is not accepting new work!");
                return JobHandle();
            }

            {
                std::lock_guard<std::mutex> taskLock(s_Data->ActiveTasksMutex);
                s_Data->ActiveTasks.push_back(task);
            }
            s_Data->Scheduler.AddTaskSetToPipe(task.get());
        }

        return JobHandle(task);
    }

    void JobSystem::Dispatch(enki::ITaskSet* taskSet) {
        if (!taskSet) return;
        if (!TryAdmitTaskSet(taskSet)) {
            AXLE_CORE_WARN("JobSystem::Dispatch(ITaskSet*) rejected!");
        }
    }

    void JobSystem::Wait(const enki::ICompletable* completable) {
        if (!s_Data || !completable) return;
        if (!IsValidSchedulerContext()) {
            ReportInvalidContext("Wait");
            return;
        }
        s_Data->Scheduler.WaitforTask(completable);
    }

    void JobSystem::WaitForAll() {
        if (!s_Data) return;
        if (!IsValidSchedulerContext()) {
            ReportInvalidContext("WaitForAll");
            return;
        }
        s_Data->Scheduler.WaitforAll();
    }

    PostResult JobSystem::PostToMainThread(const OwnerToken& owner, std::function<void()> completion) {
        if (!completion) return PostResult::Success;

        if (!s_Data || s_Data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) {
            AXLE_CORE_WARN("JobSystem::PostToMainThread rejected: JobSystem stopped!");
            return PostResult::RejectedStopped;
        }

        std::lock_guard<std::mutex> lock(s_Data->MainThreadMutex);
        if (s_Data->MainThreadQueue.size() >= JobSystemData::MaxMainThreadQueueCapacity) {
            AXLE_CORE_ERROR("JobSystem::PostToMainThread rejected: queue capacity exhausted!");
            return PostResult::RejectedQueueFull;
        }

        s_Data->MainThreadQueue.push_back(QueuedCompletion{
            owner,
            s_Epoch.load(std::memory_order_relaxed),
            std::move(completion)
        });

        return PostResult::Success;
    }

    PostResult JobSystem::PostToMainThread(std::function<void()> completion) {
        return PostToMainThread(OwnerToken::Global(), std::move(completion));
    }

    u32 JobSystem::PumpMainThreadCompletions(u32 maxItems) {
        if (!s_Data) return 0;
        if (!IsMainThread()) {
            ReportInvalidContext("PumpMainThreadCompletions");
            return 0;
        }

        std::vector<QueuedCompletion> toRun;
        {
            std::lock_guard<std::mutex> lock(s_Data->MainThreadMutex);
            if (s_Data->MainThreadQueue.empty()) {
                return 0;
            }
            if (maxItems == 0 || s_Data->MainThreadQueue.size() <= maxItems) {
                toRun.swap(s_Data->MainThreadQueue);
            } else {
                toRun.assign(
                    std::make_move_iterator(s_Data->MainThreadQueue.begin()),
                    std::make_move_iterator(s_Data->MainThreadQueue.begin() + maxItems)
                );
                s_Data->MainThreadQueue.erase(
                    s_Data->MainThreadQueue.begin(),
                    s_Data->MainThreadQueue.begin() + maxItems
                );
            }
        }

        u32 executedCount = 0;
        const u32 currentEpoch = s_Epoch.load(std::memory_order_relaxed);

        for (auto& item : toRun) {
            if (item.Token.IsValid() && item.Epoch == currentEpoch) {
                if (item.Callback) {
                    item.Callback();
                    ++executedCount;
                }
            } else {
                // Invalidation: release callback and captures safely without invocation
                item.Callback = nullptr;
            }
        }

        return executedCount;
    }

    void JobSystem::DispatchToMainThread(std::function<void()> func) {
        PostToMainThread(std::move(func));
    }

    void JobSystem::IncrementActiveTaskDepth() {
        ++t_ActiveTaskDepth;
    }

    void JobSystem::DecrementActiveTaskDepth() {
        if (t_ActiveTaskDepth > 0) {
            --t_ActiveTaskDepth;
        }
    }

    u32 JobSystem::GetActiveTaskDepth() {
        return t_ActiveTaskDepth;
    }

    bool JobSystem::TryAdmitTaskSet(enki::ITaskSet* taskSet) {
        if (!taskSet) return false;
        if (!IsValidSchedulerContext()) {
            ReportInvalidContext("TryAdmitTaskSet");
            return false;
        }
        if (!s_Data) return false;

        std::lock_guard<std::mutex> lock(s_Data->AdmissionMutex);
        JobSystemState state = s_Data->State.load(std::memory_order_acquire);
        if (state == JobSystemState::Running) {
            s_Data->Scheduler.AddTaskSetToPipe(taskSet);
            return true;
        }
        if ((state == JobSystemState::Quiescing || state == JobSystemState::Draining) && t_ActiveTaskDepth > 0) {
            s_Data->Scheduler.AddTaskSetToPipe(taskSet);
            return true;
        }
        return false;
    }

    void JobSystem::ReportInvalidContext(const char* operation) {
        AXLE_CORE_ERROR("JobSystem::{0} called from invalid execution context! ThreadIndex: {1}, IsMainThread: {2}",
            operation,
            GetCurrentThreadIndex(),
            IsMainThread());
    }

    void JobSystem::ResetOwnerForTesting() {
        s_OwnerThreadId = std::this_thread::get_id();
    }

}
