#include <Axle/FbxCurveBaker.hpp>
#include <ufbx.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include <string>
#include <type_traits>
#include <map>
#include <limits>

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
        StepPrevious,
        StepNext,
        Continuous,
        Mixed,
        Invalid
    };

    struct BakeWorkBudget {
        size_t Remaining;
        bool Failed = false;
        bool Consume(size_t amount,std::string* error) {
            if (Failed) return false;
            if (amount>Remaining) {
                Failed=true;
                if (error) *error="FBX source evaluation/work budget exceeded";
                return false;
            }
            Remaining-=amount; return true;
        }
    };

    TrackCurveMode InspectCurves(const ufbx_anim* anim, const ufbx_node* node, const char* propName,
                                 double timeBegin, double timeEnd, std::vector<double>& outKeyTimes, BakeWorkBudget& work, std::string* error) {
        if (!anim || !node) return TrackCurveMode::Static;
        if (work.Failed) return TrackCurveMode::Invalid;
        bool hasStep = false;
        bool hasNext = false;
        bool hasContinuous = false;

        for (size_t l = 0; l < anim->layers.count; ++l) {
            if (!work.Consume(1,error)) return TrackCurveMode::Invalid;
            const ufbx_anim_layer* layer = anim->layers[l];
            if (!layer) continue;
            const ufbx_anim_prop* prop = ufbx_find_anim_prop(layer, &node->element, propName);
            if (!prop || !prop->anim_value) continue;

            for (int c = 0; c < 3; ++c) {
                const ufbx_anim_curve* curve = prop->anim_value->curves[c];
                if (!curve) continue;
                if (!work.Consume(curve->keyframes.count,error)) return TrackCurveMode::Invalid;
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
                        if (kfA.interpolation == UFBX_INTERPOLATION_CONSTANT_PREV) {
                            hasStep = true;
                        } else if (kfA.interpolation == UFBX_INTERPOLATION_CONSTANT_NEXT) {
                            hasNext = true;
                        } else {
                            hasContinuous = true;
                        }
                    }
                }
            }
        }

        if (int(hasStep) + int(hasNext) + int(hasContinuous) > 1) return TrackCurveMode::Mixed;
        if (hasNext) return TrackCurveMode::StepNext;
        if (hasStep && !hasContinuous) return TrackCurveMode::StepPrevious;
        if (!hasStep && hasContinuous) return TrackCurveMode::Continuous;
        return TrackCurveMode::Static;
    }

    double ComputeMaxAngularRateDegPerSec(const ufbx_anim* anim, const ufbx_node* node,
                                         double timeBegin, double timeEnd, BakeWorkBudget& work, std::string* error) {
        if (!anim || !node) return 0.0;
        double maxRate = 0.0;
        for (size_t l = 0; l < anim->layers.count; ++l) {
            if (!work.Consume(1,error)) return 0;
            const ufbx_anim_layer* layer = anim->layers[l];
            if (!layer) continue;
            const ufbx_anim_prop* prop = ufbx_find_anim_prop(layer, &node->element, UFBX_Lcl_Rotation);
            if (!prop || !prop->anim_value) continue;

            double layerRate = 0.0;
            for (int c = 0; c < 3; ++c) {
                const ufbx_anim_curve* curve = prop->anim_value->curves[c];
                if (!curve) continue;
                if (!work.Consume(curve->keyframes.count,error)) return 0;

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

bool FbxBakeTolerances::Validate(std::string* diagnostic) const {
    if (diagnostic) diagnostic->clear();
    auto fail=[&](const char* text) { if (diagnostic) *diagnostic=text; return false; };
    for (float value : {TranslationTolerance,RotationTolerance,ScaleTolerance,MinIntervalSeconds})
        if (!std::isfinite(value) || value<=0) return fail("FBX bake tolerances and intervals must be positive and finite");
    if (MinIntervalSeconds>3600) return fail("FBX minimum interval exceeds the supported clip duration");
    if (InteriorValidationProbes<1 || InteriorValidationProbes>64 || MaxRecursionDepth>24)
        return fail("FBX bake probe/depth limits exceed supported bounds");
    if (MaxTotalKeysPerStack<3 || MaxTotalKeysPerStack>3000000 || MaxWorkUnitsPerStack<1 || MaxWorkUnitsPerStack>8000000)
        return fail("FBX bake key/work limits exceed supported bounds");
    return true;
}

float FbxCurveBaker::QuaternionAngularDistance(const glm::quat& q1,const glm::quat& q2) {
    const glm::dquat first(q1),second(q2);
    const double a=glm::length(first), b=glm::length(second);
    if (!std::isfinite(a) || !std::isfinite(b) || a<1e-6 || b<1e-6) return std::numeric_limits<float>::infinity();
    const double dot=std::clamp(std::abs(glm::dot(first/a,second/b)),0.0,1.0);
    return float(2*std::acos(dot));
}

static bool BakeChannelImpl(
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
    std::string* outError,
    BakeWorkBudget& work
) {
    if (outError) outError->clear();
    if (!anim || !node) {
        if (outError) *outError = "Null FBX animation or node pointer";
        return false;
    }

    if (!tolerances.Validate(outError)) return false;
    if (!std::isfinite(timeBegin) || !std::isfinite(timeEnd) || boneName.empty() || boneIndex<0 || boneIndex>=128 ||
        remainingKeyBudget>tolerances.MaxTotalKeysPerStack) {
        if (outError) *outError="Invalid FBX channel binding, time range or key budget";
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

    const auto counterLimit=std::numeric_limits<size_t>::max();
    if (localStats.PositionKeys>counterLimit-localBudget || localStats.RotationKeys>counterLimit-localBudget ||
        localStats.ScaleKeys>counterLimit-localBudget || localStats.SubdivisionsPerformed>counterLimit-tolerances.MaxWorkUnitsPerStack) {
        if (outError) *outError="FBX statistics counters would overflow";
        return false;
    }

    // Inspect curves for Translation, Rotation, and Scaling
    std::vector<double> transTimes, rotTimes, scaleTimes;
    TrackCurveMode transMode = InspectCurves(anim, node, UFBX_Lcl_Translation, timeBegin, timeEnd, transTimes, work, outError);
    TrackCurveMode rotMode = InspectCurves(anim, node, UFBX_Lcl_Rotation, timeBegin, timeEnd, rotTimes, work, outError);
    TrackCurveMode scaleMode = InspectCurves(anim, node, UFBX_Lcl_Scaling, timeBegin, timeEnd, scaleTimes, work, outError);

    if (work.Failed) return false;

    // Reject mixed interpolation within any single transform component
    if (transMode == TrackCurveMode::Mixed || rotMode == TrackCurveMode::Mixed || scaleMode == TrackCurveMode::Mixed) {
        if (outError) {
            *outError = "FBX node '" + boneName + "' contains unsupported mixed step and continuous interpolation";
        }
        return false;
    }

    const auto interpolation = [](TrackCurveMode mode) {
        if (mode == TrackCurveMode::StepPrevious) return AnimationInterpolation::Step;
        if (mode == TrackCurveMode::StepNext) return AnimationInterpolation::StepNext;
        return AnimationInterpolation::Linear;
    };
    candidateChannel.PositionsInterpolation = interpolation(transMode);
    candidateChannel.RotationsInterpolation = interpolation(rotMode);
    candidateChannel.ScalesInterpolation = interpolation(scaleMode);

    auto evalTransform = [&](double localTime) -> TransformSample {
        if (!work.Consume(1,outError)) return {};
        const auto xf=ufbx_evaluate_transform(anim,node,timeBegin+localTime);
        TransformSample sample{localTime,ToVec3(xf.translation),ToQuat(xf.rotation),ToVec3(xf.scale)};
        if (!BoneTransform(sample.Translation,sample.Rotation,sample.Scale).IsValid()) {
            work.Failed=true;
            if (outError) *outError="FBX source evaluates to a nonfinite or singular transform";
        }
        return sample;
    };

    // Authored boundaries are mandatory. Distinct source times must remain distinct in runtime floats.
    std::vector<double> sourceTimes{0.0,duration};
    for (const auto* times : {&transTimes,&rotTimes,&scaleTimes})
        for (double time : *times) sourceTimes.push_back(time-timeBegin);
    std::sort(sourceTimes.begin(),sourceTimes.end());
    sourceTimes.erase(std::unique(sourceTimes.begin(),sourceTimes.end()),sourceTimes.end());
    for (size_t i=0;i<sourceTimes.size();++i) {
        const float time = float(sourceTimes[i]);
        if (!std::isfinite(time) || (i && time<=float(sourceTimes[i-1]))) {
            if (outError) *outError = "Distinct FBX source timestamps collapse at runtime float precision";
            return false;
        }
    }
    if (sourceTimes.size()>localBudget/3) {
        if (outError) *outError = "FBX sampled key budget exceeded by source boundaries";
        return false;
    }

    // Case 1: Pure step track across all active components
    const bool allStep = transMode != TrackCurveMode::Continuous &&
                         rotMode != TrackCurveMode::Continuous &&
                         scaleMode != TrackCurveMode::Continuous &&
                         (transMode != TrackCurveMode::Static || rotMode != TrackCurveMode::Static || scaleMode != TrackCurveMode::Static);

    if (allStep) {
        const auto& uniqueStepTimes = sourceTimes;

        for (size_t i = 0; i < uniqueStepTimes.size() - 1; ++i) {
            if (localBudget < 3) {
                if (outError) *outError = "FBX sampled key budget exceeded";
                return false;
            }
            double stA = uniqueStepTimes[i];
            // Preserve the value at the exact key; StepNext owns open-interval semantics.
            double tEval = stA;
            float timeStamp = static_cast<float>(stA);
            auto sample = evalTransform(tEval);
            if (work.Failed) return false;
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
        if (work.Failed) return false;
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
    double maxAngularRate = rotMode == TrackCurveMode::Continuous ? ComputeMaxAngularRateDegPerSec(anim, node, timeBegin, timeEnd, work, outError) : 0.0;
    if (work.Failed) return false;
    if (!std::isfinite(maxAngularRate)) {
        if (outError) *outError="FBX rotational variation exceeds the supported numeric range";
        return false;
    }
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

    // A grid may share a float with a mandatory source key; retain the source's exact evaluation time.
    std::map<float,double> seeds;
    for (double time : sourceTimes) seeds.emplace(float(time),time);

    const double requiredIntervals=std::ceil(duration/gridInterval);
    if (!std::isfinite(requiredIntervals) || requiredIntervals<1 || requiredIntervals>=double(localBudget/3)) {
        if (outError) *outError="FBX sampled key budget exceeded by initial seed grid";
        return false;
    }
    const size_t gridSteps=size_t(requiredIntervals);
    for (size_t g = 0; g <= gridSteps; ++g) {
        const double time = std::min(duration,double(g)*gridInterval);
        seeds.try_emplace(float(time),time);
    }
    if (seeds.size()>localBudget/3) {
        if (outError) *outError = "FBX sampled key budget exceeded by seed timestamps";
        return false;
    }
    std::vector<double> uniqueSeeds;
    uniqueSeeds.reserve(seeds.size());
    for (const auto& [time,sourceTime] : seeds) uniqueSeeds.push_back(sourceTime);

    auto emitSample = [&](const TransformSample& s) -> bool {
        float timeStamp = static_cast<float>(s.LocalTime);
        if (localBudget < 3) {
            if (outError) *outError = "FBX sampled key budget exceeded";
            return false;
        }
        if (!candidateChannel.Positions.empty() && timeStamp <= candidateChannel.Positions.back().TimeStamp) {
            if (outError) *outError = "FBX refined timestamps collapse at runtime float precision";
            return false;
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
            if (work.Failed) return false;

            float uf = static_cast<float>(u);
            const auto approximate = [uf](const auto& first, const auto& second, AnimationInterpolation mode) {
                if (mode == AnimationInterpolation::Step) return first;
                if (mode == AnimationInterpolation::StepNext) return second;
                using Value = std::decay_t<decltype(first)>;
                if constexpr (std::is_same_v<Value,glm::quat>) {
                    auto a = glm::normalize(first), b = glm::normalize(second);
                    if (glm::dot(a,b)<0) b = -b;
                    return glm::slerp(a,b,uf);
                } else return glm::mix(first,second,uf);
            };
            const auto approxPos = approximate(sA.Translation,sB.Translation,candidateChannel.PositionsInterpolation);
            const auto approxScale = approximate(sA.Scale,sB.Scale,candidateChannel.ScalesInterpolation);
            const auto approxRot = approximate(sA.Rotation,sB.Rotation,candidateChannel.RotationsInterpolation);

            float errPos = glm::length(trueSample.Translation - approxPos);
            float errRot = FbxCurveBaker::QuaternionAngularDistance(trueSample.Rotation, approxRot);
            float errScale = glm::length(trueSample.Scale - approxScale);

            maxErrPos = std::max(maxErrPos, errPos);
            maxErrRot = std::max(maxErrRot, errRot);
            maxErrScale = std::max(maxErrScale, errScale);
        }

        const bool meetsTolerance = (maxErrPos <= tolerances.TranslationTolerance) &&
                                    (maxErrRot <= tolerances.RotationTolerance) &&
                                    (maxErrScale <= tolerances.ScaleTolerance);

        if (meetsTolerance) {
            localStats.MaxPositionError = std::max(localStats.MaxPositionError, maxErrPos);
            localStats.MaxRotationError = std::max(localStats.MaxRotationError, maxErrRot);
            localStats.MaxScaleError = std::max(localStats.MaxScaleError, maxErrScale);

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
        if (!(float(tMid)>float(sA.LocalTime) && float(tMid)<float(sB.LocalTime))) {
            if (outError) *outError = "FBX refinement exceeds runtime timestamp precision";
            return false;
        }
        auto sMid = evalTransform(tMid);
        if (work.Failed) return false;

        if (!subdivide(sA, sMid, depth + 1)) return false;
        if (!emitSample(sMid)) return false;
        if (!subdivide(sMid, sB, depth + 1)) return false;

        return true;
    };

    // Emit initial sample
    auto s0 = evalTransform(uniqueSeeds[0]);
    if (work.Failed) return false;
    if (!emitSample(s0)) return false;

    // Subdivide and emit each seed interval
    for (size_t i = 0; i < uniqueSeeds.size() - 1; ++i) {
        auto sA = evalTransform(uniqueSeeds[i]);
        auto sB = evalTransform(uniqueSeeds[i + 1]);
        if (work.Failed) return false;
        if (!subdivide(sA, sB, 0)) return false;
        if (!emitSample(sB)) return false;
    }

    outChannel = std::move(candidateChannel);
    remainingKeyBudget = localBudget;
    if (outStats) *outStats = localStats;
    return true;
}

bool FbxCurveBaker::BakeChannel(const ufbx_anim* anim,const ufbx_node* node,double begin,double end,
    const std::string& boneName,int boneIndex,AnimationChannel& channel,const FbxBakeTolerances& tolerances,
    size_t& remainingKeys,FbxBakeStats* stats,std::string* error) {
    BakeWorkBudget work{tolerances.MaxWorkUnitsPerStack};
    return BakeChannelImpl(anim,node,begin,end,boneName,boneIndex,channel,tolerances,remainingKeys,stats,error,work);
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

    if (!tolerances.Validate(outError)) return nullptr;
    if (!rig.IsValid() || rig.GetBoneCount()>128 || nodes.empty() || nodes.size()>rig.GetBoneCount()) {
        if (outError) *outError="Invalid FBX node-to-rig binding";
        return nullptr;
    }
    BakeWorkBudget work{tolerances.MaxWorkUnitsPerStack};
    FbxBakeStats stats=outStats ? *outStats : FbxBakeStats{};
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
        if (!BakeChannelImpl(stack->anim, node, stack->time_begin, stack->time_end,
                         boneName, boneIndex, channel, tolerances,
                         remainingBudget, &stats, outError, work)) {
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

    if (outStats) *outStats=stats;
    return clip;
}

} // namespace Axle
