#pragma once
#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>
#include <cmath>

#define MAT_TEST_CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "  [CHECK FAILED] " << message \
                      << "\n    Expression: " #condition \
                      << "\n    Location:   " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

#define MAT_TEST_APPROX_EQUAL(a, b, eps, message) \
    do { \
        if (std::abs((a) - (b)) > (eps)) { \
            std::cerr << "  [APPROX FAILED] " << message \
                      << "\n    Actual:   " << (a) \
                      << "\n    Expected: " << (b) \
                      << "\n    Location: " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

namespace Axle::MaterialTests {

    struct ScopedTempDir {
        std::filesystem::path Path;
        ScopedTempDir(const std::string& prefix = "axle_mat_unit") {
            auto base = std::filesystem::temp_directory_path();
            auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            Path = base / (prefix + "_" + std::to_string(now));
            std::error_code ec;
            std::filesystem::create_directories(Path, ec);
        }
        ~ScopedTempDir() {
            std::error_code ec;
            std::filesystem::remove_all(Path, ec);
        }
    };

} // namespace Axle::MaterialTests

