#include <Axle/BlendSpace1D.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    void BlendSpace1D::AddPoint(float value, const std::shared_ptr<AnimationClip>& clip) {
        if (!clip) return;

        BlendSpacePoint1D pt{ value, clip };
        // Insert maintaining sorted order by Value
        auto it = std::lower_bound(m_Points.begin(), m_Points.end(), pt,
            [](const BlendSpacePoint1D& a, const BlendSpacePoint1D& b) {
                return a.Value < b.Value;
            });
        m_Points.insert(it, pt);
    }

    void BlendSpace1D::Clear() {
        m_Points.clear();
    }

    float BlendSpace1D::GetBlendedDuration(float parameter) const {
        if (m_Points.empty()) return 1.0f;
        if (m_Points.size() == 1 || parameter <= m_Points.front().Value) {
            return m_Points.front().Clip->GetDuration();
        }
        if (parameter >= m_Points.back().Value) {
            return m_Points.back().Clip->GetDuration();
        }

        for (size_t i = 0; i < m_Points.size() - 1; ++i) {
            float v0 = m_Points[i].Value;
            float v1 = m_Points[i + 1].Value;
            if (parameter <= v1) {
                float range = v1 - v0;
                float factor = (range > 1e-6f) ? (parameter - v0) / range : 0.0f;
                float d0 = m_Points[i].Clip->GetDuration();
                float d1 = m_Points[i + 1].Clip->GetDuration();
                return std::lerp(d0, d1, factor);
            }
        }
        return m_Points.back().Clip->GetDuration();
    }

    void BlendSpace1D::SamplePose(float parameter, float time, const Skeleton& skeleton, AnimationPose& outPose, bool loop) const {
        if (m_Points.empty()) {
            outPose.InitFromSkeletonBindPose(skeleton);
            return;
        }

        if (m_Points.size() == 1 || parameter <= m_Points.front().Value) {
            m_Points.front().Clip->SamplePose(time, skeleton, outPose, loop);
            return;
        }

        if (parameter >= m_Points.back().Value) {
            m_Points.back().Clip->SamplePose(time, skeleton, outPose, loop);
            return;
        }

        // Find bounding pair
        for (size_t i = 0; i < m_Points.size() - 1; ++i) {
            float v0 = m_Points[i].Value;
            float v1 = m_Points[i + 1].Value;

            if (parameter <= v1) {
                float range = v1 - v0;
                float factor = (range > 1e-6f) ? (parameter - v0) / range : 0.0f;

                const auto& clip0 = m_Points[i].Clip;
                const auto& clip1 = m_Points[i + 1].Clip;

                float dur0 = clip0->GetDuration();
                float dur1 = clip1->GetDuration();
                float blendDuration = std::lerp(dur0, dur1, factor);

                // Compute phase-matched cycle position
                float phase = 0.0f;
                if (blendDuration > 1e-6f) {
                    phase = std::fmod(time, blendDuration) / blendDuration;
                    if (phase < 0.0f) phase += 1.0f;
                }

                float t0 = loop ? (phase * dur0) : std::clamp(time, 0.0f, dur0);
                float t1 = loop ? (phase * dur1) : std::clamp(time, 0.0f, dur1);

                AnimationPose pose0(skeleton.GetBoneCount());
                AnimationPose pose1(skeleton.GetBoneCount());

                clip0->SamplePose(t0, skeleton, pose0, loop);
                clip1->SamplePose(t1, skeleton, pose1, loop);

                AnimationPose::Blend(pose0, pose1, factor, outPose);
                return;
            }
        }

        m_Points.back().Clip->SamplePose(time, skeleton, outPose, loop);
    }

}
