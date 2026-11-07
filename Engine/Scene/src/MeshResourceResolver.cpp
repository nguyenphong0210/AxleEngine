#include <Axle/MeshResourceResolver.hpp>
#include <Axle/Components.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/SceneRenderBridge.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/RHI/RHIDevice.hpp>

namespace Axle {
    std::shared_ptr<VertexArray> BuiltinMeshResources::Get(BuiltinMeshId id) const {
        switch (id) {
            case BuiltinMeshId::Cube: return CubeMesh;
            case BuiltinMeshId::Sphere: return SphereMesh;
            case BuiltinMeshId::Cylinder: return CylinderMesh;
            case BuiltinMeshId::Cone: return ConeMesh;
            case BuiltinMeshId::Plane: return PlaneMesh;
            default: return nullptr;
        }
    }

    std::string IdentifyBuiltinMesh(const std::shared_ptr<VertexArray>& mesh, const BuiltinMeshResources& resources) {
        if (mesh) for (const auto& definition : BuiltinMeshes)
            if (resources.Get(definition.Id) == mesh) return std::string(definition.URI);
        return {};
    }

    bool ResolveMeshResources(MeshInstance3D& mesh, const BuiltinMeshResources* resources) {
        const auto builtin = ParseBuiltinMesh(mesh.AssetPath);
        if (builtin) mesh.AssetPath = GetBuiltinMeshDefinition(*builtin)->URI;
        auto& device = RHI::RHIDevice::Get();
        if (device.GetBackend() == RHI::RHIType::Direct3D12 || device.GetBackend() == RHI::RHIType::Vulkan) {
            if (!device.CanCreateResources()) return false;
            if ((!mesh.RHIMeshAsset || !mesh.RHIMeshAsset->IsValid()) && !mesh.AssetPath.empty())
                mesh.RHIMeshAsset = RHIMeshRegistry::Get().GetOrCreateMesh(mesh.AssetPath);
            return mesh.RHIMeshAsset && mesh.RHIMeshAsset->IsValid();
        }
        if (mesh.VAO) return true;
        if (builtin && resources) mesh.VAO = resources->Get(*builtin);
        if (mesh.VAO) return true;
        if (!PrimitiveMesh::HasContext()) return false;
        if (builtin) mesh.VAO = PrimitiveMesh::CreateBuiltin(*builtin);
        else if (!mesh.AssetPath.empty()) {
            const auto source = AssetSerializer::LoadMesh(FileSystem::ResolveAssetPath(mesh.AssetPath));
            if (source.Success) mesh.VAO = CreateVertexArrayFromMeshSource(source.SourceData);
        }
        return mesh.VAO != nullptr;
    }
}
