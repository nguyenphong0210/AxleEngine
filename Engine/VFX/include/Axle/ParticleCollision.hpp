#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <cstdint>

namespace Axle {

    struct ParticleCollisionPlane {
        glm::vec3 Normal = glm::vec3(0.0f, 1.0f, 0.0f);
        float Distance = 0.0f; // Plane equation: Normal . P + Distance = 0
        float Restitution = 0.5f; // Bounciness factor [0 = no bounce, 1 = elastic]
        float Friction = 0.2f;    // Tangential friction [0 = frictionless, 1 = full stop]
        bool KillOnCollision = false;

        ParticleCollisionPlane() = default;
        ParticleCollisionPlane(const glm::vec3& normal, float distance, float restitution = 0.5f, float friction = 0.2f, bool kill = false)
            : Normal(glm::normalize(normal)), Distance(distance), Restitution(restitution), Friction(friction), KillOnCollision(kill) {}

        static ParticleCollisionPlane FromPointAndNormal(const glm::vec3& point, const glm::vec3& normal,
                                                         float restitution = 0.5f, float friction = 0.2f, bool kill = false) {
            glm::vec3 norm = glm::normalize(normal);
            return ParticleCollisionPlane(norm, -glm::dot(norm, point), restitution, friction, kill);
        }
    };

    struct ParticleCollisionBox {
        glm::vec3 Min = glm::vec3(-1.0f);
        glm::vec3 Max = glm::vec3(1.0f);
        bool CollideInside = false; // true = particles contained inside box, false = bounce off outside
        float Restitution = 0.5f;
        float Friction = 0.2f;
        bool KillOnCollision = false;

        ParticleCollisionBox() = default;
        ParticleCollisionBox(const glm::vec3& minBounds, const glm::vec3& maxBounds, bool collideInside = false,
                             float restitution = 0.5f, float friction = 0.2f, bool kill = false)
            : Min(minBounds), Max(maxBounds), CollideInside(collideInside),
              Restitution(restitution), Friction(friction), KillOnCollision(kill) {}
    };

    using ParticleRaycastFn = std::function<bool(const glm::vec3& rayOrigin,
                                                 const glm::vec3& rayDir,
                                                 float maxDist,
                                                 glm::vec3& outHitPoint,
                                                 glm::vec3& outHitNormal)>;

    struct ParticleCollisionConfig {
        bool Enabled = false;
        float Restitution = 0.5f;        // Global fallback restitution
        float Friction = 0.2f;           // Global fallback friction
        float MinBounceSpeed = 0.1f;     // Threshold (m/s) below which bounce stops to prevent resting jitter
        float ParticleRadius = 0.05f;    // Collision margin radius
        bool KillOnCollision = false;    // Kill particles on impact if true

        std::vector<ParticleCollisionPlane> Planes;
        std::vector<ParticleCollisionBox> Boxes;
        ParticleRaycastFn WorldRaycast = nullptr;
    };

    class ParticleCollisionSolver {
    public:
        // Resolves collision against a static plane. Returns true if collision occurred.
        static bool ResolvePlane(const ParticleCollisionPlane& plane,
                                 glm::vec3& pos,
                                 glm::vec3& vel,
                                 float radius,
                                 float restitution,
                                 float friction,
                                 float minBounceSpeed);

        // Resolves collision against an AABB box (interior or exterior). Returns true if collision occurred.
        static bool ResolveBox(const ParticleCollisionBox& box,
                               glm::vec3& pos,
                               glm::vec3& vel,
                               float radius,
                               float restitution,
                               float friction,
                               float minBounceSpeed);

        // Resolves continuous swept collision along path from prevPos to current pos using raycast.
        static bool ResolveRaycast(const ParticleRaycastFn& raycast,
                                   const glm::vec3& prevPos,
                                   glm::vec3& pos,
                                   glm::vec3& vel,
                                   float radius,
                                   float restitution,
                                   float friction,
                                   float minBounceSpeed);
    };

}
