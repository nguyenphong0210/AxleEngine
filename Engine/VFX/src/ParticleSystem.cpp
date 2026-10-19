#include <Axle/ParticleSystem.hpp>
#include <cmath>
#include <algorithm>

namespace Axle {

    ParticleSystem::ParticleSystem(const ParticleEmitterConfig& config)
        : m_Config(config), m_Pool(config.MaxParticles) {
    }

    void ParticleSystem::SetConfig(const ParticleEmitterConfig& config) {
        m_Config = config;
        if (config.MaxParticles != m_Pool.GetCapacity()) {
            m_Pool.Allocate(config.MaxParticles);
        }
    }

    void ParticleSystem::Reset() {
        m_Pool.Reset();
        m_SpawnAccumulator = 0.0f;
        m_BurstTimer = 0.0f;
        m_TotalSimulationTime = 0.0f;
        m_CollisionCount = 0;
    }

    void ParticleSystem::EmitBurst(uint32_t count, const glm::vec3& emitterWorldPos, const glm::quat& emitterWorldRot) {
        for (uint32_t i = 0; i < count; ++i) {
            SpawnParticle(emitterWorldPos, emitterWorldRot);
        }
    }

    void ParticleSystem::SpawnParticle(const glm::vec3& emitterWorldPos, const glm::quat& emitterWorldRot) {
        glm::vec3 offset(0.0f);
        glm::vec3 dir(0.0f, 1.0f, 0.0f);
        m_Config.Shape.Sample(offset, dir);

        ParticleSpawnData data;
        if (m_Config.SimulationSpace == ParticleSimulationSpace::World) {
            data.Position = emitterWorldPos + (emitterWorldRot * offset);
            data.Velocity = glm::normalize(emitterWorldRot * dir) * m_Config.InitialSpeed.Sample();
        } else {
            data.Position = offset;
            data.Velocity = glm::normalize(dir) * m_Config.InitialSpeed.Sample();
        }

        data.Acceleration = m_Config.ConstantAcceleration;
        data.StartSize = m_Config.StartSize.Sample();
        data.EndSize = m_Config.EndSize.Sample();
        data.StartColor = m_Config.ColorOverLifetime.StartColor;
        data.EndColor = m_Config.ColorOverLifetime.EndColor;
        data.StartRotation = m_Config.StartRotation.Sample();
        data.AngularVelocity = m_Config.AngularVelocity.Sample();
        data.Lifetime = m_Config.Lifetime.Sample();

        if (m_Config.TotalFrames > 1 && m_Config.TextureCols > 0 && m_Config.TextureRows > 0) {
            float scaleU = 1.0f / static_cast<float>(m_Config.TextureCols);
            float scaleV = 1.0f / static_cast<float>(m_Config.TextureRows);
            data.UVOffsetScale = glm::vec4(0.0f, 0.0f, scaleU, scaleV);
        } else {
            data.UVOffsetScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        }

        m_Pool.Spawn(data);
    }

