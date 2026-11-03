#include <Axle/FbxCurveBaker.hpp>
#include <ufbx.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include <string>

namespace Axle {

namespace {

    inline glm::vec3 ToVec3(const ufbx_vec3& v) {
        return { static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) };
    }

    inline glm::quat ToQuat(const ufbx_quat& q) {
        // ufbx_quat has x, y, z, w; glm::quat constructor takes (w, x, y, z)
        return { static_cast<float>(q.w), static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z) };
    }

    enum class TrackCurveMode {
        Static,
        Step,
        Continuous,
        Mixed
    };

    TrackCurveMode InspectCurves(const ufbx_anim* anim, const ufbx_node* node, const char* propName,
                                 double timeBegin, double timeEnd, std::vector<double>& outKeyTimes) {
        if (!anim || !node) return TrackCurveMode::Static;
        bool hasStep = false;
        bool hasContinuous = false;

        for (size_t l = 0; l < anim->layers.count; ++l) {
            const ufbx_anim_layer* layer = anim->layers[l];
            if (!layer) continue;
            const ufbx_anim_prop* prop = ufbx_find_anim_prop(layer, &node->element, propName);
            if (!prop || !prop->anim_value) continue;

            for (int c = 0; c < 3; ++c) {
                const ufbx_anim_curve* curve = prop->anim_value->curves[c];
                if (!curve) continue;
                for (size_t k = 0; k < curve->keyframes.count; ++k) {
                    const auto& kf = curve->keyframes[k];
                    if (kf.time >= timeBegin && kf.time <= timeEnd) {
                        outKeyTimes.push_back(kf.time);
                    }
                }
                // Only spans [k, k+1] inside the curve contribute to interpolation.
                // The terminal key (k == count - 1) has no outgoing interval inside the curve.
                if (curve->keyframes.count >= 2) {
                    for (size_t k = 0; k < curve->keyframes.count - 1; ++k) {
                        const auto& kfA = curve->keyframes[k];
                        const auto& kfB = curve->keyframes[k + 1];
                        if (kfB.time <= timeBegin || kfA.time >= timeEnd) {
                            continue;
                        }
                        if (kfA.interpolation == UFBX_INTERPOLATION_CONSTANT_PREV ||
                            kfA.interpolation == UFBX_INTERPOLATION_CONSTANT_NEXT) {
                            hasStep = true;
                        } else {
                            hasContinuous = true;
                        }
                    }
                }
            }
        }

        if (hasStep && hasContinuous) return TrackCurveMode::Mixed;
        if (hasStep && !hasContinuous) return TrackCurveMode::Step;
        if (!hasStep && hasContinuous) return TrackCurveMode::Continuous;
        return TrackCurveMode::Static;
    }

        double ComputeMaxAngularRateDegPerSec(const ufbx_anim* anim, const ufbx_node* node,
                                         double timeBegin, double timeEnd) {
        if (!anim || !node) return 0.0;
        double maxRate = 0.0;
        for (size_t l = 0; l < anim->layers.count; ++l) {
            const ufbx_anim_layer* layer = anim->layers[l];
            if (!layer) continue;
            const ufbx_anim_prop* prop = ufbx_find_anim_prop(layer, &node->element, UFBX_Lcl_Rotation);
            if (!prop || !prop->anim_value) continue;

            double layerRate = 0.0;
            for (int c = 0; c < 3; ++c) {
                const ufbx_anim_curve* curve = prop->anim_value->curves[c];
                if (!curve) continue;

                double channelMaxRate = 0.0;
                if (curve->keyframes.count >= 2) {
                    for (size_t k = 0; k < curve->keyframes.count - 1; ++k) {
                        const auto& kfA = curve->keyframes[k];
                        const auto& kfB = curve->keyframes[k + 1];
                        if (kfB.time <= timeBegin || kfA.time >= timeEnd) continue;
                        double dt = kfB.time - kfA.time;
                        if (dt <= 1e-9) continue;

                        double secantRate = std::abs(kfB.value - kfA.value) / dt;
                        double segMaxRate = secantRate;

                        if (kfA.interpolation == UFBX_INTERPOLATION_CUBIC) {
                            double rightSlope = (kfA.right.dx > 1e-9f) ? (std::abs(kfA.right.dy) / kfA.right.dx) : secantRate;
                            double leftSlope = (kfB.left.dx > 1e-9f) ? (std::abs(kfB.left.dy) / kfB.left.dx) : secantRate;
                            segMaxRate = std::max({ secantRate, rightSlope, leftSlope, 3.0 * secantRate });
                        }
                        channelMaxRate = std::max(channelMaxRate, segMaxRate);
                    }
                }
                layerRate += channelMaxRate;
            }
            maxRate = std::max(maxRate, layerRate);
        }
        return maxRate;
    }

    struct TransformSample {
        double LocalTime = 0.0;
        glm::vec3 Translation{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f };
    };

} // namespace

