#include "RenderTestCommon.hpp"
#include <Axle/EnvironmentBake.hpp>
#include <Axle/ResolvedSurface.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {
constexpr double Pi = 3.14159265358979323846;

// Midpoint quadrature over light directions is intentionally different from
// the production Hammersley/half-vector importance sampler and its LUT code.
double NormalDistribution(const glm::dvec3& h, double ax, double ay) {
    const double q = (h.x / ax) * (h.x / ax) +
                     (h.y / ay) * (h.y / ay) + h.z * h.z;
    return 1.0 / (Pi * ax * ay * q * q);
}

double Lambda(double nDotW, double alpha) {
    if (nDotW <= 0.0) return 0.0;
    const double tan2 = (1.0 - nDotW * nDotW) / (nDotW * nDotW);
    return 0.5 * (std::sqrt(1.0 + alpha * alpha * tan2) - 1.0);
}

std::array<double, 2> ReferenceSplitSum(double nDotV, double roughness) {
    constexpr int thetaCount = 256, phiCount = 512;
    const double dTheta = (0.5 * Pi) / thetaCount;
    const double dPhi = (2.0 * Pi) / phiCount;
    const double alpha = roughness * roughness;
    const glm::dvec3 v(std::sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
    double scale = 0.0, bias = 0.0;
    for (int t = 0; t < thetaCount; ++t) {
        const double theta = (t + 0.5) * dTheta;
        const double nl = std::cos(theta), s = std::sin(theta);
        for (int p = 0; p < phiCount; ++p) {
            const double phi = (p + 0.5) * dPhi;
            const glm::dvec3 l(s * std::cos(phi), s * std::sin(phi), nl);
            const glm::dvec3 h = glm::normalize(v + l);
            const double vh = std::max(glm::dot(v, h), 0.0);
            const double G = 1.0 / (1.0 + Lambda(nDotV, alpha) + Lambda(nl, alpha));
            const double energy = NormalDistribution(h, alpha, alpha) * G /
                                  (4.0 * nDotV) * s * dTheta * dPhi;
            const double grazing = std::pow(1.0 - vh, 5.0);
            scale += energy * (1.0 - grazing);
            bias += energy * grazing;
        }
    }
    return {scale, bias};
}

double IntegrateAnisotropicNDF(double ax, double ay) {
    constexpr int thetaCount = 256, phiCount = 512;
    const double dTheta = (0.5 * Pi) / thetaCount;
    const double dPhi = (2.0 * Pi) / phiCount;
    double total = 0.0;
    for (int t = 0; t < thetaCount; ++t) {
        const double theta = (t + 0.5) * dTheta;
        const double c = std::cos(theta), s = std::sin(theta);
        for (int p = 0; p < phiCount; ++p) {
            const double phi = (p + 0.5) * dPhi;
            total += NormalDistribution({s * std::cos(phi), s * std::sin(phi), c}, ax, ay) *
                     c * s * dTheta * dPhi;
        }
    }
    return total;
}

glm::vec3 ReferenceF0(const glm::vec3& baseColor, float metallic) {
    return glm::mix(glm::vec3(0.04f), baseColor, std::clamp(metallic, 0.0f, 1.0f));
}

double Compensation(double f0, double singleScatter) {
    // Missing or malformed LUTs must be neutral, not amplify the fallback.
    if (!std::isfinite(singleScatter) || singleScatter <= 0.0) return 1.0;
    const double r = std::clamp(singleScatter, 0.05, 1.0);
    return std::min(1.0 + f0 * (1.0 / r - 1.0), 8.0);
}

size_t Count(const std::string& source, const std::string& token) {
    size_t result = 0, position = 0;
    while ((position = source.find(token, position)) != std::string::npos) {
        ++result;
        position += token.size();
    }
    return result;
}
}

