#pragma once

#include <Axle/Types.hpp>
#include <Axle/JobSystem.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/FixedSizeFreeList.h>

#include <TaskScheduler.h>

#include <memory>
#include <atomic>

namespace Axle {

    /// Implementation of JPH::JobSystem bridging Jolt Physics tasks into Axle's unified enkiTS JobSystem.
    /// Eliminates thread oversubscription by routing constraint solving, collision detection, and island
    /// building into Axle::JobSystem worker threads instead of maintaining a separate OS thread pool.
    class JobSystemEnkiTS final : public JPH::JobSystemWithBarrier {
    public:
        JPH_OVERRIDE_NEW_DELETE

        explicit JobSystemEnkiTS(JPH::uint inMaxJobs = JPH::cMaxPhysicsJobs, JPH::uint inMaxBarriers = JPH::cMaxPhysicsBarriers);
        virtual ~JobSystemEnkiTS() override;

        void Init(JPH::uint inMaxJobs, JPH::uint inMaxBarriers);

        // JPH::JobSystem interface overrides
        virtual int GetMaxConcurrency() const override;
        virtual JPH::JobHandle CreateJob(const char* inName, JPH::ColorArg inColor, const JobFunction& inJobFunction, JPH::uint32 inNumDependencies = 0) override;
        virtual void WaitForJobs(Barrier* inBarrier) override;

    protected:
        virtual void QueueJob(Job* inJob) override;
        virtual void QueueJobs(Job** inJobs, JPH::uint inNumJobs) override;
        virtual void FreeJob(Job* inJob) override;

    private:
        class EnkiJob : public Job {
        public:
            using Job::Job;
            JPH::uint32 m_Index = 0;
        };

        class EnkiTask : public enki::ITaskSet {
        public:
            EnkiTask() : enki::ITaskSet(1) {}

            void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;

            JobSystemEnkiTS* m_Owner = nullptr;
            Job* m_Job = nullptr;
        };

        using AvailableJobs = JPH::FixedSizeFreeList<EnkiJob>;
        AvailableJobs m_Jobs;

        std::unique_ptr<EnkiTask[]> m_EnkiTasks;
        JPH::uint32 m_MaxJobs = 0;
        std::atomic<uint32_t> m_ActiveEnkiTasks{ 0 };
    };

}
