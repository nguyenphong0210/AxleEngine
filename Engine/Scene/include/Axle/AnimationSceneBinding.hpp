#pragma once
#include <string>

namespace Axle {
    struct SkeletalMeshComponent;
    struct AnimatorComponent;
    // Fallbacks are hex encoded because scene/assembly payloads occupy one line.
    bool CaptureAnimationBinding(const SkeletalMeshComponent* mesh, const AnimatorComponent* animator,
                                 std::string& meshFallback, std::string& controllerFallback,
                                 std::string* diagnostic = nullptr);
    bool ResolveAnimationBinding(SkeletalMeshComponent* mesh, AnimatorComponent* animator,
                                 const std::string& ownerPath = "");
}
