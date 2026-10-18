#pragma once
#include <AxleSpline/Curve3D.hpp>
#include <AxleSpline/SplineComponent.hpp>
#include <Axle/Types.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/PhysicsScene.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace Axle::Spline {

    struct TrackMeshData {
        std::vector<glm::vec3> Positions;
        std::vector<glm::vec2> UVs;
        std::vector<u32> Indices;

        size_t GetVertexCount() const { return Positions.size(); }
        size_t GetTriangleCount() const { return Indices.size() / 3; }
        bool IsEmpty() const { return Positions.empty() || Indices.empty(); }
    };

    class TrackGenerator {
    public:
        // Generate CPU vertex and index geometry along spline curve
        static TrackMeshData GenerateTrackMesh(const Curve3D& curve, const SplineComponent& config);

        // Upload geometry to GPU VertexArray (VAO with Float3 Position and Float2 TexCoord)
        static std::shared_ptr<VertexArray> BuildVAO(const TrackMeshData& meshData);

        // Build Jolt Physics Static MeshShape body for collision
        static BodyID CreatePhysicsCollider(
            const TrackMeshData& meshData,
            const SplineComponent& config,
            PhysicsScene& scene,
            const glm::mat4& worldTransform = glm::mat4(1.0f)
        );

        // Safely destroy runtime physics body
        static void DestroyPhysicsCollider(BodyID bodyID, PhysicsScene& scene);
    };

}
