#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <vector>
#include <random>
#include <algorithm>

namespace Axle {

    enum class ParticleBlendMode : uint8_t {
        Additive = 0,
        AlphaBlend,
        Multiply
    };

    enum class ParticleSimulationSpace : uint8_t {
        World = 0,
        Local
    };

    enum class ParticleOrientation : uint8_t {
        CameraFacing = 0,
        AxialY,
        VelocityAligned
    };

    enum class ParticleEmitterShapeType : uint8_t {
        Point = 0,
        Sphere,
        Hemisphere,
        Box,
        Cone,
        Line
    };

    struct FloatRange {
        float Min = 0.0f;
        float Max = 0.0f;

        FloatRange() = default;
        FloatRange(float val) : Min(val), Max(val) {}
        FloatRange(float minVal, float maxVal) : Min(minVal), Max(maxVal) {}

        float Sample() const;
    };

    struct ColorGradient {
        glm::vec4 StartColor = glm::vec4(1.0f);
        glm::vec4 EndColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

        ColorGradient() = default;
        ColorGradient(const glm::vec4& start, const glm::vec4& end)
            : StartColor(start), EndColor(end) {}

        glm::vec4 Evaluate(float t) const {
            t = glm::clamp(t, 0.0f, 1.0f);
            return glm::mix(StartColor, EndColor, t);
        }
    };

    class VFXRandom {
    public:
        static void SetSeed(uint32_t seed);
        static float Float(float min = 0.0f, float max = 1.0f);
        static glm::vec3 UnitVector();
        static glm::vec3 InUnitSphere();
        static glm::vec3 InUnitBox(const glm::vec3& halfExtents);
    };

}
