#pragma once
#include <Axle/AnimationClip.hpp>
#include <Axle/AnimationPose.hpp>
#include <string>
#include <vector>
#include <memory>

namespace Axle {

    struct BlendSpacePoint1D {
        float Value = 0.0f; // Parameter coordinate (e.g. Speed 0.0, 2.0, 5.0)
        std::shared_ptr<AnimationClip> Clip;
    };

    class BlendSpace1D {
    public:
        BlendSpace1D() = default;
        explicit BlendSpace1D(std::string name) : m_Name(std::move(name)) {}

        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }

        void AddPoint(float value, const std::shared_ptr<AnimationClip>& clip);
        void Clear();

        const std::vector<BlendSpacePoint1D>& GetPoints() const { return m_Points; }
        size_t GetPointCount() const { return m_Points.size(); }

        // Compute effective blended cycle duration at parameter
        float GetBlendedDuration(float parameter) const;

        // Sample blended pose with phase-matched time synchronization
        void SamplePose(float parameter, float time, const Skeleton& skeleton, AnimationPose& outPose, bool loop = true) const;

    private:
        std::string m_Name;
        std::vector<BlendSpacePoint1D> m_Points;
    };

}
