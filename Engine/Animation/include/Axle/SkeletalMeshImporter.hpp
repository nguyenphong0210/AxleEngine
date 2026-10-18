#pragma once
#include <Axle/SkeletalMesh.hpp>
#include <Axle/AnimationClip.hpp>
#include <filesystem>
#include <memory>
#include <vector>

namespace Axle {

    class SkeletalMeshImporter {
    public:
        // Automatically dispatches based on file extension (.fbx, .gltf, .glb)
        static bool Load(const std::filesystem::path& path,
                         SkeletalMeshData& outMesh,
                         std::shared_ptr<Skeleton>& outSkeleton,
                         std::vector<std::shared_ptr<AnimationClip>>& outClips);

        // Native FBX loading via embedded ufbx
        static bool LoadFBX(const std::filesystem::path& path,
                            SkeletalMeshData& outMesh,
                            std::shared_ptr<Skeleton>& outSkeleton,
                            std::vector<std::shared_ptr<AnimationClip>>& outClips);

        // Native glTF 2.0 / GLB loading via embedded cgltf
        static bool LoadGLTF(const std::filesystem::path& path,
                             SkeletalMeshData& outMesh,
                             std::shared_ptr<Skeleton>& outSkeleton,
                             std::vector<std::shared_ptr<AnimationClip>>& outClips);

        // Procedural animated humanoid biped rig generator (for automated tests, CI, and fallback)
        static bool CreateProceduralBiped(SkeletalMeshData& outMesh,
                                          std::shared_ptr<Skeleton>& outSkeleton,
                                          std::vector<std::shared_ptr<AnimationClip>>& outClips);

        // Generate complete humanoid animation suite (Idle, Walk, Run, Jump, Fall, Land)
        static void GenerateHumanoidClips(const Skeleton& skeleton,
                                          std::vector<std::shared_ptr<AnimationClip>>& outClips);
    };

}