bool RunM15BRDFReferenceUnitTests() {
    std::cout << "  [M15BRDFReference] Running independent GGX and F0 checks...\n";
    std::ifstream shader(AXLE_FORWARD_PBR_SHADER_PATH);
    const std::string source{std::istreambuf_iterator<char>(shader), std::istreambuf_iterator<char>()};
    RENDER_CHECK(!source.empty(), "ForwardPBR shader source is available");

    // U1-02.1: normalization includes the anisotropic path used by the shader.
    for (const auto& axes : {std::array<double, 2>{0.16, 0.16}, {0.12, 0.42}, {0.64, 0.28}}) {
        RENDER_CHECK(std::abs(IntegrateAnisotropicNDF(axes[0], axes[1]) - 1.0) < 0.002,
            "anisotropic GGX normal distribution integrates to unity");
    }
    for (const auto& sample : {std::array<double, 2>{0.35, 0.8}, {0.65, 0.55}, {0.85, 0.25}}) {
        const double nv = sample[0], roughness = sample[1];
        const auto expected = ReferenceSplitSum(nv, roughness);
        const auto actual = Axle::IntegrateEnvironmentBRDF(
            static_cast<float>(nv), static_cast<float>(roughness), 32768);
        std::cout << "  [M15BRDFReference] NdotV=" << nv << " roughness=" << roughness
                  << " DFG reference=(" << expected[0] << "," << expected[1]
                  << ") bake=(" << actual.x << "," << actual.y << ")\n";
        RENDER_CHECK(std::isfinite(actual.x) && std::isfinite(actual.y) &&
                     std::abs(actual.x - expected[0]) < 0.002 &&
                     std::abs(actual.y - expected[1]) < 0.002,
            "production DFG LUT matches independent correlated-Smith quadrature");
        const double r = expected[0] + expected[1];
        RENDER_CHECK(r > 0.05 && r <= 1.0 + 0.002 &&
                     Compensation(0.8, r) >= Compensation(0.04, r) &&
                     Compensation(0.04, r) >= 1.0,
            "GGX energy compensation remains finite, bounded and F0 ordered");
    }
    RENDER_CHECK(Compensation(0.8, 1.0) == 1.0 &&
                 Compensation(1.0, 0.001) == 8.0 &&
                 Compensation(0.8, 0.0) == 1.0 &&
                 Compensation(0.8, std::numeric_limits<double>::quiet_NaN()) == 1.0,
        "neutral, floor/cap and malformed-LUT fallback are explicit");
    RENDER_CHECK(Count(source, "MaterialDistribution(N,") == 4 &&
                 Count(source, "MaterialGeometry(N,") == 4 &&
                 Count(source, "ggxEnergyCompensation;") >= 5 &&
                 source.find("EvaluateLTCAreaLightTerms(") != std::string::npos &&
                 source.find("return 1.0 / max(PI * axes.x * axes.y * d * d, 1e-8);") != std::string::npos &&
                 source.find("return 1.0 / (1.0 + AnisotropicLambda(N,V,T,B,axes) + AnisotropicLambda(N,L,T,B,axes));") != std::string::npos &&
                 source.find("float ggxSingleScatterEnergy = ggxDFG.x + ggxDFG.y;") != std::string::npos &&
                 source.find("!isfinite(ggxSingleScatterEnergy)") != std::string::npos &&
                 source.find("ggxSingleScatterEnergy = clamp(ggxSingleScatterEnergy, 0.05, 1.0);") != std::string::npos &&
                 source.find("min(1.0 + F0 * (1.0 / ggxSingleScatterEnergy - 1.0), 8.0)") != std::string::npos,
        "sun, point, spot, ambient and anisotropic area paths retain GGX compensation alongside fitted area LTC");

    // U1-02.2: neutral dielectric F0, RGB metal F0, and continuous mixtures.
    const glm::vec3 gold(0.85f, 0.52f, 0.13f);
    RENDER_CHECK(Axle::RenderTests::Near(ReferenceF0(gold, 0.0f), glm::vec3(0.04f), 1e-6f) &&
                 Axle::RenderTests::Near(ReferenceF0(gold, 1.0f), gold, 1e-6f) &&
                 Axle::RenderTests::Near(ReferenceF0(gold, 0.5f),
                     glm::vec3(0.445f, 0.28f, 0.085f), 1e-6f),
        "dielectric, metal and half-metal F0 match the independent RGB reference");
    RENDER_CHECK(Axle::RenderTests::Near(ReferenceF0(gold, -1.0f), glm::vec3(0.04f), 1e-6f) &&
                 Axle::RenderTests::Near(ReferenceF0(gold, 2.0f), gold, 1e-6f),
        "metallic out-of-range values clamp to the dielectric/metal endpoints");
    Axle::RHI::RHIMaterialDesc materialDesc;
    materialDesc.BaseColor = glm::vec4(gold, 1.0f);
    for (float metallic : {0.0f, 0.5f, 1.0f}) {
        materialDesc.Metallic = metallic;
        Axle::RHI::RHIMaterial material(materialDesc);
        const auto resolved = Axle::MaterialResolver::Resolve(material);
        Axle::RHI::RHIObjectPushConstants push{};
        resolved.ApplyToPushConstants(push);
        RENDER_CHECK(Axle::RenderTests::Near(glm::vec3(push.ColorPrimary), gold, 1e-6f) &&
                     Axle::RenderTests::Near(push.Params.w, metallic, 1e-6f) &&
                     Axle::RenderTests::Near(ReferenceF0(glm::vec3(push.ColorPrimary), push.Params.w),
                         ReferenceF0(gold, metallic), 1e-6f),
            "resolved material carries per-channel base color and metallic into shader constants");
    }
    RENDER_CHECK(source.find("float3 F0 = float3(0.04, 0.04, 0.04);") != std::string::npos &&
                 source.find("F0 = lerp(F0, albedo.rgb, metallic);") != std::string::npos &&
                 source.find("clamp(u_Params.w, 0.0, 1.0)") != std::string::npos &&
                 source.find("metallic = clamp(orm.b * metallic, 0.0, 1.0);") != std::string::npos &&
                 source.find("u_MetallicMap.Sample(u_Sampler, uv).r") != std::string::npos &&
                 Count(source, "FresnelSchlick(max(dot(H, V), 0.0), F0)") == 3 &&
                 source.find("EvaluateLTCAreaLightTerms(") != std::string::npos &&
                 source.find("FresnelSchlickRoughness(NdotV, F0, roughness)") != std::string::npos,
        "all direct and ambient shader paths consume the shared material F0");

    std::cout << "  [M15BRDFReference] GGX and F0 checks passed.\n";
    return true;
}
