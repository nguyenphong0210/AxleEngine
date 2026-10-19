#include <Axle/ParticleForceField.hpp>
#include <cmath>
#include <algorithm>

namespace Axle {

    ParticleForceField ParticleForceField::CreateWind(const glm::vec3& direction, float speed, float influence,
                                                      float gustFreq, float gustAmp) {
        ParticleForceField field;
        field.Type = ForceFieldType::DirectionalWind;
        field.Direction = (glm::length(direction) > 1e-5f) ? glm::normalize(direction) : glm::vec3(1.0f, 0.0f, 0.0f);
        field.Strength = speed;
        field.WindInfluence = influence;
        field.GustFrequency = gustFreq;
        field.GustAmplitude = gustAmp;
        field.Falloff = ForceFieldFalloff::None;
        field.Radius = 0.0f;
        return field;
    }

    ParticleForceField ParticleForceField::CreateBuoyancy(float liftForce) {
        ParticleForceField field;
        field.Type = ForceFieldType::Buoyancy;
        field.Strength = liftForce;
        field.Direction = glm::vec3(0.0f, 1.0f, 0.0f);
        field.Falloff = ForceFieldFalloff::None;
        field.Radius = 0.0f;
        return field;
    }

    ParticleForceField ParticleForceField::CreateAttractor(const glm::vec3& center, float strength, float radius,
                                                          ForceFieldFalloff falloff) {
        ParticleForceField field;
        field.Type = ForceFieldType::PointAttractor;
        field.Position = center;
        field.Strength = std::abs(strength);
        field.Radius = radius;
        field.Falloff = falloff;
        return field;
    }

    ParticleForceField ParticleForceField::CreateRepeller(const glm::vec3& center, float strength, float radius,
                                                         ForceFieldFalloff falloff) {
        ParticleForceField field;
        field.Type = ForceFieldType::PointAttractor;
        field.Position = center;
        field.Strength = -std::abs(strength);
        field.Radius = radius;
        field.Falloff = falloff;
        return field;
    }

    ParticleForceField ParticleForceField::CreateVortex(const glm::vec3& center, const glm::vec3& axis,
                                                       float radius, float angularSpeed, float suction, float lift) {
        ParticleForceField field;
        field.Type = ForceFieldType::Vortex;
        field.Position = center;
        field.Direction = (glm::length(axis) > 1e-5f) ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
        field.Radius = radius;
        field.Strength = angularSpeed;
        field.RadialSuction = suction;
        field.AxialLift = lift;
        field.Falloff = ForceFieldFalloff::Linear;
        return field;
    }

    glm::vec3 ParticleForceField::Evaluate(const glm::vec3& particlePos, const glm::vec3& particleVel, float time) const {
        switch (Type) {
            case ForceFieldType::DirectionalWind: {
                float effectiveSpeed = Strength;
                if (GustFrequency > 0.0f && GustAmplitude > 0.0f) {
                    effectiveSpeed *= (1.0f + GustAmplitude * std::sin(GustFrequency * time));
                }
                glm::vec3 windVelocity = Direction * effectiveSpeed;
                glm::vec3 relativeVelocity = windVelocity - particleVel;
                return relativeVelocity * WindInfluence;
            }

            case ForceFieldType::Buoyancy: {
                return glm::vec3(0.0f, Strength, 0.0f);
            }

            case ForceFieldType::PointAttractor: {
                glm::vec3 delta = Position - particlePos;
                float dist = glm::length(delta);
                if (dist < 1e-4f || (Radius > 0.0f && dist > Radius)) {
                    return glm::vec3(0.0f);
                }

                glm::vec3 dir = delta / dist;
                float falloffFactor = 1.0f;
                if (Radius > 0.0f) {
                    if (Falloff == ForceFieldFalloff::Linear) {
                        falloffFactor = 1.0f - (dist / Radius);
                    } else if (Falloff == ForceFieldFalloff::InverseSquare) {
                        falloffFactor = 1.0f / (1.0f + dist * dist);
                    }
                }

                return dir * (Strength * falloffFactor);
            }

            case ForceFieldType::Vortex: {
                glm::vec3 axis = glm::normalize(Direction);
                glm::vec3 delta = particlePos - Position;
                float axialProj = glm::dot(delta, axis);
                glm::vec3 radialVec = delta - (axis * axialProj);
                float r = glm::length(radialVec);

                if (r < 1e-4f || (Radius > 0.0f && r > Radius)) {
                    return glm::vec3(0.0f);
                }

                glm::vec3 radialDir = radialVec / r;
                glm::vec3 tangentDir = glm::normalize(glm::cross(axis, radialDir));
                float falloffFactor = (Radius > 0.0f) ? (1.0f - (r / Radius)) : 1.0f;

                glm::vec3 tangentialForce = tangentDir * Strength;
                glm::vec3 suctionForce = -radialDir * RadialSuction;
                glm::vec3 liftForce = axis * AxialLift;

                return (tangentialForce + suctionForce + liftForce) * falloffFactor;
            }
        }

        return glm::vec3(0.0f);
    }

}
