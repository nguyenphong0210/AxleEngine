#pragma once

#include <Axle/Types.hpp>
#include <Axle/JobSystem.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/FixedSizeFreeList.h>

#include <TaskScheduler.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace Axle {

    /// Implementation of JPH::JobSystem bridging Jolt Physics tasks into Axle's unified enkiTS JobSystem.
    /// Eliminates thread oversubscription by routing constraint solving, collision detection, and island
    /// building into Axle::JobSystem worker threads instead of maintaining a separate OS thread pool.
    class JobSystemEnkiTS final : public JPH::JobSystemWithBarrier {
    public:
        JPH_OVERRIDE_NEW_DELETE

        /// Stable owned submission record passed to enkiTS.
        /// Has a permanent stable memory address; lifetime is decoupled from JPH::Job slot allocation.
        class PhysicsTaskRecord : public enki::ITaskSet {
        public:
            PhysicsTaskRecord() : enki::ITaskSet(1) {}

            void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;
            void ExecuteSynchronous();

            JobSystemEnkiTS* m_Owner = nullptr;
            Job* m_Job = nullptr;
            std::atomic<bool> m_BodyCompleted{false};
            std::atomic<bool> m_Published{false};
        };

        explicit JobSystemEnkiTS(JPH::uint inMaxJobs = JPH::cMaxPhysicsJobs, JPH::uint inMaxBarriers = JPH::cMaxPhysicsBarriers);
        virtual ~JobSystemEnkiTS() override;

        void Init(JPH::uint inMaxJobs, JPH::uint inMaxBarriers);

        // JPH::JobSystem interface overrides
        virtual int GetMaxConcurrency() const override;
        virtual JPH::JobHandle CreateJob(const char* inName, JPH::ColorArg inColor, const JobFunction& inJobFunction, JPH::uint32 inNumDependencies = 0) override;
        virtual void WaitForJobs(Barrier* inBarrier) override;

        // Metrics and lifecycle inspection for verification
        u32 GetLiveTaskCount() const;
        u32 GetAllocatedTaskCount() const;
        u32 GetAllocatedHighWaterMark() const;
        bool WaitForLiveTasks(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

        // Explicit shutdown and disposal control
        void Shutdown();
        bool IsShutdown() const { return m_IsShutdown.load(std::memory_order_acquire); }

    protected:
        virtual void QueueJob(Job* inJob) override;
        virtual void QueueJobs(Job** inJobs, JPH::uint inNumJobs) override;
        virtual void FreeJob(Job* inJob) override;

    private:
        friend class PhysicsTaskRecord;

        void OnTaskBodyCompleted(PhysicsTaskRecord* record);
        PhysicsTaskRecord* AcquireRecord();
        void DrainPendingReclaim();
        void ReclaimRecordSynchronous(PhysicsTaskRecord* record);

        class EnkiJob : public Job {
        public:
            using Job::Job;
        };

        using AvailableJobs = JPH::FixedSizeFreeList<EnkiJob>;
        AvailableJobs m_Jobs;

        JPH::uint32 m_MaxJobs = 0;

        // Thread-safe stable submission record pool
        mutable std::mutex m_PoolMutex;
        std::vector<std::unique_ptr<PhysicsTaskRecord>> m_AllRecords;
        std::vector<PhysicsTaskRecord*> m_FreeRecords;
        std::vector<PhysicsTaskRecord*> m_PendingReclaim;

        std::atomic<u32> m_LiveTaskCount{0};
        std::atomic<u32> m_AllocatedHighWaterMark{0};
        std::atomic<bool> m_IsShutdown{false};
    };

}
