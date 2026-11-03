#include <Axle/ParticleEffectDefinition.hpp>
#include <Axle/ParticleEffectSerializer.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace Axle {

    static std::atomic<uint64_t> s_EmitterIdCounter{1};

    EmitterId GenerateUniqueEmitterId(const std::string& prefix) {
        uint64_t id = s_EmitterIdCounter.fetch_add(1);
        return prefix + "-" + std::to_string(id);
    }

    bool EmitterDefinition::operator==(const EmitterDefinition& other) const {
        if (Id != other.Id || Name != other.Name || Enabled != other.Enabled || TriggerOnly != other.TriggerOnly)
            return false;
        if (LocalTransform != other.LocalTransform)
            return false;
        if (Resources != other.Resources)
            return false;
        return ParticleEmitterConfigsEqual(Config, other.Config);
    }

    bool ParticleEmitterConfigsEqual(const ParticleEmitterConfig& a, const ParticleEmitterConfig& b) {
        // Compare all persisted module fields, excluding runtime callbacks and mesh caches.
        ParticleEffectDefinition left, right;
        left.Emitters.emplace_back();
        right.Emitters.emplace_back();
        left.Emitters[0].Config = a;
        right.Emitters[0].Config = b;
        return ParticleEffectSerializer::Serialize(left) == ParticleEffectSerializer::Serialize(right);
    }

    bool ParticleEffectDefinition::operator==(const ParticleEffectDefinition& other) const {
        if (SchemaVersion != other.SchemaVersion || Name != other.Name || Seed != other.Seed)
            return false;
        if (Emitters.size() != other.Emitters.size())
            return false;
        for (size_t i = 0; i < Emitters.size(); ++i) {
            if (Emitters[i] != other.Emitters[i])
                return false;
        }
        return true;
    }

    const EmitterDefinition* ParticleEffectDefinition::FindEmitter(const EmitterId& id) const {
        for (const auto& emitter : Emitters) {
            if (emitter.Id == id) {
                return &emitter;
            }
        }
        return nullptr;
    }

    EmitterDefinition* ParticleEffectDefinition::FindEmitter(const EmitterId& id) {
        for (auto& emitter : Emitters) {
            if (emitter.Id == id) {
                return &emitter;
            }
        }
        return nullptr;
    }

    bool ValidateParticleEffectDefinition(const ParticleEffectDefinition& definition, std::vector<std::string>& outErrors) {
        outErrors.clear();

        if (definition.SchemaVersion == 0 || definition.SchemaVersion > 2) {
            outErrors.push_back("Unsupported schema version: " + std::to_string(definition.SchemaVersion));
        }

        if (definition.Emitters.size() > 256)
            outErrors.push_back("Effect exceeds the 256-emitter authoring limit.");
        uint64_t poolCapacity = 0;
        std::unordered_set<EmitterId> seenIds;
        for (size_t i = 0; i < definition.Emitters.size(); ++i) {
            const auto& emitter = definition.Emitters[i];
            if (emitter.Id.empty()) {
                outErrors.push_back("Emitter at index " + std::to_string(i) + " has an empty ID.");
            } else if (!seenIds.insert(emitter.Id).second) {
                outErrors.push_back("Duplicate emitter ID detected: '" + emitter.Id + "'. Emitter IDs must be strictly unique.");
            }

            // Validate transform values for NaNs/Infs
            const auto& t = emitter.LocalTransform;
            if (std::isnan(t.Translation.x) || std::isnan(t.Translation.y) || std::isnan(t.Translation.z) ||
                std::isinf(t.Translation.x) || std::isinf(t.Translation.y) || std::isinf(t.Translation.z)) {
                outErrors.push_back("Emitter '" + emitter.Id + "' has invalid translation (NaN or Inf).");
            }

            float rotNormSq = t.Rotation.w * t.Rotation.w + t.Rotation.x * t.Rotation.x +
                              t.Rotation.y * t.Rotation.y + t.Rotation.z * t.Rotation.z;
            if (std::isnan(rotNormSq) || std::isinf(rotNormSq) || rotNormSq < 1e-6f) {
                outErrors.push_back("Emitter '" + emitter.Id + "' has invalid zero or non-finite rotation quaternion.");
            }

            if (t.Scale.x <= 0.0f || t.Scale.y <= 0.0f || t.Scale.z <= 0.0f || !std::isfinite(t.Scale.x) ||
                !std::isfinite(t.Scale.y) || !std::isfinite(t.Scale.z)) {
                outErrors.push_back("Emitter '" + emitter.Id + "' has non-positive or non-finite scale.");
            }

            // Validate basic config
            poolCapacity += emitter.Config.MaxParticles;
            if (emitter.Config.MaxParticles == 0 || emitter.Config.MaxParticles > 1000000) {
                outErrors.push_back("Emitter '" + emitter.Id + "' has MaxParticles outside [1, 1000000].");
            }
            if (!std::isfinite(emitter.Config.Duration) || emitter.Config.Duration < 0.0f) {
                outErrors.push_back("Emitter '" + emitter.Id + "' has negative or non-finite Duration.");
            }
            if (!std::isfinite(emitter.Config.EmissionRate) || emitter.Config.EmissionRate < 0.0f) {
                outErrors.push_back("Emitter '" + emitter.Id + "' has negative or non-finite EmissionRate.");
            }
            const auto &config = emitter.Config;
            auto require = [&](bool valid, const std::string &field) {
                if (!valid)
                    outErrors.push_back("Emitter '" + emitter.Id + "' has invalid " + field + ".");
            };
            auto finite = [](const auto &value) {
                for (int axis = 0; axis < value.length(); ++axis)
                    if (!std::isfinite(value[axis]))
                        return false;
                return true;
            };
            auto unit = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
            require(std::abs(rotNormSq - 1.0f) < 0.001f, "non-unit rotation quaternion");
            for (const auto *range : {&config.Lifetime, &config.InitialSpeed, &config.StartSize,
                                      &config.EndSize, &config.StartRotation, &config.AngularVelocity})
                require(std::isfinite(range->Min) && std::isfinite(range->Max) && range->Min <= range->Max,
                        "numeric range");
            require(config.Lifetime.Min > 0 && config.StartSize.Min >= 0 && config.EndSize.Min >= 0,
                    "lifetime or size range");
            require(static_cast<unsigned>(config.SimulationSpace) <= 1 &&
                        static_cast<unsigned>(config.BlendMode) <= 3 &&
                        static_cast<unsigned>(config.Orientation) <= 2 &&
                        static_cast<unsigned>(config.Shape.Type) <= 7,
                    "enum value");
            require(finite(config.ConstantAcceleration) && finite(config.Wind) &&
                        finite(config.Shape.Direction) && finite(config.Shape.BoxExtents),
                    "vector");
            for (float value :
                 {config.Drag, config.GravityModifier, config.Buoyancy, config.WrapLighting,
                  config.ScatteringFactor, config.NormalMapStrength, config.NearFadeStart, config.NearFadeEnd,
                  config.FarFadeStart, config.FarFadeEnd, config.SoftnessDistance, config.Shape.Radius,
                  config.Shape.LineLength, config.Shape.ConeAngle})
                require(std::isfinite(value), "non-finite parameter");
            require(config.Drag >= 0 && config.SoftnessDistance >= 0 && config.NormalMapStrength >= 0,
                    "negative dynamics/shading parameter");
            require(unit(config.WrapLighting) && unit(config.ScatteringFactor) && unit(config.WindInfluence),
                    "lighting/wind factor");
            require(!config.NearFadeEnabled || config.NearFadeEnd > config.NearFadeStart,
                    "near fade interval");
            require(!config.FarFadeEnabled || config.FarFadeEnd > config.FarFadeStart, "far fade interval");
            require(config.TextureRows > 0 && config.TextureCols > 0 && config.TextureRows <= 1024 &&
                        config.TextureCols <= 1024 && config.TotalFrames > 0 &&
                        config.TotalFrames <=
                            static_cast<uint64_t>(config.TextureRows) * config.TextureCols &&
                        std::isfinite(config.AnimationCycles),
                    "flipbook dimensions");
            for (const auto *curve :
                 {&config.SizeOverLifetime, &config.SpeedOverLifetime, &config.AlphaOverLifetime}) {
                require(finite(curve->P0) && finite(curve->P1) && finite(curve->P2) && finite(curve->P3) &&
                            static_cast<unsigned>(curve->Type) <= 6,
                        "curve controls");
                for (const auto &key : curve->Keyframes)
                    require(unit(key.Time) && std::isfinite(key.Value), "curve key");
            }
            require(finite(config.ColorOverLifetime.StartColor) && finite(config.ColorOverLifetime.EndColor),
                    "gradient endpoints");
            for (const auto &key : config.ColorOverLifetime.ColorKeys)
                require(unit(key.Time) && finite(key.Color), "gradient color key");
            for (const auto &key : config.ColorOverLifetime.AlphaKeys)
                require(unit(key.Time) && unit(key.Alpha), "gradient alpha key");
            for (const auto &burst : config.Bursts)
                require(std::isfinite(burst.Time) && burst.Time >= 0 && std::isfinite(burst.Interval) &&
                            burst.Interval >= 0 && burst.MinCount <= burst.MaxCount &&
                            unit(burst.Probability),
                        "burst");
            for (const auto &link : config.SubEmitters)
                require(static_cast<unsigned>(link.Trigger) <= 2 && unit(link.Probability),
                        "sub-emitter event");
            const auto &noise = config.CurlNoise;
            require(std::isfinite(noise.Frequency) && std::isfinite(noise.Strength) &&
                        std::isfinite(noise.Lacunarity) && unit(noise.Persistence) &&
                        finite(noise.ScrollSpeed) && noise.Frequency >= 0 && noise.Strength >= 0 &&
                        noise.Lacunarity > 0 && noise.Octaves >= 1 && noise.Octaves <= 8,
                    "noise settings");
            const auto &collision = config.Collision;
            require(static_cast<unsigned>(collision.CollisionMode) <= 2 && unit(collision.Restitution) &&
                        unit(collision.Friction) && std::isfinite(collision.FloorHeight) &&
                        std::isfinite(collision.ParticleRadius) && collision.ParticleRadius >= 0 &&
                        std::isfinite(collision.MinBounceSpeed) && collision.MinBounceSpeed >= 0,
                    "collision settings");
            for (const auto &plane : collision.Planes)
                require(finite(plane.Normal) && glm::dot(plane.Normal, plane.Normal) > 0.000001f &&
                            std::isfinite(plane.Distance) && std::isfinite(plane.Restitution) &&
                            std::isfinite(plane.Friction),
                        "collision plane");
            for (const auto &box : collision.Boxes)
                require(finite(box.Min) && finite(box.Max) && box.Min.x < box.Max.x &&
                            box.Min.y < box.Max.y && box.Min.z < box.Max.z &&
                            std::isfinite(box.Restitution) && std::isfinite(box.Friction),
                        "collision box");
            for (const auto &field : config.ForceFields)
                require(static_cast<unsigned>(field.Type) <= 3 && static_cast<unsigned>(field.Falloff) <= 2 &&
                            finite(field.Position) && finite(field.Direction) &&
                            std::isfinite(field.Strength) && std::isfinite(field.Radius) &&
                            field.Radius >= 0 && std::isfinite(field.FalloffExponent) &&
                            std::isfinite(field.RadialSuction) && std::isfinite(field.AxialLift) &&
                            std::isfinite(field.WindInfluence) && std::isfinite(field.GustFrequency) &&
                            std::isfinite(field.GustAmplitude),
                        "force field");
        }

        if (poolCapacity > 4000000)
            outErrors.push_back("Effect exceeds the aggregate 4000000-particle CPU pool limit.");

        // Sub-Emitter Graph Cycle and Target Validation
        ValidateSubEmitterGraph(definition, outErrors);

        return outErrors.empty();
    }

    bool ValidateSubEmitterGraph(const ParticleEffectDefinition& definition, std::vector<std::string>& outErrors) {
        bool valid = true;

        std::unordered_map<std::string, size_t> idToIndex;
        std::unordered_map<std::string, size_t> nameToIndex;
        for (size_t i = 0; i < definition.Emitters.size(); ++i) {
            idToIndex[definition.Emitters[i].Id] = i;
            if (!definition.Emitters[i].Name.empty()) {
                nameToIndex[definition.Emitters[i].Name] = i;
            }
        }

        std::vector<std::vector<size_t>> adj(definition.Emitters.size());

        for (size_t i = 0; i < definition.Emitters.size(); ++i) {
            const auto& emitter = definition.Emitters[i];
            for (const auto& sub : emitter.Config.SubEmitters) {
                if (sub.TargetEmitterName.empty()) {
                    outErrors.push_back("Emitter '" + emitter.Id + "' has a sub-emitter trigger with an empty target.");
                    valid = false;
                    continue;
                }

                size_t targetIdx = definition.Emitters.size();
                auto itId = idToIndex.find(sub.TargetEmitterName);
                if (itId != idToIndex.end()) {
                    targetIdx = itId->second;
                } else {
                    auto itName = nameToIndex.find(sub.TargetEmitterName);
                    if (itName != nameToIndex.end()) {
                        const auto matches = std::count_if(
                            definition.Emitters.begin(), definition.Emitters.end(),
                            [&](const auto &candidate) { return candidate.Name == sub.TargetEmitterName; });
                        if (matches == 1)
                            targetIdx = itName->second;
                    }
                }

                if (targetIdx >= definition.Emitters.size()) {
                    outErrors.push_back("Emitter '" + emitter.Id + "' references non-existent sub-emitter target: '" + sub.TargetEmitterName + "'.");
                    valid = false;
                    continue;
                }

                if (targetIdx == i) {
                    outErrors.push_back("Emitter '" + emitter.Id + "' contains self-referencing sub-emitter target.");
                    valid = false;
                    continue;
                }

                adj[i].push_back(targetIdx);
            }
        }

        // Cycle detection via 3-color DFS
        std::vector<int> visitState(definition.Emitters.size(), 0);
        std::function<bool(size_t)> hasCycle = [&](size_t u) -> bool {
            visitState[u] = 1;
            for (size_t v : adj[u]) {
                if (visitState[v] == 1) {
                    outErrors.push_back("Cycle detected in sub-emitter graph: '" + definition.Emitters[u].Id + "' -> '" + definition.Emitters[v].Id + "'.");
                    return true;
                }
                if (visitState[v] == 0) {
                    if (hasCycle(v)) return true;
                }
            }
            visitState[u] = 2;
            return false;
        };

        for (size_t i = 0; i < definition.Emitters.size(); ++i) {
            if (visitState[i] == 0) {
                if (hasCycle(i)) {
                    valid = false;
                    break;
                }
            }
        }

        return valid;
    }

    ParticleEffectDefinition CreateDefaultParticleEffect(const std::string& name) {
        ParticleEffectDefinition def;
        def.SchemaVersion = 2;
        def.Name = name.empty() ? "Default Effect" : name;
        def.Seed = 1337;

        EmitterDefinition baseEmitter;
        baseEmitter.Id = GenerateUniqueEmitterId("emitter");
        baseEmitter.Name = "Base Emitter";
        baseEmitter.Enabled = true;
        baseEmitter.TriggerOnly = false;
        baseEmitter.LocalTransform = EmitterTransform{};
        baseEmitter.Config = ParticleEmitterConfig::CreateNeutral();
        baseEmitter.Config.Name = baseEmitter.Name;
        baseEmitter.Resources = EmitterResources{};

        def.Emitters.push_back(std::move(baseEmitter));
        return def;
    }

} // namespace Axle
