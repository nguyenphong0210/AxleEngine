#include <Axle/CascadedShadowMap.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Axle {
namespace {
    bool Finite(const glm::dmat4& matrix) {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                if (!std::isfinite(matrix[c][r])) return false;
        return true;
    }
    bool Fail(std::string* error, const char* message) {
        if (error) *error = message;
        return false;
    }
}

    CascadedShadowMap::CascadedShadowMap(CascadedShadowMapSettings settings) {
        Configure(settings);
    }

    bool CascadedShadowMap::ValidateSettings(const CascadedShadowMapSettings& settings, std::string* error) {
        if (error) error->clear();
        if (settings.CascadeCount < 3 || settings.CascadeCount > kMaxCascadedShadowCascades)
            return Fail(error, "CSM cascade count must be 3 or 4");
        if (settings.Resolution < 32 || settings.Resolution > 4096)
            return Fail(error, "CSM resolution must be in [32, 4096]");
        if (!std::isfinite(settings.NearClip) || !std::isfinite(settings.FarClip) ||
            settings.NearClip <= 0 || settings.FarClip <= settings.NearClip)
            return Fail(error, "CSM clip distances must be finite, positive, and ordered");
        if (!std::isfinite(settings.MaxDistance) || settings.MaxDistance <= settings.NearClip || settings.MaxDistance > settings.FarClip)
            return Fail(error, "CSM max distance must be greater than near and at most far clip");
        if (!std::isfinite(settings.SplitLambda) || settings.SplitLambda < 0 || settings.SplitLambda > 1)
            return Fail(error, "CSM split lambda must be in [0, 1]");
        for (float bias : {settings.DepthBias, settings.SlopeBias, settings.NormalBias})
            if (!std::isfinite(bias) || bias < 0)
                return Fail(error, "CSM biases must be finite and non-negative");
        return true;
    }

    bool CascadedShadowMap::Configure(CascadedShadowMapSettings settings, std::string* error) {
        if (!ValidateSettings(settings, error)) return false;
        m_Settings = settings;
        m_Configured = true;
        m_Frame = {};
        return true;
    }

    bool CascadedShadowMap::Update(const glm::mat4& viewMatrix,
                                   const glm::mat4& projectionMatrixZO,
                                   const glm::vec3& lightDirection,
                                   std::string* error) {
        if (error) error->clear();
        // Failed updates invalidate the frame so a caller cannot consume stale
        // matrices for the new camera. No partially calculated cascade escapes.
        m_Frame = {};
        if (!m_Configured) return Fail(error, "CSM has no valid configuration");
        const glm::dmat4 view(viewMatrix), projection(projectionMatrixZO);
        if (!Finite(view) || !Finite(projection) ||
            std::abs(glm::determinant(view)) < 1e-12 || std::abs(glm::determinant(projection)) < 1e-12)
            return Fail(error, "CSM camera matrices must be finite and invertible");
        const double lightLength = glm::length(glm::dvec3(lightDirection));
        if (!std::isfinite(lightLength) || lightLength < 1e-8)
            return Fail(error, "CSM direction toward the light must be finite and non-zero");

        const auto inverseProjection = glm::inverse(projection);
        const auto inverseView = glm::inverse(view);
        // Avoid unprojecting Z=1: a float perspective matrix loses substantial
        // far-plane accuracy when far/near is large. An interior depth supplies
        // the same camera rays without that ill-conditioning.
        std::array<glm::dvec3, 4> nearCorners{}, interiorCorners{};
        for (const auto [distance, depth] : {std::pair<double, double>{m_Settings.NearClip, 0},
                                           std::pair<double, double>{m_Settings.FarClip, 1}}) {
            const auto projected = projection * glm::dvec4(0, 0, -distance, 1);
            if (!std::isfinite(projected.w) || std::abs(projected.w) < 1e-12 ||
                std::abs(projected.z / projected.w - depth) > 8 * std::numeric_limits<float>::epsilon())
                return Fail(error, "CSM requires a right-handed ZO projection matching the configured clip range");
        }
        for (int i = 0; i < 4; ++i) {
            const auto unproject = [&](double z, glm::dvec3& result) {
                auto point = inverseProjection * glm::dvec4((i & 1) ? 1.0 : -1.0, (i & 2) ? 1.0 : -1.0, z, 1.0);
                if (!std::isfinite(point.w) || std::abs(point.w) < 1e-12) return false;
                result = glm::dvec3(point) / point.w;
                return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z);
            };
            if (!unproject(0, nearCorners[i]) || !unproject(0.5, interiorCorners[i]) ||
                std::abs(-nearCorners[i].z - m_Settings.NearClip) > m_Settings.NearClip * 0.002 ||
                -interiorCorners[i].z <= -nearCorners[i].z)
                return Fail(error, "CSM requires a right-handed ZO projection matching the configured clip range");
        }
        // View matrices must represent a rigid camera, not a scale/shear or a
        // projective transform. Radius then stays independent of world position.
        const auto rotation = glm::dmat3(view);
        const auto gram = glm::transpose(rotation) * rotation;
        for (int c = 0; c < 4; ++c) {
            if (std::abs(view[c][3] - (c == 3 ? 1.0 : 0.0)) > 1e-5)
                return Fail(error, "CSM view must be an affine rigid camera transform");
            for (int r = 0; c < 3 && r < 3; ++r)
                if (std::abs(gram[c][r] - (c == r ? 1.0 : 0.0)) > 1e-4)
                    return Fail(error, "CSM view must not contain scale or shear");
        }
        const glm::dvec3 light = glm::dvec3(lightDirection) / lightLength;
        const glm::dvec3 up = std::abs(light.y) > 0.98 ? glm::dvec3(0, 0, 1) : glm::dvec3(0, 1, 0);
        // Fixed light orientation in world coordinates is essential: snapping
        // around a camera-relative light origin does not stabilize the shadow.
        const auto lightView = glm::lookAtRH(glm::dvec3(0), -light, up);
        CascadedShadowMapFrame candidate;
        candidate.CascadeCount = m_Settings.CascadeCount;
        candidate.Resolution = m_Settings.Resolution;
        double previousSplit = m_Settings.NearClip;
        for (u32 index = 0; index < candidate.CascadeCount; ++index) {
            const double p = double(index + 1) / candidate.CascadeCount;
            const double logarithmic = m_Settings.NearClip * std::pow(double(m_Settings.MaxDistance) / m_Settings.NearClip, p);
            const double uniform = m_Settings.NearClip + (m_Settings.MaxDistance - m_Settings.NearClip) * p;
            const double split = index + 1 == candidate.CascadeCount ? m_Settings.MaxDistance :
                logarithmic * m_Settings.SplitLambda + uniform * (1 - m_Settings.SplitLambda);
            // The receiver blends over the preceding split's final 10%.
            // Fit that overlap explicitly so a narrow-FOV camera cannot sample
            // beyond the next cascade's near edge during blending.
            const double priorNear = index > 0 ? candidate.Cascades[index-1].NearDistance : previousSplit;
            const double fitNear = previousSplit - (previousSplit-priorNear)*0.1;
            std::array<glm::dvec3, 8> corners;
            glm::dvec3 center(0);
            for (int i = 0; i < 4; ++i) {
                const auto delta = interiorCorners[i] - nearCorners[i];
                const double denominator = -interiorCorners[i].z + nearCorners[i].z;
                corners[i] = nearCorners[i] + delta * ((fitNear + nearCorners[i].z) / denominator);
                corners[i + 4] = nearCorners[i] + delta * ((split + nearCorners[i].z) / denominator);
                center += corners[i] + corners[i + 4];
            }
            center /= 8.0;
            double radius = 0;
            for (const auto& corner : corners) radius = std::max(radius, glm::length(corner - center));
            // Rotation invariant sphere and fixed quantization avoid scale shimmer.
            // Eight texels of guard room cover snapping and the 5x5 PCF footprint.
            const double halfExtent = std::ceil(radius * 16) / 16 * candidate.Resolution / (candidate.Resolution - 16.0);
            const double texel = 2 * halfExtent / candidate.Resolution;
            const auto worldCenter = glm::dvec3(inverseView * glm::dvec4(center, 1));
            const auto lightCenter = glm::dvec3(lightView * glm::dvec4(worldCenter, 1));
            const double x = std::floor(lightCenter.x / texel + 0.5) * texel;
            const double y = std::floor(lightCenter.y / texel + 0.5) * texel;
            // Extend toward/away from the light for off-frustum casters. This is
            // a bounded distance policy, not an unbounded scene-fit assumption.
            const double depthExtent = halfExtent + m_Settings.MaxDistance;
            const double nearPlane = -lightCenter.z - depthExtent;
            const double farPlane = -lightCenter.z + depthExtent;
            const auto matrix = glm::orthoRH_ZO(x - halfExtent, x + halfExtent,
                y - halfExtent, y + halfExtent, nearPlane, farPlane) * lightView;
            auto& cascade = candidate.Cascades[index];
            cascade.LightViewProjection = glm::mat4(matrix);
            if (!Finite(glm::dmat4(cascade.LightViewProjection)))
                return Fail(error, "CSM matrix is outside the supported numeric range");
            cascade.NearDistance = static_cast<float>(previousSplit);
            cascade.FarDistance = static_cast<float>(split);
            cascade.TexelWorldSize = static_cast<float>(texel);
            cascade.DepthRange = static_cast<float>(farPlane - nearPlane);
            if (!(cascade.FarDistance > cascade.NearDistance) ||
                !std::isfinite(cascade.TexelWorldSize) || !(cascade.TexelWorldSize > 0) ||
                !std::isfinite(cascade.DepthRange) || !(cascade.DepthRange > 0))
                return Fail(error, "CSM interval or dimensions are outside the supported float range");
            previousSplit = split;
        }
        candidate.Valid = true;
        m_Frame = candidate;
        return true;
    }

    const CascadedShadowCascade& CascadedShadowMap::GetCascade(u32 index) const {
        static const CascadedShadowCascade empty{};
        return index < m_Frame.CascadeCount ? m_Frame.Cascades[index] : empty;
    }

}
