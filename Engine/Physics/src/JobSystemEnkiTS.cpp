#include <Axle/JobSystemEnkiTS.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/Log.hpp>

#include <thread>
#include <chrono>
#include <algorithm>
#include <exception>

namespace Axle {

    static thread_local uint32_t t_InJobScope = 0;

    // --- PhysicsTaskRecord Implementation ---

    void JobSystemEnkiTS::PhysicsTaskRecord::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) {
        (void)range;
        (void)threadnum;
        Detail::TaskExecutionScope scope;
        if (m_Job) {
            Job* job = m_Job;
            m_Job = nullptr;
            job->Execute();
            job->Release();
        }
        m_BodyCompleted.store(true, std::memory_order_release);
        if (m_Owner) {
            m_Owner->OnTaskBodyCompleted(this);
        }
    }

    void JobSystemEnkiTS::PhysicsTaskRecord::ExecuteSynchronous() {
        if (m_Job) {
            Job* job = m_Job;
            m_Job = nullptr;
            job->Execute();
            job->Release();
        }
        m_BodyCompleted.store(true, std::memory_order_release);
    }

    // --- JobSystemEnkiTS Implementation ---

    JobSystemEnkiTS::JobSystemEnkiTS(JPH::uint inMaxJobs, JPH::uint inMaxBarriers) {
        Init(inMaxJobs, inMaxBarriers);
    }

    JobSystemEnkiTS::~JobSystemEnkiTS() {
        if (!Axle::JobSystem::IsMainThread() || t_InJobScope > 0 || Axle::JobSystem::GetActiveTaskDepth() > 0) {
            AXLE_CORE_ERROR("JobSystemEnkiTS destructor called from non-owner thread or worker task!");
            std::terminate(); // Returning would free records that executing jobs still own.
        }
        Shutdown();
    }

    void JobSystemEnkiTS::Shutdown() {
        if (!Axle::JobSystem::IsMainThread() || t_InJobScope > 0 || Axle::JobSystem::GetActiveTaskDepth() > 0) {
            AXLE_CORE_ERROR("JobSystemEnkiTS::Shutdown() rejected: called from non-owner thread or worker task!");
            return;
        }

        m_IsShutdown.store(true, std::memory_order_release);

        while (!WaitForLiveTasks(std::chrono::milliseconds(2000))) {
            AXLE_CORE_ERROR("JobSystemEnkiTS drain is still waiting for live submissions; retaining their storage");
        }
    }

    void JobSystemEnkiTS::Init(JPH::uint inMaxJobs, JPH::uint inMaxBarriers) {
        JobSystemWithBarrier::Init(inMaxBarriers);

        m_MaxJobs = inMaxJobs;

        JPH::uint32 pageSize = inMaxJobs;
        if (!JPH::IsPowerOf2(pageSize)) {
            pageSize = JPH::GetNextPowerOf2(pageSize);
        }
        m_Jobs.Init(inMaxJobs, pageSize);

        AXLE_CORE_INFO("JobSystemEnkiTS initialized (MaxJobs: {0}, MaxBarriers: {1}, Concurrency: {2})",
            inMaxJobs, inMaxBarriers, GetMaxConcurrency());
    }

    int JobSystemEnkiTS::GetMaxConcurrency() const {
        if (!Axle::JobSystem::IsInitialized()) {
            return static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        }
        return static_cast<int>(Axle::JobSystem::GetTotalThreadCount());
    }

    JPH::JobHandle JobSystemEnkiTS::CreateJob(const char* inName, JPH::ColorArg inColor, const JobFunction& inJobFunction, JPH::uint32 inNumDependencies) {
        if (m_IsShutdown.load(std::memory_order_acquire) && t_InJobScope == 0) {
            AXLE_CORE_WARN("JobSystemEnkiTS::CreateJob rejected: bridge is shut down!");
            return JPH::JobHandle();
        }

        JobFunction wrappedFunction = [inJobFunction] {
            struct JobScope {
                JobScope() { t_InJobScope++; }
                ~JobScope() { t_InJobScope--; }
            } scope;
            inJobFunction();
        };

        auto index = m_Jobs.ConstructObject(inName, inColor, this, wrappedFunction, inNumDependencies);
        if (index == AvailableJobs::cInvalidObjectIndex && Axle::JobSystem::IsMainThread() &&
            Axle::JobSystem::GetActiveTaskDepth() == 0 && t_InJobScope == 0) {
            // Jolt can finish a barrier by executing its jobs itself while their
            // enki wrappers are still queued and retain job-slot references.
            // Drain those physics wrappers before diagnosing genuine capacity
            // exhaustion. Never steal unrelated work or spin on held handles.
            if (GetLiveTaskCount() > 0) WaitForLiveTasks();
            // Even a zero count needs a retry: the last wrapper may have freed
            // its slot between the failed allocation and the live-count query.
            index = m_Jobs.ConstructObject(inName, inColor, this, wrappedFunction, inNumDependencies);
        }
        if (index == AvailableJobs::cInvalidObjectIndex) {
            AXLE_CORE_ERROR("JobSystemEnkiTS job capacity exhausted ({0}); submission rejected", m_MaxJobs);
            return {};
        }

        EnkiJob* job = &m_Jobs.Get(index);

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
        for (JPH::uint i = 0; i < inNumJobs; ++i) {
            Job* job = inJobs[i];
            job->AddRef();

            PhysicsTaskRecord* record = AcquireRecord();
            record->m_Job = job;

            // With no dedicated worker, run only this physics job. A scheduler
            // wait here could steal arbitrary Scene/UI work under Jolt locks.
            const bool synchronous = !Axle::JobSystem::IsInitialized() ||
                                     Axle::JobSystem::GetDedicatedWorkerThreadCount() == 0;
            if (synchronous || !Axle::JobSystem::TryAdmitPhysicsTaskSet(record)) {
                // If admission fails (e.g. Quiescing/Stopped/Queue full), execute synchronously
                record->ExecuteSynchronous();
                ReclaimRecordSynchronous(record);
            } else {
                record->m_Published.store(true, std::memory_order_release);
            }
        }
    }

    void JobSystemEnkiTS::FreeJob(Job* inJob) {
        m_Jobs.DestructObject(static_cast<EnkiJob*>(inJob));
    }

    void JobSystemEnkiTS::WaitForJobs(Barrier* inBarrier) {
        JobSystemWithBarrier::WaitForJobs(inBarrier);

        std::lock_guard<std::mutex> lock(m_PoolMutex);
        DrainPendingReclaim();
    }

    void JobSystemEnkiTS::OnTaskBodyCompleted(PhysicsTaskRecord* record) {
        std::lock_guard<std::mutex> lock(m_PoolMutex);
        m_PendingReclaim.push_back(record);
    }

    JobSystemEnkiTS::PhysicsTaskRecord* JobSystemEnkiTS::AcquireRecord() {
        std::lock_guard<std::mutex> lock(m_PoolMutex);
        DrainPendingReclaim();

        PhysicsTaskRecord* record = nullptr;
        if (!m_FreeRecords.empty()) {
            record = m_FreeRecords.back();
            m_FreeRecords.pop_back();
        } else {
            auto newRec = std::make_unique<PhysicsTaskRecord>();
            record = newRec.get();
            m_AllRecords.push_back(std::move(newRec));
        }

        u32 live = m_LiveTaskCount.fetch_add(1, std::memory_order_relaxed) + 1;
        u32 currentHw = m_AllocatedHighWaterMark.load(std::memory_order_relaxed);
        while (live > currentHw && !m_AllocatedHighWaterMark.compare_exchange_weak(currentHw, live, std::memory_order_relaxed)) {
        }

        record->m_Owner = this;
        record->m_Job = nullptr;
        record->m_BodyCompleted.store(false, std::memory_order_relaxed);
        record->m_Published.store(false, std::memory_order_relaxed);
        return record;
    }

    void JobSystemEnkiTS::DrainPendingReclaim() {
        if (m_PendingReclaim.empty()) return;

        auto it = m_PendingReclaim.begin();
        while (it != m_PendingReclaim.end()) {
            PhysicsTaskRecord* record = *it;
            if (record->m_Published.load(std::memory_order_acquire) && record->GetIsComplete()) {
                record->m_Job = nullptr;
                record->m_BodyCompleted.store(false, std::memory_order_relaxed);
                m_FreeRecords.push_back(record);
                m_LiveTaskCount.fetch_sub(1, std::memory_order_release);
                it = m_PendingReclaim.erase(it);
            } else {
                ++it;
            }
        }
    }

    void JobSystemEnkiTS::ReclaimRecordSynchronous(PhysicsTaskRecord* record) {
        std::lock_guard<std::mutex> lock(m_PoolMutex);
        record->m_Job = nullptr;
        record->m_BodyCompleted.store(false, std::memory_order_relaxed);
        m_FreeRecords.push_back(record);
        m_LiveTaskCount.fetch_sub(1, std::memory_order_release);
    }

    u32 JobSystemEnkiTS::GetLiveTaskCount() const {
        std::lock_guard<std::mutex> lock(m_PoolMutex);
        const_cast<JobSystemEnkiTS*>(this)->DrainPendingReclaim();
        return m_LiveTaskCount.load(std::memory_order_relaxed);
    }

    u32 JobSystemEnkiTS::GetAllocatedTaskCount() const {
        std::lock_guard<std::mutex> lock(m_PoolMutex);
        return static_cast<u32>(m_AllRecords.size());
    }

    u32 JobSystemEnkiTS::GetAllocatedHighWaterMark() const {
        return m_AllocatedHighWaterMark.load(std::memory_order_relaxed);
    }

    bool JobSystemEnkiTS::WaitForLiveTasks(std::chrono::milliseconds timeout) {
        if (!Axle::JobSystem::IsMainThread() || t_InJobScope > 0 || Axle::JobSystem::GetActiveTaskDepth() > 0) return false;
        const auto start = std::chrono::steady_clock::now();
        while (GetLiveTaskCount() > 0) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start) > timeout) {
                return false;
            }
            PhysicsTaskRecord* pending = nullptr;
            {
                std::lock_guard<std::mutex> lock(m_PoolMutex);
                for (auto& record : m_AllRecords) {
                    if (record->m_Published.load(std::memory_order_acquire) && !record->GetIsComplete()) {
                        pending = record.get();
                        break;
                    }
                }
            }
            // The reserved high lane contains only physics wrappers. It cannot
            // steal general work even when every dedicated worker is occupied.
            if (pending) Axle::JobSystem::WaitPhysicsTask(pending);
            else std::this_thread::yield();
        }
        return true;
    }

}
