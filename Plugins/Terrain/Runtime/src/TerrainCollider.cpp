#include <AxleTerrain/TerrainCollider.hpp>
#include <Axle/PhysicsLayers.hpp>
#include <Axle/Log.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

namespace Axle::Terrain {

    BodyID TerrainCollider::CreateTerrainCollider(
        const TerrainData& terrain,
        PhysicsScene& scene,
        f32 friction,
        f32 restitution
    ) {
        u32 resX = terrain.GetResolutionX();
        u32 resZ = terrain.GetResolutionZ();

        if (resX < 4 || resZ < 4) {
            AXLE_ERROR("TerrainCollider: Resolution ({0}x{1}) too small for physics.", resX, resZ);
            return BodyID();
        }

        const auto& heights = terrain.GetHeights();
        const auto& origin = terrain.GetOrigin();

        // 1. If grid is square (resX == resZ), use optimal JPH::HeightFieldShape
        if (resX == resZ) {
            JPH::Vec3 joltOffset(origin.x, origin.y, origin.z);
            JPH::Vec3 joltScale(terrain.GetSpacingX(), 1.0f, terrain.GetSpacingZ());

            JPH::HeightFieldShapeSettings hfSettings(
                heights.data(),
                joltOffset,
                joltScale,
                resX
            );
            hfSettings.mBlockSize = 2;
            hfSettings.mBitsPerSample = 8;

            JPH::ShapeSettings::ShapeResult shapeResult = hfSettings.Create();
            if (shapeResult.IsValid()) {
                JPH::ShapeRefC shape = shapeResult.Get();
                JPH::BodyCreationSettings bodySettings(
                    shape,
                    JPH::RVec3::sZero(),
                    JPH::Quat::sIdentity(),
                    JPH::EMotionType::Static,
                    Layers::NON_MOVING
                );
                bodySettings.mFriction = friction;
                bodySettings.mRestitution = restitution;

                JPH::BodyID bodyID = scene.GetBodyInterface().CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);
                return bodyID;
            } else {
                AXLE_WARN("TerrainCollider: JPH::HeightFieldShape failed: {0}. Falling back to MeshShape.", shapeResult.GetError().c_str());
            }
        }

        // 2. Fallback to JPH::MeshShape for non-square or irregular terrains
        JPH::VertexList joltVertices;
        joltVertices.reserve(heights.size());
        for (u32 z = 0; z < resZ; ++z) {
            for (u32 x = 0; x < resX; ++x) {
                glm::vec3 wp = terrain.GridToWorld(x, z);
                joltVertices.emplace_back(wp.x, wp.y, wp.z);
            }
        }

        JPH::IndexedTriangleList joltTriangles;
        joltTriangles.reserve((resX - 1) * (resZ - 1) * 2);
        for (u32 z = 0; z < resZ - 1; ++z) {
            for (u32 x = 0; x < resX - 1; ++x) {
                u32 i00 = z * resX + x;
                u32 i10 = z * resX + (x + 1);
                u32 i01 = (z + 1) * resX + x;
                u32 i11 = (z + 1) * resX + (x + 1);

                joltTriangles.emplace_back(i00, i01, i10);
                joltTriangles.emplace_back(i10, i01, i11);
            }
        }

        JPH::MeshShapeSettings meshShapeSettings(joltVertices, joltTriangles);
        JPH::ShapeSettings::ShapeResult shapeResult = meshShapeSettings.Create();

        if (shapeResult.IsValid()) {
            JPH::ShapeRefC shape = shapeResult.Get();
            JPH::BodyCreationSettings bodySettings(
                shape,
                JPH::RVec3::sZero(),
                JPH::Quat::sIdentity(),
                JPH::EMotionType::Static,
                Layers::NON_MOVING
            );
            bodySettings.mFriction = friction;
            bodySettings.mRestitution = restitution;

            JPH::BodyID bodyID = scene.GetBodyInterface().CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);
            return bodyID;
        }

        AXLE_ERROR("TerrainCollider: Failed to create collision shape: {0}", shapeResult.GetError().c_str());
        return BodyID();
    }

    BodyID TerrainCollider::UpdateTerrainCollider(
        BodyID bodyID,
        const TerrainData& terrain,
        PhysicsScene& scene,
        f32 friction,
        f32 restitution
    ) {
        DestroyTerrainCollider(bodyID, scene);
        return CreateTerrainCollider(terrain, scene, friction, restitution);
    }

    void TerrainCollider::DestroyTerrainCollider(BodyID bodyID, PhysicsScene& scene) {
        if (!bodyID.IsInvalid() && scene.IsBodyValid(bodyID)) {
            scene.GetBodyInterface().RemoveBody(bodyID);
            scene.GetBodyInterface().DestroyBody(bodyID);
        }
    }

    bool TerrainCollider::RaycastTerrain(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDir,
        const TerrainData& terrain,
        glm::vec3& outHitPoint,
        f32 maxDistance,
        f32 stepSize
    ) {
        if (glm::dot(rayDir, rayDir) < 0.0001f) {
            return false;
        }

        glm::vec3 dir = glm::normalize(rayDir);
        f32 t = 0.0f;
        f32 prevT = 0.0f;
        bool wasAbove = true;

        glm::vec3 p = rayOrigin;
        f32 terrainH = terrain.SampleHeight(p.x, p.z);
        if (p.y <= terrainH) {
            outHitPoint = p;
            return true;
        }

        while (t < maxDistance) {
            t += stepSize;
            p = rayOrigin + dir * t;

            // Check if point is roughly within terrain bounds
            f32 gx = 0.0f, gz = 0.0f;
            terrain.WorldToGrid(p.x, p.z, gx, gz);
            terrainH = terrain.SampleHeight(p.x, p.z);

            if (p.y <= terrainH) {
                // Crossed beneath the surface -> refine with bisection
                f32 tLow = prevT;
                f32 tHigh = t;

                for (int iter = 0; iter < 12; ++iter) {
                    f32 tMid = (tLow + tHigh) * 0.5f;
                    glm::vec3 pMid = rayOrigin + dir * tMid;
                    f32 hMid = terrain.SampleHeight(pMid.x, pMid.z);

                    if (pMid.y <= hMid) {
                        tHigh = tMid;
                    } else {
                        tLow = tMid;
                    }
                }

                f32 finalT = (tLow + tHigh) * 0.5f;
                outHitPoint = rayOrigin + dir * finalT;
                return true;
            }

            prevT = t;
        }

        return false;
    }

}
