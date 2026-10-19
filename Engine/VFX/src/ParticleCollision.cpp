#include <Axle/ParticleCollision.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    bool ParticleCollisionSolver::ResolvePlane(const ParticleCollisionPlane& plane,
                                               glm::vec3& pos,
                                               glm::vec3& vel,
                                               float radius,
                                               float restitution,
                                               float friction,
                                               float minBounceSpeed) {
        float dist = glm::dot(plane.Normal, pos) + plane.Distance;
        if (dist >= radius) {
            return false;
        }

        // Project position out of surface penetration
        float penetration = radius - dist;
        pos += plane.Normal * penetration;

        // Normal velocity reflection
        float vn = glm::dot(vel, plane.Normal);
        if (vn < 0.0f) {
            float effRest = (plane.Restitution >= 0.0f) ? plane.Restitution : restitution;
            float effFric = (plane.Friction >= 0.0f) ? plane.Friction : friction;

            float bounceSpeed = -vn * effRest;
            if (bounceSpeed < minBounceSpeed) {
                bounceSpeed = 0.0f;
            }

            // Tangential velocity damped by friction
            glm::vec3 vt = vel - (vn * plane.Normal);
            vt *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);

            vel = (plane.Normal * bounceSpeed) + vt;
        }

        return true;
    }

    bool ParticleCollisionSolver::ResolveBox(const ParticleCollisionBox& box,
                                             glm::vec3& pos,
                                             glm::vec3& vel,
                                             float radius,
                                             float restitution,
                                             float friction,
                                             float minBounceSpeed) {
        float effRest = (box.Restitution >= 0.0f) ? box.Restitution : restitution;
        float effFric = (box.Friction >= 0.0f) ? box.Friction : friction;
        bool collided = false;

        if (box.CollideInside) {
            // Keep particle bounded inside the box volume
            glm::vec3 effectiveMin = box.Min + glm::vec3(radius);
            glm::vec3 effectiveMax = box.Max - glm::vec3(radius);

            // X axis
            if (pos.x < effectiveMin.x) {
                pos.x = effectiveMin.x;
                if (vel.x < 0.0f) {
                    float bounce = -vel.x * effRest;
                    vel.x = (bounce < minBounceSpeed) ? 0.0f : bounce;
                    vel.y *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                    vel.z *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                }
                collided = true;
            } else if (pos.x > effectiveMax.x) {
                pos.x = effectiveMax.x;
                if (vel.x > 0.0f) {
                    float bounce = -vel.x * effRest;
                    vel.x = (-bounce < minBounceSpeed) ? 0.0f : bounce;
                    vel.y *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                    vel.z *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                }
                collided = true;
            }

            // Y axis
            if (pos.y < effectiveMin.y) {
                pos.y = effectiveMin.y;
                if (vel.y < 0.0f) {
                    float bounce = -vel.y * effRest;
                    vel.y = (bounce < minBounceSpeed) ? 0.0f : bounce;
                    vel.x *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                    vel.z *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                }
                collided = true;
            } else if (pos.y > effectiveMax.y) {
                pos.y = effectiveMax.y;
                if (vel.y > 0.0f) {
                    float bounce = -vel.y * effRest;
                    vel.y = (-bounce < minBounceSpeed) ? 0.0f : bounce;
                    vel.x *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                    vel.z *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                }
                collided = true;
            }

            // Z axis
            if (pos.z < effectiveMin.z) {
                pos.z = effectiveMin.z;
                if (vel.z < 0.0f) {
                    float bounce = -vel.z * effRest;
                    vel.z = (bounce < minBounceSpeed) ? 0.0f : bounce;
                    vel.x *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                    vel.y *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                }
                collided = true;
            } else if (pos.z > effectiveMax.z) {
                pos.z = effectiveMax.z;
                if (vel.z > 0.0f) {
                    float bounce = -vel.z * effRest;
                    vel.z = (-bounce < minBounceSpeed) ? 0.0f : bounce;
                    vel.x *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                    vel.y *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                }
                collided = true;
            }
        } else {
            // Exterior obstacle collision
            glm::vec3 expandedMin = box.Min - glm::vec3(radius);
            glm::vec3 expandedMax = box.Max + glm::vec3(radius);

            if (pos.x >= expandedMin.x && pos.x <= expandedMax.x &&
                pos.y >= expandedMin.y && pos.y <= expandedMax.y &&
                pos.z >= expandedMin.z && pos.z <= expandedMax.z) {
                
                // Determine closest face
                float distMinX = pos.x - expandedMin.x;
                float distMaxX = expandedMax.x - pos.x;
                float distMinY = pos.y - expandedMin.y;
                float distMaxY = expandedMax.y - pos.y;
                float distMinZ = pos.z - expandedMin.z;
                float distMaxZ = expandedMax.z - pos.z;

                float minDist = std::min({ distMinX, distMaxX, distMinY, distMaxY, distMinZ, distMaxZ });
                glm::vec3 normal(0.0f);

                if (minDist == distMinX) {
                    normal = glm::vec3(-1.0f, 0.0f, 0.0f);
                    pos.x = expandedMin.x;
                } else if (minDist == distMaxX) {
                    normal = glm::vec3(1.0f, 0.0f, 0.0f);
                    pos.x = expandedMax.x;
                } else if (minDist == distMinY) {
                    normal = glm::vec3(0.0f, -1.0f, 0.0f);
                    pos.y = expandedMin.y;
                } else if (minDist == distMaxY) {
                    normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    pos.y = expandedMax.y;
                } else if (minDist == distMinZ) {
                    normal = glm::vec3(0.0f, 0.0f, -1.0f);
                    pos.z = expandedMin.z;
                } else {
                    normal = glm::vec3(0.0f, 0.0f, 1.0f);
                    pos.z = expandedMax.z;
                }

                float vn = glm::dot(vel, normal);
                if (vn < 0.0f) {
                    float bounceSpeed = -vn * effRest;
                    if (bounceSpeed < minBounceSpeed) bounceSpeed = 0.0f;
                    glm::vec3 vt = vel - (vn * normal);
                    vt *= glm::clamp(1.0f - effFric, 0.0f, 1.0f);
                    vel = (normal * bounceSpeed) + vt;
                }

                collided = true;
            }
        }

        return collided;
    }

    bool ParticleCollisionSolver::ResolveRaycast(const ParticleRaycastFn& raycast,
                                                 const glm::vec3& prevPos,
                                                 glm::vec3& pos,
                                                 glm::vec3& vel,
                                                 float radius,
                                                 float restitution,
                                                 float friction,
                                                 float minBounceSpeed) {
        if (!raycast) return false;

        glm::vec3 travel = pos - prevPos;
        float dist = glm::length(travel);
        if (dist < 1e-5f) return false;

        glm::vec3 dir = travel / dist;
        glm::vec3 hitPoint, hitNormal;

        if (raycast(prevPos, dir, dist + radius, hitPoint, hitNormal)) {
            // Position safely offset along hit normal
            pos = hitPoint + hitNormal * (radius + 0.001f);

            float vn = glm::dot(vel, hitNormal);
            if (vn < 0.0f) {
                float bounceSpeed = -vn * restitution;
                if (bounceSpeed < minBounceSpeed) bounceSpeed = 0.0f;
                glm::vec3 vt = vel - (vn * hitNormal);
                vt *= glm::clamp(1.0f - friction, 0.0f, 1.0f);
                vel = (hitNormal * bounceSpeed) + vt;
            }
            return true;
        }

        return false;
    }

}
