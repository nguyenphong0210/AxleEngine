#include "RenderTestCommon.hpp"

#include <Axle/RenderCapabilities.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
constexpr double Pi = 3.14159265358979323846;

bool IsFinite(const glm::dvec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

glm::dvec3 SafeNormalize(const glm::dvec3& value, bool& valid) {
    const double lengthSquared = glm::dot(value, value);
    valid = IsFinite(value) && std::isfinite(lengthSquared) && lengthSquared > 1e-16;
    return valid ? value / std::sqrt(lengthSquared) : glm::dvec3(0.0);
}

double SmithLambda(double nDotW, double alpha) {
    if (nDotW <= 0.0) return 0.0;
    const double nDotW2 = nDotW * nDotW;
    const double tan2 = std::max((1.0 - nDotW2) / nDotW2, 0.0);
    return 0.5 * (std::sqrt(1.0 + alpha * alpha * tan2) - 1.0);
}

// Independent CPU reference for a single direct Cook-Torrance sample. It
// intentionally does not call production shader helpers or renderer code.
double EvaluateDirectReference(const glm::dvec3& normalInput,
                               const glm::dvec3& viewInput,
                               const glm::dvec3& lightInput,
                               double roughness,
                               double metallic) {
    bool normalValid = false;
    bool viewValid = false;
    bool lightValid = false;
    const glm::dvec3 normal = SafeNormalize(normalInput, normalValid);
    const glm::dvec3 view = SafeNormalize(viewInput, viewValid);
    const glm::dvec3 light = SafeNormalize(lightInput, lightValid);
    if (!normalValid || !viewValid || !lightValid || !std::isfinite(roughness) || !std::isfinite(metallic)) return 0.0;

    const double nDotV = glm::dot(normal, view);
    const double nDotL = glm::dot(normal, light);
    if (!std::isfinite(nDotV) || !std::isfinite(nDotL) || nDotV <= 0.0 || nDotL <= 0.0) return 0.0;

    bool halfValid = false;
    const glm::dvec3 half = SafeNormalize(view + light, halfValid);
    if (!halfValid) return 0.0;

    const double nDotH = std::max(glm::dot(normal, half), 0.0);
    const double vDotH = std::max(glm::dot(view, half), 0.0);
    const double alpha = std::max(std::clamp(roughness, 0.04, 1.0) * std::clamp(roughness, 0.04, 1.0), 1e-4);
    const double alpha2 = alpha * alpha;
    const double denominator = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    const double distribution = alpha2 / (Pi * denominator * denominator);
    const double geometry = 1.0 / (1.0 + SmithLambda(nDotV, alpha) + SmithLambda(nDotL, alpha));
    const double fresnel = 0.04 + 0.96 * std::pow(1.0 - vDotH, 5.0);
    const double value = distribution * geometry * fresnel / (4.0 * nDotV * nDotL) * nDotL * (1.0 - 0.5 * std::clamp(metallic, 0.0, 1.0));
    return std::isfinite(value) && value >= 0.0 ? value : 0.0;
}

std::string ReadText(const char* path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

bool RunM15DirectLightEdgeReferenceUnitTests() {
    std::cout << "  [M15DirectLightEdgeReference] Running zero/behind-surface finite cases...\n";

    const std::string shader = ReadText(AXLE_FORWARD_PBR_SHADER_PATH);
    RENDER_CHECK(!shader.empty(), "ForwardPBR shader source is available");
    RENDER_CHECK(shader.find("if (dot(N, V) < 0.0)") != std::string::npos &&
                 shader.find("float NdotV = max(dot(N, V), 0.001);") != std::string::npos,
        "the shader reorients a back-facing normal and keeps NdotV finite");
    RENDER_CHECK(shader.find("if (NdotL <= 0.0 && NdotL_Diff <= 0.0) continue;") != std::string::npos &&
                 shader.find("if (NdotL <= 0.0) continue;") != std::string::npos,
        "local-light paths reject behind-surface samples before normalization");
    RENDER_CHECK(shader.find("BRDF_SPECULAR_DENOMINATOR_SCALE * NdotV * max(NdotL, 0.001) + 0.0001") != std::string::npos,
        "direct specular denominator has an explicit finite floor");

    const glm::dvec3 normal(0.0, 0.0, 1.0);
    const std::vector<glm::dvec3> views{
        {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0}, {1.0, 0.0, 0.0}, {1e-8, 0.0, 1.0}
    };
    const std::vector<glm::dvec3> lights{
        {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0}, {-1.0, 0.0, 0.0}, {-1e-8, 0.0, 1.0}
    };

    for (const double roughness : {0.04, 0.25, 0.80, 1.0}) {
        for (const double metallic : {0.0, 1.0}) {
            for (const auto& view : views) {
                for (const auto& light : lights) {
                    const double value = EvaluateDirectReference(normal, view, light, roughness, metallic);
                    RENDER_CHECK(std::isfinite(value) && value >= 0.0,
                        "zero, tangent, and behind-surface samples remain finite and non-negative");
                }
            }
            const double front = EvaluateDirectReference(normal, {0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, roughness, metallic);
            RENDER_CHECK(std::isfinite(front) && front > 0.0, "front-facing direct sample remains non-zero");
            const double zeroView = EvaluateDirectReference(normal, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, roughness, metallic);
            const double behindLight = EvaluateDirectReference(normal, {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0}, roughness, metallic);
            RENDER_CHECK(zeroView == 0.0 && behindLight == 0.0,
                "zero and behind-surface inputs return the bounded zero contribution");
        }
    }

    std::cout << "  [M15DirectLightEdgeReference] Zero/behind-surface checks passed.\n";
    return true;
}

bool RunM15DirectLightFallbackReferenceUnitTests() {
    std::cout << "  [M15DirectLightFallbackReference] Running capability-tier fallback checks...\n";

    const auto d3d12 = Axle::QueryRendererCapabilities(Axle::RendererBackendType::ModernRHI_D3D12);
    const auto vulkan = Axle::QueryRendererCapabilities(Axle::RendererBackendType::ModernRHI_Vulkan);
    const auto gl = Axle::QueryRendererCapabilities(Axle::RendererBackendType::OpenGL_Compatibility);
    const auto none = Axle::QueryRendererCapabilities(Axle::RendererBackendType::None);
    RENDER_CHECK(d3d12.SupportsLocalLightShadows && vulkan.SupportsLocalLightShadows,
        "native D3D12/Vulkan tiers advertise local-light shadow support");
    RENDER_CHECK(!gl.SupportsLocalLightShadows && !none.SupportsLocalLightShadows,
        "OpenGL compatibility and no-backend tiers do not advertise local-light shadows");

    const std::string capabilities = ReadText(AXLE_RENDER_CAPABILITIES_SOURCE_PATH);
    const std::string renderService = ReadText(AXLE_RENDER_SERVICE_SOURCE_PATH);
    RENDER_CHECK(capabilities.find("caps.SupportsLocalLightShadows = true;") != std::string::npos &&
                 capabilities.find("case RendererBackendType::OpenGL_Compatibility:") != std::string::npos,
        "capability source keeps native support and an explicit compatibility tier");
    RENDER_CHECK(renderService.find("LOCAL_SHADOW_UNSUPPORTED") != std::string::npos &&
                 renderService.find("request.Settings.EnableShadows") != std::string::npos &&
                 renderService.find("light.Shadows.CastShadows&&light.Intensity>0") != std::string::npos,
        "OpenGL fallback reports unsupported local shadows only when requested and lit");

    std::cout << "  [M15DirectLightFallbackReference] Capability/fallback checks passed.\n";
    return true;
}
