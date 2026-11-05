#pragma once

#include <Axle/Types.hpp>
#include <TaskScheduler.h>

#include <atomic>
#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

namespace Axle {

    class JobSystem;

    // Lifecycle states for the JobSystem scheduler
    enum class JobSystemState {
        Stopped = 0,
        Running,
        Quiescing,
        Draining
    };

    inline static constexpr u32 InvalidThreadIndex = enki::NO_THREAD_NUM;

    // Operation owner state supporting generation-based invalidation
    struct OperationOwnerState {
        std::atomic<bool> Valid{true};
        std::atomic<u64> Generation{1};
    };

    // Lightweight token carried by queued completions to verify ownership validity
    class OwnerToken {
    public:
        OwnerToken() = default;
        OwnerToken(std::weak_ptr<OperationOwnerState> state, u64 generation)
            : m_State(std::move(state)), m_Generation(generation), m_IsGlobal(false) {}

        // Global token that remains perpetually valid across the engine session
        static OwnerToken Global() {
            OwnerToken token;
            token.m_IsGlobal = true;
            return token;
        }

        // Returns true if the owning object is still alive and has not been invalidated or generation-bumped
        bool IsValid() const {
            if (m_IsGlobal) return true;
            auto state = m_State.lock();
            return state && state->Valid.load(std::memory_order_acquire) &&
                   (state->Generation.load(std::memory_order_acquire) == m_Generation);
        }

        u64 GetGeneration() const { return m_Generation; }
        bool IsGlobal() const { return m_IsGlobal; }

    private:
        std::weak_ptr<OperationOwnerState> m_State;
        u64 m_Generation = 0;
        bool m_IsGlobal = false;
    };

    // Scoped owner of asynchronous main-thread operations.
    // Invalidation or destruction safely prevents pending callbacks from executing.
    class OperationOwner {
    public:
        OperationOwner()
            : m_State(std::make_shared<OperationOwnerState>()) {}

        ~OperationOwner() {
            Invalidate();
        }

        OperationOwner(const OperationOwner&) = delete;
        OperationOwner& operator=(const OperationOwner&) = delete;
        OperationOwner(OperationOwner&&) noexcept = default;
        OperationOwner& operator=(OperationOwner&&) noexcept = default;

