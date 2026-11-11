#pragma once

#include <Axle/Types.hpp>
#include <glm/glm.hpp>

#include <array>
#include <string>

namespace Axle {

    constexpr u32 kMaxCascadedShadowCascades = 4;

    struct CascadedShadowMapSettings {
        u32 CascadeCount = kMaxCascadedShadowCascades;
        u32 Resolution = 2048;
        float NearClip = 0.1f;
        float FarClip = 1000.0f;
        float MaxDistance = 150.0f;
        float SplitLambda = 0.85f;
        float DepthBias = 0.0012f;
        float SlopeBias = 0.0020f;
        float NormalBias = 0.0150f;
    };

    struct CascadedShadowCascade {
        glm::mat4 LightViewProjection{1.0f};
        float NearDistance = 0.0f;
        float FarDistance = 0.0f;
        float TexelWorldSize = 0.0f;
        float DepthRange = 0.0f;
    };

    struct CascadedShadowMapFrame {
        std::array<CascadedShadowCascade, kMaxCascadedShadowCascades> Cascades{};
        u32 CascadeCount = 0;
        u32 Resolution = 0;
        bool Valid = false;
    };

    // Right-handed camera, positive camera-space split depths, ZO light depth.
    // lightDirection points from the scene toward the light.
    // Failed Configure preserves the prior configuration; failed Update clears
    // the frame. Bias is normalized depth (base/slope) and world units (normal).
    // CPU CSM contract exposing matrices/splits for later modern RHI integration.
    // The OpenGL renderer retains its separate ShadowMap implementation.
    class CascadedShadowMap {
    public:
        explicit CascadedShadowMap(CascadedShadowMapSettings settings = {});

        bool Configure(CascadedShadowMapSettings settings, std::string* error = nullptr);
        bool Update(const glm::mat4& viewMatrix,
                    const glm::mat4& projectionMatrixZO,
                    const glm::vec3& lightDirection,
                    std::string* error = nullptr);

        const CascadedShadowMapSettings& GetSettings() const { return m_Settings; }
        const CascadedShadowMapFrame& GetFrame() const { return m_Frame; }
        const CascadedShadowCascade& GetCascade(u32 index) const;

        static bool ValidateSettings(const CascadedShadowMapSettings& settings, std::string* error = nullptr);

    private:
        bool m_Configured = false;
        CascadedShadowMapSettings m_Settings;
        CascadedShadowMapFrame m_Frame;
    };

}
