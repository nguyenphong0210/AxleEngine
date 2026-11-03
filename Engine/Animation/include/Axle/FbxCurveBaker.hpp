#pragma once

#include <Axle/AnimationClip.hpp>
#include <Axle/Skeleton.hpp>
#include <ufbx.h>
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Axle {

    /// Tolerances and subdivision limits for baking FBX animation curves.
    ///
    /// Continuous accuracy guarantee:
    /// Initial seed interval is constrained by source Euler rotation rates (<= 60 deg rotation
    /// per seed interval) to eliminate rotational Nyquist aliasing. Interior validation probes
    /// (InteriorValidationProbes) verify continuous transform agreement within the declared
    /// tolerances. If a curve exceeds tolerance and cannot be refined (due to MaxRecursionDepth
    /// or MinIntervalSeconds), baking fails with an explicit diagnostic.
    struct FbxBakeTolerances {
        float TranslationTolerance = 1e-3f;       // In meters (1 mm)
        float RotationTolerance = 1e-3f;          // In radians (~0.057 degrees)
        float ScaleTolerance = 1e-3f;             // Relative scale factor
        uint32_t MaxRecursionDepth = 12;          // Max refinement levels (up to 4096 subdivisions)
        float MinIntervalSeconds = 1e-4f;         // 0.1 ms minimum subdivision interval
        size_t MaxTotalKeysPerStack = 1000000;    // Work budget cap per animation stack
        uint32_t InteriorValidationProbes = 5;    // Number of off-grid validation checks per segment
    };

    struct FbxBakeStats {
        size_t PositionKeys = 0;
        size_t RotationKeys = 0;
        size_t ScaleKeys = 0;
        size_t SubdivisionsPerformed = 0;
        float MaxPositionError = 0.0f;
        float MaxRotationError = 0.0f;
        float MaxScaleError = 0.0f;
    };

    class FbxCurveBaker {
    public:
        // Measure angular distance between two quaternions accounting for antipodal sign equivalence (q == -q)
        static float QuaternionAngularDistance(const glm::quat& q1, const glm::quat& q2);

        // Bake an entire ufbx animation stack for a list of nodes into an AnimationClip
        static std::shared_ptr<AnimationClip> BakeAnimStack(
            const ufbx_anim_stack* stack,
            const std::vector<const ufbx_node*>& nodes,
            const Skeleton& rig,
            const FbxBakeTolerances& tolerances = {},
            FbxBakeStats* outStats = nullptr,
            std::string* outError = nullptr
        );

        /// Bake a single channel for a node over [timeBegin, timeEnd] relative to stack start.
        /// Transactional: on failure (e.g. mixed interpolation, budget exhaustion, or tolerance
        /// violation), outChannel, remainingKeyBudget, and outStats remain completely untouched.
        static bool BakeChannel(
            const ufbx_anim* anim,
            const ufbx_node* node,
            double timeBegin,
            double timeEnd,
            const std::string& boneName,
            int boneIndex,
            AnimationChannel& outChannel,
            const FbxBakeTolerances& tolerances,
            size_t& remainingKeyBudget,
            FbxBakeStats* outStats,
            std::string* outError
        );
    };

}
