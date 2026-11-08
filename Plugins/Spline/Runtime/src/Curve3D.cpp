#include <AxleSpline/Curve3D.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Axle::Spline {

    Curve3D::Curve3D(SplineType type, bool loop)
        : m_Type(type), m_Loop(loop) {}

    void Curve3D::AddNode(const SplineNode& node) {
        m_Nodes.push_back(node);
        m_CacheDirty = true;
    }

    void Curve3D::InsertNode(size_t index, const SplineNode& node) {
        if (index >= m_Nodes.size()) {
            m_Nodes.push_back(node);
        } else {
            m_Nodes.insert(m_Nodes.begin() + index, node);
        }
        m_CacheDirty = true;
    }

    void Curve3D::RemoveNode(size_t index) {
        if (index < m_Nodes.size()) {
            m_Nodes.erase(m_Nodes.begin() + index);
            m_CacheDirty = true;
        }
    }

    void Curve3D::ClearNodes() {
        m_Nodes.clear();
        m_ArcLengthLUT.clear();
        m_CachedNormals.clear();
        m_CachedBinormals.clear();
        m_TotalLength = 0.0f;
        m_CacheDirty = false;
    }

    void Curve3D::SetSplineType(SplineType type) {
        if (m_Type != type) {
            m_Type = type;
            m_CacheDirty = true;
        }
    }

    void Curve3D::SetLoop(bool loop) {
        if (m_Loop != loop) {
            m_Loop = loop;
            m_CacheDirty = true;
        }
    }

    void Curve3D::SetSubdivisionsPerSegment(u32 count) {
        count = std::max(4u, count);
        if (m_SubdivisionsPerSegment != count) {
            m_SubdivisionsPerSegment = count;
            m_CacheDirty = true;
        }
    }

    u32 Curve3D::GetSegmentCount() const {
        if (m_Nodes.size() < 2) return 0;
        return m_Loop ? static_cast<u32>(m_Nodes.size()) : static_cast<u32>(m_Nodes.size() - 1);
    }

    float Curve3D::GetTotalLength() const {
        if (m_CacheDirty) {
            const_cast<Curve3D*>(this)->RebuildCache();
        }
        return m_TotalLength;
    }

    void Curve3D::RebuildCache() {
        m_ArcLengthLUT.clear();
        m_CachedNormals.clear();
        m_CachedBinormals.clear();
        m_TotalLength = 0.0f;

        u32 segCount = GetSegmentCount();
        if (segCount == 0) {
            m_CacheDirty = false;
            return;
        }

        // 1. Build initial sample
        m_ArcLengthLUT.push_back({ 0.0f, 0, 0.0f });
        glm::vec3 prevPos = EvaluatePosition(0, 0.0f);

        // 2. Sample arc-length table along each segment
        for (u32 seg = 0; seg < segCount; ++seg) {
            for (u32 step = 1; step <= m_SubdivisionsPerSegment; ++step) {
                float t = static_cast<float>(step) / static_cast<float>(m_SubdivisionsPerSegment);
                glm::vec3 currentPos = EvaluatePosition(seg, t);
                float stepDist = glm::length(currentPos - prevPos);
                m_TotalLength += stepDist;

                m_ArcLengthLUT.push_back({ m_TotalLength, seg, t });
                prevPos = currentPos;
            }
        }

        // 3. Compute Rotation-Minimizing Frames (RMF) across all LUT points
        ComputeRotationMinimizingFrames();

        m_CacheDirty = false;
    }

    void Curve3D::ComputeRotationMinimizingFrames() {
        size_t sampleCount = m_ArcLengthLUT.size();
        if (sampleCount == 0) return;

        m_CachedNormals.resize(sampleCount);
        m_CachedBinormals.resize(sampleCount);

        // Compute initial tangent
        glm::vec3 t0 = EvaluateTangent(m_ArcLengthLUT[0].SegmentIndex, m_ArcLengthLUT[0].SegmentT);
        if (glm::length(t0) > 1e-5f) {
            t0 = glm::normalize(t0);
        } else {
            t0 = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        // Select initial up reference
        glm::vec3 upRef(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(t0, upRef)) > 0.95f) {
            upRef = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        glm::vec3 b0 = glm::normalize(glm::cross(t0, upRef));
        glm::vec3 n0 = glm::normalize(glm::cross(b0, t0));

        m_CachedNormals[0] = n0;
        m_CachedBinormals[0] = b0;

        glm::vec3 currentTangent = t0;

        // Double Reflection Method (Wang et al. 2008)
        for (size_t i = 0; i < sampleCount - 1; ++i) {
            glm::vec3 x_i = EvaluatePosition(m_ArcLengthLUT[i].SegmentIndex, m_ArcLengthLUT[i].SegmentT);
            glm::vec3 x_next = EvaluatePosition(m_ArcLengthLUT[i + 1].SegmentIndex, m_ArcLengthLUT[i + 1].SegmentT);

            glm::vec3 t_next = EvaluateTangent(m_ArcLengthLUT[i + 1].SegmentIndex, m_ArcLengthLUT[i + 1].SegmentT);
            if (glm::length(t_next) > 1e-5f) {
                t_next = glm::normalize(t_next);
            } else {
                t_next = currentTangent;
            }

            glm::vec3 v1 = x_next - x_i;
            float c1 = glm::dot(v1, v1);

            glm::vec3 n_next = m_CachedNormals[i];
            glm::vec3 b_next = m_CachedBinormals[i];

            if (c1 > 1e-8f) {
                glm::vec3 n_L = m_CachedNormals[i] - (2.0f / c1) * glm::dot(v1, m_CachedNormals[i]) * v1;
                glm::vec3 t_L = currentTangent - (2.0f / c1) * glm::dot(v1, currentTangent) * v1;

                glm::vec3 v2 = t_next - t_L;
                float c2 = glm::dot(v2, v2);

                if (c2 > 1e-8f) {
                    n_next = n_L - (2.0f / c2) * glm::dot(v2, n_L) * v2;
                } else {
                    n_next = n_L;
                }

                // Orthogonalize against t_next
                n_next = n_next - glm::dot(n_next, t_next) * t_next;
                if (glm::length(n_next) > 1e-5f) {
                    n_next = glm::normalize(n_next);
                } else {
                    n_next = m_CachedNormals[i];
                }
                b_next = glm::normalize(glm::cross(t_next, n_next));
            }

            m_CachedNormals[i + 1] = n_next;
            m_CachedBinormals[i + 1] = b_next;
            currentTangent = t_next;
        }
    }

    void Curve3D::GetSegmentAndLocalT(float distance, u32& outSegment, float& outT) const {
        if (m_CacheDirty) {
            const_cast<Curve3D*>(this)->RebuildCache();
        }

        if (m_ArcLengthLUT.empty()) {
            outSegment = 0;
            outT = 0.0f;
            return;
        }

        if (m_TotalLength <= 1e-5f) {
            outSegment = m_ArcLengthLUT.front().SegmentIndex;
            outT = m_ArcLengthLUT.front().SegmentT;
            return;
        }

        if (m_Loop) {
            distance = std::fmod(distance, m_TotalLength);
            if (distance < 0.0f) distance += m_TotalLength;
        } else {
            distance = std::clamp(distance, 0.0f, m_TotalLength);
        }

        // Binary search in LUT
        auto it = std::lower_bound(m_ArcLengthLUT.begin(), m_ArcLengthLUT.end(), distance,
            [](const ArcLengthSample& sample, float val) {
                return sample.Distance < val;
            });

        if (it == m_ArcLengthLUT.begin()) {
            outSegment = it->SegmentIndex;
            outT = it->SegmentT;
            return;
        }
        if (it == m_ArcLengthLUT.end()) {
            outSegment = m_ArcLengthLUT.back().SegmentIndex;
            outT = m_ArcLengthLUT.back().SegmentT;
            return;
        }

        auto prevIt = it - 1;
        float d0 = prevIt->Distance;
        float d1 = it->Distance;
        float alpha = (d1 > d0) ? (distance - d0) / (d1 - d0) : 0.0f;

        if (prevIt->SegmentIndex == it->SegmentIndex) {
            outSegment = prevIt->SegmentIndex;
            outT = glm::mix(prevIt->SegmentT, it->SegmentT, alpha);
        } else {
            // Segment transition boundary
            if (alpha < 0.5f) {
                outSegment = prevIt->SegmentIndex;
                outT = glm::mix(prevIt->SegmentT, 1.0f, alpha * 2.0f);
            } else {
                outSegment = it->SegmentIndex;
                outT = glm::mix(0.0f, it->SegmentT, (alpha - 0.5f) * 2.0f);
            }
        }
    }

    glm::vec3 Curve3D::SamplePosition(float distance) const {
        u32 segment = 0;
        float t = 0.0f;
        GetSegmentAndLocalT(distance, segment, t);
        return EvaluatePosition(segment, t);
    }

    glm::vec3 Curve3D::SampleTangent(float distance) const {
        u32 segment = 0;
        float t = 0.0f;
        GetSegmentAndLocalT(distance, segment, t);
        return EvaluateTangent(segment, t);
    }

    float Curve3D::SampleRoll(float distance) const {
        u32 segment = 0;
        float t = 0.0f;
        GetSegmentAndLocalT(distance, segment, t);
        return EvaluateRoll(segment, t);
    }

    float Curve3D::SampleScale(float distance) const {
        u32 segment = 0;
        float t = 0.0f;
        GetSegmentAndLocalT(distance, segment, t);
        return EvaluateScale(segment, t);
    }

    SplineFrame Curve3D::SampleFrame(float distance) const {
        if (m_CacheDirty) {
            const_cast<Curve3D*>(this)->RebuildCache();
        }

        SplineFrame frame;
        if (m_ArcLengthLUT.empty()) {
            return frame;
        }

        float queryDist = distance;
        if (m_Loop && m_TotalLength > 1e-5f) {
            queryDist = std::fmod(distance, m_TotalLength);
            if (queryDist < 0.0f) queryDist += m_TotalLength;
        } else {
            queryDist = std::clamp(distance, 0.0f, m_TotalLength);
        }

        frame.Distance = distance;
        u32 segment = 0;
        float t = 0.0f;
        GetSegmentAndLocalT(queryDist, segment, t);

        frame.Position = EvaluatePosition(segment, t);
        frame.Tangent = EvaluateTangent(segment, t);
        frame.Roll = EvaluateRoll(segment, t);
        frame.Scale = EvaluateScale(segment, t);

        // Find sample indices for RMF interpolation
        auto it = std::lower_bound(m_ArcLengthLUT.begin(), m_ArcLengthLUT.end(), queryDist,
            [](const ArcLengthSample& sample, float val) {
                return sample.Distance < val;
            });

        glm::vec3 rmfNormal(0.0f, 1.0f, 0.0f);
        glm::vec3 rmfBinormal(1.0f, 0.0f, 0.0f);

        if (it == m_ArcLengthLUT.begin()) {
            rmfNormal = m_CachedNormals.front();
            rmfBinormal = m_CachedBinormals.front();
        } else if (it == m_ArcLengthLUT.end()) {
            rmfNormal = m_CachedNormals.back();
            rmfBinormal = m_CachedBinormals.back();
        } else {
            size_t idx1 = std::distance(m_ArcLengthLUT.begin(), it);
            size_t idx0 = idx1 - 1;
            float d0 = m_ArcLengthLUT[idx0].Distance;
            float d1 = m_ArcLengthLUT[idx1].Distance;
            float alpha = (d1 > d0) ? (queryDist - d0) / (d1 - d0) : 0.0f;

            rmfNormal = glm::normalize(glm::mix(m_CachedNormals[idx0], m_CachedNormals[idx1], alpha));
            rmfBinormal = glm::normalize(glm::mix(m_CachedBinormals[idx0], m_CachedBinormals[idx1], alpha));
        }

        // Apply Roll (Banking) around Tangent
        float rollRad = glm::radians(frame.Roll);
        float cosR = std::cos(rollRad);
        float sinR = std::sin(rollRad);

        frame.Normal = glm::normalize(cosR * rmfNormal + sinR * rmfBinormal);
        frame.Binormal = glm::normalize(glm::cross(frame.Tangent, frame.Normal));

        return frame;
    }

    glm::vec3 Curve3D::EvaluatePosition(u32 segmentIndex, float t) const {
        if (m_Nodes.empty()) return glm::vec3(0.0f);
        if (m_Nodes.size() == 1) return m_Nodes[0].Position;

        u32 n = static_cast<u32>(m_Nodes.size());
        u32 i = segmentIndex;
        u32 i_next = (i + 1) % n;

        t = std::clamp(t, 0.0f, 1.0f);

        switch (m_Type) {
            case SplineType::Linear: {
                return glm::mix(m_Nodes[i].Position, m_Nodes[i_next].Position, t);
            }
            case SplineType::CubicBezier: {
                glm::vec3 p0 = m_Nodes[i].Position;
                glm::vec3 p1 = p0 + m_Nodes[i].HandleOut;
                glm::vec3 p3 = m_Nodes[i_next].Position;
                glm::vec3 p2 = p3 + m_Nodes[i_next].HandleIn;

                float u = 1.0f - t;
                return (u * u * u) * p0 +
                       (3.0f * u * u * t) * p1 +
                       (3.0f * u * t * t) * p2 +
                       (t * t * t) * p3;
            }
            case SplineType::CatmullRom:
            default: {
                glm::vec3 p1 = m_Nodes[i].Position;
                glm::vec3 p2 = m_Nodes[i_next].Position;
                glm::vec3 p0, p3;

                if (m_Loop) {
                    p0 = m_Nodes[(i + n - 1) % n].Position;
                    p3 = m_Nodes[(i + 2) % n].Position;
                } else {
                    p0 = (i == 0) ? (2.0f * p1 - p2) : m_Nodes[i - 1].Position;
                    p3 = (i + 2 >= n) ? (2.0f * p2 - p1) : m_Nodes[i + 2].Position;
                }

                float t2 = t * t;
                float t3 = t2 * t;

                return 0.5f * (
                    (2.0f * p1) +
                    (-p0 + p2) * t +
                    (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                    (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
                );
            }
        }
    }

    glm::vec3 Curve3D::EvaluateTangent(u32 segmentIndex, float t) const {
        if (m_Nodes.empty()) return glm::vec3(0.0f, 0.0f, 1.0f);
        if (m_Nodes.size() == 1) return glm::vec3(0.0f, 0.0f, 1.0f);

        u32 n = static_cast<u32>(m_Nodes.size());
        u32 i = segmentIndex;
        u32 i_next = (i + 1) % n;

        t = std::clamp(t, 0.0f, 1.0f);
        glm::vec3 d(0.0f);

        switch (m_Type) {
            case SplineType::Linear: {
                d = m_Nodes[i_next].Position - m_Nodes[i].Position;
                break;
            }
            case SplineType::CubicBezier: {
                glm::vec3 p0 = m_Nodes[i].Position;
                glm::vec3 p1 = p0 + m_Nodes[i].HandleOut;
                glm::vec3 p3 = m_Nodes[i_next].Position;
                glm::vec3 p2 = p3 + m_Nodes[i_next].HandleIn;

                float u = 1.0f - t;
                d = 3.0f * (u * u) * (p1 - p0) +
                    6.0f * u * t * (p2 - p1) +
                    3.0f * (t * t) * (p3 - p2);
                break;
            }
            case SplineType::CatmullRom:
            default: {
                glm::vec3 p1 = m_Nodes[i].Position;
                glm::vec3 p2 = m_Nodes[i_next].Position;
                glm::vec3 p0, p3;

                if (m_Loop) {
                    p0 = m_Nodes[(i + n - 1) % n].Position;
                    p3 = m_Nodes[(i + 2) % n].Position;
                } else {
                    p0 = (i == 0) ? (2.0f * p1 - p2) : m_Nodes[i - 1].Position;
                    p3 = (i + 2 >= n) ? (2.0f * p2 - p1) : m_Nodes[i + 2].Position;
                }

                float t2 = t * t;
                d = 0.5f * (
                    (-p0 + p2) +
                    2.0f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t +
                    3.0f * (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t2
                );
                break;
            }
        }

        if (glm::length(d) > 1e-5f) {
            return glm::normalize(d);
        }
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }

    float Curve3D::EvaluateRoll(u32 segmentIndex, float t) const {
        if (m_Nodes.empty()) return 0.0f;
        u32 n = static_cast<u32>(m_Nodes.size());
        u32 i = segmentIndex;
        u32 i_next = (i + 1) % n;

        t = std::clamp(t, 0.0f, 1.0f);
        // Smooth Hermite interpolation for banking angle
        float smoothT = t * t * (3.0f - 2.0f * t);
        return glm::mix(m_Nodes[i].Roll, m_Nodes[i_next].Roll, smoothT);
    }

    float Curve3D::EvaluateScale(u32 segmentIndex, float t) const {
        if (m_Nodes.empty()) return 1.0f;
        u32 n = static_cast<u32>(m_Nodes.size());
        u32 i = segmentIndex;
        u32 i_next = (i + 1) % n;

        t = std::clamp(t, 0.0f, 1.0f);
        float smoothT = t * t * (3.0f - 2.0f * t);
        return glm::mix(m_Nodes[i].Scale, m_Nodes[i_next].Scale, smoothT);
    }

}