float FbxCurveBaker::QuaternionAngularDistance(const glm::quat& q1, const glm::quat& q2) {
    float len1 = glm::length(q1);
    float len2 = glm::length(q2);
    if (len1 < 1e-6f || len2 < 1e-6f) return 3.14159265f;

    glm::quat n1 = q1 / len1;
    glm::quat n2 = q2 / len2;

    // Both q and -q represent the exact same 3D orientation.
    // Taking the absolute value of the dot product correctly measures
    // the geodesic distance on the 3-sphere modulo sign inversion.
    float dotVal = std::abs(glm::dot(n1, n2));
    if (dotVal >= 1.0f) return 0.0f;
    return 2.0f * std::acos(dotVal);
}

bool FbxCurveBaker::BakeChannel(
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
) {
    if (outError) outError->clear();
    if (!anim || !node) {
        if (outError) *outError = "Null FBX animation or node pointer";
        return false;
    }

    if (tolerances.TranslationTolerance <= 0.0f ||
        tolerances.RotationTolerance <= 0.0f ||
        tolerances.ScaleTolerance <= 0.0f) {
        if (outError) *outError = "Invalid non-positive bake tolerance";
        return false;
    }
    if (tolerances.MinIntervalSeconds <= 0.0f) {
        if (outError) *outError = "Invalid non-positive MinIntervalSeconds";
        return false;
    }

    const double duration = timeEnd - timeBegin;
    if (!std::isfinite(duration) || duration <= 0.0 || duration > 3600.0) {
        if (outError) *outError = "Invalid channel duration: " + std::to_string(duration);
        return false;
    }

    // Transactional local candidate: work on candidateChannel and local state.
    // outChannel, remainingKeyBudget, and outStats are only committed on full success.
    AnimationChannel candidateChannel;
    candidateChannel.BoneName = boneName;
    candidateChannel.BoneIndex = boneIndex;
    size_t localBudget = remainingKeyBudget;
    FbxBakeStats localStats = outStats ? *outStats : FbxBakeStats{};

    // Inspect curves for Translation, Rotation, and Scaling
    std::vector<double> transTimes, rotTimes, scaleTimes;
    TrackCurveMode transMode = InspectCurves(anim, node, UFBX_Lcl_Translation, timeBegin, timeEnd, transTimes);
    TrackCurveMode rotMode = InspectCurves(anim, node, UFBX_Lcl_Rotation, timeBegin, timeEnd, rotTimes);
    TrackCurveMode scaleMode = InspectCurves(anim, node, UFBX_Lcl_Scaling, timeBegin, timeEnd, scaleTimes);

    // Reject mixed interpolation within any single transform component
    if (transMode == TrackCurveMode::Mixed || rotMode == TrackCurveMode::Mixed || scaleMode == TrackCurveMode::Mixed) {
        if (outError) {
            *outError = "FBX node '" + boneName + "' contains unsupported mixed step and continuous interpolation";
        }
        return false;
    }

    candidateChannel.PositionsInterpolation = (transMode == TrackCurveMode::Step) ? AnimationInterpolation::Step : AnimationInterpolation::Linear;
    candidateChannel.RotationsInterpolation = (rotMode == TrackCurveMode::Step) ? AnimationInterpolation::Step : AnimationInterpolation::Linear;
    candidateChannel.ScalesInterpolation = (scaleMode == TrackCurveMode::Step) ? AnimationInterpolation::Step : AnimationInterpolation::Linear;

    auto evalTransform = [&](double localTime) -> TransformSample {
        ufbx_transform xf = ufbx_evaluate_transform(anim, node, timeBegin + localTime);
        return { localTime, ToVec3(xf.translation), ToQuat(xf.rotation), ToVec3(xf.scale) };
    };

    // Case 1: Pure step track across all active components
    const bool allStep = (transMode == TrackCurveMode::Step || transMode == TrackCurveMode::Static) &&
                         (rotMode == TrackCurveMode::Step || rotMode == TrackCurveMode::Static) &&
                         (scaleMode == TrackCurveMode::Step || scaleMode == TrackCurveMode::Static) &&
                         (transMode == TrackCurveMode::Step || rotMode == TrackCurveMode::Step || scaleMode == TrackCurveMode::Step);

    if (allStep) {
        std::vector<double> stepTimes;
        stepTimes.push_back(0.0);
        auto collectStep = [&](const std::vector<double>& src) {
            for (double t : src) {
                if (t >= timeBegin && t <= timeEnd) {
                    stepTimes.push_back(t - timeBegin);
                }
            }
        };
        collectStep(transTimes);
        collectStep(rotTimes);
        collectStep(scaleTimes);

        std::sort(stepTimes.begin(), stepTimes.end());
        std::vector<double> uniqueStepTimes;
        for (double st : stepTimes) {
            float tf = static_cast<float>(st);
            if (uniqueStepTimes.empty() || tf > static_cast<float>(uniqueStepTimes.back())) {
                uniqueStepTimes.push_back(st);
            }
        }
        if (uniqueStepTimes.empty()) {
            uniqueStepTimes.push_back(0.0);
        }
        if (static_cast<float>(duration) > static_cast<float>(uniqueStepTimes.back())) {
            uniqueStepTimes.push_back(duration);
        }

        if (uniqueStepTimes.size() == 1) {
            if (localBudget < 3) {
                if (outError) *outError = "FBX sampled key budget exceeded";
                return false;
            }
            auto sample = evalTransform(0.0);
            candidateChannel.Positions.push_back({ 0.0f, sample.Translation });
            candidateChannel.Rotations.push_back({ 0.0f, sample.Rotation });
            candidateChannel.Scales.push_back({ 0.0f, sample.Scale });
            localBudget -= 3;
            localStats.PositionKeys++;
            localStats.RotationKeys++;
            localStats.ScaleKeys++;

            outChannel = std::move(candidateChannel);
            remainingKeyBudget = localBudget;
            if (outStats) *outStats = localStats;
            return true;
        }

        for (size_t i = 0; i < uniqueStepTimes.size() - 1; ++i) {
            if (localBudget < 3) {
                if (outError) *outError = "FBX sampled key budget exceeded";
                return false;
            }
            double stA = uniqueStepTimes[i];
            double stB = uniqueStepTimes[i + 1];
            double tEval = 0.5 * (stA + stB);
            float timeStamp = static_cast<float>(stA);
            auto sample = evalTransform(tEval);
            candidateChannel.Positions.push_back({ timeStamp, sample.Translation });
            candidateChannel.Rotations.push_back({ timeStamp, sample.Rotation });
            candidateChannel.Scales.push_back({ timeStamp, sample.Scale });
            localBudget -= 3;
            localStats.PositionKeys++;
            localStats.RotationKeys++;
            localStats.ScaleKeys++;
        }

        // Final key at duration boundary
        if (localBudget < 3) {
            if (outError) *outError = "FBX sampled key budget exceeded";
            return false;
        }
        double stEnd = uniqueStepTimes.back();
        float timeStamp = static_cast<float>(stEnd);
        auto sampleEnd = evalTransform(stEnd);
        candidateChannel.Positions.push_back({ timeStamp, sampleEnd.Translation });
        candidateChannel.Rotations.push_back({ timeStamp, sampleEnd.Rotation });
        candidateChannel.Scales.push_back({ timeStamp, sampleEnd.Scale });
        localBudget -= 3;
        localStats.PositionKeys++;
        localStats.RotationKeys++;
        localStats.ScaleKeys++;

        outChannel = std::move(candidateChannel);
        remainingKeyBudget = localBudget;
        if (outStats) *outStats = localStats;
        return true;
    }

    // Case 2: Continuous track (or static)
    // Inspect source angular variation rate to bound initial seed intervals and avoid Nyquist aliasing.
    double maxAngularRate = ComputeMaxAngularRateDegPerSec(anim, node, timeBegin, timeEnd);
    double gridInterval = 1.0 / 30.0;
    if (maxAngularRate > 0.0) {
        const double maxArcDegrees = 60.0; // Keep SLERP arcs <= 60 deg per interval
        double dtRot = maxArcDegrees / maxAngularRate;
        if (dtRot < tolerances.MinIntervalSeconds) {
            if (outError) {
                *outError = "FBX rotational variation rate on node '" + boneName + "' (" +
                            std::to_string(maxAngularRate) + " deg/s) requires seed interval below MinIntervalSeconds (" +
                            std::to_string(dtRot) + "s < " + std::to_string(tolerances.MinIntervalSeconds) + "s)";
            }
            return false;
        }
        gridInterval = std::min(gridInterval, dtRot);
    }

    // Gather seed timestamps: start, end, source key times, and initial uniform grid bounded by Nyquist rate
    std::vector<double> seeds;
    seeds.push_back(0.0);
    seeds.push_back(duration);

    auto addKeys = [&](const std::vector<double>& times) {
        for (double t : times) {
            if (t >= timeBegin && t <= timeEnd) {
                seeds.push_back(t - timeBegin);
            }
        }
    };
    addKeys(transTimes);
    addKeys(rotTimes);
    addKeys(scaleTimes);

    const size_t gridSteps = static_cast<size_t>(std::ceil(duration / gridInterval));
    if (gridSteps * 3 > localBudget) {
        if (outError) *outError = "FBX sampled key budget exceeded by initial seed grid";
        return false;
    }
    for (size_t g = 0; g <= gridSteps; ++g) {
        double gt = std::min(duration, static_cast<double>(g) * gridInterval);
        seeds.push_back(gt);
    }

    std::sort(seeds.begin(), seeds.end());
    std::vector<double> uniqueSeeds;
    for (double s : seeds) {
        if (uniqueSeeds.empty() || (s - uniqueSeeds.back()) >= tolerances.MinIntervalSeconds) {
            uniqueSeeds.push_back(s);
        } else if (!uniqueSeeds.empty() && std::abs(s - duration) < 1e-6) {
            uniqueSeeds.back() = duration;
        }
    }
    if (uniqueSeeds.back() < duration) {
        if ((duration - uniqueSeeds.back()) >= tolerances.MinIntervalSeconds) {
            uniqueSeeds.push_back(duration);
        } else {
            uniqueSeeds.back() = duration;
        }
    }

    auto emitSample = [&](const TransformSample& s) -> bool {
        float timeStamp = static_cast<float>(s.LocalTime);
        if (localBudget < 3) {
            if (outError) *outError = "FBX sampled key budget exceeded";
            return false;
        }
        if (!candidateChannel.Positions.empty() && timeStamp <= candidateChannel.Positions.back().TimeStamp) {
            return true; // Skip redundant / non-increasing time stamp
        }
        candidateChannel.Positions.push_back({ timeStamp, s.Translation });
        candidateChannel.Rotations.push_back({ timeStamp, s.Rotation });
        candidateChannel.Scales.push_back({ timeStamp, s.Scale });
        localBudget -= 3;
        localStats.PositionKeys++;
        localStats.RotationKeys++;
        localStats.ScaleKeys++;
        return true;
    };

    // Recursive adaptive subdivision
    std::function<bool(const TransformSample&, const TransformSample&, uint32_t)> subdivide;
    subdivide = [&](const TransformSample& sA, const TransformSample& sB, uint32_t depth) -> bool {
        double interval = sB.LocalTime - sA.LocalTime;
        if (interval <= 0.0) return true;

        const uint32_t numProbes = std::max(1u, tolerances.InteriorValidationProbes);
        float maxErrPos = 0.0f;
        float maxErrRot = 0.0f;
        float maxErrScale = 0.0f;

        for (uint32_t p = 1; p <= numProbes; ++p) {
            double u = static_cast<double>(p) / static_cast<double>(numProbes + 1);
            double tProbe = sA.LocalTime + u * interval;
            auto trueSample = evalTransform(tProbe);

            float uf = static_cast<float>(u);
            glm::vec3 approxPos = glm::mix(sA.Translation, sB.Translation, uf);
            glm::vec3 approxScale = glm::mix(sA.Scale, sB.Scale, uf);

            glm::quat qA = glm::normalize(sA.Rotation);
            glm::quat qB = glm::normalize(sB.Rotation);
            if (glm::dot(qA, qB) < 0.0f) qB = -qB;
            glm::quat approxRot = glm::slerp(qA, qB, uf);

            float errPos = glm::length(trueSample.Translation - approxPos);
            float errRot = QuaternionAngularDistance(trueSample.Rotation, approxRot);
            float errScale = glm::length(trueSample.Scale - approxScale);

            maxErrPos = std::max(maxErrPos, errPos);
            maxErrRot = std::max(maxErrRot, errRot);
            maxErrScale = std::max(maxErrScale, errScale);
        }

        localStats.MaxPositionError = std::max(localStats.MaxPositionError, maxErrPos);
        localStats.MaxRotationError = std::max(localStats.MaxRotationError, maxErrRot);
        localStats.MaxScaleError = std::max(localStats.MaxScaleError, maxErrScale);

        const bool meetsTolerance = (maxErrPos <= tolerances.TranslationTolerance) &&
                                    (maxErrRot <= tolerances.RotationTolerance) &&
                                    (maxErrScale <= tolerances.ScaleTolerance);

        if (meetsTolerance) {
            return true;
        }

        // Error exceeds tolerance: check recursion and interval limits
        if (depth >= tolerances.MaxRecursionDepth || interval <= tolerances.MinIntervalSeconds) {
            if (outError) {
                *outError = "FBX curve error on node '" + boneName + "' exceeds tolerance (" +
                            std::to_string(maxErrPos) + "m pos, " + std::to_string(maxErrRot) + "rad rot, " +
                            std::to_string(maxErrScale) + " scl) and cannot be refined within limit (depth=" +
                            std::to_string(depth) + ", interval=" + std::to_string(interval) + "s)";
            }
            return false;
        }

        if (localBudget < 3) {
            if (outError) *outError = "FBX sampled key budget exceeded";
            return false;
        }

        localStats.SubdivisionsPerformed++;

        double tMid = 0.5 * (sA.LocalTime + sB.LocalTime);
        auto sMid = evalTransform(tMid);

        if (!subdivide(sA, sMid, depth + 1)) return false;
        if (!emitSample(sMid)) return false;
        if (!subdivide(sMid, sB, depth + 1)) return false;

        return true;
    };

    // Emit initial sample
    auto s0 = evalTransform(uniqueSeeds[0]);
    if (!emitSample(s0)) return false;

    // Subdivide and emit each seed interval
    for (size_t i = 0; i < uniqueSeeds.size() - 1; ++i) {
        auto sA = evalTransform(uniqueSeeds[i]);
        auto sB = evalTransform(uniqueSeeds[i + 1]);
        if (!subdivide(sA, sB, 0)) return false;
        if (!emitSample(sB)) return false;
    }

    outChannel = std::move(candidateChannel);
    remainingKeyBudget = localBudget;
    if (outStats) *outStats = localStats;
    return true;
}

