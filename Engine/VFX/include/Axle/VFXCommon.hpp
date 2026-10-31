#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <cmath>

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
        Line,
        Circle,
        MeshSurface
    };

    struct FloatRange {
        float Min = 0.0f;
        float Max = 0.0f;

        FloatRange() = default;
        FloatRange(float val) : Min(val), Max(val) {}
        FloatRange(float minVal, float maxVal) : Min(minVal), Max(maxVal) {}

        float Sample() const;
    };

    // =========================================================================
    // Burst Emissions (Periodic or One-shot burst particles)
    // =========================================================================
    struct ParticleBurst {
        float Time = 0.0f;           // Seconds into playback to trigger
        uint32_t MinCount = 30;      // Minimum particle count
        uint32_t MaxCount = 30;      // Maximum particle count
        uint32_t Cycles = 1;         // Repeat count (0 = infinite while playing)
        float Interval = 0.0f;       // Interval between cycles in seconds
        float Probability = 1.0f;    // 0.0 -> 1.0 chance of firing

        // Runtime simulation state
        uint32_t CompletedCycles = 0;
        float NextTriggerTime = 0.0f;

        ParticleBurst() : Time(0.0f), MinCount(30), MaxCount(30), Cycles(1), Interval(0.0f), Probability(1.0f), CompletedCycles(0), NextTriggerTime(0.0f) {}
        ParticleBurst(float time, uint32_t minC, uint32_t maxC, uint32_t cycles = 1, float interval = 0.0f, float prob = 1.0f)
            : Time(time), MinCount(minC), MaxCount(maxC), Cycles(cycles), Interval(interval), Probability(prob), CompletedCycles(0), NextTriggerTime(time) {}

        void Reset() {
            CompletedCycles = 0;
            NextTriggerTime = Time;
        }

        uint32_t SampleCount() const {
            if (MinCount >= MaxCount) return MinCount;
            return MinCount + (std::rand() % (MaxCount - MinCount + 1));
        }
    };

    // =========================================================================
    // Cubic Bezier Curve (Size, Speed, Alpha over Lifetime)
    // =========================================================================
    enum class CurveType : uint8_t {
        Linear = 0,
        CubicBezier,
        EaseIn,
        EaseOut,
        EaseInOut,
        Bell,
        Constant
    };

    struct CurveKeyframe {
        float Time = 0.0f;
        float Value = 1.0f;
    };

    struct FloatCurve {
        CurveType Type = CurveType::Linear;
        glm::vec2 P0{ 0.0f, 1.0f }; // Start point
        glm::vec2 P1{ 0.33f, 1.0f };// Control point 1
        glm::vec2 P2{ 0.66f, 0.5f };// Control point 2
        glm::vec2 P3{ 1.0f, 0.0f }; // End point
        std::vector<CurveKeyframe> Keyframes; // Optional multi-key

        FloatCurve() {
            SetLinear(1.0f, 0.0f);
        }
        FloatCurve(float startVal, float endVal) {
            SetLinear(startVal, endVal);
        }

        void SetConstant(float val) {
            Type = CurveType::Constant;
            P0 = { 0.0f, val }; P1 = { 0.33f, val }; P2 = { 0.66f, val }; P3 = { 1.0f, val };
        }
        void SetLinear(float start, float end) {
            Type = CurveType::Linear;
            P0 = { 0.0f, start }; P1 = { 0.33f, glm::mix(start, end, 0.33f) };
            P2 = { 0.66f, glm::mix(start, end, 0.66f) }; P3 = { 1.0f, end };
        }
        void SetEaseIn(float start, float end) {
            Type = CurveType::EaseIn;
            P0 = { 0.0f, start }; P1 = { 0.5f, start }; P2 = { 0.8f, glm::mix(start, end, 0.3f) }; P3 = { 1.0f, end };
        }
        void SetEaseOut(float start, float end) {
            Type = CurveType::EaseOut;
            P0 = { 0.0f, start }; P1 = { 0.2f, glm::mix(start, end, 0.7f) }; P2 = { 0.5f, end }; P3 = { 1.0f, end };
        }
        void SetEaseInOut(float start, float end) {
            Type = CurveType::EaseInOut;
            P0 = { 0.0f, start }; P1 = { 0.4f, start }; P2 = { 0.6f, end }; P3 = { 1.0f, end };
        }
        void SetBell(float start, float peak, float end) {
            Type = CurveType::Bell;
            P0 = { 0.0f, start }; P1 = { 0.35f, peak }; P2 = { 0.65f, peak }; P3 = { 1.0f, end };
        }
        void SetBell(float peak = 1.0f) {
            SetBell(0.0f, peak, 0.0f);
        }
        void SetCubicBezier(float p0y, float p1y, float p2y, float p3y) {
            Type = CurveType::CubicBezier;
            P0 = { 0.0f, p0y };
            P1 = { 0.33f, p1y };
            P2 = { 0.66f, p2y };
            P3 = { 1.0f, p3y };
        }
        void SetCubicBezier(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3) {
            Type = CurveType::CubicBezier;
            P0 = p0; P1 = p1; P2 = p2; P3 = p3;
        }

        float Evaluate(float t) const {
            t = std::clamp(t, 0.0f, 1.0f);
            if (Type == CurveType::Constant) return P0.y;
            if (!Keyframes.empty()) {
                if (t <= Keyframes.front().Time) return Keyframes.front().Value;
                if (t >= Keyframes.back().Time) return Keyframes.back().Value;
                for (size_t i = 0; i < Keyframes.size() - 1; ++i) {
                    if (t >= Keyframes[i].Time && t <= Keyframes[i + 1].Time) {
                        float span = Keyframes[i + 1].Time - Keyframes[i].Time;
                        if (span <= 1e-5f) return Keyframes[i].Value;
                        float u = (t - Keyframes[i].Time) / span;
                        return glm::mix(Keyframes[i].Value, Keyframes[i + 1].Value, u);
                    }
                }
            }
            // Cubic Bezier evaluation for y with parameter t
            float u = 1.0f - t;
            float tt = t * t;
            float uu = u * u;
            float uuu = uu * u;
            float ttt = tt * t;
            return (uuu * P0.y) + (3.0f * uu * t * P1.y) + (3.0f * u * tt * P2.y) + (ttt * P3.y);
        }
    };

    // =========================================================================
    // Multi-Key Color Gradient (RGB & Alpha Keyframes over Lifetime)
    // =========================================================================
    struct GradientColorKey {
        float Time = 0.0f;
        glm::vec3 Color{ 1.0f };
    };

    struct GradientAlphaKey {
        float Time = 0.0f;
        float Alpha = 1.0f;
    };

    struct ColorGradient {
        glm::vec4 StartColor = glm::vec4(1.0f);
        glm::vec4 EndColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

        std::vector<GradientColorKey> ColorKeys;
        std::vector<GradientAlphaKey> AlphaKeys;

        ColorGradient() {
            SetSimple(glm::vec4(1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
        }
        ColorGradient(const glm::vec4& start, const glm::vec4& end) {
            SetSimple(start, end);
        }

        void SetSimple(const glm::vec4& start, const glm::vec4& end) {
            StartColor = start;
            EndColor = end;
            ColorKeys = { { 0.0f, glm::vec3(start) }, { 1.0f, glm::vec3(end) } };
            AlphaKeys = { { 0.0f, start.a }, { 1.0f, end.a } };
        }

        void AddColorKey(float time, const glm::vec3& color) {
            ColorKeys.push_back({ std::clamp(time, 0.0f, 1.0f), color });
            std::sort(ColorKeys.begin(), ColorKeys.end(), [](const auto& a, const auto& b) { return a.Time < b.Time; });
        }

        void AddAlphaKey(float time, float alpha) {
            AlphaKeys.push_back({ std::clamp(time, 0.0f, 1.0f), std::clamp(alpha, 0.0f, 1.0f) });
            std::sort(AlphaKeys.begin(), AlphaKeys.end(), [](const auto& a, const auto& b) { return a.Time < b.Time; });
        }

        void SetFlamePreset() {
            ColorKeys = {
                { 0.0f, glm::vec3(1.0f, 0.95f, 0.4f) }, // Yellow
                { 0.35f, glm::vec3(1.0f, 0.45f, 0.05f) },// Orange
                { 0.75f, glm::vec3(0.85f, 0.1f, 0.02f) },// Red
                { 1.0f, glm::vec3(0.15f, 0.15f, 0.15f) } // Smoke dark
            };
            AlphaKeys = {
                { 0.0f, 0.0f },
                { 0.1f, 1.0f },
                { 0.7f, 0.8f },
                { 1.0f, 0.0f }
            };
            StartColor = glm::vec4(ColorKeys.front().Color, AlphaKeys.front().Alpha);
            EndColor = glm::vec4(ColorKeys.back().Color, AlphaKeys.back().Alpha);
        }

        void SetSmokePreset() {
            ColorKeys = {
                { 0.0f, glm::vec3(0.35f, 0.35f, 0.35f) },
                { 0.5f, glm::vec3(0.55f, 0.55f, 0.55f) },
                { 1.0f, glm::vec3(0.80f, 0.80f, 0.80f) }
            };
            AlphaKeys = {
                { 0.0f, 0.0f },
                { 0.15f, 0.75f },
                { 0.8f, 0.45f },
                { 1.0f, 0.0f }
            };
            StartColor = glm::vec4(ColorKeys.front().Color, AlphaKeys.front().Alpha);
            EndColor = glm::vec4(ColorKeys.back().Color, AlphaKeys.back().Alpha);
        }

        void SetSparksPreset() {
            ColorKeys = {
                { 0.0f, glm::vec3(1.0f, 1.0f, 0.9f) },
                { 0.4f, glm::vec3(1.0f, 0.8f, 0.2f) },
                { 0.8f, glm::vec3(1.0f, 0.3f, 0.05f) },
                { 1.0f, glm::vec3(0.3f, 0.05f, 0.0f) }
            };
            AlphaKeys = {
                { 0.0f, 1.0f },
                { 0.8f, 0.9f },
                { 1.0f, 0.0f }
            };
            StartColor = glm::vec4(ColorKeys.front().Color, AlphaKeys.front().Alpha);
            EndColor = glm::vec4(ColorKeys.back().Color, AlphaKeys.back().Alpha);
        }

        glm::vec4 Evaluate(float t) const {
            t = std::clamp(t, 0.0f, 1.0f);
            glm::vec3 rgb = glm::vec3(1.0f);
            float a = 1.0f;

            if (ColorKeys.empty()) {
                rgb = glm::mix(glm::vec3(StartColor), glm::vec3(EndColor), t);
            } else if (ColorKeys.size() == 1 || t <= ColorKeys.front().Time) {
                rgb = ColorKeys.front().Color;
            } else if (t >= ColorKeys.back().Time) {
                rgb = ColorKeys.back().Color;
            } else {
                for (size_t i = 0; i < ColorKeys.size() - 1; ++i) {
                    if (t >= ColorKeys[i].Time && t <= ColorKeys[i + 1].Time) {
                        float span = ColorKeys[i + 1].Time - ColorKeys[i].Time;
                        float u = (span > 1e-5f) ? (t - ColorKeys[i].Time) / span : 0.0f;
                        rgb = glm::mix(ColorKeys[i].Color, ColorKeys[i + 1].Color, u);
                        break;
                    }
                }
            }

            if (AlphaKeys.empty()) {
                a = glm::mix(StartColor.a, EndColor.a, t);
            } else if (AlphaKeys.size() == 1 || t <= AlphaKeys.front().Time) {
                a = AlphaKeys.front().Alpha;
            } else if (t >= AlphaKeys.back().Time) {
                a = AlphaKeys.back().Alpha;
            } else {
                for (size_t i = 0; i < AlphaKeys.size() - 1; ++i) {
                    if (t >= AlphaKeys[i].Time && t <= AlphaKeys[i + 1].Time) {
                        float span = AlphaKeys[i + 1].Time - AlphaKeys[i].Time;
                        float u = (span > 1e-5f) ? (t - AlphaKeys[i].Time) / span : 0.0f;
                        a = glm::mix(AlphaKeys[i].Alpha, AlphaKeys[i + 1].Alpha, u);
                        break;
                    }
                }
            }

            return glm::vec4(rgb, a);
        }
    };

    // =========================================================================
    // Sub-Emitter Triggering (Child particle systems on Birth, Collision, Death)
    // =========================================================================
    enum class SubEmitterTrigger : uint8_t {
        Birth = 0,
        Collision,
        Death
    };

    struct SubEmitterConfig {
        SubEmitterTrigger Trigger = SubEmitterTrigger::Collision;
        std::string TargetEmitterName;
        uint32_t EmitCount = 10;
        float Probability = 1.0f;
        bool InheritColor = true;
        bool InheritVelocity = false;

        SubEmitterConfig() = default;
        SubEmitterConfig(SubEmitterTrigger trig, const std::string& target, uint32_t count = 10, float prob = 1.0f)
            : Trigger(trig), TargetEmitterName(target), EmitCount(count), Probability(prob) {}
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
