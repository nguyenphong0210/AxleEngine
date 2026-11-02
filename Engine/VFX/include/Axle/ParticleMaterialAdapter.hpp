#pragma once

#include <Axle/VFXCommon.hpp>
#include <Axle/ParticleEmitter.hpp>
#include <Axle/ParticleEffectDefinition.hpp>
#include <Axle/ParticleEffectSerializer.hpp>
#include <Axle/Material.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace Axle {

    struct EffectiveParticleRenderSettings {
        glm::vec4 BaseColor = glm::vec4(1.0f);
        std::string TexturePath;
        std::string NormalMapPath;
        std::string MaterialPath;
        ParticleBlendMode BlendMode = ParticleBlendMode::AlphaBlend;
        float AlphaCutoff = 0.5f;
        bool DepthWrite = false;
        bool UseTexture = false;
        bool UseNormalMap = false;

        // Material-derived lighting & emission
        bool LightingEnabled = false;
        float WrapLighting = 0.4f;
        float ScatteringFactor = 0.25f;
        float NormalMapStrength = 1.0f;
        glm::vec3 EmissiveColor = glm::vec3(0.0f);
        float EmissiveIntensity = 0.0f;
        glm::vec2 UVScale = glm::vec2(1.0f);
        glm::vec2 UVOffset = glm::vec2(0.0f);

        std::vector<VFXDiagnostic> Diagnostics;
    };

    class ParticleMaterialAdapter {
    public:
        using MaterialLoaderFunc = std::function<std::shared_ptr<Material>(const std::string& path)>;

        static EffectiveParticleRenderSettings Resolve(
            const ParticleEmitterConfig& config,
            const EmitterResources& resources,
            MaterialLoaderFunc materialLoader = nullptr);
    };

}
