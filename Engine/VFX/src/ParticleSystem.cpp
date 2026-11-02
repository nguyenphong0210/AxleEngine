#include <Axle/ParticleSystem.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <limits>

namespace Axle {

    ParticleSystem::ParticleSystem(const ParticleEmitterConfig& config)
        : m_Config(config), m_Pool(config.MaxParticles) {
        m_BurstRuntime.resize(m_Config.Bursts.size());
        for (size_t i = 0; i < m_Config.Bursts.size(); ++i) {
            m_BurstRuntime[i].CompletedCycles = 0;
            m_BurstRuntime[i].NextTriggerTime = m_Config.Bursts[i].Time;
        }
    }

    void ParticleSystem::SetConfig(const ParticleEmitterConfig& config) {
        m_Config = config;
        if (config.MaxParticles != m_Pool.GetCapacity()) {
            m_Pool.Allocate(config.MaxParticles);
        }
        m_BurstRuntime.resize(m_Config.Bursts.size());
        for (size_t i = 0; i < m_Config.Bursts.size(); ++i) {
            m_BurstRuntime[i].CompletedCycles = 0;
            m_BurstRuntime[i].NextTriggerTime = m_Config.Bursts[i].Time;
        }
    }

    void ParticleSystem::SetSeed(uint32_t seed) {
        m_InitialSeed = seed;
        m_RNG.seed(seed);
    }

    uint32_t ParticleSystem::GetActiveParticleCount() const {
        uint32_t count = m_Pool.GetActiveCount();
        for (const auto& [name, child] : m_ChildSystems) {
            if (child) count += child->GetActiveParticleCount();
        }
        return count;
    }

    void ParticleSystem::Reset() {
        m_Pool.Reset();
        m_SpawnAccumulator = 0.0f;
        m_BurstTimer = 0.0f;
        m_TotalSimulationTime = 0.0f;
        m_EmitterTime = 0.0f;
        m_CollisionCount = 0;
        m_RNG.seed(m_InitialSeed);
        m_EventQueue.clear();

        m_BurstRuntime.resize(m_Config.Bursts.size());
        for (size_t i = 0; i < m_Config.Bursts.size(); ++i) {
            m_BurstRuntime[i].CompletedCycles = 0;
            m_BurstRuntime[i].NextTriggerTime = m_Config.Bursts[i].Time;
        }
        for (auto& burst : m_Config.Bursts) {
            burst.Reset();
        }
        for (auto& [name, child] : m_ChildSystems) {
            if (child) child->Reset();
        }
    }

    void ParticleSystem::EmitBurst(uint32_t count, const glm::vec3& emitterWorldPos, const glm::quat& emitterWorldRot) {
        for (uint32_t i = 0; i < count; ++i) {
            SpawnParticle(emitterWorldPos, emitterWorldRot);
        }
    }

    void ParticleSystem::AddChildSystem(const std::string& name, const std::shared_ptr<ParticleSystem>& child) {
        if (child) {
            m_ChildSystems[name] = child;
        }
    }

    void ParticleSystem::RemoveChildSystem(const std::string& name) {
        m_ChildSystems.erase(name);
    }

    std::shared_ptr<ParticleSystem> ParticleSystem::GetChildSystem(const std::string& name) const {
        auto it = m_ChildSystems.find(name);
        return (it != m_ChildSystems.end()) ? it->second : nullptr;
    }

    void ParticleSystem::DispatchSubEmitters(SubEmitterTrigger trigger, const glm::vec3& pos, const glm::vec3& vel, const glm::vec4& color) {
        for (const auto& sub : m_Config.SubEmitters) {
            if (sub.Trigger == trigger) {
                if (sub.Probability >= 1.0f || VFXRandom::Float(m_RNG, 0.0f, 1.0f) <= sub.Probability) {
                    if (m_SubEmitterCallback) {
                        m_SubEmitterCallback(trigger, sub.TargetEmitterName, pos, vel, color, sub.EmitCount);
                    }
                    m_EventQueue.push_back({ m_Config.Name, sub.TargetEmitterName, trigger, pos, vel, color, sub.EmitCount, sub.InheritColor, sub.InheritVelocity });

                    auto it = m_ChildSystems.find(sub.TargetEmitterName);
                    if (it != m_ChildSystems.end() && it->second) {
                        it->second->EmitBurst(sub.EmitCount, pos);
                    }
                }
            }
        }
    }

