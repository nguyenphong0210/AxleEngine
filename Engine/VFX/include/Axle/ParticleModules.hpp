#pragma once
#include <Axle/ParticleEmitter.hpp>

namespace Axle {
    enum class ParticleModule { Main, Emission, Shape, Size, Speed, Color, Alpha, Rotation,
                                Forces, Noise, Collision, SubEmitters, TextureSheet, Renderer };
    const char* GetParticleModuleName(ParticleModule module);
    // Null for always-present Main and Renderer. Disabled modules retain their authored values.
    bool* GetParticleModuleToggle(ParticleEmitterConfig& config, ParticleModule module);
    void CopyParticleModule(ParticleEmitterConfig& target, const ParticleEmitterConfig& source,
                            ParticleModule module);
    void ResetParticleModule(ParticleEmitterConfig& target, ParticleModule module);
}
