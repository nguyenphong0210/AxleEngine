#pragma once

#include <Axle/VFXCommon.hpp>
#include <vector>
#include <cstdint>

namespace Axle {

    struct ParticleSpawnData {
        glm::vec3 Position = glm::vec3(0.0f);
        glm::vec3 Velocity = glm::vec3(0.0f);
        glm::vec3 Acceleration = glm::vec3(0.0f);
        float StartSize = 1.0f;
        float EndSize = 1.0f;
        glm::vec4 StartColor = glm::vec4(1.0f);
        glm::vec4 EndColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        float StartRotation = 0.0f;
        float AngularVelocity = 0.0f;
        float Lifetime = 1.0f;
        glm::vec4 UVOffsetScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        uint32_t CustomFlags = 0;
    };

    class ParticlePool {
    public:
        static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;

        explicit ParticlePool(uint32_t capacity = 10000);
        ~ParticlePool() = default;

        void Allocate(uint32_t capacity);
        void Reset();

        // Spawns a particle into the pool. Returns index, or INVALID_INDEX if full.
        uint32_t Spawn(const ParticleSpawnData& data);

        // Kills particle at index by swapping with the last active particle (O(1)).
        void Kill(uint32_t index);

        uint32_t GetActiveCount() const { return m_ActiveCount; }
        uint32_t GetCapacity() const { return m_Capacity; }
        bool IsFull() const { return m_ActiveCount >= m_Capacity; }
        bool IsEmpty() const { return m_ActiveCount == 0; }

        // Contiguous SoA data buffers for cache-friendly SIMD operations & GPU streaming
        std::vector<glm::vec3>& GetPositions() { return m_Positions; }
        const std::vector<glm::vec3>& GetPositions() const { return m_Positions; }

        std::vector<glm::vec3>& GetVelocities() { return m_Velocities; }
        const std::vector<glm::vec3>& GetVelocities() const { return m_Velocities; }

        std::vector<glm::vec3>& GetAccelerations() { return m_Accelerations; }
        const std::vector<glm::vec3>& GetAccelerations() const { return m_Accelerations; }

        std::vector<glm::vec4>& GetColors() { return m_Colors; }
        const std::vector<glm::vec4>& GetColors() const { return m_Colors; }

        std::vector<float>& GetSizes() { return m_Sizes; }
        const std::vector<float>& GetSizes() const { return m_Sizes; }

        std::vector<float>& GetRotations() { return m_Rotations; }
        const std::vector<float>& GetRotations() const { return m_Rotations; }

        std::vector<float>& GetAngularVelocities() { return m_AngularVelocities; }
        const std::vector<float>& GetAngularVelocities() const { return m_AngularVelocities; }

        std::vector<float>& GetAges() { return m_Ages; }
        const std::vector<float>& GetAges() const { return m_Ages; }

        std::vector<float>& GetLifetimes() { return m_Lifetimes; }
        const std::vector<float>& GetLifetimes() const { return m_Lifetimes; }

        std::vector<glm::vec4>& GetUVOffsetScales() { return m_UVOffsetScales; }
        const std::vector<glm::vec4>& GetUVOffsetScales() const { return m_UVOffsetScales; }

        std::vector<glm::vec4>& GetStartColors() { return m_StartColors; }
        const std::vector<glm::vec4>& GetStartColors() const { return m_StartColors; }

        std::vector<glm::vec4>& GetEndColors() { return m_EndColors; }
        const std::vector<glm::vec4>& GetEndColors() const { return m_EndColors; }

        std::vector<float>& GetStartSizes() { return m_StartSizes; }
        const std::vector<float>& GetStartSizes() const { return m_StartSizes; }

        std::vector<float>& GetEndSizes() { return m_EndSizes; }
        const std::vector<float>& GetEndSizes() const { return m_EndSizes; }

        std::vector<uint32_t>& GetCustomFlags() { return m_CustomFlags; }
        const std::vector<uint32_t>& GetCustomFlags() const { return m_CustomFlags; }

    private:
        uint32_t m_Capacity = 0;
        uint32_t m_ActiveCount = 0;

        // SoA contiguous vectors pre-allocated to m_Capacity
        std::vector<glm::vec3> m_Positions;
        std::vector<glm::vec3> m_Velocities;
        std::vector<glm::vec3> m_Accelerations;
        std::vector<glm::vec4> m_Colors;
        std::vector<float> m_Sizes;
        std::vector<float> m_Rotations;
        std::vector<float> m_AngularVelocities;
        std::vector<float> m_Ages;
        std::vector<float> m_Lifetimes;
        std::vector<glm::vec4> m_UVOffsetScales;
        std::vector<glm::vec4> m_StartColors;
        std::vector<glm::vec4> m_EndColors;
        std::vector<float> m_StartSizes;
        std::vector<float> m_EndSizes;
        std::vector<uint32_t> m_CustomFlags;
    };

}
