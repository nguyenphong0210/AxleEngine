#include "RenderTestCommon.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr double Pi = 3.14159265358979323846;

glm::dvec3 Direction(double degrees) {
    const double radians = degrees * Pi / 180.0;
    return glm::normalize(glm::dvec3(std::sin(radians), 0.0, std::cos(radians)));
}

double SmithLambda(double nDotW, double alpha) {
    if (nDotW <= 0.0) return 0.0;
    const double nDotW2 = nDotW * nDotW;
    const double tan2 = std::max((1.0 - nDotW2) / nDotW2, 0.0);
    return 0.5 * (std::sqrt(1.0 + alpha * alpha * tan2) - 1.0);
}

// Independent direct-light reference. It intentionally does not call the
// production shader helpers or the environment LUT baker.
double EvaluateSpecular(double roughness, double viewDegrees, double mirrorOffsetDegrees) {
    const glm::dvec3 normal(0.0, 0.0, 1.0);
    const glm::dvec3 view = Direction(viewDegrees);
    const glm::dvec3 light = Direction(-viewDegrees + mirrorOffsetDegrees);
    const glm::dvec3 half = glm::normalize(view + light);
    const double nDotV = std::max(glm::dot(normal, view), 0.0);
    const double nDotL = std::max(glm::dot(normal, light), 0.0);
    const double nDotH = std::max(glm::dot(normal, half), 0.0);
    const double vDotH = std::max(glm::dot(view, half), 0.0);
    if (nDotV <= 0.0 || nDotL <= 0.0 || !std::isfinite(nDotV) || !std::isfinite(nDotL)) return 0.0;

    const double alpha = std::max(roughness * roughness, 1e-4);
    const double alpha2 = alpha * alpha;
    const double denominator = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    const double distribution = alpha2 / (Pi * denominator * denominator);
    const double geometry = 1.0 / (1.0 + SmithLambda(nDotV, alpha) + SmithLambda(nDotL, alpha));
    const double fresnel = 0.04 + (1.0 - 0.04) * std::pow(1.0 - vDotH, 5.0);
    return distribution * geometry * fresnel / (4.0 * nDotV * nDotL) * nDotL;
}

double HalfPowerWidth(double roughness) {
    constexpr double viewDegrees = 35.0;
    const double peak = EvaluateSpecular(roughness, viewDegrees, 0.0);
    for (int sample = 1; sample <= 360; ++sample) {
        const double offset = sample * 0.25;
        if (EvaluateSpecular(roughness, viewDegrees, offset) <= peak * 0.5) return offset;
    }
    return 90.0;
}
}

bool RunM15RoughnessSweepReferenceUnitTests() {
    std::cout << "  [M15RoughnessSweepReference] Running independent roughness/view/light-angle checks...\n";

    std::ifstream shader(AXLE_FORWARD_PBR_SHADER_PATH);
    const std::string source{std::istreambuf_iterator<char>(shader), std::istreambuf_iterator<char>()};
    RENDER_CHECK(!source.empty(), "ForwardPBR shader source is available");
    RENDER_CHECK(source.find("float roughness = clamp(u_Params.z, 0.04, 1.0);") != std::string::npos &&
                 source.find("roughness = clamp(roughness, 0.04, 1.0);") != std::string::npos &&
                 source.find("roughness = clamp(orm.g * roughness, 0.04, 1.0);") != std::string::npos,
        "all roughness input paths clamp to the supported 0.04..1.0 range");

    const std::vector<double> roughnessValues{0.04, 0.12, 0.25, 0.50, 0.80};
    double previousPeak = std::numeric_limits<double>::infinity();
    double previousWidth = 0.0;
    for (const double roughness : roughnessValues) {
        const double peak = EvaluateSpecular(roughness, 35.0, 0.0);
        const double width = HalfPowerWidth(roughness);
        std::cout << "  [M15RoughnessSweepReference] roughness=" << roughness
                  << " peak=" << peak << " half-power-width-deg=" << width << "\n";
        RENDER_CHECK(std::isfinite(peak) && peak > 0.0 && std::isfinite(width) && width > 0.0,
            "roughness samples produce finite non-zero mirror response");
        RENDER_CHECK(peak <= previousPeak + 1e-9 && width + 1e-9 >= previousWidth,
            "roughness lowers the peak and broadens the specular lobe monotonically");
        previousPeak = peak;
        previousWidth = width;
    }

    // Sweep view and light angles around the mirror direction. The center must
    // remain the local maximum and every valid sample must stay finite.
    for (const double viewDegrees : {0.0, 15.0, 30.0, 45.0}) {
        std::vector<double> samples;
        for (const double offset : {-20.0, -10.0, 0.0, 10.0, 20.0}) {
            const double value = EvaluateSpecular(0.50, viewDegrees, offset);
            RENDER_CHECK(std::isfinite(value) && value >= 0.0,
                "view/light-angle sweep remains finite and non-negative");
            samples.push_back(value);
        }
        RENDER_CHECK(samples[2] >= samples[0] && samples[2] >= samples[1] &&
                     samples[2] >= samples[3] && samples[2] >= samples[4],
            "mirror-aligned light direction remains the sweep maximum");
    }

    // A denser angular sweep guards against discontinuous jumps in the valid
    // hemisphere at the roughness used by the Editor fixture.
    double previous = EvaluateSpecular(0.50, 30.0, -20.0);
    for (int step = 1; step <= 80; ++step) {
        const double offset = -20.0 + step * 0.5;
        const double current = EvaluateSpecular(0.50, 30.0, offset);
        RENDER_CHECK(std::isfinite(current) && std::abs(current - previous) < 0.02,
            "light-angle sweep has no finite-range discontinuity");
        previous = current;
    }

    std::cout << "  [M15RoughnessSweepReference] Roughness/view/light-angle checks passed.\n";
    return true;
}
