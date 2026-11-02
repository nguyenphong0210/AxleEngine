#pragma once

#include <Axle/ParticleEmitter.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace Axle {

    using EmitterId = std::string;

    struct EmitterTransform {
        glm::vec3 Translation{0.0f, 0.0f, 0.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f}; // [w, x, y, z]
        glm::vec3 Scale{1.0f, 1.0f, 1.0f};

        bool operator==(const EmitterTransform& other) const {
            return Translation == other.Translation &&
                   Rotation == other.Rotation &&
                   Scale == other.Scale;
        }
        bool operator!=(const EmitterTransform& other) const {
            return !(*this == other);
        }
    };

    struct EmitterResources {
        std::string TexturePath;
        std::string NormalMapPath;
        std::string MaterialPath;
        bool UseTexture = false;
        bool UseNormalMap = false;

        bool operator==(const EmitterResources& other) const {
            return TexturePath == other.TexturePath &&
                   NormalMapPath == other.NormalMapPath &&
                   MaterialPath == other.MaterialPath &&
                   UseTexture == other.UseTexture &&
                   UseNormalMap == other.UseNormalMap;
        }
        bool operator!=(const EmitterResources& other) const {
            return !(*this == other);
        }
    };

    struct EmitterDefinition {
        EmitterId Id;
        std::string Name;
        bool Enabled = true;
        bool TriggerOnly = false;
        EmitterTransform LocalTransform;
        ParticleEmitterConfig Config;
        EmitterResources Resources;

        bool operator==(const EmitterDefinition& other) const;
        bool operator!=(const EmitterDefinition& other) const {
            return !(*this == other);
        }
    };

    struct ParticleEffectDefinition {
        uint32_t SchemaVersion = 2;
        std::string Name = "New Effect";
        uint32_t Seed = 1;
        std::vector<EmitterDefinition> Emitters;

        bool operator==(const ParticleEffectDefinition& other) const;
        bool operator!=(const ParticleEffectDefinition& other) const {
            return !(*this == other);
        }

        const EmitterDefinition* FindEmitter(const EmitterId& id) const;
        EmitterDefinition* FindEmitter(const EmitterId& id);
        bool HasEmitter(const EmitterId& id) const { return FindEmitter(id) != nullptr; }
    };

    bool ParticleEmitterConfigsEqual(const ParticleEmitterConfig& a, const ParticleEmitterConfig& b);

    // Validation & Factory Helpers
    bool ValidateParticleEffectDefinition(const ParticleEffectDefinition& definition, std::vector<std::string>& outErrors);
    bool ValidateSubEmitterGraph(const ParticleEffectDefinition& definition, std::vector<std::string>& outErrors);
    ParticleEffectDefinition CreateDefaultParticleEffect(const std::string& name = "Default Effect");
    EmitterId GenerateUniqueEmitterId(const std::string& prefix = "emitter");

} // namespace Axle
