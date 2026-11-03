#pragma once
#include <cmath>
#include <iostream>
#include <glm/glm.hpp>

namespace Axle::AnimationTests {
    inline bool Check(bool condition, const char* message, const char* file, int line) {
        if (!condition) std::cerr << file << ':' << line << " [FAIL] " << message << '\n';
        return condition;
    }
    inline bool Near(float a, float b, float epsilon = 0.0001f) {
        return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= epsilon;
    }
    inline bool Near(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.0001f) {
        return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon);
    }
    inline bool Near(const glm::mat4& a, const glm::mat4& b, float epsilon = 0.0001f) {
        for (int col = 0; col < 4; ++col) for (int row = 0; row < 4; ++row)
            if (!Near(a[col][row], b[col][row], epsilon)) return false;
        return true;
    }
}
#define ANIMATION_CHECK(condition, message) \
    do { if (!Axle::AnimationTests::Check(bool(condition), message, __FILE__, __LINE__)) return false; } while (false)
