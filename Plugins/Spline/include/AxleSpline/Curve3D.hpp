#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>

namespace Axle::Spline {

    enum class SplineType {
        CatmullRom = 0,
        CubicBezier = 1,
        Linear = 2
    };

    struct SplineNode {
        glm::vec3 Position{ 0.0f };
        glm::vec3 HandleIn{ -2.0f, 0.0f, 0.0f };  // Local offset for Bézier tangents
        glm::vec3 HandleOut{ 2.0f, 0.0f, 0.0f };  // Local offset for Bézier tangents
        float Roll = 0.0f;                        // Banking / roll angle in degrees
        float Scale = 1.0f;                       // Width / cross-section scale factor

        SplineNode() = default;
        SplineNode(const glm::vec3& position, float roll = 0.0f, float scale = 1.0f)
            : Position(position), Roll(roll), Scale(scale) {}
        SplineNode(const glm::vec3& position, const glm::vec3& handleIn, const glm::vec3& handleOut, float roll = 0.0f, float scale = 1.0f)
            : Position(position), HandleIn(handleIn), HandleOut(handleOut), Roll(roll), Scale(scale) {}
    };

    struct SplineFrame {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Tangent{ 0.0f, 0.0f, 1.0f };   // Forward direction along curve
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };    // Up direction (incorporates banking)
        glm::vec3 Binormal{ 1.0f, 0.0f, 0.0f };  // Right / lateral direction
        float Roll = 0.0f;                       // Interpolated roll in degrees
        float Scale = 1.0f;                      // Interpolated scale
        float Distance = 0.0f;                   // Traveled distance from start
    };

    struct ArcLengthSample {
        float Distance = 0.0f;
        u32 SegmentIndex = 0;
        float SegmentT = 0.0f;
    };

    class Curve3D {
    public:
        Curve3D() = default;
        Curve3D(SplineType type, bool loop = false);

        // Node manipulation
        void AddNode(const SplineNode& node);
        void InsertNode(size_t index, const SplineNode& node);
        void RemoveNode(size_t index);
        void ClearNodes();

        std::vector<SplineNode>& GetNodes() { return m_Nodes; }
        const std::vector<SplineNode>& GetNodes() const { return m_Nodes; }
        size_t GetNodeCount() const { return m_Nodes.size(); }

        void SetSplineType(SplineType type);
        SplineType GetSplineType() const { return m_Type; }

        void SetLoop(bool loop);
        bool IsLoop() const { return m_Loop; }

        void SetSubdivisionsPerSegment(u32 count);
        u32 GetSubdivisionsPerSegment() const { return m_SubdivisionsPerSegment; }

        // Cache rebuilding (Call whenever nodes or properties are mutated)
        void RebuildCache();
        bool IsCacheDirty() const { return m_CacheDirty; }

        // Length and queries
        float GetTotalLength() const;

        // Metric speed evaluation (Uniform distance s in [0, TotalLength])
        glm::vec3 SamplePosition(float distance) const;
        glm::vec3 SampleTangent(float distance) const;
        float SampleRoll(float distance) const;
        float SampleScale(float distance) const;

        // Full orthonormal frame with Rotation-Minimizing Frame + Banking
        SplineFrame SampleFrame(float distance) const;

        // Raw segment evaluation (t in [0, 1] for segment segmentIndex)
        glm::vec3 EvaluatePosition(u32 segmentIndex, float t) const;
        glm::vec3 EvaluateTangent(u32 segmentIndex, float t) const;
        float EvaluateRoll(u32 segmentIndex, float t) const;
        float EvaluateScale(u32 segmentIndex, float t) const;

        // Number of active curve segments
        u32 GetSegmentCount() const;

    private:
        void GetSegmentAndLocalT(float distance, u32& outSegment, float& outT) const;
        void ComputeRotationMinimizingFrames();

    private:
        SplineType m_Type = SplineType::CatmullRom;
        bool m_Loop = false;
        u32 m_SubdivisionsPerSegment = 32;
        std::vector<SplineNode> m_Nodes;

        // Cached arc-length reparameterization table
        std::vector<ArcLengthSample> m_ArcLengthLUT;
        float m_TotalLength = 0.0f;

        // Cached Rotation-Minimizing Frames per LUT sample
        std::vector<glm::vec3> m_CachedNormals;
        std::vector<glm::vec3> m_CachedBinormals;

        mutable bool m_CacheDirty = true;
    };

}
