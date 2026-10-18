#include <AxleSpline/TrackGenerator.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/Log.hpp>
#include <Axle/PhysicsLayers.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <cmath>
#include <algorithm>

namespace Axle::Spline {

    struct ProfilePoint {
        float U;
        float V;
        float TexU;
    };

    TrackMeshData TrackGenerator::GenerateTrackMesh(const Curve3D& curve, const SplineComponent& config) {
        TrackMeshData meshData;

        float totalLength = curve.GetTotalLength();
        if (totalLength < 0.1f) {
            return meshData;
        }

        float halfWidth = config.TrackWidth * 0.5f;
        std::vector<ProfilePoint> profile;

        // 1. Build 2D cross-section profile
        // Left Guardrail
        if (config.GenerateGuardrails) {
            float curbOffset = config.GenerateCurbs ? config.CurbWidth : 0.0f;
            float leftOuter = -halfWidth - curbOffset - config.GuardrailThickness;
            float leftInner = -halfWidth - curbOffset;
            profile.push_back({ leftOuter, 0.0f, -0.4f });
            profile.push_back({ leftOuter, config.GuardrailHeight, -0.35f });
            profile.push_back({ leftInner, config.GuardrailHeight, -0.3f });
        }

        // Left Curb
        if (config.GenerateCurbs) {
            profile.push_back({ -halfWidth - config.CurbWidth, 0.0f, -0.1f });
            profile.push_back({ -halfWidth, config.CurbHeight, -0.05f });
        }

        // Road surface (subdivided with subtle road camber for visual depth)
        profile.push_back({ -halfWidth, 0.0f, 0.0f });
        profile.push_back({ -halfWidth * 0.333f, 0.03f, 0.333f });
        profile.push_back({ +halfWidth * 0.333f, 0.03f, 0.667f });
        profile.push_back({ +halfWidth, 0.0f, 1.0f });

        // Right Curb
        if (config.GenerateCurbs) {
            profile.push_back({ +halfWidth, config.CurbHeight, 1.05f });
            profile.push_back({ +halfWidth + config.CurbWidth, 0.0f, 1.1f });
        }

        // Right Guardrail
        if (config.GenerateGuardrails) {
            float curbOffset = config.GenerateCurbs ? config.CurbWidth : 0.0f;
            float rightInner = +halfWidth + curbOffset;
            float rightOuter = +halfWidth + curbOffset + config.GuardrailThickness;
            profile.push_back({ rightInner, config.GuardrailHeight, 1.3f });
            profile.push_back({ rightOuter, config.GuardrailHeight, 1.35f });
            profile.push_back({ rightOuter, 0.0f, 1.4f });
        }

        u32 pointsPerRing = static_cast<u32>(profile.size());
        if (pointsPerRing < 2) return meshData;

        // 2. March along spline to generate vertex rings
        float step = std::max(0.2f, config.StepDistance);
        u32 ringCount = std::max(2u, static_cast<u32>(std::ceil(totalLength / step)));
        if (curve.IsLoop()) {
            ringCount = std::max(3u, ringCount);
        }

        meshData.Positions.reserve(ringCount * pointsPerRing);
        meshData.UVs.reserve(ringCount * pointsPerRing);

        for (u32 r = 0; r < ringCount; ++r) {
            float dist = curve.IsLoop()
                ? (static_cast<float>(r) / static_cast<float>(ringCount)) * totalLength
                : (static_cast<float>(r) / static_cast<float>(ringCount - 1)) * totalLength;

            SplineFrame frame = curve.SampleFrame(dist);

            for (const auto& pt : profile) {
                glm::vec3 worldPos = frame.Position
                    + (frame.Scale * pt.U) * frame.Binormal
                    + pt.V * frame.Normal;

                glm::vec2 uv(pt.TexU, dist * config.TextureRepeatV);

                meshData.Positions.push_back(worldPos);
                meshData.UVs.push_back(uv);
            }
        }

        // 3. Connect rings into triangles
        u32 segmentCount = curve.IsLoop() ? ringCount : (ringCount - 1);
        meshData.Indices.reserve(segmentCount * (pointsPerRing - 1) * 6);

        for (u32 s = 0; s < segmentCount; ++s) {
            u32 r0 = s;
            u32 r1 = (s + 1) % ringCount;

            for (u32 k = 0; k < pointsPerRing - 1; ++k) {
                u32 v00 = r0 * pointsPerRing + k;
                u32 v01 = r0 * pointsPerRing + (k + 1);
                u32 v10 = r1 * pointsPerRing + k;
                u32 v11 = r1 * pointsPerRing + (k + 1);

                // CCW upward facing triangles
                meshData.Indices.push_back(v00);
                meshData.Indices.push_back(v01);
                meshData.Indices.push_back(v10);

                meshData.Indices.push_back(v01);
                meshData.Indices.push_back(v11);
                meshData.Indices.push_back(v10);
            }
        }

        return meshData;
    }

