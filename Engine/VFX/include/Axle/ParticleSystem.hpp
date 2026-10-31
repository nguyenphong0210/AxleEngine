#pragma once

#include <Axle/ParticlePool.hpp>
#include <Axle/ParticleEmitter.hpp>
#include <functional>
#include <memory>
#include <unordered_map>

namespace Axle {

    class ParticleSystem {
    public:
        using SubEmitterCallback = std::function<void(SubEmitterTrigger trigger, const std::string& targetName, const glm::vec3& pos, const glm::vec3& vel, const glm::vec4& color, uint32_t count)>;

        explicit ParticleSystem(const ParticleEmitterConfig& config = ParticleEmitterConfig());
        ~ParticleSystem() = default;

        void Update(float dt,
                    const glm::vec3& emitterWorldPos = glm::vec3(0.0f),
                    const glm::quat& emitterWorldRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

        void EmitBurst(uint32_t count,
                       const glm::vec3& emitterWorldPos = glm::vec3(0.0f),
                       const glm::quat& emitterWorldRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

        void SetEmitting(bool emitting) { m_IsEmitting = emitting; }
        bool IsEmitting() const { return m_IsEmitting; }

        void Reset();

        const ParticlePool& GetPool() const { return m_Pool; }
        ParticlePool& GetPool() { return m_Pool; }

        const ParticleEmitterConfig& GetConfig() const { return m_Config; }
        ParticleEmitterConfig& GetConfig() { return m_Config; }
        void SetConfig(const ParticleEmitterConfig& config);

        uint32_t GetActiveParticleCount() const;
        float GetTotalSimulationTime() const { return m_TotalSimulationTime; }
        uint64_t GetCollisionCount() const { return m_CollisionCount; }

        // Sub-Emitter & Multi-Emitter Child Management
        void SetSubEmitterCallback(SubEmitterCallback cb) { m_SubEmitterCallback = std::move(cb); }
        void AddChildSystem(const std::string& name, const std::shared_ptr<ParticleSystem>& child);
        void RemoveChildSystem(const std::string& name);
        std::shared_ptr<ParticleSystem> GetChildSystem(const std::string& name) const;
        const std::unordered_map<std::string, std::shared_ptr<ParticleSystem>>& GetChildSystems() const { return m_ChildSystems; }

    private:
        void SpawnParticle(const glm::vec3& emitterWorldPos, const glm::quat& emitterWorldRot);
        void DispatchSubEmitters(SubEmitterTrigger trigger, const glm::vec3& pos, const glm::vec3& vel, const glm::vec4& color);

    private:
        ParticleEmitterConfig m_Config;
        ParticlePool m_Pool;

        float m_SpawnAccumulator = 0.0f;
        float m_BurstTimer = 0.0f;
        float m_TotalSimulationTime = 0.0f;
        uint64_t m_CollisionCount = 0;
        bool m_IsEmitting = true;

        SubEmitterCallback m_SubEmitterCallback;
        std::unordered_map<std::string, std::shared_ptr<ParticleSystem>> m_ChildSystems;
    };

}
