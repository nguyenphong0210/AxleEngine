#pragma once

#include <Axle/Types.hpp>
#include <TaskScheduler.h>

#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

namespace Axle {

    class JobSystem;

    // Lightweight handle referencing an asynchronous task dispatched to JobSystem
    class JobHandle {
    public:
        JobHandle() = default;
        explicit JobHandle(std::shared_ptr<enki::ICompletable> task)
            : m_Task(std::move(task)) {}

        bool IsValid() const { return m_Task != nullptr; }
        bool IsComplete() const;
        void Wait();

        enki::ICompletable* GetTask() const { return m_Task.get(); }

    private:
        std::shared_ptr<enki::ICompletable> m_Task;
    };

    class JobSystem {
    public:
        // Initialize persistent worker threads matching CPU hardware concurrency.
        // numThreads: Total threads in scheduler (including main thread = 0).
        // If 0, automatically uses CPU hardware concurrency.
        static void Init(u32 numThreads = 0);

        // Terminate worker threads and shut down the scheduler.
        static void Shutdown();

        // Returns true if the JobSystem is initialized and active.
        static bool IsInitialized();

        // Total worker threads participating in scheduling (including main thread).
        static u32 GetWorkerThreadCount();

        // Calling thread's scheduler index (0 = main thread, < WorkerThreadCount).
        static u32 GetCurrentThreadIndex();

        // Returns true if caller is executing on the engine's main thread (thread 0).
        static bool IsMainThread();

        // Direct access to the underlying enki::TaskScheduler.
        static enki::TaskScheduler& GetScheduler();

        // Run pinned tasks scheduled for the calling thread (e.g. main thread frame loop).
        static void RunPinnedTasks();

        // Dispatch a lambda task asynchronously. Returns a JobHandle to query or wait on.
        static JobHandle Dispatch(std::function<void()> func);

        // Dispatch an enki::ITaskSet to the scheduler without blocking.
        static void Dispatch(enki::ITaskSet* taskSet);

        // Block until the specified task or completable is finished.
        static void Wait(const enki::ICompletable* completable);

        // Block until all currently queued tasks are finished.
        static void WaitForAll();

        // Post a function to be executed specifically on the main thread (thread 0).
        static void DispatchToMainThread(std::function<void()> func);

        // Execute a parallel loop dividing [0, count) across worker threads.
        // Supports func(u32 index) or func(u32 index, u32 threadNum).
        // minRange specifies the grain size to avoid excessive subdivision.
        template<typename Func>
        static void ParallelFor(u32 count, Func&& func, u32 minRange = 1) {
            if (count == 0) return;
            if (!IsInitialized() || count == 1 || GetWorkerThreadCount() <= 1) {
                for (u32 i = 0; i < count; ++i) {
                    if constexpr (std::is_invocable_v<std::decay_t<Func>, uint32_t, uint32_t>) {
                        func(i, 0);
                    } else {
                        func(i);
                    }
                }
                return;
            }

            struct LambdaTaskSet : public enki::ITaskSet {
                std::decay_t<Func> m_Func;
                LambdaTaskSet(u32 size, u32 minR, Func&& f)
                    : enki::ITaskSet(size, minR), m_Func(std::forward<Func>(f)) {}

                void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
                    for (uint32_t i = range.start; i < range.end; ++i) {
                        if constexpr (std::is_invocable_v<std::decay_t<Func>, uint32_t, uint32_t>) {
                            m_Func(i, threadnum);
                        } else {
                            m_Func(i);
                        }
                    }
                }
            };

            LambdaTaskSet taskSet(count, minRange, std::forward<Func>(func));
            GetScheduler().AddTaskSetToPipe(&taskSet);
            GetScheduler().WaitforTask(&taskSet);
        }

        // Execute a parallel loop passing partition ranges [start, end) across worker threads.
        // Supports func(u32 start, u32 end) or func(u32 start, u32 end, u32 threadNum).
        template<typename Func>
        static void ParallelForRange(u32 count, Func&& func, u32 minRange = 1) {
            if (count == 0) return;
            if (!IsInitialized() || count == 1 || GetWorkerThreadCount() <= 1) {
                if constexpr (std::is_invocable_v<std::decay_t<Func>, uint32_t, uint32_t, uint32_t>) {
                    func(0, count, 0);
                } else {
                    func(0, count);
                }
                return;
            }

            struct LambdaRangeTaskSet : public enki::ITaskSet {
                std::decay_t<Func> m_Func;
                LambdaRangeTaskSet(u32 size, u32 minR, Func&& f)
                    : enki::ITaskSet(size, minR), m_Func(std::forward<Func>(f)) {}

                void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
                    if constexpr (std::is_invocable_v<std::decay_t<Func>, uint32_t, uint32_t, uint32_t>) {
                        m_Func(range.start, range.end, threadnum);
                    } else {
                        m_Func(range.start, range.end);
                    }
                }
            };

            LambdaRangeTaskSet taskSet(count, minRange, std::forward<Func>(func));
            GetScheduler().AddTaskSetToPipe(&taskSet);
            GetScheduler().WaitforTask(&taskSet);
        }
    };

}
