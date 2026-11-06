#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>

#include <algorithm>
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

    static std::shared_ptr<JobSystemData> s_Data = nullptr;
    static std::mutex s_LifecycleMutex;
    static std::thread::id s_OwnerThreadId{};
    static std::atomic<u32> s_Epoch{0};

    struct ThreadTaskContext {
        u32 Epoch = 0;
        u32 Depth = 0;
    };
    static thread_local ThreadTaskContext t_TaskContext{};

    static std::shared_ptr<JobSystemData> GetJobSystemData() {
        std::lock_guard<std::mutex> lock(s_LifecycleMutex);
        return s_Data;
    }

    // --- TaskExecutionScope Implementation ---

    namespace Detail {
        TaskExecutionScope::TaskExecutionScope() {
            if (!JobSystem::IsValidSchedulerContext()) {
                JobSystem::ReportInvalidContext("TaskExecutionScope");
                return;
            }
            t_TaskContext.Epoch = JobSystem::GetEpoch();
            ++t_TaskContext.Depth;
            m_Active = true;
        }

        TaskExecutionScope::~TaskExecutionScope() {
            if (m_Active) {
                if (t_TaskContext.Depth > 0) {
                    --t_TaskContext.Depth;
                    if (t_TaskContext.Depth == 0) {
                        t_TaskContext.Epoch = 0;
                    }
                }
            }
        }
    }

    // --- JobHandle Implementation ---

    JobHandle::JobHandle() = default;
    JobHandle::~JobHandle() = default;
    JobHandle::JobHandle(const JobHandle&) = default;
    JobHandle& JobHandle::operator=(const JobHandle&) = default;
    JobHandle::JobHandle(JobHandle&&) noexcept = default;
    JobHandle& JobHandle::operator=(JobHandle&&) noexcept = default;

    JobHandle::JobHandle(std::shared_ptr<enki::ICompletable> task)
        : m_Task(std::move(task))
        , m_Data(GetJobSystemData())
        , m_Epoch(s_Epoch.load(std::memory_order_relaxed))
        , m_Cancelled(nullptr) {}

    JobHandle::JobHandle(std::shared_ptr<enki::ICompletable> task,
                         std::shared_ptr<JobSystemData> data,
                         u32 epoch,
                         std::shared_ptr<std::atomic<bool>> cancelled)
        : m_Task(std::move(task))
        , m_Data(std::move(data))
        , m_Epoch(epoch)
        , m_Cancelled(std::move(cancelled)) {}

    bool JobHandle::IsValid() const {
        return m_Task != nullptr;
    }

    bool JobHandle::IsComplete() const {
        if (!m_Task) return true;
        if (m_Task->GetIsComplete()) return true;
        if (m_Data && m_Data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) {
            return true;
        }
        return false;
    }

    bool JobHandle::IsCancelled() const {
        return m_Cancelled && m_Cancelled->load(std::memory_order_acquire);
    }

    bool JobHandle::Cancel() {
        if (!m_Cancelled || !m_Task) return false;
        if (IsComplete()) return false;
        bool expected = false;
        return m_Cancelled->compare_exchange_strong(expected, true, std::memory_order_acq_rel);
    }

    void JobHandle::Detach() {
        m_Task.reset();
        m_Data.reset();
        m_Cancelled.reset();
        m_Epoch = 0;
    }

    void JobHandle::Wait() {
        if (!m_Task || IsComplete()) return;
        if (!JobSystem::IsValidSchedulerContext()) {
            JobSystem::ReportInvalidContext("JobHandle::Wait");
            return;
        }
        if (m_Data) {
            if (m_Data->State.load(std::memory_order_acquire) != JobSystemState::Stopped) {
                m_Data->Scheduler.WaitforTask(m_Task.get());
            }
        } else {
            JobSystem::Wait(m_Task.get());
        }
    }

    enki::ICompletable* JobHandle::GetTask() const {
        return m_Task.get();
    }

    u32 JobHandle::GetEpoch() const {
        return m_Epoch;
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

        std::lock_guard<std::mutex> lifeLock(s_LifecycleMutex);
        if (s_Data && s_Data->State.load(std::memory_order_acquire) != JobSystemState::Stopped) {
            AXLE_CORE_WARN("JobSystem::Init() called while already initialized (state: {0})!",
                static_cast<int>(s_Data->State.load()));
            return;
        }

        u32 hwThreads = std::thread::hardware_concurrency();
        if (hwThreads == 0) hwThreads = 4;

        u32 totalThreads = (numThreads > 0) ? numThreads : hwThreads;
        u32 dedicatedWorkers = (totalThreads > 1) ? (totalThreads - 1) : 0;

        auto data = std::make_shared<JobSystemData>();
        data->TotalThreads = totalThreads;
        data->DedicatedWorkerThreads = dedicatedWorkers;
        data->Scheduler.Initialize(totalThreads);
        data->State.store(JobSystemState::Running, std::memory_order_release);

        s_Data = data;
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

        if (GetActiveTaskDepth() > 0) {
            AXLE_CORE_ERROR("JobSystem::Shutdown() rejected: cannot shut down from within an active task scope!");
            return;
        }

        std::shared_ptr<JobSystemData> data;
        {
            std::lock_guard<std::mutex> lifeLock(s_LifecycleMutex);
            data = s_Data;
        }

        if (!data || data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) {
            return;
        }

        AXLE_CORE_INFO("JobSystem shutting down (epoch {0})...", s_Epoch.load());

        // 1. Quiescing: reject new application work under admission lock
        {
            std::lock_guard<std::mutex> lock(data->AdmissionMutex);
            data->State.store(JobSystemState::Quiescing, std::memory_order_release);
        }

        // 2. Draining: wait for all admitted tasks and internal children to complete, shut down enkiTS
        data->State.store(JobSystemState::Draining, std::memory_order_release);
        data->Scheduler.WaitforAllAndShutdown();

        // 3. Clear active tasks and queues
        {
            std::lock_guard<std::mutex> lock(data->ActiveTasksMutex);
            data->ActiveTasks.clear();
        }

        {
            std::lock_guard<std::mutex> lock(data->MainThreadMutex);
            data->MainThreadQueue.clear();
        }

        // 4. Stopped
        {
            std::lock_guard<std::mutex> lock(data->AdmissionMutex);
            data->State.store(JobSystemState::Stopped, std::memory_order_release);
            data->TotalThreads = 0;
            data->DedicatedWorkerThreads = 0;
        }

        {
            std::lock_guard<std::mutex> lifeLock(s_LifecycleMutex);
            if (s_Data == data) {
                s_Data.reset();
            }
        }

        AXLE_CORE_INFO("JobSystem shutdown complete.");
    }

    bool JobSystem::IsInitialized() {
        auto data = GetJobSystemData();
        return data != nullptr && data->State.load(std::memory_order_acquire) == JobSystemState::Running;
    }

    JobSystemState JobSystem::GetState() {
        auto data = GetJobSystemData();
        if (!data) return JobSystemState::Stopped;
        return data->State.load(std::memory_order_acquire);
    }

    u32 JobSystem::GetEpoch() {
        return s_Epoch.load(std::memory_order_relaxed);
    }

    u32 JobSystem::GetTotalThreadCount() {
        auto data = GetJobSystemData();
        if (!data || data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) {
            return 0;
        }
        return data->TotalThreads;
    }

    u32 JobSystem::GetDedicatedWorkerThreadCount() {
        auto data = GetJobSystemData();
        if (!data || data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) {
            return 0;
        }
        return data->DedicatedWorkerThreads;
    }

    u32 JobSystem::GetWorkerThreadCount() {
        return GetTotalThreadCount();
    }

    u32 JobSystem::GetCurrentThreadIndex() {
        auto data = GetJobSystemData();
        if (!data || data->State.load(std::memory_order_acquire) != JobSystemState::Running) {
            return enki::NO_THREAD_NUM;
        }
        return data->Scheduler.GetThreadNum();
    }

    bool JobSystem::IsMainThread() {
        return (s_OwnerThreadId != std::thread::id()) && (std::this_thread::get_id() == s_OwnerThreadId);
    }

    bool JobSystem::IsValidSchedulerContext() {
        if (IsMainThread()) return true;
        auto data = GetJobSystemData();
        if (!data || data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) return false;
        uint32_t threadNum = data->Scheduler.GetThreadNum();
        return (threadNum != enki::NO_THREAD_NUM && threadNum < data->Scheduler.GetNumTaskThreads());
    }

    enki::TaskScheduler* JobSystem::GetScheduler() {
        if (!IsValidSchedulerContext()) {
            ReportInvalidContext("GetScheduler");
            return nullptr;
        }
        auto data = GetJobSystemData();
        if (!data || data->State.load(std::memory_order_acquire) != JobSystemState::Running) {
            ReportInvalidContext("GetScheduler");
            return nullptr;
        }
        return &data->Scheduler;
    }

    void JobSystem::RunPinnedTasks() {
        if (!IsMainThread()) {
            ReportInvalidContext("RunPinnedTasks");
            return;
        }
        auto data = GetJobSystemData();
        if (!data || data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) return;

        // Run enkiTS pinned tasks assigned to this thread (thread 0)
        data->Scheduler.RunPinnedTasks();

        // Drain deferred main thread completions for unified pump coverage
        PumpMainThreadCompletions();

        // Periodically clean up completed async task handles
        std::lock_guard<std::mutex> lock(data->ActiveTasksMutex);
        if (!data->ActiveTasks.empty()) {
            auto it = std::remove_if(data->ActiveTasks.begin(), data->ActiveTasks.end(),
                [](const std::shared_ptr<enki::ICompletable>& task) {
                    return task->GetIsComplete();
                });
            data->ActiveTasks.erase(it, data->ActiveTasks.end());
        }
    }

    JobHandle JobSystem::Dispatch(std::function<void()> func) {
        if (!func) return JobHandle();

        if (!IsValidSchedulerContext()) {
            ReportInvalidContext("Dispatch");
            return JobHandle();
        }

        auto data = GetJobSystemData();
        if (!data) {
            AXLE_CORE_WARN("JobSystem::Dispatch rejected: JobSystem stopped!");
            return JobHandle();
        }

        struct AsyncLambdaTask : public enki::ITaskSet {
            std::function<void()> TaskFunc;
            std::shared_ptr<std::atomic<bool>> Cancelled;
            std::shared_ptr<JobSystemData> SchedulerData;

            AsyncLambdaTask(std::function<void()> f,
                            std::shared_ptr<std::atomic<bool>> cancelled,
                            std::shared_ptr<JobSystemData> data)
                : enki::ITaskSet(1)
                , TaskFunc(std::move(f))
                , Cancelled(std::move(cancelled))
                , SchedulerData(std::move(data)) {}

            void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
                (void)range; (void)threadnum;
                if (Cancelled && Cancelled->load(std::memory_order_acquire)) {
                    return;
                }
                Detail::TaskExecutionScope scope;
                if (TaskFunc) TaskFunc();
            }
        };

        const u32 currentEpoch = s_Epoch.load(std::memory_order_relaxed);
        auto cancelledFlag = std::make_shared<std::atomic<bool>>(false);
        auto task = std::make_shared<AsyncLambdaTask>(std::move(func), cancelledFlag, data);

        {
            std::lock_guard<std::mutex> lock(data->AdmissionMutex);
            JobSystemState state = data->State.load(std::memory_order_acquire);
            bool isChild = (t_TaskContext.Depth > 0 && t_TaskContext.Epoch == currentEpoch);
            bool admit = (state == JobSystemState::Running) ||
                         ((state == JobSystemState::Quiescing || state == JobSystemState::Draining) && isChild);
            if (!admit) {
                AXLE_CORE_WARN("JobSystem::Dispatch rejected: scheduler is not accepting new work!");
                return JobHandle();
            }

            {
                std::lock_guard<std::mutex> taskLock(data->ActiveTasksMutex);
                data->ActiveTasks.push_back(task);
            }
            data->Scheduler.AddTaskSetToPipe(task.get());
        }

        return JobHandle(task, data, currentEpoch, cancelledFlag);
    }

    void JobSystem::Dispatch(enki::ITaskSet* taskSet) {
        if (!taskSet) return;
        if (!TryAdmitTaskSet(taskSet)) {
            AXLE_CORE_WARN("JobSystem::Dispatch(ITaskSet*) rejected!");
        }
    }

    void JobSystem::Wait(const enki::ICompletable* completable) {
        if (!completable) return;
        if (completable->GetIsComplete()) return;
        if (!IsValidSchedulerContext()) {
            ReportInvalidContext("Wait");
            return;
        }
        auto data = GetJobSystemData();
        if (!data || data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) return;
        data->Scheduler.WaitforTask(completable);
    }

    void JobSystem::WaitForAll() {
        if (!IsValidSchedulerContext()) {
            ReportInvalidContext("WaitForAll");
            return;
        }
        auto data = GetJobSystemData();
        if (!data || data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) return;
        data->Scheduler.WaitforAll();
    }

    PostResult JobSystem::PostToMainThread(const OwnerToken& owner, std::function<void()> completion) {
        if (!completion) return PostResult::Success;

        auto data = GetJobSystemData();
        if (!data || data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) {
            AXLE_CORE_WARN("JobSystem::PostToMainThread rejected: JobSystem stopped!");
            return PostResult::RejectedStopped;
        }

        std::lock_guard<std::mutex> lock(data->MainThreadMutex);
        if (data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) {
            return PostResult::RejectedStopped;
        }

        if (data->MainThreadQueue.size() >= JobSystemData::MaxMainThreadQueueCapacity) {
            AXLE_CORE_ERROR("JobSystem::PostToMainThread rejected: queue capacity exhausted!");
            return PostResult::RejectedQueueFull;
        }

        data->MainThreadQueue.push_back(QueuedCompletion{
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
        if (!IsMainThread()) {
            ReportInvalidContext("PumpMainThreadCompletions");
            return 0;
        }
        auto data = GetJobSystemData();
        if (!data) return 0;

        std::vector<QueuedCompletion> toRun;
        {
            std::lock_guard<std::mutex> lock(data->MainThreadMutex);
            if (data->MainThreadQueue.empty()) {
                return 0;
            }
            if (maxItems == 0 || data->MainThreadQueue.size() <= maxItems) {
                toRun.swap(data->MainThreadQueue);
            } else {
                toRun.assign(
                    std::make_move_iterator(data->MainThreadQueue.begin()),
                    std::make_move_iterator(data->MainThreadQueue.begin() + maxItems)
                );
                data->MainThreadQueue.erase(
                    data->MainThreadQueue.begin(),
                    data->MainThreadQueue.begin() + maxItems
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

    u32 JobSystem::GetActiveTaskDepth() {
        return t_TaskContext.Depth;
    }

    bool JobSystem::TryAdmitTaskSet(enki::ITaskSet* taskSet) {
        if (!taskSet) return false;
        if (!IsValidSchedulerContext()) {
            ReportInvalidContext("TryAdmitTaskSet");
            return false;
        }
        auto data = GetJobSystemData();
        if (!data) return false;

        std::lock_guard<std::mutex> lock(data->AdmissionMutex);
        JobSystemState state = data->State.load(std::memory_order_acquire);
        if (state == JobSystemState::Running) {
            data->Scheduler.AddTaskSetToPipe(taskSet);
            return true;
        }
        const u32 currentEpoch = s_Epoch.load(std::memory_order_relaxed);
        bool isChild = (t_TaskContext.Depth > 0 && t_TaskContext.Epoch == currentEpoch);
        if ((state == JobSystemState::Quiescing || state == JobSystemState::Draining) && isChild) {
            data->Scheduler.AddTaskSetToPipe(taskSet);
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
