#pragma once

#include <Axle/VFXCommon.hpp>
#include <Axle/CurlNoise.hpp>
#include <Axle/ParticleCollision.hpp>
#include <Axle/ParticleForceField.hpp>
#include <string>

namespace Axle {

    struct ParticleEmitterShape {
        ParticleEmitterShapeType Type = ParticleEmitterShapeType::Point;
        float Radius = 1.0f;
        float ConeAngle = 0.52359877f; // 30 degrees in radians
        glm::vec3 BoxExtents = glm::vec3(1.0f);
        float LineLength = 1.0f;
        glm::vec3 Direction = glm::vec3(0.0f, 1.0f, 0.0f); // Primary emission axis
        bool SurfaceOnly = false; // For sphere/box: emit only on surface shell vs inside volume

        void Sample(glm::vec3& outOffset, glm::vec3& outDir) const;
    };

    struct ParticleEmitterConfig {
        std::string Name = "DefaultEmitter";

        // Emission rate & burst
        float EmissionRate = 50.0f;        // Particles spawned per second
        uint32_t BurstCount = 0;           // Periodic burst particle count
        float BurstInterval = 0.0f;        // Seconds between bursts (0 = no periodic burst)
        uint32_t MaxParticles = 5000;      // Pool allocation limit

        // Particle lifecycle & kinematics
        FloatRange Lifetime = { 1.0f, 2.0f };
        FloatRange InitialSpeed = { 2.0f, 5.0f };
        glm::vec3 ConstantAcceleration = glm::vec3(0.0f);
        float GravityModifier = 0.0f;      // Multiplied by -9.81 m/s^2 along Y
        float Drag = 0.1f;                 // Air resistance drag factor

        // Size & Rotation
        FloatRange StartSize = { 0.2f, 0.4f };
        FloatRange EndSize = { 0.8f, 1.2f };
        FloatRange StartRotation = { 0.0f, 6.2831853f };
        FloatRange AngularVelocity = { -1.5f, 1.5f };

        // Color & Alpha
        ColorGradient ColorOverLifetime = { glm::vec4(1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f) };

        // Flipbook Texture Sheet Animation
        uint32_t TextureRows = 1;
        uint32_t TextureCols = 1;
        uint32_t TotalFrames = 1;
        float AnimationCycles = 1.0f;

        // Space, Blend Mode & Orientation
        ParticleSimulationSpace SimulationSpace = ParticleSimulationSpace::World;
        ParticleBlendMode BlendMode = ParticleBlendMode::AlphaBlend;
        ParticleOrientation Orientation = ParticleOrientation::CameraFacing;

        // Shape
        ParticleEmitterShape Shape;

        // Simulation Dynamics: Curl Noise Turbulence
        CurlNoiseConfig CurlNoise;

        // Environmental Forces & Fields
        glm::vec3 Wind = glm::vec3(0.0f);
        float WindInfluence = 0.5f;
        float Buoyancy = 0.0f;
        std::vector<ParticleForceField> ForceFields;

        // Particle Physics Collisions
        ParticleCollisionConfig Collision;

        // Factory Presets for common automotive & character gameplay VFX
        static ParticleEmitterConfig CreateSmoke(const glm::vec3& color = glm::vec3(0.8f));
        static ParticleEmitterConfig CreateFire();
        static ParticleEmitterConfig CreateSparks();
        static ParticleEmitterConfig CreateTireBurnout();
        static ParticleEmitterConfig CreateDustTrail();
        static ParticleEmitterConfig CreateExhaustBackfire();
    };

}