    void ParticleSystem::SpawnInherited(const glm::vec3& worldPos, const glm::vec3& inheritedVel, const glm::vec4& inheritedColor, bool inheritColor, bool inheritVel, uint32_t count) {
        const uint32_t MAX_SPAWNS_PER_BATCH = 200;
        count = std::min(count, MAX_SPAWNS_PER_BATCH);

        for (uint32_t i = 0; i < count; ++i) {
            glm::vec3 offset(0.0f);
            glm::vec3 dir(0.0f, 1.0f, 0.0f);
            m_Config.Shape.Sample(offset, dir, m_RNG);

            ParticleSpawnData data;
            data.Position = worldPos + offset;
            float speed = m_Config.InitialSpeed.Sample(m_RNG);
            data.Velocity = glm::normalize(dir) * speed;
            if (inheritVel) {
                data.Velocity += inheritedVel;
            }
            data.Acceleration = m_Config.ConstantAcceleration;

            data.StartSize = m_Config.StartSize.Sample(m_RNG);
            data.EndSize = m_Config.EndSize.Sample(m_RNG);

            glm::vec4 authoredStartCol = m_Config.ColorOverLifetime.Evaluate(0.0f);
            glm::vec4 authoredEndCol = m_Config.ColorOverLifetime.Evaluate(1.0f);
            if (inheritColor) {
                data.StartColor = authoredStartCol * inheritedColor;
                data.EndColor = authoredEndCol * inheritedColor;
            } else {
                data.StartColor = authoredStartCol;
                data.EndColor = authoredEndCol;
            }

            data.Lifetime = std::max(0.01f, m_Config.Lifetime.Sample(m_RNG));
            data.StartRotation = m_Config.StartRotation.Sample(m_RNG);
            data.AngularVelocity = m_Config.AngularVelocity.Sample(m_RNG);

            data.UVOffsetScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            data.UVSubFrame = glm::vec4(0.0f);

            uint32_t spawnedIdx = m_Pool.Spawn(data);
            if (spawnedIdx == ParticlePool::INVALID_INDEX) {
                break;
            }
        }
    }

    void ParticleSystem::SpawnParticle(const glm::vec3& emitterWorldPos, const glm::quat& emitterWorldRot) {
        glm::vec3 offset(0.0f);
        glm::vec3 dir(0.0f, 1.0f, 0.0f);
        m_Config.Shape.Sample(offset, dir, m_RNG);

        ParticleSpawnData data;
        if (m_Config.SimulationSpace == ParticleSimulationSpace::World) {
            data.Position = emitterWorldPos + (emitterWorldRot * offset);
            data.Velocity = glm::normalize(emitterWorldRot * dir) * m_Config.InitialSpeed.Sample(m_RNG);
        } else {
            data.Position = offset;
            data.Velocity = glm::normalize(dir) * m_Config.InitialSpeed.Sample(m_RNG);
        }

        data.Acceleration = m_Config.ConstantAcceleration;
        data.StartSize = m_Config.StartSize.Sample(m_RNG);
        data.EndSize = m_Config.EndSize.Sample(m_RNG);
        data.StartColor = m_Config.ColorOverLifetime.Evaluate(0.0f);
        data.EndColor = m_Config.ColorOverLifetime.Evaluate(1.0f);
        data.StartRotation = m_Config.StartRotation.Sample(m_RNG);
        data.AngularVelocity = m_Config.AngularVelocity.Sample(m_RNG);
        data.Lifetime = m_Config.Lifetime.Sample(m_RNG);

        float lightingFlag = m_Config.LightingEnabled ? 1.0f : 0.0f;
        if (m_Config.TotalFrames > 1 && m_Config.TextureCols > 0 && m_Config.TextureRows > 0) {
            float scaleU = 1.0f / static_cast<float>(m_Config.TextureCols);
            float scaleV = 1.0f / static_cast<float>(m_Config.TextureRows);
            data.UVOffsetScale = glm::vec4(0.0f, 0.0f, scaleU, scaleV);
            uint32_t frameB = (m_Config.TotalFrames > 1) ? 1 : 0;
            data.UVSubFrame = glm::vec4((frameB % m_Config.TextureCols) * scaleU,
                                        (frameB / m_Config.TextureCols) * scaleV,
                                        0.0f, lightingFlag);
        } else {
            data.UVOffsetScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            data.UVSubFrame = glm::vec4(0.0f, 0.0f, 0.0f, lightingFlag);
        }

        m_Pool.Spawn(data);

        // Sub-Emitter Trigger on Birth
        DispatchSubEmitters(SubEmitterTrigger::Birth, data.Position, data.Velocity, data.StartColor);
    }

