#include <Axle/ParticleSystem.hpp>
#include <Axle/ParticleMeshSampler.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_set>

namespace Axle {

    ParticleSystem::ParticleSystem(const ParticleEmitterConfig &config)
        : m_Config(config), m_Pool(config.MaxParticles), m_IsEmitting(config.PlayOnAwake) {
        m_BurstRuntime.resize(m_Config.Bursts.size());
        for (size_t i = 0; i < m_Config.Bursts.size(); ++i) {
            m_BurstRuntime[i].CompletedCycles = 0;
            m_BurstRuntime[i].NextTriggerTime = m_Config.Bursts[i].Time;
        }
    }

    void ParticleSystem::SetConfig(const ParticleEmitterConfig& config) {
        bool scheduleChanged = config.Bursts.size() != m_Config.Bursts.size();
        if (!scheduleChanged) for (size_t i=0; i<config.Bursts.size(); ++i) {
            const auto& a=config.Bursts[i]; const auto& b=m_Config.Bursts[i];
            if (a.Time!=b.Time || a.Cycles!=b.Cycles || a.Interval!=b.Interval) scheduleChanged=true;
        }
        m_Config = config;
        if (config.MaxParticles != m_Pool.GetCapacity()) m_Pool.Allocate(config.MaxParticles);
        if (scheduleChanged) {
            m_BurstRuntime.resize(m_Config.Bursts.size());
            for (size_t i=0; i<m_Config.Bursts.size(); ++i) {
                auto& state=m_BurstRuntime[i]; const auto& burst=m_Config.Bursts[i];
                state.CompletedCycles=0; state.NextTriggerTime=burst.Time;
                // Edits take effect from now; they never replay elapsed events.
                if (state.NextTriggerTime < m_EmitterTime) {
                    if (burst.Interval > 0) {
                        auto skipped=static_cast<uint32_t>(std::ceil((m_EmitterTime-burst.Time)/burst.Interval));
                        state.CompletedCycles=skipped; state.NextTriggerTime+=skipped*burst.Interval;
                    } else { state.CompletedCycles=1; state.NextTriggerTime=std::numeric_limits<float>::infinity(); }
                }
            }
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

    void ParticleSystem::EmitBurst(uint32_t count, const glm::vec3& emitterWorldPos, const glm::quat& emitterWorldRot, const glm::vec3& emitterWorldScale) {
        SetEmitterTransform(emitterWorldPos, emitterWorldRot, emitterWorldScale);
        count = std::min(count, m_Pool.GetCapacity() - m_Pool.GetActiveCount());
        for (uint32_t i = 0; i < count; ++i) {
            SpawnParticle(emitterWorldPos, emitterWorldRot);
        }
    }

    void ParticleSystem::AddChildSystem(const std::string& name, const std::shared_ptr<ParticleSystem>& child) {
        if (!child)
            return;
        std::unordered_set<const ParticleSystem *> visited;
        std::function<bool(const ParticleSystem *)> reachesThis = [&](const ParticleSystem *system) {
            if (system == this)
                return true;
            if (!visited.insert(system).second)
                return false;
            for (const auto &entry : system->m_ChildSystems)
                if (entry.second && reachesThis(entry.second.get()))
                    return true;
            return false;
        };
        if (reachesThis(child.get()))
            return;
        for (const auto &entry : m_ChildSystems)
            if (entry.first != name && entry.second == child)
                return;
        m_ChildSystems[name] = child;
    }

    void ParticleSystem::RemoveChildSystem(const std::string& name) {
        m_ChildSystems.erase(name);
    }

    std::shared_ptr<ParticleSystem> ParticleSystem::GetChildSystem(const std::string& name) const {
        auto it = m_ChildSystems.find(name);
        return (it != m_ChildSystems.end()) ? it->second : nullptr;
    }

    void ParticleSystem::DispatchSubEmitters(SubEmitterTrigger trigger, const glm::vec3& pos, const glm::vec3& vel, const glm::vec4& color) {
        if (!m_Config.Modules.SubEmitters) return;
        for (const auto& sub : m_Config.SubEmitters) {
            if (sub.Trigger == trigger) {
                if (sub.Probability >= 1.0f || VFXRandom::Float(m_RNG, 0.0f, 1.0f) < sub.Probability) {
                    const bool local = m_Config.SimulationSpace == ParticleSimulationSpace::Local;
                    const auto worldPos =
                        local ? m_WorldPosition + m_WorldRotation * (pos * m_WorldScale) : pos;
                    const auto worldVel = local ? m_WorldRotation * (vel * m_WorldScale) : vel;
                    if (m_SubEmitterCallback)
                        m_SubEmitterCallback(trigger, sub.TargetEmitterName, worldPos, worldVel, color,
                                             sub.EmitCount);
                    if (m_EventQueue.size() < 500)
                        m_EventQueue.push_back({m_Config.Name, sub.TargetEmitterName, trigger, worldPos,
                                                worldVel, color, sub.EmitCount, sub.InheritColor,
                                                sub.InheritVelocity});
                    auto it = m_ChildSystems.find(sub.TargetEmitterName);
                    if (it != m_ChildSystems.end() && it->second) {
                        it->second->SetEmitterTransform(m_WorldPosition, m_WorldRotation, m_WorldScale);
                        it->second->SpawnInherited(worldPos, worldVel, color, sub.InheritColor,
                                                   sub.InheritVelocity, sub.EmitCount);
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
            if (m_Config.Modules.Shape) m_Config.Shape.Sample(offset, dir, m_RNG);

            ParticleSpawnData data;
            const bool local = m_Config.SimulationSpace == ParticleSimulationSpace::Local;
            glm::vec3 safeScale = m_WorldScale;
            for (int axis=0; axis<3; ++axis) if (std::abs(safeScale[axis]) < 0.0001f) safeScale[axis]=0.0001f;
            data.Position = local ? glm::inverse(m_WorldRotation) * (worldPos - m_WorldPosition) / safeScale + offset
                                  : worldPos + m_WorldRotation * (offset * m_WorldScale);
            float speed = m_Config.InitialSpeed.Sample(m_RNG);
            data.Velocity = (local ? glm::normalize(dir) : m_WorldRotation * glm::normalize(dir)) * speed;
            if (inheritVel) {
                data.Velocity += local ? glm::inverse(m_WorldRotation) * inheritedVel / safeScale : inheritedVel;
            }
            data.Acceleration = m_Config.ConstantAcceleration;

            data.StartSize = m_Config.StartSize.Sample(m_RNG);
            data.EndSize = m_Config.EndSize.Sample(m_RNG);
            if (!local) {
                const float scale =
                    std::max({std::abs(m_WorldScale.x), std::abs(m_WorldScale.y), std::abs(m_WorldScale.z)});
                data.StartSize *= scale;
                data.EndSize *= scale;
            }

            glm::vec4 authoredStartCol = m_Config.ColorOverLifetime.Evaluate(0.0f);
            glm::vec4 authoredEndCol = m_Config.ColorOverLifetime.Evaluate(1.0f);
            if (inheritColor) {
                data.StartColor = authoredStartCol * inheritedColor;
                data.EndColor = authoredEndCol * inheritedColor;
            } else {
                data.StartColor = authoredStartCol;
                data.EndColor = authoredEndCol;
            }

            data.ColorMultiplier = inheritColor ? inheritedColor : glm::vec4(1);
            data.Lifetime = std::max(0.01f, m_Config.Lifetime.Sample(m_RNG));
            data.StartRotation = m_Config.StartRotation.Sample(m_RNG);
            data.AngularVelocity = m_Config.AngularVelocity.Sample(m_RNG);

            data.UVOffsetScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            data.UVSubFrame = glm::vec4(0.0f);

            uint32_t spawnedIdx = m_Pool.Spawn(data);
            if (spawnedIdx == ParticlePool::INVALID_INDEX) break;
            DispatchSubEmitters(SubEmitterTrigger::Birth, data.Position, data.Velocity, data.StartColor);
        }
    }

    void ParticleSystem::SpawnParticle(const glm::vec3& emitterWorldPos, const glm::quat& emitterWorldRot) {
        glm::vec3 offset(0.0f);
        glm::vec3 dir(0.0f, 1.0f, 0.0f);
        if (m_Config.Modules.Shape) m_Config.Shape.Sample(offset, dir, m_RNG);

        ParticleSpawnData data;
        if (m_Config.SimulationSpace == ParticleSimulationSpace::World) {
            data.Position = emitterWorldPos + (emitterWorldRot * (offset * m_WorldScale));
            data.Velocity = glm::normalize(emitterWorldRot * dir) * m_Config.InitialSpeed.Sample(m_RNG);
        } else {
            data.Position = offset;
            data.Velocity = glm::normalize(dir) * m_Config.InitialSpeed.Sample(m_RNG);
        }

        data.Acceleration = m_Config.ConstantAcceleration;
        data.StartSize = m_Config.StartSize.Sample(m_RNG);
        data.EndSize = m_Config.EndSize.Sample(m_RNG);
        if (m_Config.SimulationSpace == ParticleSimulationSpace::World) {
            const float scale =
                std::max({std::abs(m_WorldScale.x), std::abs(m_WorldScale.y), std::abs(m_WorldScale.z)});
            data.StartSize *= scale;
            data.EndSize *= scale;
        }
        data.StartColor = m_Config.ColorOverLifetime.Evaluate(0.0f);
        data.EndColor = m_Config.ColorOverLifetime.Evaluate(1.0f);
        data.StartRotation = m_Config.StartRotation.Sample(m_RNG);
        data.AngularVelocity = m_Config.AngularVelocity.Sample(m_RNG);
        data.Lifetime = m_Config.Lifetime.Sample(m_RNG);

        float lightingFlag = m_Config.LightingEnabled ? 1.0f : 0.0f;
        if (m_Config.Modules.TextureSheet && m_Config.TotalFrames > 1 && m_Config.TextureCols > 0 && m_Config.TextureRows > 0) {
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

        if (m_Pool.Spawn(data) == ParticlePool::INVALID_INDEX) return;

        // Only successful births generate events.
        DispatchSubEmitters(SubEmitterTrigger::Birth, data.Position, data.Velocity, data.StartColor);
    }

    void ParticleSystem::Update(float dt, const glm::vec3& emitterWorldPos, const glm::quat& emitterWorldRot, const glm::vec3& emitterWorldScale) {
        SetEmitterTransform(emitterWorldPos, emitterWorldRot, emitterWorldScale);
        if (dt <= 0.0f || !std::isfinite(dt))
            return;

        m_TotalSimulationTime += dt;
        float prevEmitterTime = m_EmitterTime;
        if (m_IsEmitting)
            m_EmitterTime += dt; // Draining particles does not consume a not-yet-started emission window.

        if (!m_Config.Modules.Emission) m_SpawnAccumulator = 0;
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

        uint32_t burstEventsLeft = 64;
        auto processBursts = [&](float windowStart, float windowEnd) {
            for (size_t b = 0; b < m_Config.Bursts.size(); ++b) {
                if (b >= m_BurstRuntime.size()) continue;
                const auto& burst = m_Config.Bursts[b];
                auto& runtime = m_BurstRuntime[b];

                while ((burst.Cycles == 0 || runtime.CompletedCycles < burst.Cycles) &&
                       runtime.NextTriggerTime <= windowEnd && burstEventsLeft > 0) {
                    --burstEventsLeft;
                    uint32_t count = burst.SampleCount(m_RNG);
                    if (m_Config.Modules.Emission && count > 0 && (burst.Probability >= 1.0f || VFXRandom::Float(m_RNG, 0.0f, 1.0f) <= burst.Probability)) {
                        EmitBurst(count, emitterWorldPos, emitterWorldRot, m_WorldScale);
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
            if (m_Config.Modules.Emission && m_Config.EmissionRate > 0.0f) {
                float activeDt = dt;
                if (!m_Config.Looping && m_Config.Duration > 0.0f && m_EmitterTime > m_Config.Duration) {
                    activeDt = std::max(0.0f, m_Config.Duration - prevEmitterTime);
                }
                m_SpawnAccumulator += activeDt * m_Config.EmissionRate;
                const float whole = std::floor(m_SpawnAccumulator);
                uint32_t spawnBudget =
                    static_cast<uint32_t>(std::min(whole, static_cast<float>(m_Pool.GetCapacity())));
                m_SpawnAccumulator -= whole;
                for (uint32_t i = 0; i < spawnBudget; ++i) {
                    SpawnParticle(emitterWorldPos, emitterWorldRot);
                }
            }

            // Legacy periodic burst emission
            if (m_Config.BurstCount > 0 && m_Config.BurstInterval > 0.0f) {
                const float activeDt = (!m_Config.Looping && m_Config.Duration > 0)
                                           ? std::min(dt, std::max(0.0f, m_Config.Duration - prevEmitterTime))
                                           : dt;
                m_BurstTimer += activeDt;
                unsigned budget = 64;
                while (m_BurstTimer >= m_Config.BurstInterval && budget--) {
                    m_BurstTimer -= m_Config.BurstInterval;
                    if (m_Config.Modules.Emission)
                        EmitBurst(m_Config.BurstCount, emitterWorldPos, emitterWorldRot, m_WorldScale);
                }
                m_BurstTimer = std::fmod(m_BurstTimer, m_Config.BurstInterval);
            }

            // Advanced Multi-Burst list evaluation (Unity Shuriken style)
            if (looped) {
                const unsigned loops = static_cast<unsigned>(
                    std::min(64.0, std::floor(static_cast<double>(m_EmitterTime) / m_Config.Duration)));
                processBursts(prevEmitterTime, m_Config.Duration);
                const float remainder = std::fmod(m_EmitterTime, m_Config.Duration);
                for (unsigned loop = 0; loop < loops; ++loop) {
                    for (size_t b = 0; b < m_Config.Bursts.size(); ++b) {
                        m_BurstRuntime[b].CompletedCycles = 0;
                        m_BurstRuntime[b].NextTriggerTime = m_Config.Bursts[b].Time;
                    }
                    processBursts(0, loop + 1 == loops ? remainder : m_Config.Duration);
                }
                m_EmitterTime = remainder;
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
        glm::vec3 baseAccel = gravityAccel + (m_Config.Modules.Forces ? m_Config.ConstantAcceleration : glm::vec3(0));
        if (m_Config.Modules.Forces && m_Config.Buoyancy != 0.0f) {
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
            if (m_Config.Modules.Forces && glm::length(m_Config.Wind) > 1e-5f) {
                glm::vec3 relWind = m_Config.Wind - velocities[i];
                particleAccel += relWind * m_Config.WindInfluence;
            }

            // Force Fields (Wind, Buoyancy, Attractor, Vortex)
            if (m_Config.Modules.Forces) for (const auto& field : m_Config.ForceFields) {
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
            float speedCurveScale = m_Config.Modules.Speed ? m_Config.SpeedOverLifetime.Evaluate(t) : 1.0f;
            glm::vec3 currentVel = velocities[i] * speedCurveScale;

            glm::vec3 prevPos = positions[i];
            glm::vec3 nextPos = positions[i] + currentVel * dt;

            // Physics collisions
            if (m_Config.Collision.Enabled) {
                bool collided = false;
                bool killParticle = m_Config.Collision.KillOnCollision;
                float radius = (m_Config.Collision.ParticleRadius > 0.0f) ? m_Config.Collision.ParticleRadius : (sizes[i] * 0.5f);
                float rest = m_Config.Collision.Restitution;
                float fric = m_Config.Collision.Friction;
                float minBounce = m_Config.Collision.MinBounceSpeed;

                const bool local = m_Config.SimulationSpace == ParticleSimulationSpace::Local;
                glm::vec3 safeScale = m_WorldScale;
                for (int axis = 0; axis < 3; ++axis)
                    if (std::abs(safeScale[axis]) < 0.0001f)
                        safeScale[axis] = 0.0001f;
                auto collisionPrev =
                    local ? m_WorldPosition + m_WorldRotation * (prevPos * m_WorldScale) : prevPos;
                auto collisionPos =
                    local ? m_WorldPosition + m_WorldRotation * (nextPos * m_WorldScale) : nextPos;
                auto collisionVel = local ? m_WorldRotation * (velocities[i] * m_WorldScale) : velocities[i];
                if (local)
                    radius *= std::max({std::abs(safeScale.x), std::abs(safeScale.y), std::abs(safeScale.z)});

                // 1. Floor plane (Floor mode)
                if (m_Config.Collision.CollisionMode == ParticleCollisionMode::Floor) {
                    ParticleCollisionPlane floorPlane(glm::vec3(0.0f, 1.0f, 0.0f), -m_Config.Collision.FloorHeight, rest, fric, m_Config.Collision.FloorKill);
                    if (ParticleCollisionSolver::ResolvePlane(floorPlane, collisionPos, collisionVel, radius,
                                                              rest, fric, minBounce)) {
                        collided = true;
                        if (floorPlane.KillOnCollision) killParticle = true;
                    }
                }

                // 2. Continuous swept raycast (ScenePhysics or explicit raycast)
                if ((m_Config.Collision.CollisionMode == ParticleCollisionMode::ScenePhysics || m_Config.Collision.WorldRaycast) && m_Config.Collision.WorldRaycast) {
                    if (ParticleCollisionSolver::ResolveRaycast(m_Config.Collision.WorldRaycast,
                                                                collisionPrev, collisionPos, collisionVel,
                                                                radius, rest, fric, minBounce)) {
                        collided = true;
                    }
                }

                // 3. Static Collision Planes
                if (m_Config.Collision.CollisionMode == ParticleCollisionMode::PlanesAndBoxes)
                    for (const auto &plane : m_Config.Collision.Planes) {
                        if (ParticleCollisionSolver::ResolvePlane(plane, collisionPos, collisionVel, radius,
                                                                  rest, fric, minBounce)) {
                            collided = true;
                            if (plane.KillOnCollision) {
                                killParticle = true;
                                break;
                            }
                        }
                    }

                // 4. Collision Boxes
                if (m_Config.Collision.CollisionMode == ParticleCollisionMode::PlanesAndBoxes)
                    for (const auto &box : m_Config.Collision.Boxes) {
                        if (ParticleCollisionSolver::ResolveBox(box, collisionPos, collisionVel, radius, rest,
                                                                fric, minBounce)) {
                            collided = true;
                            if (box.KillOnCollision) {
                                killParticle = true;
                                break;
                            }
                        }
                    }

                if (collided) {
                    nextPos =
                        local ? glm::inverse(m_WorldRotation) * (collisionPos - m_WorldPosition) / safeScale
                              : collisionPos;
                    velocities[i] =
                        local ? glm::inverse(m_WorldRotation) * collisionVel / safeScale : collisionVel;
                    m_CollisionCount++;
                    // Sub-Emitter Trigger on Collision
                    DispatchSubEmitters(SubEmitterTrigger::Collision, nextPos, velocities[i], colors[i]);

                    if (killParticle) {
                        m_Pool.Kill(i);
                        continue;
                    }
                }
            }

            positions[i] = nextPos;
            if (m_Config.Modules.Rotation) rotations[i] += angVels[i] * dt;

            // Size & Color curves evaluation
            float curveSize = m_Config.SizeOverLifetime.Evaluate(t);
            sizes[i] = m_Config.Modules.Size ? glm::mix(startSizes[i], endSizes[i], t) * curveSize : startSizes[i];

            colors[i] = m_Config.ColorOverLifetime.Evaluate(m_Config.Modules.Color ? t : 0.0f) * m_Pool.GetColorMultipliers()[i];
            if (m_Config.Modules.Alpha) colors[i].a *= m_Config.AlphaOverLifetime.Evaluate(t);

            // Flipbook UV frame interpolation & sub-frame blending
            float lightingFlag = m_Config.LightingEnabled ? 1.0f : 0.0f;
            if (m_Config.Modules.TextureSheet && m_Config.TotalFrames > 1 && m_Config.TextureCols > 0 && m_Config.TextureRows > 0) {
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
                uvOffsets[i] = glm::vec4(0, 0, 1, 1);
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
