#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string_view>

namespace Axle {
    namespace BuiltinAssets {
        inline constexpr uint64_t CubeUUID = 0xBA5E0001ULL;
        inline constexpr uint64_t SphereUUID = 0xBA5E0002ULL;
        inline constexpr uint64_t CylinderUUID = 0xBA5E0003ULL;
        inline constexpr uint64_t ConeUUID = 0xBA5E0004ULL;
        inline constexpr uint64_t PlaneUUID = 0xBA5E0005ULL;
    }

    enum class BuiltinMeshId : uint8_t { Cube, Sphere, Cylinder, Cone, Plane, Count };
    struct BuiltinMeshDefinition {
        BuiltinMeshId Id;
        uint64_t AssetUUID;
        std::string_view Name;
        std::string_view URI;
        float Radius;
        float Height;
        float Size;
        uint32_t Stacks;
        uint32_t Segments;
    };
    // Fixed unit geometry follows the existing GL authoring contract. Collision
    // defaults and authored transforms are independent and remain unchanged.
    inline constexpr std::array<BuiltinMeshDefinition, 5> BuiltinMeshes{{
        {BuiltinMeshId::Cube, BuiltinAssets::CubeUUID, "cube", "builtin://cube", 0, 0, 1, 0, 0},
        // High-fidelity baseline: M14's specular highlights expose coarse
        // latitude/longitude bands at the editor's normal inspection distance.
        {BuiltinMeshId::Sphere, BuiltinAssets::SphereUUID, "sphere", "builtin://sphere", 1, 0, 0, 64, 128},
        {BuiltinMeshId::Cylinder, BuiltinAssets::CylinderUUID, "cylinder", "builtin://cylinder", 1, 2, 0, 0, 32},
        {BuiltinMeshId::Cone, BuiltinAssets::ConeUUID, "cone", "builtin://cone", 1, 2, 0, 0, 32},
        {BuiltinMeshId::Plane, BuiltinAssets::PlaneUUID, "plane", "builtin://plane", 0, 0, 1, 1, 1}
    }};
    inline const BuiltinMeshDefinition* GetBuiltinMeshDefinition(BuiltinMeshId id) {
        const auto index = static_cast<std::size_t>(id);
        return index < BuiltinMeshes.size() ? &BuiltinMeshes[index] : nullptr;
    }
    inline std::optional<BuiltinMeshId> ParseBuiltinMesh(std::string_view path) {
        // filesystem::path can normalize a URI's repeated separators.
        if (!path.starts_with("builtin:")) return {};
        path.remove_prefix(8);
        while (!path.empty() && (path.front() == '/' || path.front() == '\\')) path.remove_prefix(1);
        for (const auto& definition : BuiltinMeshes) if (path == definition.Name) return definition.Id;
        return {};
    }
}
