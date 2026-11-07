#pragma once
#include <Axle/ParticlePool.hpp>
#include <Axle/ParticleEmitter.hpp>
#include <Axle/ParticleEffectDefinition.hpp>
#include <Axle/Texture2D.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Axle {
    // Owned simulation values only; no live system, scene or callback capture.
    struct RenderParticleData {
        ParticlePool Pool;
        ParticleEmitterConfig Config;
        EmitterResources Resources;
        std::shared_ptr<Texture2D> Texture, Normal;
        glm::vec3 Position{0}, Scale{1};
        glm::quat Rotation{1,0,0,0};
    };
}
