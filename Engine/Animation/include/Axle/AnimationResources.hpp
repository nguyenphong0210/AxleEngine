#pragma once
#include <Axle/AnimationDefinition.hpp>
#include <Axle/Animator.hpp>
#include <Axle/SkeletalMesh.hpp>

namespace Axle {
    // Reads through FileSystem so cooked resources also work in mounted packages.
    std::string ResolveAnimationReference(const std::string& reference, const std::string& ownerPath = "");
    std::shared_ptr<Skeleton> BuildAnimationSkeleton(const SkeletonDefinition& definition, std::string* diagnostic = nullptr);
    std::string EncodeAnimationRigSnapshot(const SkeletalMeshData& geometry, std::string* diagnostic = nullptr);
    std::shared_ptr<SkeletalMesh> DecodeAnimationRigSnapshot(const std::string& text, std::string* diagnostic = nullptr);
    std::shared_ptr<SkeletalMesh> LoadAnimationMesh(const std::string& reference, const std::string& fallback = "",
                                                 const std::string& ownerPath = "", std::string* diagnostic = nullptr);
    std::shared_ptr<Animator> LoadAnimationAnimator(const std::string& reference, const std::string& fallback,
                                                 bool unique, std::shared_ptr<Skeleton> rig,
                                                 const std::string& ownerPath = "", std::string* diagnostic = nullptr);
}
