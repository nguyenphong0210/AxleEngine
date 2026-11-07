#include <Axle/MeshResourceResolver.hpp>
#include <Axle/Components.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/SceneRenderBridge.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/RHI/RHIDevice.hpp>

namespace Axle {
    uint64_t MeshSourceRevision(const AxleMeshSource& source) {
        uint64_t revision = 14695981039346656037ull;
        auto append = [&](const void* bytes, size_t count) {
            auto data = static_cast<const uint8_t*>(bytes);
            for (size_t i=0;i<count;++i) { revision ^= data[i]; revision *= 1099511628211ull; }
        };
        append(source.Vertices.data(),source.Vertices.size()*sizeof(glm::vec3));
        append(source.Normals.data(),source.Normals.size()*sizeof(glm::vec3));
        append(source.UVs.data(),source.UVs.size()*sizeof(glm::vec2));
        append(source.Indices.data(),source.Indices.size()*sizeof(uint32_t));
        append(&source.ColorPrimary,sizeof(source.ColorPrimary));
        append(&source.ColorSecondary,sizeof(source.ColorSecondary));
        return revision;
    }
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
            if ((!mesh.RHIMeshAsset || !mesh.RHIMeshAsset->IsValid()) && mesh.Source) {
                const auto& source = *mesh.Source;
                mesh.RHIMeshAsset = source.Normals.size()==source.Vertices.size() && source.UVs.size()==source.Vertices.size()
                    ? RHI::RHIMesh::CreateFromRaw(source.Vertices,source.Indices,source.Normals,source.UVs,source.ColorPrimary,source.Name)
                    : RHI::RHIMesh::CreateFromAxleMesh(source.Vertices,source.Indices,source.ColorPrimary,source.Name);
                return mesh.RHIMeshAsset && mesh.RHIMeshAsset->IsValid();
            }
            if ((!mesh.RHIMeshAsset || !mesh.RHIMeshAsset->IsValid()) && !mesh.AssetPath.empty())
                mesh.RHIMeshAsset = RHIMeshRegistry::Get().GetOrCreateMesh(mesh.AssetPath);
            return mesh.RHIMeshAsset && mesh.RHIMeshAsset->IsValid();
        }
        if (mesh.VAO) return true;
        if (builtin && resources) mesh.VAO = resources->Get(*builtin);
        if (mesh.VAO) return true;
        if (!PrimitiveMesh::HasContext()) return false;
        if (mesh.Source) mesh.VAO = CreateVertexArrayFromMeshSource(*mesh.Source);
        else if (builtin) mesh.VAO = PrimitiveMesh::CreateBuiltin(*builtin);
        else if (!mesh.AssetPath.empty()) {
            const auto source = AssetSerializer::LoadMesh(FileSystem::ResolveAssetPath(mesh.AssetPath));
            if (source.Success) mesh.VAO = CreateVertexArrayFromMeshSource(source.SourceData);
        }
        return mesh.VAO != nullptr;
    }
}
