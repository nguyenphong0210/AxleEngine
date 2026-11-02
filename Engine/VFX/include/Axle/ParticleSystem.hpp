#pragma once

#include <Axle/ParticlePool.hpp>
#include <Axle/ParticleEmitter.hpp>
#include <functional>
#include <memory>
#include <unordered_map>

namespace Axle {

    struct BurstRuntimeState {
        uint32_t CompletedCycles = 0;
        float NextTriggerTime = 0.0f;
    };

    struct ParticleEvent {
        std::string SourceEmitterName;
        std::string TargetEmitterName;
        SubEmitterTrigger Trigger = SubEmitterTrigger::Collision;
        glm::vec3 WorldPosition{ 0.0f };
        glm::vec3 WorldVelocity{ 0.0f };
        glm::vec4 Color{ 1.0f };
        uint32_t Count = 10;
        bool InheritColor = true;
        bool InheritVelocity = false;
    };

    class ParticleSystem {
    public:
        using SubEmitterCallback = std::function<void(SubEmitterTrigger trigger, const std::string& targetName, const glm::vec3& pos, const glm::vec3& vel, const glm::vec4& color, uint32_t count)>;

        explicit ParticleSystem(const ParticleEmitterConfig& config = ParticleEmitterConfig());
        ~ParticleSystem() = default;

        void Update(float dt,
                    const glm::vec3& emitterWorldPos = glm::vec3(0.0f),
                    const glm::quat& emitterWorldRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                    const glm::vec3& emitterWorldScale = glm::vec3(1.0f));

        void EmitBurst(uint32_t count,
                       const glm::vec3& emitterWorldPos = glm::vec3(0.0f),
                       const glm::quat& emitterWorldRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                    const glm::vec3& emitterWorldScale = glm::vec3(1.0f));

        void SetEmitting(bool emitting) { m_IsEmitting = emitting; }
        bool IsEmitting() const { return m_IsEmitting; }

        void Reset();
        void SetEmitterTransform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale = glm::vec3(1)) {
            m_WorldPosition = position; m_WorldRotation = rotation; m_WorldScale = scale;
        }

        const ParticlePool& GetPool() const { return m_Pool; }
        ParticlePool& GetPool() { return m_Pool; }

        const ParticleEmitterConfig& GetConfig() const { return m_Config; }
        ParticleEmitterConfig& GetConfig() { return m_Config; }
        void SetConfig(const ParticleEmitterConfig& config);

        uint32_t GetActiveParticleCount() const;
        float GetTotalSimulationTime() const { return m_TotalSimulationTime; }
        float GetEmitterTime() const { return m_EmitterTime; }
        uint64_t GetCollisionCount() const { return m_CollisionCount; }

        void SetSeed(uint32_t seed);
        uint32_t GetSeed() const { return m_InitialSeed; }
        std::mt19937& GetRNG() { return m_RNG; }

        // Sub-Emitter & Multi-Emitter Child Management
        void SetSubEmitterCallback(SubEmitterCallback cb) { m_SubEmitterCallback = std::move(cb); }
        void AddChildSystem(const std::string& name, const std::shared_ptr<ParticleSystem>& child);
        void RemoveChildSystem(const std::string& name);
        std::shared_ptr<ParticleSystem> GetChildSystem(const std::string& name) const;
        const std::unordered_map<std::string, std::shared_ptr<ParticleSystem>>& GetChildSystems() const { return m_ChildSystems; }

        const std::vector<ParticleEvent>& GetEventQueue() const { return m_EventQueue; }
        void ClearEventQueue() { m_EventQueue.clear(); }
        void DrainEvents(std::vector<ParticleEvent>& outEvents) {
            outEvents.insert(outEvents.end(), m_EventQueue.begin(), m_EventQueue.end());
            m_EventQueue.clear();
        }
        void SpawnInherited(const glm::vec3& worldPos, const glm::vec3& inheritedVel, const glm::vec4& inheritedColor, bool inheritColor, bool inheritVel, uint32_t count);

    private:
        void SpawnParticle(const glm::vec3& emitterWorldPos, const glm::quat& emitterWorldRot);
        void DispatchSubEmitters(SubEmitterTrigger trigger, const glm::vec3& pos, const glm::vec3& vel, const glm::vec4& color);

    private:
        ParticleEmitterConfig m_Config;
        ParticlePool m_Pool;

        glm::vec3 m_WorldPosition{0}, m_WorldScale{1};
        glm::quat m_WorldRotation{1,0,0,0};
        float m_SpawnAccumulator = 0.0f;
        float m_BurstTimer = 0.0f;
        float m_TotalSimulationTime = 0.0f;
        float m_EmitterTime = 0.0f;
        uint64_t m_CollisionCount = 0;
        bool m_IsEmitting = true;

        uint32_t m_InitialSeed = 1337;
        std::mt19937 m_RNG{ 1337 };

        std::vector<BurstRuntimeState> m_BurstRuntime;
        SubEmitterCallback m_SubEmitterCallback;
        std::unordered_map<std::string, std::shared_ptr<ParticleSystem>> m_ChildSystems;
        std::vector<ParticleEvent> m_EventQueue;
    };

}
