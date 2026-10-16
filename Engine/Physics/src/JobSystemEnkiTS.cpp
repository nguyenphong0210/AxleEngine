#include <Axle/JobSystemEnkiTS.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/Log.hpp>

#include <thread>
#include <chrono>
#include <algorithm>

namespace Axle {

    JobSystemEnkiTS::JobSystemEnkiTS(JPH::uint inMaxJobs, JPH::uint inMaxBarriers) {
        if (!PhysicsEngine::IsInitialized()) {
            PhysicsEngine::Init();
        }
        if (!Axle::JobSystem::IsInitialized()) {
            Axle::JobSystem::Init();
        }
        Init(inMaxJobs, inMaxBarriers);
    }

    JobSystemEnkiTS::~JobSystemEnkiTS() {
        while (m_ActiveEnkiTasks.load(std::memory_order_acquire) > 0) {
            std::this_thread::yield();
        }
    }

    void JobSystemEnkiTS::Init(JPH::uint inMaxJobs, JPH::uint inMaxBarriers) {
        if (!PhysicsEngine::IsInitialized()) {
            PhysicsEngine::Init();
        }
        if (!Axle::JobSystem::IsInitialized()) {
            Axle::JobSystem::Init();
        }
        JobSystemWithBarrier::Init(inMaxBarriers);

        m_MaxJobs = inMaxJobs;

        JPH::uint32 pageSize = inMaxJobs;
        if (!JPH::IsPowerOf2(pageSize)) {
            pageSize = JPH::GetNextPowerOf2(pageSize);
        }
        m_Jobs.Init(inMaxJobs, pageSize);

        m_EnkiTasks = std::make_unique<EnkiTask[]>(inMaxJobs);
        for (JPH::uint32 i = 0; i < inMaxJobs; ++i) {
            m_EnkiTasks[i].m_Owner = this;
            m_EnkiTasks[i].m_Job = nullptr;
        }

        AXLE_CORE_INFO("JobSystemEnkiTS initialized (MaxJobs: {0}, MaxBarriers: {1}, Concurrency: {2})",
            inMaxJobs, inMaxBarriers, GetMaxConcurrency());
    }

    int JobSystemEnkiTS::GetMaxConcurrency() const {
        if (!Axle::JobSystem::IsInitialized()) {
            return static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        }
        return static_cast<int>(Axle::JobSystem::GetScheduler().GetNumTaskThreads());
    }

    JPH::JobHandle JobSystemEnkiTS::CreateJob(const char* inName, JPH::ColorArg inColor, const JobFunction& inJobFunction, JPH::uint32 inNumDependencies) {
        JPH::uint32 index;
        for (;;) {
            index = m_Jobs.ConstructObject(inName, inColor, this, inJobFunction, inNumDependencies);
            if (index != AvailableJobs::cInvalidObjectIndex)
                break;
            JPH_ASSERT(false, "No jobs available in JobSystemEnkiTS!");
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        EnkiJob* job = &m_Jobs.Get(index);
        job->m_Index = index;

        // Construct handle to keep a reference
        JPH::JobHandle handle(job);

        // If no dependencies, immediately queue the job
        if (inNumDependencies == 0)
            QueueJob(job);

        return handle;
    }

    void JobSystemEnkiTS::QueueJob(Job* inJob) {
        QueueJobs(&inJob, 1);
    }

    void JobSystemEnkiTS::QueueJobs(Job** inJobs, JPH::uint inNumJobs) {
        if (!Axle::JobSystem::IsInitialized()) {
            Axle::JobSystem::Init();
        }
        auto& scheduler = Axle::JobSystem::GetScheduler();
        for (JPH::uint i = 0; i < inNumJobs; ++i) {
            Job* job = inJobs[i];
            job->AddRef();

            EnkiJob* enkiJob = static_cast<EnkiJob*>(job);
            JPH::uint32 index = enkiJob->m_Index;
            m_EnkiTasks[index].m_Job = job;
            m_EnkiTasks[index].m_Owner = this;

            m_ActiveEnkiTasks.fetch_add(1, std::memory_order_relaxed);
            scheduler.AddTaskSetToPipe(&m_EnkiTasks[index]);
        }
    }

    void JobSystemEnkiTS::FreeJob(Job* inJob) {
        m_Jobs.DestructObject(static_cast<EnkiJob*>(inJob));
    }

    void JobSystemEnkiTS::WaitForJobs(Barrier* inBarrier) {
        JobSystemWithBarrier::WaitForJobs(inBarrier);
    }

    void JobSystemEnkiTS::EnkiTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) {
        (void)range;
        (void)threadnum;
        if (m_Job) {
            Job* job = m_Job;
            m_Job = nullptr;
            job->Execute();
            job->Release();
        }
        if (m_Owner) {
            m_Owner->m_ActiveEnkiTasks.fetch_sub(1, std::memory_order_release);
        }
    }

}
