#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace Axle {

    enum class ForceFieldType : uint8_t {
        DirectionalWind = 0,
        Buoyancy,
        PointAttractor,
        Vortex
    };

    enum class ForceFieldFalloff : uint8_t {
        None = 0,
        Linear,
        InverseSquare
    };

    struct ParticleForceField {
        ForceFieldType Type = ForceFieldType::DirectionalWind;
        glm::vec3 Position = glm::vec3(0.0f);        // Center position (Attractor / Vortex)
        glm::vec3 Direction = glm::vec3(1.0f, 0.0f, 0.0f); // Direction (Wind / Vortex Axis)
        float Strength = 5.0f;                       // General strength or wind speed
        float Radius = 10.0f;                        // Bounding radius of effect
        float FalloffExponent = 1.0f;
        ForceFieldFalloff Falloff = ForceFieldFalloff::Linear;

        // Specialized fields for Vortex
        float RadialSuction = 0.0f;                  // Inward force toward vortex axis
        float AxialLift = 0.0f;                      // Updraft force along vortex axis

        // Specialized fields for Wind
        float WindInfluence = 1.0f;                  // Aerodynamic drag coupling factor
        float GustFrequency = 0.0f;                  // Wind gust modulation frequency
        float GustAmplitude = 0.0f;                  // Wind gust peak amplitude multiplier

        ParticleForceField() = default;

        // Factory helpers
        static ParticleForceField CreateWind(const glm::vec3& direction, float speed, float influence = 1.0f,
                                            float gustFreq = 0.0f, float gustAmp = 0.0f);
        static ParticleForceField CreateBuoyancy(float liftForce = 4.0f);
        static ParticleForceField CreateAttractor(const glm::vec3& center, float strength, float radius,
                                                  ForceFieldFalloff falloff = ForceFieldFalloff::Linear);
        static ParticleForceField CreateRepeller(const glm::vec3& center, float strength, float radius,
                                                 ForceFieldFalloff falloff = ForceFieldFalloff::Linear);
        static ParticleForceField CreateVortex(const glm::vec3& center, const glm::vec3& axis,
                                               float radius, float angularSpeed, float suction = 2.0f, float lift = 3.0f);

        // Evaluates force acceleration vector (m/s^2) for a particle at position and velocity
        glm::vec3 Evaluate(const glm::vec3& particlePos, const glm::vec3& particleVel, float time) const;
    };

    class ParticleForceFieldManager {
    public:
        void AddField(const ParticleForceField& field) { m_Fields.push_back(field); }
        void Clear() { m_Fields.clear(); }

        const std::vector<ParticleForceField>& GetFields() const { return m_Fields; }
        std::vector<ParticleForceField>& GetFields() { return m_Fields; }

        glm::vec3 EvaluateTotalAcceleration(const glm::vec3& pos, const glm::vec3& vel, float time) const {
            glm::vec3 total(0.0f);
            for (const auto& field : m_Fields) {
                total += field.Evaluate(pos, vel, time);
            }
            return total;
        }

    private:
        std::vector<ParticleForceField> m_Fields;
    };

}
