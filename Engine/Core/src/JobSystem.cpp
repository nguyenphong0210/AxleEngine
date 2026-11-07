#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <condition_variable>

namespace Axle {

    struct JobSystemData {
        enki::TaskScheduler Scheduler;
        std::atomic<JobSystemState> State{JobSystemState::Stopped};
        u32 TotalThreads = 0;
        u32 DedicatedWorkerThreads = 0;

        std::mutex AdmissionMutex;
        std::condition_variable AdmissionChanged;
        u32 Submitting = 0;

        std::mutex MainThreadMutex;
        std::vector<QueuedCompletion> MainThreadQueue;
        static constexpr size_t MaxMainThreadQueueCapacity = 16384;

        std::mutex ActiveTasksMutex;
        struct OwnedTask {
            std::shared_ptr<enki::ICompletable> Task;
            std::shared_ptr<std::atomic<bool>> Submitted;
        };
        std::vector<OwnedTask> ActiveTasks;
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
    static thread_local bool t_PumpingCompletions = false;

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

        struct AsyncLambdaTask : public enki::ITaskSet {
            std::function<void()> TaskFunc;
            std::shared_ptr<std::atomic<bool>> Cancelled;
            std::shared_ptr<JobSystemData> SchedulerData;
            std::atomic<int> ExecutionState{0}; // 0 pending, 1 running, 2 cancelled

            AsyncLambdaTask(std::function<void()> f,
                            std::shared_ptr<std::atomic<bool>> cancelled,
                            std::shared_ptr<JobSystemData> data)
                : enki::ITaskSet(1)
                , TaskFunc(std::move(f))
                , Cancelled(std::move(cancelled))
                , SchedulerData(std::move(data)) {}

            void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
                (void)range; (void)threadnum;
                Detail::TaskExecutionScope scope;
                auto function = std::move(TaskFunc);
                int pending = 0;
                if (!ExecutionState.compare_exchange_strong(pending, 1, std::memory_order_acq_rel)) return;
                if (Cancelled && Cancelled->load(std::memory_order_acquire)) {
                    return;
                }
                if (function) function();
            }
        };
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
        if (auto lambdaTask = dynamic_cast<Detail::AsyncLambdaTask*>(m_Task.get())) {
            int pending = 0;
            if (!lambdaTask->ExecutionState.compare_exchange_strong(pending, 2, std::memory_order_acq_rel)) return false;
            m_Cancelled->store(true, std::memory_order_release);
            return true;
        }
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
        std::lock_guard<std::mutex> lifeLock(s_LifecycleMutex);
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
        if (!IsMainThread()) {
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
            std::unique_lock<std::mutex> lock(data->AdmissionMutex);
            data->State.store(JobSystemState::Quiescing, std::memory_order_release);
            data->AdmissionChanged.wait(lock, [&] { return data->Submitting == 0; });
        }

        // 2. Draining: wait for all admitted tasks and internal children to complete, shut down enkiTS
        data->State.store(JobSystemState::Draining, std::memory_order_release);
        data->Scheduler.WaitforAllAndShutdown();

        // 3. Retire captures outside all scheduler/queue locks. Destructors may post.
        std::vector<JobSystemData::OwnedTask> retiredTasks;
        std::vector<QueuedCompletion> retiredCompletions;
        data->State.store(JobSystemState::Stopped, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(data->ActiveTasksMutex);
            retiredTasks.swap(data->ActiveTasks);
        }