        // Invalidate all tokens issued up to this point
        void Invalidate() {
            if (m_State) {
                m_State->Valid.store(false, std::memory_order_release);
                m_State->Generation.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Reset and begin a fresh generation
        void Reset() {
            Invalidate();
            m_State = std::make_shared<OperationOwnerState>();
        }

        bool IsValid() const {
            return m_State && m_State->Valid.load(std::memory_order_acquire);
        }

        u64 GetGeneration() const {
            return m_State ? m_State->Generation.load(std::memory_order_acquire) : 0;
        }

        OwnerToken GetToken() const {
            if (!m_State) return OwnerToken{};
            return OwnerToken(m_State, m_State->Generation.load(std::memory_order_acquire));
        }

    private:
        std::shared_ptr<OperationOwnerState> m_State;
    };

    enum class PostResult {
        Success = 0,
        RejectedStopped,
        RejectedQueueFull
    };

    struct QueuedCompletion {
        OwnerToken Token;
        u32 Epoch = 0;
        std::function<void()> Callback;
    };

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
        // Must be called from the Engine's OS owner thread.
        // numThreads: Total threads in scheduler (including main thread = 0).
        // If 0, automatically uses CPU hardware concurrency.
        static void Init(u32 numThreads = 0);

        // Terminate worker threads and shut down the scheduler.
        // Must be called from the Engine's OS owner thread.
        static void Shutdown();

        // Returns true if the JobSystem is initialized and actively running.
        static bool IsInitialized();

        // Current lifecycle state of the JobSystem scheduler.
        static JobSystemState GetState();

        // Current initialization epoch counter (increments on each successful Init).
        static u32 GetEpoch();

        // Total threads participating in scheduling (including main thread = 0). Returns 0 when stopped.
        static u32 GetTotalThreadCount();

        // Dedicated background worker threads created by scheduler (excluding main thread). Returns 0 when stopped.
        static u32 GetDedicatedWorkerThreadCount();

        // Total worker/task threads in scheduler (including main thread = 0). Preserved for existing callers.
        static u32 GetWorkerThreadCount();

        // Calling thread's scheduler index (0 = main thread, < TotalThreadCount, or enki::NO_THREAD_NUM).
        // External unregistered threads receive enki::NO_THREAD_NUM (never mapped to 0).
        static u32 GetCurrentThreadIndex();

        // Returns true if caller is executing on the Engine's OS owner thread.
        // Verifies actual OS thread identity; preserves false for external threads across stopped/reinitialized states.
        static bool IsMainThread();

        // Returns true if caller is executing in a valid scheduler context (OS owner thread or registered worker).
        static bool IsValidSchedulerContext();

        // Direct access to the underlying enki::TaskScheduler.
        static enki::TaskScheduler& GetScheduler();

        // Run pinned tasks scheduled for the calling thread (e.g. main thread frame loop).
        // Must be called from the OS owner thread.
        static void RunPinnedTasks();

        // Dispatch a lambda task asynchronously. Returns a JobHandle to query or wait on.
        // Rejects new application work after quiescing; preserves progress of already-admitted task graphs.
        static JobHandle Dispatch(std::function<void()> func);

        // Dispatch an enki::ITaskSet to the scheduler without blocking.
        static void Dispatch(enki::ITaskSet* taskSet);

        // Block until the specified task or completable is finished.
        // Rejects execution from unsupported external thread contexts with an active diagnostic.
        static void Wait(const enki::ICompletable* completable);

        // Block until all currently queued tasks are finished.
        static void WaitForAll();

        // Post an always-deferred operation completion to be executed on the Engine's OS owner thread.
        // Never executes inline; caller can specify an OwnerToken for automatic cancellation.
        static PostResult PostToMainThread(const OwnerToken& owner, std::function<void()> completion);
        static PostResult PostToMainThread(std::function<void()> completion);

        // Execute a bounded FIFO batch of queued main-thread completions at a safe frame boundary.
        // Returns the number of completions actually executed during this pump.
        // Discards entries whose owner token is invalid or whose epoch does not match.
        // maxItems: Maximum number of completions to pump in one call (0 = drain all currently queued).
        static u32 PumpMainThreadCompletions(u32 maxItems = 1024);

        // Post a function to be executed specifically on the main thread (thread 0).
        // Accepts external completion producer threads safely.
        // Unified compatibility wrapper that routes through PostToMainThread (always deferred).
        static void DispatchToMainThread(std::function<void()> func);

        // Internal permit / task depth helpers for admitted task graphs and internal children.
        static void IncrementActiveTaskDepth();
        static void DecrementActiveTaskDepth();
        static u32 GetActiveTaskDepth();

        // Internal admission helper: synchronizes admission check and enqueue under admission lock.
        static bool TryAdmitTaskSet(enki::ITaskSet* taskSet);

        // Diagnostic reporter for unsupported external context violations in both Debug and Release.
        static void ReportInvalidContext(const char* operation);

        // Optional reset helper for isolated test harnesses.
        static void ResetOwnerForTesting();

        // Execute a parallel loop dividing [0, count) across worker threads.
        // Supports func(u32 index) or func(u32 index, u32 threadNum).
        // minRange specifies the grain size to avoid excessive subdivision.
        template<typename Func>
        static void ParallelFor(u32 count, Func&& func, u32 minRange = 1) {
            if (count == 0) return;
            if (!IsValidSchedulerContext()) {
                ReportInvalidContext("ParallelFor");
                return;
            }
            if (!IsInitialized() || count == 1 || GetTotalThreadCount() <= 1) {
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
                    IncrementActiveTaskDepth();
                    for (uint32_t i = range.start; i < range.end; ++i) {
                        if constexpr (std::is_invocable_v<std::decay_t<Func>, uint32_t, uint32_t>) {
                            m_Func(i, threadnum);
                        } else {
                            m_Func(i);
                        }
                    }
                    DecrementActiveTaskDepth();
                }
            };

            LambdaTaskSet taskSet(count, minRange, std::forward<Func>(func));
            if (TryAdmitTaskSet(&taskSet)) {
                Wait(&taskSet);
            } else {
                if (IsMainThread()) {
                    for (u32 i = 0; i < count; ++i) {
                        if constexpr (std::is_invocable_v<std::decay_t<Func>, uint32_t, uint32_t>) {
                            func(i, 0);
                        } else {
                            func(i);
                        }
                    }
                }
            }
        }

        // Execute a parallel loop passing partition ranges [start, end) across worker threads.
        // Supports func(u32 start, u32 end) or func(u32 start, u32 end, u32 threadNum).
        template<typename Func>
        static void ParallelForRange(u32 count, Func&& func, u32 minRange = 1) {
            if (count == 0) return;
            if (!IsValidSchedulerContext()) {
                ReportInvalidContext("ParallelForRange");
                return;
            }
            if (!IsInitialized() || count == 1 || GetTotalThreadCount() <= 1) {
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
                    IncrementActiveTaskDepth();
                    if constexpr (std::is_invocable_v<std::decay_t<Func>, uint32_t, uint32_t, uint32_t>) {
                        m_Func(range.start, range.end, threadnum);
                    } else {
                        m_Func(range.start, range.end);
                    }
                    DecrementActiveTaskDepth();
                }
            };

            LambdaRangeTaskSet taskSet(count, minRange, std::forward<Func>(func));
            if (TryAdmitTaskSet(&taskSet)) {
                Wait(&taskSet);
            } else {
                if (IsMainThread()) {
                    if constexpr (std::is_invocable_v<std::decay_t<Func>, uint32_t, uint32_t, uint32_t>) {
                        func(0, count, 0);
                    } else {
                        func(0, count);
                    }
                }
            }
        }
    };

}