    std::shared_ptr<VertexArray> TrackGenerator::BuildVAO(const TrackMeshData& meshData) {
        if (meshData.IsEmpty()) {
            return nullptr;
        }

        std::vector<f32> vertexBufferData;
        vertexBufferData.reserve(meshData.Positions.size() * 5);

        for (size_t i = 0; i < meshData.Positions.size(); ++i) {
            vertexBufferData.push_back(meshData.Positions[i].x);
            vertexBufferData.push_back(meshData.Positions[i].y);
            vertexBufferData.push_back(meshData.Positions[i].z);
            vertexBufferData.push_back(meshData.UVs[i].x);
            vertexBufferData.push_back(meshData.UVs[i].y);
        }

        auto va = std::make_shared<VertexArray>();
        auto vb = std::make_shared<VertexBuffer>(
            vertexBufferData.data(),
            static_cast<u32>(vertexBufferData.size() * sizeof(f32))
        );
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        va->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(
            meshData.Indices.data(),
            static_cast<u32>(meshData.Indices.size())
        );
        va->SetIndexBuffer(ib);

        return va;
    }

    BodyID TrackGenerator::CreatePhysicsCollider(
        const TrackMeshData& meshData,
        const SplineComponent& config,
        PhysicsScene& scene,
        const glm::mat4& worldTransform
    ) {
        if (meshData.IsEmpty() || !config.GeneratePhysics) {
            return BodyID();
        }

        JPH::VertexList joltVertices;
        joltVertices.reserve(meshData.Positions.size());
        for (const auto& p : meshData.Positions) {
            glm::vec4 wp = worldTransform * glm::vec4(p, 1.0f);
            joltVertices.emplace_back(wp.x, wp.y, wp.z);
        }

        JPH::IndexedTriangleList joltTriangles;
        joltTriangles.reserve(meshData.Indices.size() / 3);
        for (size_t i = 0; i < meshData.Indices.size(); i += 3) {
            joltTriangles.emplace_back(meshData.Indices[i], meshData.Indices[i + 1], meshData.Indices[i + 2]);
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
            bodySettings.mFriction = config.Friction;
            bodySettings.mRestitution = config.Restitution;

            JPH::BodyID bodyID = scene.GetBodyInterface().CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);
            return bodyID;
        } else {
            AXLE_ERROR("TrackGenerator: Failed to create JPH::MeshShape: {0}", shapeResult.GetError().c_str());
            return BodyID();
        }
    }

    void TrackGenerator::DestroyPhysicsCollider(BodyID bodyID, PhysicsScene& scene) {
        if (!bodyID.IsInvalid() && scene.IsBodyValid(bodyID)) {
            scene.GetBodyInterface().RemoveBody(bodyID);
            scene.GetBodyInterface().DestroyBody(bodyID);
        }
    }

}
