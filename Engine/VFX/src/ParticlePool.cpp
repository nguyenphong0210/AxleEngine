#include <Axle/ParticlePool.hpp>

namespace Axle {

    ParticlePool::ParticlePool(uint32_t capacity) {
        Allocate(capacity);
    }

    void ParticlePool::Allocate(uint32_t capacity) {
        m_Capacity = capacity;
        m_ActiveCount = 0;

        m_Positions.resize(capacity, glm::vec3(0.0f));
        m_Velocities.resize(capacity, glm::vec3(0.0f));
        m_Accelerations.resize(capacity, glm::vec3(0.0f));
        m_Colors.resize(capacity, glm::vec4(1.0f));
        m_Sizes.resize(capacity, 1.0f);
        m_Rotations.resize(capacity, 0.0f);
        m_AngularVelocities.resize(capacity, 0.0f);
        m_Ages.resize(capacity, 0.0f);
        m_Lifetimes.resize(capacity, 1.0f);
        m_UVOffsetScales.resize(capacity, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
        m_StartColors.resize(capacity, glm::vec4(1.0f));
        m_EndColors.resize(capacity, glm::vec4(1.0f));
        m_StartSizes.resize(capacity, 1.0f);
        m_EndSizes.resize(capacity, 1.0f);
        m_CustomFlags.resize(capacity, 0);
    }

    void ParticlePool::Reset() {
        m_ActiveCount = 0;
    }

    uint32_t ParticlePool::Spawn(const ParticleSpawnData& data) {
        if (m_ActiveCount >= m_Capacity) {
            return INVALID_INDEX;
        }

        uint32_t idx = m_ActiveCount++;
        m_Positions[idx] = data.Position;
        m_Velocities[idx] = data.Velocity;
        m_Accelerations[idx] = data.Acceleration;
        m_Sizes[idx] = data.StartSize;
        m_StartSizes[idx] = data.StartSize;
        m_EndSizes[idx] = data.EndSize;
        m_Colors[idx] = data.StartColor;
        m_StartColors[idx] = data.StartColor;
        m_EndColors[idx] = data.EndColor;
        m_Rotations[idx] = data.StartRotation;
        m_AngularVelocities[idx] = data.AngularVelocity;
        m_Ages[idx] = 0.0f;
        m_Lifetimes[idx] = (data.Lifetime > 0.0001f) ? data.Lifetime : 1.0f;
        m_UVOffsetScales[idx] = data.UVOffsetScale;
        m_CustomFlags[idx] = data.CustomFlags;

        return idx;
    }

    void ParticlePool::Kill(uint32_t index) {
        if (index >= m_ActiveCount || m_ActiveCount == 0) {
            return;
        }

        uint32_t lastIdx = m_ActiveCount - 1;
        if (index != lastIdx) {
            m_Positions[index] = m_Positions[lastIdx];
            m_Velocities[index] = m_Velocities[lastIdx];
            m_Accelerations[index] = m_Accelerations[lastIdx];
            m_Colors[index] = m_Colors[lastIdx];
            m_Sizes[index] = m_Sizes[lastIdx];
            m_Rotations[index] = m_Rotations[lastIdx];
            m_AngularVelocities[index] = m_AngularVelocities[lastIdx];
            m_Ages[index] = m_Ages[lastIdx];
            m_Lifetimes[index] = m_Lifetimes[lastIdx];
            m_UVOffsetScales[index] = m_UVOffsetScales[lastIdx];
            m_StartColors[index] = m_StartColors[lastIdx];
            m_EndColors[index] = m_EndColors[lastIdx];
            m_StartSizes[index] = m_StartSizes[lastIdx];
            m_EndSizes[index] = m_EndSizes[lastIdx];
            m_CustomFlags[index] = m_CustomFlags[lastIdx];
        }

        --m_ActiveCount;
    }

}
