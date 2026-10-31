#pragma once

#include <Axle/ParticleEmitter.hpp>
#include <string>
#include <string_view>

namespace Axle {

    class ParticleEmitterSerializer {
    public:
        // Serializes a ParticleEmitterConfig (and optional texture path) to standardized .axleasset DSL text
        static std::string Serialize(const ParticleEmitterConfig& config, const std::string& texturePath = "");

        // Deserializes a ParticleEmitterConfig (and optional texture path) from .axleasset DSL text
        static bool Deserialize(std::string_view dslText, ParticleEmitterConfig& outConfig, std::string& outTexturePath);

        // File operations
        static bool SerializeToFile(const std::string& filePath, const ParticleEmitterConfig& config, const std::string& texturePath = "");
        static bool DeserializeFromFile(const std::string& filePath, ParticleEmitterConfig& outConfig, std::string& outTexturePath);
    };

}
