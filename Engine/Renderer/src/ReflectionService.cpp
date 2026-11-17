#include <Axle/ReflectionService.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace Axle {
    namespace {
        bool IsFiniteVec3(const glm::vec3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        float SmoothStep01(float x) {
            const float t = std::clamp(x, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        glm::vec3 SafeNormalizeOrFallback(const glm::vec3& v, const glm::vec3& fallback) {
            if (!IsFiniteVec3(v)) return fallback;
            const float lenSq = glm::dot(v, v);
            if (!std::isfinite(lenSq) || !(lenSq > 1e-12f)) return fallback;
            return v / std::sqrt(lenSq);
        }
    }

    ReflectionProbeBasis ReflectionService::BuildProbeBasis(const RenderReflectionProbe& probe) {
        ReflectionProbeBasis basis;
        basis.Position = IsFiniteVec3(probe.Position) ? probe.Position : glm::vec3(0.0f);
        const glm::vec3 absExt = IsFiniteVec3(probe.BoxExtents) ? glm::abs(probe.BoxExtents) : glm::vec3(10.0f);
        basis.HalfExtents = glm::max(absExt * 0.5f, glm::vec3(0.001f));
        basis.Radius = (std::isfinite(probe.Radius) && std::abs(probe.Radius) > 0.0f)
            ? std::max(std::abs(probe.Radius), 0.001f) : 0.001f;
        basis.Shape = (probe.Shape == 1) ? 1 : 0;
        basis.ParallaxCorrected = probe.ParallaxCorrected;
        basis.Intensity = (std::isfinite(probe.Intensity) && probe.Intensity > 0.0f) ? probe.Intensity : 0.0f;

        if (std::isfinite(probe.BlendDistance) && probe.BlendDistance > 0.0f) {
            basis.BlendDistance = std::max(probe.BlendDistance, 0.001f);
        } else {
            basis.BlendDistance = (basis.Shape == 1) ? basis.Radius : glm::length(basis.HalfExtents);
        }

        const glm::vec3 col0(probe.WorldTransform[0]);
        const glm::vec3 col1(probe.WorldTransform[1]);
        const glm::vec3 col2(probe.WorldTransform[2]);
        const glm::vec3 x = SafeNormalizeOrFallback(col0, glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 yOrtho = col1 - x * glm::dot(x, col1);
        const glm::vec3 reference = std::abs(x.z) < 0.9f ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
        const glm::vec3 yFallback = glm::normalize(glm::cross(reference, x));
        const glm::vec3 y = SafeNormalizeOrFallback(yOrtho, yFallback);
        const glm::vec3 zCandidate = glm::cross(x, y);
        glm::vec3 z = SafeNormalizeOrFallback(zCandidate, glm::vec3(0.0f, 0.0f, 1.0f));
        if (IsFiniteVec3(col2) && glm::dot(z, col2) < 0.0f) {
            z = -z;
        }
        basis.AxisX = x;
        basis.AxisY = y;
        basis.AxisZ = z;
        return basis;
    }

    ReflectionProbeInfluence ReflectionService::EvaluateProbeInfluence(
        const RenderReflectionProbe& probe,
        const glm::vec3& samplePosition) {
        ReflectionProbeInfluence out{};
        if (!IsFiniteVec3(samplePosition) || !IsFiniteVec3(probe.Position)) return out;

        const ReflectionProbeBasis basis = BuildProbeBasis(probe);
        const glm::vec3 delta = samplePosition - basis.Position;
        const glm::vec3 local(
            glm::dot(delta, basis.AxisX),
            glm::dot(delta, basis.AxisY),
            glm::dot(delta, basis.AxisZ));

        float distanceOutside = 0.0f;
        float innerRatio = 0.0f;
        if (basis.Shape == 1) {
            const float dist = glm::length(local);
            innerRatio = std::clamp(dist / basis.Radius, 0.0f, 1.0f);
            distanceOutside = std::max(dist - basis.Radius, 0.0f);
        } else {
            const glm::vec3 absLocal = glm::abs(local);
            const glm::vec3 normAxes = absLocal / basis.HalfExtents;
            innerRatio = std::clamp(std::max({normAxes.x, normAxes.y, normAxes.z}), 0.0f, 1.0f);
            const glm::vec3 outsideVec = glm::max(absLocal - basis.HalfExtents, glm::vec3(0.0f));
            distanceOutside = glm::length(outsideVec);
        }

        if (!std::isfinite(distanceOutside) || !std::isfinite(innerRatio)) return out;
        const float outerRatio = std::clamp(distanceOutside / basis.BlendDistance, 0.0f, 1.0f);
        const float coverage = 1.0f - SmoothStep01(outerRatio);
        const float interior = 1.0f - SmoothStep01(innerRatio);

        out.InnerRatio = innerRatio;
        out.OuterRatio = outerRatio;
        out.InsideCore = (outerRatio <= 0.0f);
        out.Coverage = coverage;
        out.BlendWeight = coverage * (0.05f + 0.95f * interior);
        return out;
    }

    glm::vec3 ReflectionService::ApplyProbeParallax(
        const RenderReflectionProbe& probe,
        const glm::vec3& worldPos,
        const glm::vec3& reflDir) {
        const glm::vec3 unitRefl = SafeNormalizeOrFallback(reflDir, glm::vec3(0.0f, 1.0f, 0.0f));
        if (!probe.ParallaxCorrected || !IsFiniteVec3(worldPos) || !IsFiniteVec3(probe.Position)) {
            return unitRefl;
        }

        const ReflectionProbeBasis basis = BuildProbeBasis(probe);
        const glm::vec3 delta = worldPos - basis.Position;
        const glm::vec3 localPos(
            glm::dot(delta, basis.AxisX),
            glm::dot(delta, basis.AxisY),
            glm::dot(delta, basis.AxisZ));
        const glm::vec3 localDir = SafeNormalizeOrFallback(
            glm::vec3(glm::dot(unitRefl, basis.AxisX),
                      glm::dot(unitRefl, basis.AxisY),
                      glm::dot(unitRefl, basis.AxisZ)),
            glm::vec3(0.0f, 1.0f, 0.0f));

        glm::vec3 correctedLocal = localDir;
        if (basis.Shape == 1) {
            const float b = glm::dot(localPos, localDir);
            const float c = glm::dot(localPos, localPos) - basis.Radius * basis.Radius;
            const float disc = b * b - c;
            if (disc >= 0.0f) {
                const float tExit = -b + std::sqrt(disc);
                if (tExit > 1e-4f) {
                    const glm::vec3 hitLocal = localPos + localDir * tExit;
                    correctedLocal = SafeNormalizeOrFallback(hitLocal, localDir);
                }
            }
        } else {
            float tEnter = -std::numeric_limits<float>::max();
            float tExit = std::numeric_limits<float>::max();
            for (int axis = 0; axis < 3; ++axis) {
                if (std::abs(localDir[axis]) < 1e-6f) {
                    if (std::abs(localPos[axis]) > basis.HalfExtents[axis]) return unitRefl;
                    continue;
                }
                const float t0 = (-basis.HalfExtents[axis] - localPos[axis]) / localDir[axis];
                const float t1 = (basis.HalfExtents[axis] - localPos[axis]) / localDir[axis];
                tEnter = std::max(tEnter, std::min(t0, t1));
                tExit = std::min(tExit, std::max(t0, t1));
            }
            if (tExit > 1e-4f && tExit >= tEnter) {
                const glm::vec3 hitLocal = localPos + localDir * tExit;
                correctedLocal = SafeNormalizeOrFallback(hitLocal, localDir);
            }
        }

        const glm::vec3 correctedWorld =
            basis.AxisX * correctedLocal.x +
            basis.AxisY * correctedLocal.y +
            basis.AxisZ * correctedLocal.z;
        return SafeNormalizeOrFallback(correctedWorld, unitRefl);
    }

    ReflectionEnvironmentBlend ReflectionService::EvaluateEnvironmentBlend(
        const std::vector<ReflectionProbeCandidate>& candidates,
        const glm::vec3& worldPos,
        const glm::vec3& reflDir,
        float ssrConfidence,
        float ssrStrength,
        const glm::vec3& ssrHitColor,
        const glm::vec3& skyColor,
        const std::function<glm::vec3(const RenderReflectionProbe&, const glm::vec3&, size_t)>& sampleProbeFn) {
        ReflectionEnvironmentBlend out{};
        const glm::vec3 safeSSR = IsFiniteVec3(ssrHitColor) ? glm::max(ssrHitColor, glm::vec3(0.0f)) : glm::vec3(0.0f);
        const glm::vec3 safeSky = IsFiniteVec3(skyColor) ? glm::max(skyColor, glm::vec3(0.0f)) : glm::vec3(0.0f);

        float weightSum = 0.0f;
        float skyComplementProd = 1.0f;
        glm::vec3 weightedProbeColor(0.0f);
        uint32_t contributingCount = 0;

        const size_t limit = std::min<size_t>(candidates.size(), MaxActiveReflectionProbes);
        for (size_t i = 0; i < limit; ++i) {
            const auto& cand = candidates[i];
            if (!cand.Ready) continue;
            const ReflectionProbeInfluence inf = EvaluateProbeInfluence(cand.Probe, worldPos);
            if (!(inf.Coverage > 0.0f) || !(inf.BlendWeight > 0.0f)) continue;

            const glm::vec3 parallaxDir = ApplyProbeParallax(cand.Probe, worldPos, reflDir);
            const glm::vec3 rawSample = sampleProbeFn ? sampleProbeFn(cand.Probe, parallaxDir, i) : glm::vec3(0.0f);
            const glm::vec3 sampleColor = IsFiniteVec3(rawSample) ? glm::max(rawSample, glm::vec3(0.0f)) : glm::vec3(0.0f);
            const float intensity = (std::isfinite(cand.Probe.Intensity) && cand.Probe.Intensity > 0.0f) ? cand.Probe.Intensity : 0.0f;

            weightedProbeColor += (sampleColor * intensity) * inf.BlendWeight;
            weightSum += inf.BlendWeight;
            skyComplementProd *= (1.0f - std::clamp(inf.Coverage, 0.0f, 1.0f));
            ++contributingCount;
        }

        out.ActiveProbeCount = contributingCount;
        if (weightSum > 1e-6f && contributingCount > 0) {
            out.ProbeColor = weightedProbeColor / weightSum;
            out.TotalProbeCoverage = std::clamp(1.0f - skyComplementProd, 0.0f, 1.0f);
        } else {
            out.ProbeColor = glm::vec3(0.0f);
            out.TotalProbeCoverage = 0.0f;
        }
        out.SkyFallbackWeight = 1.0f - out.TotalProbeCoverage;

        const float clampedConf = std::isfinite(ssrConfidence) ? std::clamp(ssrConfidence, 0.0f, 1.0f) : 0.0f;
        const float clampedStrength = std::isfinite(ssrStrength) ? std::clamp(ssrStrength, 0.0f, 1.0f) : 0.0f;
        out.SSRWeight = clampedConf * clampedStrength;
        out.ProbeWeight = (1.0f - out.SSRWeight) * out.TotalProbeCoverage;
        out.SkyWeight = (1.0f - out.SSRWeight) * out.SkyFallbackWeight;
        out.BlendedColor = safeSSR * out.SSRWeight + out.ProbeColor * out.ProbeWeight + safeSky * out.SkyWeight;
        return out;
    }

    ReflectionFrame ReflectionService::Prepare(const RenderWorld& world, const glm::vec3& cameraPosition, bool enableSSR, uint64_t sceneRevision) const {
        return PrepareView(world, cameraPosition, {}, enableSSR, sceneRevision);
    }

    ReflectionFrame ReflectionService::PrepareView(
        const RenderWorld& world,
        const glm::vec3& cameraPosition,
        std::span<const glm::vec3> samplePoints,
        bool enableSSR,
        uint64_t sceneRevision,
        std::span<const ReflectionSampleBounds> sampleBounds) const {
        ReflectionFrame frame;
        frame.SceneRevision=sceneRevision;
        frame.Fallback=enableSSR ? ReflectionFallback::SSR :
            (world.Skybox.Authored && world.Skybox.Enabled && world.Skybox.EnvironmentLighting
                ? ReflectionFallback::SkyIBL : ReflectionFallback::NeutralAmbient);
        frame.State=ReflectionState::Fallback;
        frame.Diagnostic="no_active_probe_influence: " + std::string(FallbackName(frame.Fallback)) + " fallback";

        struct ScoredCandidate {
            size_t SourceIndex = 0;
            ReflectionProbeCandidate Candidate;
            float CameraCoverage = 0.0f;
            float CameraWeight = 0.0f;
        };
        std::vector<ScoredCandidate> scored;
        scored.reserve(world.ReflectionProbes.size());

        for (size_t idx = 0; idx < world.ReflectionProbes.size(); ++idx) {
            const auto& probe = world.ReflectionProbes[idx];
            const bool isDynamic = probe.DynamicCapture && probe.CubemapPath.empty();
            if (probe.CubemapPath.empty() && !isDynamic) continue;

            const ReflectionProbeInfluence camInf = EvaluateProbeInfluence(probe, cameraPosition);
            float maxCoverage = camInf.Coverage;
            float maxBlendWeight = camInf.BlendWeight;
            for (const auto& pt : samplePoints) {
                const ReflectionProbeInfluence ptInf = EvaluateProbeInfluence(probe, pt);
                maxCoverage = std::max(maxCoverage, ptInf.Coverage);
                maxBlendWeight = std::max(maxBlendWeight, ptInf.BlendWeight);
            }
            for (const auto& bounds : sampleBounds) {
                if (!IsFiniteVec3(bounds.Min) || !IsFiniteVec3(bounds.Max) ||
                    glm::any(glm::greaterThan(bounds.Min, bounds.Max))) continue;
                glm::vec3 sample = glm::clamp(probe.Position, bounds.Min, bounds.Max);
                if (probe.Shape != 1) {
                    // Enclose mesh bounds in probe space. A world-space nearest
                    // point can miss a rotated slender box that intersects the
                    // mesh elsewhere. Conservative selection may include empty
                    // bounds; the shader still evaluates exact per-pixel influence.
                    const auto basis = BuildProbeBasis(probe);
                    const auto center = (bounds.Min + bounds.Max) * 0.5f - basis.Position;
                    const auto half = (bounds.Max - bounds.Min) * 0.5f;
                    const glm::vec3 localCenter(glm::dot(center, basis.AxisX),
                        glm::dot(center, basis.AxisY), glm::dot(center, basis.AxisZ));
                    const glm::vec3 localHalf(glm::dot(half, glm::abs(basis.AxisX)),
                        glm::dot(half, glm::abs(basis.AxisY)), glm::dot(half, glm::abs(basis.AxisZ)));
                    const auto closest = glm::clamp(glm::vec3(0), localCenter-localHalf, localCenter+localHalf);
                    sample = basis.Position + basis.AxisX*closest.x + basis.AxisY*closest.y + basis.AxisZ*closest.z;
                }
                const auto inf = EvaluateProbeInfluence(probe, sample);
                maxCoverage = std::max(maxCoverage, inf.Coverage);
                maxBlendWeight = std::max(maxBlendWeight, inf.BlendWeight);
            }
            if (!(maxCoverage > 0.0f) || !(maxBlendWeight > 0.0f)) continue;

            ScoredCandidate entry;
            entry.SourceIndex = idx;
            entry.CameraCoverage = camInf.Coverage;
            entry.CameraWeight = camInf.BlendWeight;
            entry.Candidate.Probe = probe;
            entry.Candidate.DynamicCapture = isDynamic;
            entry.Candidate.InfluenceCoverage = maxCoverage;
            entry.Candidate.RawBlendWeight = maxBlendWeight;
            entry.Candidate.TotalFaces = isDynamic ? 6u : 0u;
            scored.push_back(std::move(entry));
        }

        if (scored.empty()) return frame;

        std::stable_sort(scored.begin(), scored.end(), [](const ScoredCandidate& a, const ScoredCandidate& b) {
            if (a.CameraWeight != b.CameraWeight) return a.CameraWeight > b.CameraWeight;
            if (a.Candidate.RawBlendWeight != b.Candidate.RawBlendWeight)
                return a.Candidate.RawBlendWeight > b.Candidate.RawBlendWeight;
            return a.SourceIndex < b.SourceIndex;
        });
        if (scored.size() > MaxActiveReflectionProbes) {
            for (size_t i = MaxActiveReflectionProbes; i < scored.size(); ++i)
                frame.AdditionalCandidates.push_back(scored[i].Candidate);
            scored.resize(MaxActiveReflectionProbes);
        }

        float totalWeight = 0.0f;
        float skyComplementProd = 1.0f;
        for (const auto& entry : scored) {
            totalWeight += entry.Candidate.RawBlendWeight;
            skyComplementProd *= (1.0f - std::clamp(entry.Candidate.InfluenceCoverage, 0.0f, 1.0f));
        }
        frame.ActiveProbes.reserve(scored.size());
        for (auto& entry : scored) {
            entry.Candidate.NormalizedWeight = totalWeight > 1e-6f ? (entry.Candidate.RawBlendWeight / totalWeight) : 0.0f;
            frame.ActiveProbes.push_back(std::move(entry.Candidate));
        }
        frame.TotalProbeCoverage = std::clamp(1.0f - skyComplementProd, 0.0f, 1.0f);
        frame.SkyFallbackWeight = 1.0f - frame.TotalProbeCoverage;

        const auto& primary = frame.ActiveProbes[0];
        frame.SelectedProbe = primary.Probe;
        frame.SelectedProbeCount = static_cast<uint32_t>(frame.ActiveProbes.size());
        frame.SelectedProbeEntityID = primary.Probe.EntityID;
        frame.CubemapPath = primary.Probe.CubemapPath;
        frame.ParallaxCorrected = primary.Probe.ParallaxCorrected;
        frame.DynamicCaptureSupported = primary.DynamicCapture;
        frame.State = primary.DynamicCapture ? ReflectionState::DynamicPending : ReflectionState::AuthoredPending;
        frame.CaptureTotalFaces = primary.DynamicCapture ? 6u : 0u;
        frame.Diagnostic = primary.DynamicCapture
            ? "dynamic_probe_pending: six-face capture publication required"
            : "authored_probe_pending: native prefilter publication required";
        return frame;
    }

    ReflectionFrame ReflectionService::ResolvePublication(ReflectionFrame frame, const ReflectionPublication& publication,
                                                          bool ssrAvailable, bool skyIBLAvailable) const {
        if (!publication.SelectedCandidates.empty()) frame.ActiveProbes = publication.SelectedCandidates;
        frame.ProbeGeneration={};
        frame.PrefilterReady=false;
        frame.ActiveProbeCount=0;
        frame.MissingProbeCount=publication.RejectedProbeCount;
        frame.StaleProbeCount=0;
        frame.CapturePending=publication.CapturePending;
        frame.CaptureStale=publication.CaptureStale;
        frame.CaptureFailed=publication.CaptureFailed;
        frame.CaptureCompletedFaces=publication.CaptureCompletedFaces;
        frame.CaptureTotalFaces=publication.CaptureTotalFaces;
        frame.ActiveGenerationID=publication.ActiveGenerationID;
        frame.Fallback=ssrAvailable ? ReflectionFallback::SSR :
            (skyIBLAvailable ? ReflectionFallback::SkyIBL : ReflectionFallback::NeutralAmbient);

        if (!publication.ProbePublications.empty() && !frame.ActiveProbes.empty()) {
            const size_t count = std::min(frame.ActiveProbes.size(), publication.ProbePublications.size());
            float readyWeightSum = 0.0f;
            float readySkyComplementProd = 1.0f;
            const ReflectionProbeCandidate* primaryReady = nullptr;
            bool anyPending = publication.CapturePending;
            bool anyStale = publication.CaptureStale;
            bool anyFailed = publication.CaptureFailed;

            for (size_t i = 0; i < count; ++i) {
                auto& cand = frame.ActiveProbes[i];
                const auto& pub = publication.ProbePublications[i];
                cand.PrefilterGeneration = pub.PrefilterGeneration;
                cand.Diagnostic = pub.Diagnostic;
                cand.Pending = pub.CapturePending;
                cand.Stale = pub.CaptureStale;
                cand.Failed = pub.CaptureFailed;
                cand.CompletedFaces = pub.CaptureCompletedFaces;
                cand.TotalFaces = pub.CaptureTotalFaces;
                cand.ActiveGenerationID = pub.ActiveGenerationID;
                cand.Ready = pub.Ready && static_cast<bool>(pub.PrefilterGeneration);
                anyPending = anyPending || cand.Pending;
                anyStale = anyStale || cand.Stale;
                anyFailed = anyFailed || cand.Failed;
                if (cand.Stale) ++frame.StaleProbeCount;
                if (cand.Ready) {
                    ++frame.ActiveProbeCount;
                    readyWeightSum += cand.RawBlendWeight;
                    readySkyComplementProd *= (1.0f - std::clamp(cand.InfluenceCoverage, 0.0f, 1.0f));
                    if (!primaryReady) primaryReady = &cand;
                } else {
                    cand.NormalizedWeight = 0.0f;
                    ++frame.MissingProbeCount;
                }
            }
            for (size_t i = count; i < frame.ActiveProbes.size(); ++i) {
                frame.ActiveProbes[i].Ready = false;
                frame.ActiveProbes[i].NormalizedWeight = 0.0f;
                ++frame.MissingProbeCount;
            }
            for (auto& cand : frame.ActiveProbes) {
                if (cand.Ready) {
                    cand.NormalizedWeight = readyWeightSum > 1e-6f ? (cand.RawBlendWeight / readyWeightSum) : 0.0f;
                }
            }

            frame.CapturePending = anyPending;
            frame.CaptureStale = anyStale;
            frame.CaptureFailed = anyFailed;
            if (primaryReady) {
                frame.TotalProbeCoverage = std::clamp(1.0f - readySkyComplementProd, 0.0f, 1.0f);
                frame.SkyFallbackWeight = 1.0f - frame.TotalProbeCoverage;
                frame.SelectedProbe = primaryReady->Probe;
                frame.SelectedProbeEntityID = primaryReady->Probe.EntityID;
                frame.SelectedProbeCount = static_cast<uint32_t>(frame.ActiveProbes.size());
                frame.CubemapPath = primaryReady->Probe.CubemapPath;
                frame.ParallaxCorrected = primaryReady->Probe.ParallaxCorrected;
                frame.DynamicCaptureSupported = primaryReady->DynamicCapture;
                frame.ProbeGeneration = primaryReady->PrefilterGeneration;
                frame.PrefilterReady = true;
                frame.State = primaryReady->DynamicCapture ? ReflectionState::DynamicReady : ReflectionState::AuthoredReady;
                frame.Fallback = ReflectionFallback::None;
                frame.CaptureCompletedFaces = primaryReady->CompletedFaces;
                frame.CaptureTotalFaces = primaryReady->TotalFaces;
                frame.ActiveGenerationID = primaryReady->ActiveGenerationID;
                frame.Diagnostic = !publication.Diagnostic.empty() ? publication.Diagnostic : primaryReady->Diagnostic;
            } else {
                frame.TotalProbeCoverage = 0.0f;
                frame.SkyFallbackWeight = 1.0f;
                const bool primaryDynamic = frame.SelectedProbe && frame.SelectedProbe->DynamicCapture && frame.SelectedProbe->CubemapPath.empty();
                frame.State = ReflectionState::Fallback;
                frame.Diagnostic = std::string(!frame.SelectedProbe ? "no_active_probe_influence: " :
                    (primaryDynamic ? "dynamic_probe_unavailable: " : "authored_probe_unavailable: ")) +
                    FallbackName(frame.Fallback) + " fallback";
                if (!publication.Diagnostic.empty()) frame.Diagnostic += "; " + publication.Diagnostic;
            }
            return frame;
        }

        const bool isDynamic = frame.SelectedProbe && frame.SelectedProbe->DynamicCapture && frame.SelectedProbe->CubemapPath.empty();
        if (frame.SelectedProbe && publication.PrefilterGeneration) {
            frame.ProbeGeneration=publication.PrefilterGeneration;
            frame.PrefilterReady=true;
            frame.DynamicCaptureSupported=isDynamic;
            frame.State=isDynamic ? ReflectionState::DynamicReady : ReflectionState::AuthoredReady;
            frame.Fallback=ReflectionFallback::None;
            frame.Diagnostic=publication.Diagnostic;
            frame.ActiveProbeCount=1;
            if (publication.CaptureStale) frame.StaleProbeCount=1;
            if (!frame.ActiveProbes.empty()) {
                auto& cand = frame.ActiveProbes[0];
                cand.Ready = true;
                cand.NormalizedWeight = 1.0f;
                cand.PrefilterGeneration = publication.PrefilterGeneration;
                cand.Pending = publication.CapturePending;
                cand.Stale = publication.CaptureStale;
                cand.Failed = publication.CaptureFailed;
                cand.CompletedFaces = publication.CaptureCompletedFaces;
                cand.TotalFaces = publication.CaptureTotalFaces;
                cand.ActiveGenerationID = publication.ActiveGenerationID;
                cand.Diagnostic = publication.Diagnostic;
                frame.TotalProbeCoverage = cand.InfluenceCoverage;
                frame.SkyFallbackWeight = 1.0f - frame.TotalProbeCoverage;
            }
        } else {
            frame.State=ReflectionState::Fallback;
            frame.TotalProbeCoverage=0.0f;
            frame.SkyFallbackWeight=1.0f;
            if (!frame.ActiveProbes.empty()) {
                frame.MissingProbeCount = static_cast<uint32_t>(frame.ActiveProbes.size());
                for (auto& cand : frame.ActiveProbes) {
                    cand.Ready = false;
                    cand.NormalizedWeight = 0.0f;
                }
            }
            frame.Diagnostic=std::string(!frame.SelectedProbe ? "no_active_probe_influence: " :
                (isDynamic ? "dynamic_probe_unavailable: " : "authored_probe_unavailable: ")) +
                FallbackName(frame.Fallback) + " fallback";
            if (!publication.Diagnostic.empty()) frame.Diagnostic+="; "+publication.Diagnostic;
        }
        return frame;
    }
    ReflectionInvalidation ReflectionService::EvaluateInvalidation(
        const ReflectionCaptureDomainStamps& published,
        const ReflectionCaptureDomainStamps& current,
        bool manualRequested) const {
        ReflectionInvalidation out;
        out.TransformChanged = (published.TransformStamp != current.TransformStamp);
        out.SceneRevisionChanged = (published.SceneRevision != current.SceneRevision) ||
                                   (published.SceneInstanceID != current.SceneInstanceID) ||
                                   (published.SceneLifetimeEpoch != current.SceneLifetimeEpoch);
        out.GeometryChanged = (published.GeometryStamp != current.GeometryStamp);
        out.MaterialChanged = (published.MaterialStamp != current.MaterialStamp);
        out.LightingChanged = (published.LightingStamp != current.LightingStamp);
        out.AssetChanged = (published.AssetStamp != current.AssetStamp);
        out.SettingsChanged = (published.SettingsStamp != current.SettingsStamp);
        out.DeviceEpochChanged = (published.DeviceEpoch != current.DeviceEpoch);
        out.ManualRequested = manualRequested;
        return out;
    }
    const char* ReflectionService::FallbackName(ReflectionFallback fallback){switch(fallback){case ReflectionFallback::SSR:return "SSR";case ReflectionFallback::LocalProbe:return "LocalProbe";case ReflectionFallback::SkyIBL:return "SkyIBL";case ReflectionFallback::NeutralAmbient:return "NeutralAmbient";default:return "None";}}
}