        {
            std::lock_guard<std::mutex> lock(data->MainThreadMutex);
            retiredCompletions.swap(data->MainThreadQueue);
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
        std::lock_guard<std::mutex> lock(s_LifecycleMutex);
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
        if (!IsMainThread() || GetActiveTaskDepth() > 0) {
            ReportInvalidContext("RunPinnedTasks");
            return;
        }
        auto data = GetJobSystemData();
        if (!data || data->State.load(std::memory_order_acquire) == JobSystemState::Stopped) return;

        // Run enkiTS pinned tasks assigned to this thread (thread 0)
        data->Scheduler.RunPinnedTasks();

        // Periodically clean up completed async task handles
        std::vector<JobSystemData::OwnedTask> retired;
        {
            std::lock_guard<std::mutex> lock(data->ActiveTasksMutex);
            auto it = data->ActiveTasks.begin();
            while (it != data->ActiveTasks.end()) {
                if (it->Submitted->load(std::memory_order_acquire) && it->Task->GetIsComplete()) {
                    retired.push_back(std::move(*it));
                    it = data->ActiveTasks.erase(it);
                } else ++it;
            }
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

        const u32 currentEpoch = s_Epoch.load(std::memory_order_relaxed);
        auto cancelledFlag = std::make_shared<std::atomic<bool>>(false);
        auto task = std::make_shared<Detail::AsyncLambdaTask>(std::move(func), cancelledFlag, data);
        task->m_Priority = enki::TaskPriority(enki::TASK_PRIORITY_NUM - 1);
        auto submitted = std::make_shared<std::atomic<bool>>(false);

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
                data->ActiveTasks.push_back({task, submitted});
            }
            ++data->Submitting;
        }
        // enki can execute user code here when the pipe is full.
        data->Scheduler.AddTaskSetToPipe(task.get());
        submitted->store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(data->AdmissionMutex);
            --data->Submitting;
            data->AdmissionChanged.notify_all();
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
        if (!IsMainThread() || GetActiveTaskDepth() > 0 || t_PumpingCompletions) {
            ReportInvalidContext("PumpMainThreadCompletions");
            return 0;
        }
        auto data = GetJobSystemData();
        if (!data) return 0;
        struct PumpScope {
            PumpScope() { t_PumpingCompletions = true; }
            ~PumpScope() { t_PumpingCompletions = false; }
        } pumpScope;

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
        return SubmitTaskSet(taskSet, false);
    }

    bool JobSystem::SubmitTaskSet(enki::ITaskSet* taskSet, bool physics) {
        static_assert(enki::TASK_PRIORITY_NUM >= 2, "Physics wait isolation requires at least two enki priority lanes");
        if (!taskSet) return false;
        if (!IsValidSchedulerContext()) {
            ReportInvalidContext("TryAdmitTaskSet");
            return false;
        }
        auto data = GetJobSystemData();
        if (!data) return false;
        taskSet->m_Priority = physics ? enki::TASK_PRIORITY_HIGH : enki::TaskPriority(enki::TASK_PRIORITY_NUM - 1);

        {
            std::lock_guard<std::mutex> lock(data->AdmissionMutex);
            JobSystemState state = data->State.load(std::memory_order_acquire);
            const u32 currentEpoch = s_Epoch.load(std::memory_order_relaxed);
            bool isChild = (t_TaskContext.Depth > 0 && t_TaskContext.Epoch == currentEpoch);
            if (state != JobSystemState::Running &&
                !((state == JobSystemState::Quiescing || state == JobSystemState::Draining) && isChild)) return false;
            ++data->Submitting;
        }
        data->Scheduler.AddTaskSetToPipe(taskSet);
        {
            std::lock_guard<std::mutex> lock(data->AdmissionMutex);
            --data->Submitting;
            data->AdmissionChanged.notify_all();
        }
        return true;
    }

    void JobSystem::WaitPhysicsTask(const enki::ICompletable* task) {
        auto data = GetJobSystemData();
        if (data && IsValidSchedulerContext() && data->State.load(std::memory_order_acquire) != JobSystemState::Stopped)
            data->Scheduler.WaitforTask(task, enki::TASK_PRIORITY_HIGH);
    }

    void JobSystem::ReportInvalidContext(const char* operation) {
        AXLE_CORE_ERROR("JobSystem::{0} called from invalid execution context! ThreadIndex: {1}, IsMainThread: {2}",
            operation,
            GetCurrentThreadIndex(),
            IsMainThread());
    }

    void JobSystem::ResetOwnerForTesting() {
        std::lock_guard<std::mutex> lock(s_LifecycleMutex);
        s_OwnerThreadId = std::this_thread::get_id();
    }

}
