#include <Axle/ParticleEmitterSerializer.hpp>
#include <Axle/ParticleEffectSerializer.hpp>
#include <Axle/AssetDSL.hpp>
#include "ParticleAssetFileIO.hpp"
#include <fstream>
#include <sstream>

namespace Axle {
    std::string ParticleEmitterSerializer::Serialize(const ParticleEmitterConfig &config,
                                                     const std::string &texturePath) {
        // Keep the flat legacy container while sharing the complete module codec.
        AssetDocument document;
        document.Kind = AssetKind::ParticleEmitter;
        document.Root = std::make_shared<AssetBlock>();
        document.Root->Type = "ParticleEmitter";
        ParticleEffectSerializer::WriteEmitterConfig(*document.Root, config);
        if (!texturePath.empty())
            document.Root->SetString("texture", texturePath);
        return AssetDSL::SerializeDocument(document);
    }
    bool ParticleEmitterSerializer::Deserialize(std::string_view text, ParticleEmitterConfig &config,
                                                std::string &texturePath) {
        AssetDocument document;
        if (!AssetDSL::ParseSource(text, document) || !document.IsValid() ||
            document.Kind != AssetKind::ParticleEmitter)
            return false;
        // Never silently truncate a modern multi-emitter document through the single-emitter API.
        if (document.Root->GetUInt("schemaVersion", 1) >= 2)
            return false;
        ParticleEmitterConfig candidate;
        if (!ParticleEffectSerializer::ReadEmitterConfig(*document.Root, candidate))
            return false;
        config = std::move(candidate);
        texturePath = document.Root->GetString("texture", "");
        return true;
    }
    bool ParticleEmitterSerializer::SerializeToFile(const std::string &path,
                                                    const ParticleEmitterConfig &config,
                                                    const std::string &texturePath) {
        return Detail::WriteParticleAssetAtomically(path, Serialize(config, texturePath));
    }
    bool ParticleEmitterSerializer::DeserializeFromFile(const std::string &path,
                                                        ParticleEmitterConfig &config,
                                                        std::string &texturePath) {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return false;
        std::stringstream text;
        text << input.rdbuf();
        return Deserialize(text.str(), config, texturePath);
    }
}
