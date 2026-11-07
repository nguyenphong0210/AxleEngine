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
    inline void ConfigureFailureReporting() {
#if defined(_MSC_VER)
        // Keep assertions active and route failures to the test log, avoiding
        // modal CRT dialogs that would hide a failure behind a test timeout.
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0, _CALL_REPORTFAULT);
#endif
    }

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