    void ParticleSystem::Update(float dt, const glm::vec3& emitterWorldPos, const glm::quat& emitterWorldRot) {
        if (dt <= 0.0f) return;

        m_TotalSimulationTime += dt;
        float prevEmitterTime = m_EmitterTime;
        m_EmitterTime += dt;

        bool isEmittingWindow = m_IsEmitting;
        bool looped = false;
        if (m_Config.Duration > 0.0f) {
            if (m_Config.Looping) {
                if (m_EmitterTime >= m_Config.Duration) {
                    looped = true;
                }
            } else {
                if (prevEmitterTime >= m_Config.Duration) {
                    isEmittingWindow = false;
                }
            }
        }

        auto processBursts = [&](float windowStart, float windowEnd) {
            for (size_t b = 0; b < m_Config.Bursts.size(); ++b) {
                if (b >= m_BurstRuntime.size()) continue;
                const auto& burst = m_Config.Bursts[b];
                auto& runtime = m_BurstRuntime[b];

                uint32_t budget = 0;
                const uint32_t MAX_BURST_EVENTS_PER_TICK = 64;
                while ((burst.Cycles == 0 || runtime.CompletedCycles < burst.Cycles) &&
                       runtime.NextTriggerTime <= windowEnd &&
                       budget < MAX_BURST_EVENTS_PER_TICK) {
                    budget++;
                    uint32_t count = burst.SampleCount(m_RNG);
                    if (count > 0 && (burst.Probability >= 1.0f || VFXRandom::Float(m_RNG, 0.0f, 1.0f) <= burst.Probability)) {
                        EmitBurst(count, emitterWorldPos, emitterWorldRot);
                    }
                    runtime.CompletedCycles++;
                    if (burst.Interval > 0.0f) {
                        runtime.NextTriggerTime += burst.Interval;
                    } else {
                        runtime.NextTriggerTime = std::numeric_limits<float>::infinity();
                        break;
                    }
                }
            }
        };

        if (isEmittingWindow) {
            // Continuous particle spawning
            if (m_Config.EmissionRate > 0.0f) {
                float activeDt = dt;
                if (!m_Config.Looping && m_Config.Duration > 0.0f && m_EmitterTime > m_Config.Duration) {
                    activeDt = std::max(0.0f, m_Config.Duration - prevEmitterTime);
                }
                m_SpawnAccumulator += activeDt * m_Config.EmissionRate;
                uint32_t toSpawn = static_cast<uint32_t>(m_SpawnAccumulator);
                m_SpawnAccumulator -= static_cast<float>(toSpawn);

                uint32_t spawnBudget = std::min(toSpawn, m_Pool.GetCapacity());
                for (uint32_t i = 0; i < spawnBudget; ++i) {
                    SpawnParticle(emitterWorldPos, emitterWorldRot);
                }
            }

            // Legacy periodic burst emission
            if (m_Config.BurstCount > 0 && m_Config.BurstInterval > 0.0f) {
                m_BurstTimer += dt;
                if (m_BurstTimer >= m_Config.BurstInterval) {
                    m_BurstTimer -= m_Config.BurstInterval;
                    EmitBurst(m_Config.BurstCount, emitterWorldPos, emitterWorldRot);
                }
            }

            // Advanced Multi-Burst list evaluation (Unity Shuriken style)
            if (looped) {
                processBursts(prevEmitterTime, m_Config.Duration);
                m_EmitterTime = std::fmod(m_EmitterTime, m_Config.Duration);
                for (size_t b = 0; b < m_Config.Bursts.size(); ++b) {
                    m_BurstRuntime[b].CompletedCycles = 0;
                    m_BurstRuntime[b].NextTriggerTime = m_Config.Bursts[b].Time;
                }
                processBursts(0.0f, m_EmitterTime);
            } else {
                float effectiveEnd = (!m_Config.Looping && m_Config.Duration > 0.0f) ? std::min(m_EmitterTime, m_Config.Duration) : m_EmitterTime;
                processBursts(prevEmitterTime, effectiveEnd);
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
        auto& uvOffsets = m_Pool.GetUVOffsetScales();
        auto& uvSubFrames = m_Pool.GetUVSubFrames();

        glm::vec3 gravityAccel = glm::vec3(0.0f, -9.81f, 0.0f) * m_Config.GravityModifier;
        glm::vec3 baseAccel = m_Config.ConstantAcceleration + gravityAccel;
        if (m_Config.Buoyancy != 0.0f) {
            baseAccel.y += m_Config.Buoyancy;
        }

        float dragFactor = (m_Config.Drag > 0.0f) ? glm::clamp(1.0f - m_Config.Drag * dt, 0.0f, 1.0f) : 1.0f;

        while (i < m_Pool.GetActiveCount()) {
            ages[i] += dt;
            if (ages[i] >= lifetimes[i]) {
                // Sub-Emitter Trigger on Death
                DispatchSubEmitters(SubEmitterTrigger::Death, positions[i], velocities[i], colors[i]);
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

            // Speed over Lifetime curve modulation
            float speedCurveScale = m_Config.SpeedOverLifetime.Evaluate(t);
            glm::vec3 currentVel = velocities[i] * speedCurveScale;

            glm::vec3 prevPos = positions[i];
            glm::vec3 nextPos = positions[i] + currentVel * dt;

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
                    // Sub-Emitter Trigger on Collision
                    DispatchSubEmitters(SubEmitterTrigger::Collision, nextPos, velocities[i], colors[i]);

                    if (m_Config.Collision.KillOnCollision) {
                        m_Pool.Kill(i);
                        continue;
                    }
                }
            }

            positions[i] = nextPos;
            rotations[i] += angVels[i] * dt;

            // Size & Color curves evaluation
            float curveSize = m_Config.SizeOverLifetime.Evaluate(t);
            sizes[i] = glm::mix(startSizes[i], endSizes[i], t) * curveSize;

            colors[i] = m_Config.ColorOverLifetime.Evaluate(t);
            colors[i].a *= m_Config.AlphaOverLifetime.Evaluate(t);

            // Flipbook UV frame interpolation & sub-frame blending
            float lightingFlag = m_Config.LightingEnabled ? 1.0f : 0.0f;
            if (m_Config.TotalFrames > 1 && m_Config.TextureCols > 0 && m_Config.TextureRows > 0) {
                float cycleProgress = std::fmod(t * m_Config.AnimationCycles, 1.0f);
                float continuousFrame = cycleProgress * static_cast<float>(m_Config.TotalFrames);
                uint32_t frameA = static_cast<uint32_t>(continuousFrame);
                if (frameA >= m_Config.TotalFrames) frameA = m_Config.TotalFrames - 1;

                uint32_t frameB = (frameA + 1) % m_Config.TotalFrames;
                float subFrameBlend = continuousFrame - static_cast<float>(frameA);

                uint32_t colA = frameA % m_Config.TextureCols;
                uint32_t rowA = frameA / m_Config.TextureCols;

                float scaleU = 1.0f / static_cast<float>(m_Config.TextureCols);
                float scaleV = 1.0f / static_cast<float>(m_Config.TextureRows);
                uvOffsets[i] = glm::vec4(colA * scaleU, rowA * scaleV, scaleU, scaleV);

                if (m_Config.SubFrameBlending) {
                    uint32_t colB = frameB % m_Config.TextureCols;
                    uint32_t rowB = frameB / m_Config.TextureCols;
                    uvSubFrames[i] = glm::vec4(colB * scaleU, rowB * scaleV, subFrameBlend, lightingFlag);
                } else {
                    uvSubFrames[i] = glm::vec4(0.0f, 0.0f, 0.0f, lightingFlag);
                }
            } else {
                uvSubFrames[i] = glm::vec4(0.0f, 0.0f, 0.0f, lightingFlag);
            }

            ++i;
        }

        // Propagate simulation update to child sub-emitters
        for (auto& [name, child] : m_ChildSystems) {
            if (child) {
                child->Update(dt, emitterWorldPos, emitterWorldRot);
            }
        }
    }

}
