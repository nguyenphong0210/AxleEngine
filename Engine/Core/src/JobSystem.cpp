#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>

#include <mutex>
#include <thread>
#include <vector>

namespace Axle {

    struct JobSystemData {
        enki::TaskScheduler Scheduler;
        bool Initialized = false;

        std::mutex MainThreadMutex;
        std::vector<std::function<void()>> MainThreadQueue;

        std::mutex ActiveTasksMutex;
        std::vector<std::shared_ptr<enki::ICompletable>> ActiveTasks;
    };

    static std::unique_ptr<JobSystemData> s_Data = nullptr;

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
        if (s_Data && s_Data->Initialized) {
            AXLE_CORE_WARN("JobSystem::Init() called while already initialized!");
            return;
        }

        u32 hwThreads = std::thread::hardware_concurrency();
        if (hwThreads == 0) hwThreads = 4;

        u32 totalThreads = (numThreads > 0) ? numThreads : hwThreads;

        s_Data = std::make_unique<JobSystemData>();
        s_Data->Scheduler.Initialize(totalThreads);
        s_Data->Initialized = true;

        AXLE_CORE_INFO("JobSystem initialized with {0} worker threads (Hardware concurrency: {1})",
            s_Data->Scheduler.GetNumTaskThreads(), hwThreads);
    }

    void JobSystem::Shutdown() {
        if (!s_Data || !s_Data->Initialized) {
            return;
        }

        AXLE_CORE_INFO("JobSystem shutting down...");

        // Drain any pending tasks and shut down worker threads
        s_Data->Scheduler.WaitforAllAndShutdown();

        {
            std::lock_guard<std::mutex> lock(s_Data->ActiveTasksMutex);
            s_Data->ActiveTasks.clear();
        }

        {
            std::lock_guard<std::mutex> lock(s_Data->MainThreadMutex);
            s_Data->MainThreadQueue.clear();
        }

        s_Data->Initialized = false;
        s_Data.reset();
        AXLE_CORE_INFO("JobSystem shutdown complete.");
    }

    bool JobSystem::IsInitialized() {
        return s_Data != nullptr && s_Data->Initialized;
    }

    u32 JobSystem::GetWorkerThreadCount() {
        if (!IsInitialized()) return 1;
        return s_Data->Scheduler.GetNumTaskThreads();
    }

    u32 JobSystem::GetCurrentThreadIndex() {
        if (!IsInitialized()) return 0;
        uint32_t threadNum = s_Data->Scheduler.GetThreadNum();
        return (threadNum == enki::NO_THREAD_NUM) ? 0 : threadNum;
    }

    bool JobSystem::IsMainThread() {
        return GetCurrentThreadIndex() == 0;
    }

    enki::TaskScheduler& JobSystem::GetScheduler() {
        AXLE_CORE_ASSERT(s_Data && s_Data->Initialized, "JobSystem not initialized!");
        return s_Data->Scheduler;
    }

    void JobSystem::RunPinnedTasks() {
        if (!IsInitialized()) return;

        // Run enkiTS pinned tasks assigned to this thread
        s_Data->Scheduler.RunPinnedTasks();

        // If called on main thread, drain main thread dispatch queue
        if (IsMainThread()) {
            std::vector<std::function<void()>> toRun;
            {
                std::lock_guard<std::mutex> lock(s_Data->MainThreadMutex);
                if (!s_Data->MainThreadQueue.empty()) {
                    toRun.swap(s_Data->MainThreadQueue);
                }
            }

            for (auto& func : toRun) {
                if (func) func();
            }

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
    }

    JobHandle JobSystem::Dispatch(std::function<void()> func) {
        if (!IsInitialized()) {
            if (func) func();
            return JobHandle();
        }

        struct AsyncLambdaTask : public enki::ITaskSet {
            std::function<void()> TaskFunc;
            AsyncLambdaTask(std::function<void()> f)
                : enki::ITaskSet(1), TaskFunc(std::move(f)) {}

            void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
                (void)range; (void)threadnum;
                if (TaskFunc) TaskFunc();
            }
        };

        auto task = std::make_shared<AsyncLambdaTask>(std::move(func));

        {
            std::lock_guard<std::mutex> lock(s_Data->ActiveTasksMutex);
            s_Data->ActiveTasks.push_back(task);
        }

        s_Data->Scheduler.AddTaskSetToPipe(task.get());
        return JobHandle(task);
    }

    void JobSystem::Dispatch(enki::ITaskSet* taskSet) {
        if (!IsInitialized()) {
            if (taskSet) {
                enki::TaskSetPartition range{ 0, taskSet->m_SetSize };
                taskSet->ExecuteRange(range, 0);
            }
            return;
        }

        s_Data->Scheduler.AddTaskSetToPipe(taskSet);
    }

    void JobSystem::Wait(const enki::ICompletable* completable) {
        if (!IsInitialized() || !completable) return;
        s_Data->Scheduler.WaitforTask(completable);
    }

    void JobSystem::WaitForAll() {
        if (!IsInitialized()) return;
        s_Data->Scheduler.WaitforAll();
    }

    void JobSystem::DispatchToMainThread(std::function<void()> func) {
        if (!func) return;

        if (IsMainThread()) {
            func();
            return;
        }

        if (!IsInitialized()) {
            func();
            return;
        }

        std::lock_guard<std::mutex> lock(s_Data->MainThreadMutex);
        s_Data->MainThreadQueue.push_back(std::move(func));
    }

}
