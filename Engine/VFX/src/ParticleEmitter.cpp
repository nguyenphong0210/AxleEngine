#include <Axle/ParticleEmitter.hpp>
#include <cmath>

namespace Axle {

    static std::mt19937 s_RNG(1337);

    void VFXRandom::SetSeed(uint32_t seed) {
        s_RNG.seed(seed);
    }

    float VFXRandom::Float(float min, float max) {
        std::uniform_real_distribution<float> dist(min, max);
        return dist(s_RNG);
    }

    glm::vec3 VFXRandom::UnitVector() {
        float z = Float(-1.0f, 1.0f);
        float phi = Float(0.0f, 6.2831853f);
        float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return glm::vec3(r * std::cos(phi), r * std::sin(phi), z);
    }

    glm::vec3 VFXRandom::InUnitSphere() {
        glm::vec3 u = UnitVector();
        float r = std::cbrt(Float(0.0f, 1.0f));
        return u * r;
    }

    glm::vec3 VFXRandom::InUnitBox(const glm::vec3& halfExtents) {
        return glm::vec3(
            Float(-halfExtents.x, halfExtents.x),
            Float(-halfExtents.y, halfExtents.y),
            Float(-halfExtents.z, halfExtents.z)
        );
    }

    float FloatRange::Sample() const {
        if (Min >= Max) return Min;
        return VFXRandom::Float(Min, Max);
    }

