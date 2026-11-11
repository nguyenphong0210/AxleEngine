#include <Axle/CascadedShadowMap.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>

#define CHECK(expression) do { if (!(expression)) { std::cerr << "FAIL " << __LINE__ << ": " #expression "\n"; return false; } } while (0)
namespace {
    using namespace Axle;
    bool MatrixNear(const glm::mat4& a, const glm::mat4& b, float tolerance = 1e-6f) {
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            if (!std::isfinite(a[c][r]) || std::abs(a[c][r] - b[c][r]) > tolerance) return false;
        return true;
    }
    bool Contract() {
        CascadedShadowMapSettings settings;
        settings.FarClip = 500;
        std::string error;
        const auto view = glm::lookAtRH(glm::vec3(17, 4, 12), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
        const glm::vec3 light = glm::normalize(glm::vec3(0.4f, 1, 0.3f));
        for (u32 resolution : {32u, 1024u, 2048u, 4096u})
        for (bool ortho : {false, true}) for (u32 count : {3u, 4u}) for (float lambda : {0.0f, 0.85f, 1.0f}) {
            settings.Resolution = resolution;
            settings.CascadeCount = count; settings.SplitLambda = lambda;
            const auto projection = ortho ? glm::orthoRH_ZO(-20.f, 20.f, -10.f, 10.f, .1f, 500.f) :
                glm::perspectiveRH_ZO(glm::radians(60.f), 16.f / 9, .1f, 500.f);
            CascadedShadowMap map(settings);
            CHECK(map.Update(view, projection, light, &error) && error.empty());
            const auto first = map.GetFrame();
            CHECK(first.Valid && first.CascadeCount == count && first.Resolution == settings.Resolution);
            float previous = settings.NearClip;
            const auto inverseProjection = glm::inverse(glm::dmat4(projection));
            const auto inverseView = glm::inverse(glm::dmat4(view));
            for (u32 c = 0; c < count; ++c) {
                const auto& cascade = first.Cascades[c];
                const double p = double(c + 1) / count;
                const double expected = lambda * settings.NearClip * std::pow(settings.MaxDistance / settings.NearClip, p) +
                    (1 - lambda) * (settings.NearClip + (settings.MaxDistance - settings.NearClip) * p);
                CHECK(std::abs(cascade.NearDistance - previous) < 1e-4f);
                CHECK(std::abs(cascade.FarDistance - expected) < 1e-4);
                CHECK(cascade.FarDistance > previous && cascade.TexelWorldSize > 0 && cascade.DepthRange > 0);
                for (int i = 0; i < 4; ++i) {
                    auto nearPoint = inverseProjection * glm::dvec4((i & 1) ? 1 : -1, (i & 2) ? 1 : -1, 0, 1);
                    auto farPoint = inverseProjection * glm::dvec4((i & 1) ? 1 : -1, (i & 2) ? 1 : -1, 1, 1);
                    nearPoint /= nearPoint.w; farPoint /= farPoint.w;
                    for (double depth : {double(cascade.NearDistance), double(cascade.FarDistance)}) {
                        const auto point = nearPoint + (farPoint - nearPoint) * ((depth + nearPoint.z) / (-farPoint.z + nearPoint.z));
                        auto clip = glm::dmat4(cascade.LightViewProjection) * inverseView * point;
                        clip /= clip.w;
                        CHECK(std::abs(clip.x) <= 1.00001 && std::abs(clip.y) <= 1.00001 && clip.z >= 0 && clip.z <= 1);
                    }
                }
                previous = cascade.FarDistance;
            }
            CHECK(previous == settings.MaxDistance);
            CHECK(map.Update(view, projection, light, &error));
            for (u32 c = 0; c < count; ++c) CHECK(MatrixNear(first.Cascades[c].LightViewProjection, map.GetCascade(c).LightViewProjection, 0));
            // A small translation along light-view X must not move the shadow UV
            // until a texel boundary is crossed. Use a controlled centered camera.
            const auto stableView = glm::lookAtRH(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
            CHECK(map.Update(stableView, projection, glm::vec3(0, 0, 1)));
            const auto stable = map.GetFrame();
            const float shift = stable.Cascades[0].TexelWorldSize * 0.1f;
            CHECK(map.Update(glm::translate(glm::mat4(1), glm::vec3(-shift, 0, 0)), projection, glm::vec3(0, 0, 1)));
            for (u32 c = 0; c < count; ++c) CHECK(MatrixNear(stable.Cascades[c].LightViewProjection, map.GetCascade(c).LightViewProjection));
            // A whole-texel camera move yields an integer texel translation.
            const float texel = stable.Cascades[0].TexelWorldSize;
            CHECK(map.Update(glm::translate(glm::mat4(1), glm::vec3(-texel, 0, 0)), projection, glm::vec3(0, 0, 1)));
            CHECK(std::abs((map.GetCascade(0).LightViewProjection[3].x - stable.Cascades[0].LightViewProjection[3].x) * settings.Resolution * .5f + 1) < 1e-3f);
            // Parallel-to-up light direction must still produce a finite frame.
            CHECK(map.Update(view, projection, glm::vec3(0, 1, 0)));
        }
        return true;
    }
    bool LongRangeCamera() {
        CascadedShadowMapSettings settings;
        settings.NearClip = .01f; settings.FarClip = 10000;
        auto projection = glm::perspectiveRH_ZO(glm::radians(60.f), 1.f, settings.NearClip, settings.FarClip);
        CascadedShadowMap map(settings);
        std::string error;
        CHECK(map.Update(glm::mat4(1), projection, glm::vec3(0, 0, 1), &error));
        CHECK(map.GetFrame().Valid && map.GetCascade(3).FarDistance == 150);
        // Check actual camera-space corners at shadow distances, independently
        // of a numerically unstable far-clip unprojection.
        const double tangent = std::tan(glm::radians(60.0) / 2);
        for (u32 c = 0; c < 4; ++c) for (float distance : {map.GetCascade(c).NearDistance, map.GetCascade(c).FarDistance})
            for (int x : {-1, 1}) for (int y : {-1, 1}) {
                auto clip = glm::dmat4(map.GetCascade(c).LightViewProjection) *
                    glm::dvec4(x * distance * tangent, y * distance * tangent, -distance, 1);
                CHECK(std::abs(clip.x) < 1 && std::abs(clip.y) < 1 && clip.z >= 0 && clip.z <= 1);
            }
        return true;
    }
    bool Rejection() {
        CascadedShadowMapSettings valid;
        const auto projection = glm::perspectiveRH_ZO(glm::radians(60.f), 1.f, valid.NearClip, valid.FarClip);
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        std::string error;
        CascadedShadowMap map(valid);
        for (int test = 0; test < 12; ++test) {
            auto invalid = valid;
            switch (test) {
                case 0: invalid.CascadeCount = 0; break;
                case 1: invalid.CascadeCount = 5; break;
                case 2: invalid.Resolution = 0; break;
                case 3: invalid.Resolution = 4097; break;
                case 4: invalid.NearClip = 0; break;
                case 5: invalid.FarClip = valid.NearClip; break;
                case 6: invalid.MaxDistance = valid.NearClip; break;
                case 7: invalid.SplitLambda = 1.1f; break;
                case 8: invalid.DepthBias = -1; break;
                case 9: invalid.SlopeBias = nan; break;
                case 10: invalid.NormalBias = nan; break;
                case 11: invalid.MaxDistance = nan; break;
            }
            CHECK(!map.Configure(invalid, &error) && !error.empty());
            CHECK(map.GetSettings().Resolution == valid.Resolution);
            CHECK(map.Update(glm::mat4(1), projection, glm::vec3(0, 1, 0), &error) && error.empty());
            CascadedShadowMap badConstructor(invalid);
            CHECK(!badConstructor.Update(glm::mat4(1), projection, glm::vec3(0, 1, 0), &error));
        }
        for (auto light : {glm::vec3(0), glm::vec3(nan, 0, 1)}) {
            CHECK(!map.Update(glm::mat4(1), projection, light, &error));
            CHECK(!map.GetFrame().Valid && map.GetFrame().CascadeCount == 0 && !error.empty());
        }
        for (auto bad : {glm::mat4(0), glm::mat4(nan), glm::scale(glm::mat4(1), glm::vec3(2))}) {
            CHECK(!map.Update(bad, projection, glm::vec3(0, 1, 0), &error));
            CHECK(!map.GetFrame().Valid);
        }
        CHECK(!map.Update(glm::mat4(1), glm::mat4(1), glm::vec3(0, 1, 0), &error));
        CHECK(!map.Update(glm::mat4(1), glm::mat4(0), glm::vec3(0, 1, 0), &error));
        valid.DepthBias = valid.NormalBias = valid.SlopeBias = 0;
        CHECK(map.Configure(valid, &error));
        CHECK(map.Update(glm::mat4(1), projection, glm::vec3(0, 1, 0), &error));
        CHECK(map.GetCascade(99).FarDistance == 0);
        // Adjacent representable distances cannot hold 3/4 distinct intervals.
        // The double calculation must not publish collapsed float splits.
        valid.MaxDistance = std::nextafter(valid.NearClip, std::numeric_limits<float>::infinity());
        CHECK(map.Configure(valid, &error));
        CHECK(!map.Update(glm::mat4(1), projection, glm::vec3(0, 1, 0), &error));
        CHECK(!map.GetFrame().Valid && map.GetFrame().CascadeCount == 0 && !error.empty());
        return true;
    }
}
int main() {
    if (!Contract() || !LongRangeCamera() || !Rejection()) return 1;
    std::cout << "PASS CSM splits, ZO coverage, stabilization, orthographic cameras, 3/4 cascades and failure policy\n";
    return 0;
}
