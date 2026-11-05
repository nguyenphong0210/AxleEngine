#pragma once

#include <Axle/RHI/RHICommon.hpp>
#include <glm/glm.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace Axle::RenderTests {

    inline bool Check(bool condition, const char* description, const char* file, int line) {
        if (!condition) {
            std::cerr << "\n[RENDER TEST FAIL] " << description << "\n  Source: " << file << ":" << line << "\n";
            return false;
        }
        return true;
    }

    inline bool Near(float a, float b, float epsilon = 1e-4f) {
        return std::abs(a - b) <= epsilon;
    }

    inline bool Near(const glm::vec2& a, const glm::vec2& b, float epsilon = 1e-4f) {
        return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon);
    }

    inline bool Near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-4f) {
        return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon);
    }

    inline bool Near(const glm::vec4& a, const glm::vec4& b, float epsilon = 1e-4f) {
        return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon) && Near(a.w, b.w, epsilon);
    }

} // namespace Axle::RenderTests

#define RENDER_CHECK(condition, desc) \
    do { \
        if (!Axle::RenderTests::Check((condition), (desc), __FILE__, __LINE__)) { \
            return false; \
        } \
    } while (0)
