#pragma once
#include <Axle/BuiltinMesh.hpp>
#include <memory>
#include <string>

namespace Axle {
    class VertexArray;
    struct MeshInstance3D;
    struct BuiltinMeshResources {
        std::shared_ptr<VertexArray> CubeMesh, SphereMesh, CylinderMesh, ConeMesh, PlaneMesh;
        std::shared_ptr<VertexArray> Get(BuiltinMeshId id) const;
    };
    std::string IdentifyBuiltinMesh(const std::shared_ptr<VertexArray>& mesh, const BuiltinMeshResources& resources);
    bool ResolveMeshResources(MeshInstance3D& mesh, const BuiltinMeshResources* resources = nullptr);
}