    void ParticleEmitterShape::Sample(glm::vec3& outOffset, glm::vec3& outDir) const {
        glm::vec3 normDir = (glm::length(Direction) > 0.0001f) ? glm::normalize(Direction) : glm::vec3(0.0f, 1.0f, 0.0f);

        switch (Type) {
            case ParticleEmitterShapeType::Point: {
                outOffset = glm::vec3(0.0f);
                outDir = normDir;
                break;
            }
            case ParticleEmitterShapeType::Sphere: {
                if (SurfaceOnly) {
                    outOffset = VFXRandom::UnitVector() * Radius;
                } else {
                    outOffset = VFXRandom::InUnitSphere() * Radius;
                }
                outDir = (glm::length(outOffset) > 0.0001f) ? glm::normalize(outOffset) : normDir;
                break;
            }
            case ParticleEmitterShapeType::Hemisphere: {
                glm::vec3 r = VFXRandom::InUnitSphere();
                if (glm::dot(r, normDir) < 0.0f) {
                    r = -r;
                }
                if (SurfaceOnly) {
                    r = glm::normalize(r);
                }
                outOffset = r * Radius;
                outDir = (glm::length(outOffset) > 0.0001f) ? glm::normalize(outOffset) : normDir;
                break;
            }
            case ParticleEmitterShapeType::Box: {
                if (SurfaceOnly) {
                    outOffset = VFXRandom::InUnitBox(BoxExtents);
                    int face = static_cast<int>(VFXRandom::Float(0.0f, 6.0f));
                    if (face == 0) outOffset.x = BoxExtents.x;
                    else if (face == 1) outOffset.x = -BoxExtents.x;
                    else if (face == 2) outOffset.y = BoxExtents.y;
                    else if (face == 3) outOffset.y = -BoxExtents.y;
                    else if (face == 4) outOffset.z = BoxExtents.z;
                    else outOffset.z = -BoxExtents.z;
                } else {
                    outOffset = VFXRandom::InUnitBox(BoxExtents);
                }
                outDir = normDir;
                break;
            }
            case ParticleEmitterShapeType::Cone: {
                // Compute local orthogonal basis for the cone axis
                glm::vec3 up = (std::abs(normDir.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                glm::vec3 right = glm::normalize(glm::cross(normDir, up));
                up = glm::cross(right, normDir);

                // Sample base circle
                float r = Radius * std::sqrt(VFXRandom::Float(0.0f, 1.0f));
                float phi = VFXRandom::Float(0.0f, 6.2831853f);
                outOffset = (right * std::cos(phi) + up * std::sin(phi)) * r;

                // Sample cone angle
                float theta = VFXRandom::Float(0.0f, ConeAngle);
                float dirPhi = VFXRandom::Float(0.0f, 6.2831853f);
                float sinTheta = std::sin(theta);
                glm::vec3 localDir(sinTheta * std::cos(dirPhi), sinTheta * std::sin(dirPhi), std::cos(theta));
                outDir = glm::normalize(right * localDir.x + up * localDir.y + normDir * localDir.z);
                break;
            }
            case ParticleEmitterShapeType::Line: {
                float t = VFXRandom::Float(-0.5f, 0.5f) * LineLength;
                outOffset = glm::vec3(t, 0.0f, 0.0f);
                outDir = normDir;
                break;
            }
            case ParticleEmitterShapeType::Circle: {
                glm::vec3 up = (std::abs(normDir.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                glm::vec3 right = glm::normalize(glm::cross(normDir, up));
                up = glm::cross(right, normDir);
                float r = SurfaceOnly ? Radius : (Radius * std::sqrt(VFXRandom::Float(0.0f, 1.0f)));
                float phi = VFXRandom::Float(0.0f, 6.2831853f);
                outOffset = (right * std::cos(phi) + up * std::sin(phi)) * r;
                outDir = normDir;
                break;
            }
            case ParticleEmitterShapeType::MeshSurface: {
                outOffset = VFXRandom::UnitVector() * Radius;
                outDir = glm::normalize(outOffset);
                break;
            }
        }
    }

    ParticleEmitterConfig ParticleEmitterConfig::CreateSmoke(const glm::vec3& color) {
        ParticleEmitterConfig cfg;
        cfg.Name = "SmokeEmitter";
        cfg.EmissionRate = 40.0f;
        cfg.MaxParticles = 2000;
        cfg.Lifetime = FloatRange(1.5f, 3.0f);
        cfg.InitialSpeed = FloatRange(1.0f, 2.5f);
        cfg.ConstantAcceleration = glm::vec3(0.0f);
        cfg.GravityModifier = -0.1f; // Slight buoyancy upward
        cfg.Drag = 0.25f;
        cfg.StartSize = FloatRange(0.3f, 0.5f);
        cfg.EndSize = FloatRange(1.8f, 2.8f);
        cfg.StartRotation = FloatRange(0.0f, 6.2831853f);
        cfg.AngularVelocity = FloatRange(-0.5f, 0.5f);
        cfg.ColorOverLifetime = ColorGradient(
            glm::vec4(color, 0.7f),
            glm::vec4(color * 0.9f, 0.0f)
        );
        cfg.SimulationSpace = ParticleSimulationSpace::World;
        cfg.BlendMode = ParticleBlendMode::AlphaBlend;
        cfg.Orientation = ParticleOrientation::CameraFacing;
        cfg.Shape.Type = ParticleEmitterShapeType::Cone;
        cfg.Shape.Radius = 0.15f;
        cfg.Shape.ConeAngle = 0.45f;
        cfg.Shape.Direction = glm::vec3(0.0f, 1.0f, 0.0f);

        // Simulation Dynamics: Curl noise turbulence & thermal buoyancy
        cfg.CurlNoise.Enabled = true;
        cfg.CurlNoise.Frequency = 0.5f;
        cfg.CurlNoise.Strength = 1.2f;
        cfg.CurlNoise.Octaves = 2;
        cfg.CurlNoise.ScrollSpeed = glm::vec3(0.0f, 0.5f, 0.0f);
        cfg.Buoyancy = 1.0f;

        return cfg;
    }

    ParticleEmitterConfig ParticleEmitterConfig::CreateFire() {
        ParticleEmitterConfig cfg;
        cfg.Name = "FireEmitter";
        cfg.EmissionRate = 80.0f;
        cfg.MaxParticles = 2000;
        cfg.Lifetime = FloatRange(0.4f, 0.9f);
        cfg.InitialSpeed = FloatRange(2.0f, 4.5f);
        cfg.ConstantAcceleration = glm::vec3(0.0f);
        cfg.GravityModifier = -0.4f; // Rapid fire updraft
        cfg.Drag = 0.1f;
        cfg.StartSize = FloatRange(0.4f, 0.7f);
        cfg.EndSize = FloatRange(0.1f, 0.2f); // Shrinks as it burns out
        cfg.StartRotation = FloatRange(0.0f, 6.2831853f);
        cfg.AngularVelocity = FloatRange(-1.0f, 1.0f);
        cfg.ColorOverLifetime = ColorGradient(
            glm::vec4(1.0f, 0.85f, 0.25f, 1.0f), // Golden yellow core
            glm::vec4(0.85f, 0.15f, 0.02f, 0.0f)  // Crimson red fadeout
        );
        cfg.SimulationSpace = ParticleSimulationSpace::World;
        cfg.BlendMode = ParticleBlendMode::Additive;
        cfg.Orientation = ParticleOrientation::CameraFacing;
        cfg.Shape.Type = ParticleEmitterShapeType::Cone;
        cfg.Shape.Radius = 0.2f;
        cfg.Shape.ConeAngle = 0.35f;
        cfg.Shape.Direction = glm::vec3(0.0f, 1.0f, 0.0f);

        // Simulation Dynamics: Fast chaotic turbulence & high buoyancy
        cfg.CurlNoise.Enabled = true;
        cfg.CurlNoise.Frequency = 1.0f;
        cfg.CurlNoise.Strength = 2.5f;
        cfg.Buoyancy = 3.0f;

        return cfg;
    }

    ParticleEmitterConfig ParticleEmitterConfig::CreateSparks() {
        ParticleEmitterConfig cfg;
        cfg.Name = "SparksEmitter";
        cfg.EmissionRate = 80.0f;
        cfg.BurstCount = 30;
        cfg.BurstInterval = 0.5f;
        cfg.MaxParticles = 1500;
        cfg.Lifetime = FloatRange(0.3f, 0.8f);
        cfg.InitialSpeed = FloatRange(5.0f, 12.0f);
        cfg.ConstantAcceleration = glm::vec3(0.0f);
        cfg.GravityModifier = 1.0f; // Falls fast with gravity
        cfg.Drag = 0.05f;
        cfg.StartSize = FloatRange(0.06f, 0.10f);
        cfg.EndSize = FloatRange(0.02f, 0.04f);
        cfg.ColorOverLifetime = ColorGradient(
            glm::vec4(1.0f, 0.9f, 0.5f, 1.0f),
            glm::vec4(0.9f, 0.3f, 0.05f, 0.0f)
        );
        cfg.SimulationSpace = ParticleSimulationSpace::World;
        cfg.BlendMode = ParticleBlendMode::Additive;
        cfg.Orientation = ParticleOrientation::VelocityAligned;
        cfg.Shape.Type = ParticleEmitterShapeType::Hemisphere;
        cfg.Shape.Radius = 0.05f;
        cfg.Shape.Direction = glm::vec3(0.0f, 1.0f, 0.0f);

        // Physics Collisions: Ground plane bounce
        cfg.Collision.Enabled = true;
        cfg.Collision.Restitution = 0.6f;
        cfg.Collision.Friction = 0.25f;
        cfg.Collision.MinBounceSpeed = 0.15f;
        cfg.Collision.ParticleRadius = 0.02f;
        cfg.Collision.Planes.push_back(ParticleCollisionPlane(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.6f, 0.25f));

        return cfg;
    }

    ParticleEmitterConfig ParticleEmitterConfig::CreateTireBurnout() {
        ParticleEmitterConfig cfg;
        cfg.Name = "TireBurnoutEmitter";
        cfg.EmissionRate = 120.0f;
        cfg.MaxParticles = 3000;
        cfg.Lifetime = FloatRange(1.0f, 2.2f);
        cfg.InitialSpeed = FloatRange(1.5f, 3.5f);
        cfg.GravityModifier = -0.15f;
        cfg.Drag = 0.35f;
        cfg.StartSize = FloatRange(0.4f, 0.6f);
        cfg.EndSize = FloatRange(2.0f, 3.5f);
        cfg.StartRotation = FloatRange(0.0f, 6.2831853f);
        cfg.AngularVelocity = FloatRange(-0.8f, 0.8f);
        cfg.ColorOverLifetime = ColorGradient(
            glm::vec4(0.95f, 0.95f, 0.95f, 0.85f),
            glm::vec4(0.85f, 0.85f, 0.85f, 0.0f)
        );
        cfg.SimulationSpace = ParticleSimulationSpace::World;
        cfg.BlendMode = ParticleBlendMode::AlphaBlend;
        cfg.Orientation = ParticleOrientation::CameraFacing;
        cfg.Shape.Type = ParticleEmitterShapeType::Box;
        cfg.Shape.BoxExtents = glm::vec3(0.25f, 0.08f, 0.35f); // Tire contact patch footprint
        cfg.Shape.Direction = glm::vec3(0.0f, 0.5f, -1.0f); // Billows backward and slightly up
        return cfg;
    }

    ParticleEmitterConfig ParticleEmitterConfig::CreateDustTrail() {
        ParticleEmitterConfig cfg;
        cfg.Name = "DustTrailEmitter";
        cfg.EmissionRate = 60.0f;
        cfg.MaxParticles = 2500;
        cfg.Lifetime = FloatRange(0.8f, 1.8f);
        cfg.InitialSpeed = FloatRange(1.0f, 2.8f);
        cfg.GravityModifier = 0.05f;
        cfg.Drag = 0.4f;
        cfg.StartSize = FloatRange(0.35f, 0.55f);
        cfg.EndSize = FloatRange(1.8f, 2.8f);
        cfg.StartRotation = FloatRange(0.0f, 6.2831853f);
        cfg.AngularVelocity = FloatRange(-0.6f, 0.6f);
        cfg.ColorOverLifetime = ColorGradient(
            glm::vec4(0.72f, 0.62f, 0.48f, 0.65f), // Sandy earth tone
            glm::vec4(0.72f, 0.62f, 0.48f, 0.0f)
        );
        cfg.SimulationSpace = ParticleSimulationSpace::World;
        cfg.BlendMode = ParticleBlendMode::AlphaBlend;
        cfg.Orientation = ParticleOrientation::CameraFacing;
        cfg.Shape.Type = ParticleEmitterShapeType::Sphere;
        cfg.Shape.Radius = 0.25f;
        cfg.Shape.Direction = glm::vec3(0.0f, 0.3f, -1.0f);
        return cfg;
    }

    ParticleEmitterConfig ParticleEmitterConfig::CreateExhaustBackfire() {
        ParticleEmitterConfig cfg;
        cfg.Name = "ExhaustBackfireEmitter";
        cfg.EmissionRate = 0.0f; // Event burst driven
        cfg.BurstCount = 25;
        cfg.BurstInterval = 0.0f;
        cfg.MaxParticles = 500;
        cfg.Lifetime = FloatRange(0.06f, 0.16f);
        cfg.InitialSpeed = FloatRange(8.0f, 16.0f);
        cfg.GravityModifier = 0.0f;
        cfg.Drag = 0.05f;
        cfg.StartSize = FloatRange(0.12f, 0.22f);
        cfg.EndSize = FloatRange(0.04f, 0.08f);
        cfg.ColorOverLifetime = ColorGradient(
            glm::vec4(0.3f, 0.6f, 1.0f, 1.0f), // Nitro blue core
            glm::vec4(1.0f, 0.35f, 0.05f, 0.0f) // Fiery orange tip
        );
        cfg.SimulationSpace = ParticleSimulationSpace::World;
        cfg.BlendMode = ParticleBlendMode::Additive;
        cfg.Orientation = ParticleOrientation::VelocityAligned;
        cfg.Shape.Type = ParticleEmitterShapeType::Cone;
        cfg.Shape.Radius = 0.04f;
        cfg.Shape.ConeAngle = 0.18f;
        cfg.Shape.Direction = glm::vec3(0.0f, 0.0f, -1.0f); // Straight back from exhaust pipe
        return cfg;
    }

}