std::shared_ptr<AnimationClip> FbxCurveBaker::BakeAnimStack(
    const ufbx_anim_stack* stack,
    const std::vector<const ufbx_node*>& nodes,
    const Skeleton& rig,
    const FbxBakeTolerances& tolerances,
    FbxBakeStats* outStats,
    std::string* outError
) {
    if (outError) outError->clear();
    if (!stack || !stack->anim) {
        if (outError) *outError = "Null FBX animation stack or anim";
        return nullptr;
    }

    const double duration = stack->time_end - stack->time_begin;
    if (!std::isfinite(duration) || duration <= 0.0 || duration > 3600.0) {
        if (outError) *outError = "Invalid or excessive FBX animation duration";
        return nullptr;
    }

    size_t remainingBudget = tolerances.MaxTotalKeysPerStack;
    auto clip = std::make_shared<AnimationClip>(
        std::string(stack->name.data, stack->name.length),
        static_cast<float>(duration),
        30.0f
    );

    std::vector<AnimationChannel> channels;
    channels.reserve(nodes.size());

    for (size_t i = 0; i < nodes.size(); ++i) {
        const ufbx_node* node = nodes[i];
        std::string boneName = rig.GetBones()[i].Name;
        int boneIndex = static_cast<int>(i);

        AnimationChannel channel;
        if (!BakeChannel(stack->anim, node, stack->time_begin, stack->time_end,
                         boneName, boneIndex, channel, tolerances,
                         remainingBudget, outStats, outError)) {
            return nullptr;
        }
        channels.push_back(std::move(channel));
    }

    std::vector<std::string> clipErrors;
    if (!clip->ReplaceChannels(std::move(channels), &rig, &clipErrors)) {
        if (outError) {
            *outError = clipErrors.empty() ? "Invalid FBX animation channels after baking" : clipErrors.front();
        }
        return nullptr;
    }

    return clip;
}

} // namespace Axle
