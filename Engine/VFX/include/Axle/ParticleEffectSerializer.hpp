#pragma once

#include <Axle/ParticleEffectDefinition.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Axle {

    class AssetBlock;

    enum class VFXDiagnosticSeverity {
        Info,
        Warning,
        Error
    };

    struct VFXDiagnostic {
        VFXDiagnosticSeverity Severity = VFXDiagnosticSeverity::Error;
        std::string Code;
        std::string PropertyPath;
        std::string Message;
    };

    class ParticleEffectSerializer {
    public:
        // Serializes a multi-emitter ParticleEffectDefinition (schemaVersion: 2) to AssetDSL text
        static std::string Serialize(const ParticleEffectDefinition& effect);

        // Deserializes AssetDSL text into outEffect.
        // Transactional: returns false on error and leaves outEffect untouched.
        // Automatically detects and migrates legacy v1 single-emitter assets to SchemaVersion 2.
        static bool Deserialize(std::string_view dslText,
                                ParticleEffectDefinition& outEffect,
                                std::vector<VFXDiagnostic>* outDiagnostics = nullptr);

        // Transactional file save:
        // Writes to sibling temporary file (e.g. filePath + ".tmp.<timestamp>"), flushes,
        // closes, then atomically replaces the destination. Preserves existing file on failure.
        static bool SerializeToFile(const std::string& filePath,
                                    const ParticleEffectDefinition& effect,
                                    std::vector<VFXDiagnostic>* outDiagnostics = nullptr);

        // Reads file into ParticleEffectDefinition (transactional).
        static bool DeserializeFromFile(const std::string& filePath,
                                        ParticleEffectDefinition& outEffect,
                                        std::vector<VFXDiagnostic>* outDiagnostics = nullptr);

        // Reusable AssetDSL block helpers
        static void WriteEmitterBlock(AssetBlock& parentBlock, const EmitterDefinition& emitter);
        static bool ReadEmitterBlock(const AssetBlock& emitterBlock,
                                     EmitterDefinition& outEmitter,
                                     std::vector<VFXDiagnostic>* outDiagnostics = nullptr);

        static void WriteEmitterConfig(AssetBlock& configBlock, const ParticleEmitterConfig& config);
        static bool ReadEmitterConfig(const AssetBlock& configBlock,
                                      ParticleEmitterConfig& outConfig,
                                      std::vector<VFXDiagnostic>* outDiagnostics = nullptr);
    };

} // namespace Axle