    void ParticleSystem::Update(float dt, const glm::vec3& emitterWorldPos, const glm::quat& emitterWorldRot) {
        if (dt <= 0.0f) return;

        m_TotalSimulationTime += dt;

        // Continuous particle spawning
        if (m_IsEmitting && m_Config.EmissionRate > 0.0f) {
            m_SpawnAccumulator += dt * m_Config.EmissionRate;
            uint32_t toSpawn = static_cast<uint32_t>(m_SpawnAccumulator);
            m_SpawnAccumulator -= static_cast<float>(toSpawn);

            for (uint32_t i = 0; i < toSpawn; ++i) {
                SpawnParticle(emitterWorldPos, emitterWorldRot);
            }
        }

        // Periodic burst emission
        if (m_IsEmitting && m_Config.BurstCount > 0 && m_Config.BurstInterval > 0.0f) {
            m_BurstTimer += dt;
            if (m_BurstTimer >= m_Config.BurstInterval) {
                m_BurstTimer -= m_Config.BurstInterval;
                EmitBurst(m_Config.BurstCount, emitterWorldPos, emitterWorldRot);
            }
        }

        // Active particles simulation & lifecycle update
        uint32_t i = 0;
        auto& ages = m_Pool.GetAges();
        auto& lifetimes = m_Pool.GetLifetimes();
        auto& positions = m_Pool.GetPositions();
        auto& velocities = m_Pool.GetVelocities();
        auto& sizes = m_Pool.GetSizes();
        auto& colors = m_Pool.GetColors();
        auto& rotations = m_Pool.GetRotations();
        auto& angVels = m_Pool.GetAngularVelocities();
        auto& startSizes = m_Pool.GetStartSizes();
        auto& endSizes = m_Pool.GetEndSizes();
        auto& startColors = m_Pool.GetStartColors();
        auto& endColors = m_Pool.GetEndColors();
        auto& uvOffsets = m_Pool.GetUVOffsetScales();

        glm::vec3 gravityAccel = glm::vec3(0.0f, -9.81f, 0.0f) * m_Config.GravityModifier;
        glm::vec3 baseAccel = m_Config.ConstantAcceleration + gravityAccel;
        if (m_Config.Buoyancy != 0.0f) {
            baseAccel.y += m_Config.Buoyancy;
        }

        float dragFactor = (m_Config.Drag > 0.0f) ? glm::clamp(1.0f - m_Config.Drag * dt, 0.0f, 1.0f) : 1.0f;

        while (i < m_Pool.GetActiveCount()) {
            ages[i] += dt;
            if (ages[i] >= lifetimes[i]) {
                m_Pool.Kill(i);
                continue; // Element at last slot was swapped to index i, re-evaluate index i
            }

            float t = glm::clamp(ages[i] / lifetimes[i], 0.0f, 1.0f);

            glm::vec3 particleAccel = baseAccel;

            // Environmental Wind drag force
            if (glm::length(m_Config.Wind) > 1e-5f) {
                glm::vec3 relWind = m_Config.Wind - velocities[i];
                particleAccel += relWind * m_Config.WindInfluence;
            }

            // Force Fields (Wind, Buoyancy, Attractor, Vortex)
            for (const auto& field : m_Config.ForceFields) {
                particleAccel += field.Evaluate(positions[i], velocities[i], m_TotalSimulationTime);
            }

            // Divergence-free Curl Noise turbulence
            if (m_Config.CurlNoise.Enabled) {
                particleAccel += CurlNoise::EvaluateAnimated(positions[i], m_TotalSimulationTime, m_Config.CurlNoise);
            }

            // Kinematics integration
            velocities[i] *= dragFactor;
            velocities[i] += particleAccel * dt;

            glm::vec3 prevPos = positions[i];
            glm::vec3 nextPos = positions[i] + velocities[i] * dt;

            // Physics collisions
            if (m_Config.Collision.Enabled) {
                bool collided = false;
                float radius = (m_Config.Collision.ParticleRadius > 0.0f) ? m_Config.Collision.ParticleRadius : (sizes[i] * 0.5f);
                float rest = m_Config.Collision.Restitution;
                float fric = m_Config.Collision.Friction;
                float minBounce = m_Config.Collision.MinBounceSpeed;

                // 1. Continuous swept raycast
                if (m_Config.Collision.WorldRaycast) {
                    if (ParticleCollisionSolver::ResolveRaycast(m_Config.Collision.WorldRaycast, prevPos, nextPos, velocities[i], radius, rest, fric, minBounce)) {
                        collided = true;
                    }
                }

                // 2. Static Collision Planes
                for (const auto& plane : m_Config.Collision.Planes) {
                    if (ParticleCollisionSolver::ResolvePlane(plane, nextPos, velocities[i], radius, rest, fric, minBounce)) {
                        collided = true;
                        if (plane.KillOnCollision) break;
                    }
                }

                // 3. Collision Boxes
                for (const auto& box : m_Config.Collision.Boxes) {
                    if (ParticleCollisionSolver::ResolveBox(box, nextPos, velocities[i], radius, rest, fric, minBounce)) {
                        collided = true;
                        if (box.KillOnCollision) break;
                    }
                }

                if (collided) {
                    m_CollisionCount++;
                    if (m_Config.Collision.KillOnCollision) {
                        m_Pool.Kill(i);
                        continue;
                    }
                }
            }

            positions[i] = nextPos;
            rotations[i] += angVels[i] * dt;

            // Size & Color curves
            sizes[i] = glm::mix(startSizes[i], endSizes[i], t);
            colors[i] = glm::mix(startColors[i], endColors[i], t);

            // Flipbook UV frame interpolation
            if (m_Config.TotalFrames > 1 && m_Config.TextureCols > 0 && m_Config.TextureRows > 0) {
                float cycleProgress = std::fmod(t * m_Config.AnimationCycles, 1.0f);
                uint32_t frame = static_cast<uint32_t>(cycleProgress * static_cast<float>(m_Config.TotalFrames));
                if (frame >= m_Config.TotalFrames) frame = m_Config.TotalFrames - 1;

                uint32_t col = frame % m_Config.TextureCols;
                uint32_t row = frame / m_Config.TextureCols;

                float scaleU = 1.0f / static_cast<float>(m_Config.TextureCols);
                float scaleV = 1.0f / static_cast<float>(m_Config.TextureRows);
                uvOffsets[i] = glm::vec4(col * scaleU, row * scaleV, scaleU, scaleV);
            }

            ++i;
        }
    }

}
