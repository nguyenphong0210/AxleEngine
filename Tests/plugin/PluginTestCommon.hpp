#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace Axle::PluginTests {

    inline bool Check(bool condition, const char* description, const char* file, int line) {
        if (!condition) {
            std::cerr << "\n[PLUGIN TEST FAIL] " << description << "\n  Source: " << file << ":" << line << "\n";
            return false;
        }
        return true;
    }

    inline bool Near(float a, float b, float epsilon = 1e-4f) {
        return std::abs(a - b) <= epsilon;
    }

} // namespace Axle::PluginTests

#define PLUGIN_CHECK(condition, desc) \
    do { \
        if (!Axle::PluginTests::Check((condition), (desc), __FILE__, __LINE__)) { \
            return false; \
        } \
    } while (0)
