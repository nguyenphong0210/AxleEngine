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

        if (definition.Emitters.empty()) {
            outErrors.push_back("Effect definition contains zero emitters. At least one emitter is required.");
        }

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

            if (t.Scale.x <= 0.0f || t.Scale.y <= 0.0f || t.Scale.z <= 0.0f ||
                std::isnan(t.Scale.x) || std::isnan(t.Scale.y) || std::isnan(t.Scale.z)) {
                outErrors.push_back("Emitter '" + emitter.Id + "' has non-positive or non-finite scale.");
            }

            // Validate basic config
            if (emitter.Config.MaxParticles == 0) {
                outErrors.push_back("Emitter '" + emitter.Id + "' has MaxParticles == 0.");
            }
            if (emitter.Config.Duration < 0.0f) {
                outErrors.push_back("Emitter '" + emitter.Id + "' has negative Duration.");
            }
            if (emitter.Config.EmissionRate < 0.0f) {
                outErrors.push_back("Emitter '" + emitter.Id + "' has negative EmissionRate.");
            }
        }

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
        baseEmitter.Config = ParticleEmitterConfig::CreateFire();
        baseEmitter.Config.Name = baseEmitter.Name;
        baseEmitter.Resources = EmitterResources{};

        def.Emitters.push_back(std::move(baseEmitter));
        return def;
    }

} // namespace Axle
