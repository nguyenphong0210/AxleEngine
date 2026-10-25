#pragma once
#include <Axle/Types.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace Axle {

    constexpr u32 CASCADE_COUNT = 4;

    struct ShadowMapSpecification {
        u32 Width = 2048;
        u32 Height = 2048;
        u32 CascadeCount = CASCADE_COUNT;
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        float OrthoSize = 25.0f; // Half-size of orthographic frustum (legacy fallback)
        float MaxDistance = 150.0f;
        float SplitLambda = 0.85f;
    };

    class ShadowMap {
    public:
        explicit ShadowMap(const ShadowMapSpecification& spec = {});
        ~ShadowMap();

        void Bind();
        void BindCascade(u32 cascadeIndex);
        void Unbind();
        void Clear();

        void BindTexture(u32 slot = 1) const;
        void UnbindTexture(u32 slot = 1) const;

        u32 GetDepthTextureID() const { return m_DepthTexture; }
        u32 GetRendererID() const { return m_RendererID; }
        u32 GetWidth() const { return m_Specification.Width; }
        u32 GetHeight() const { return m_Specification.Height; }
        u32 GetCascadeCount() const { return m_Specification.CascadeCount; }
        const ShadowMapSpecification& GetSpecification() const { return m_Specification; }

        void SetOrthoSize(float size) { m_Specification.OrthoSize = size; }
        float GetOrthoSize() const { return m_Specification.OrthoSize; }
        void SetNearPlane(float nearPlane) { m_Specification.NearPlane = nearPlane; }
        void SetFarPlane(float farPlane) { m_Specification.FarPlane = farPlane; }

        void Resize(u32 width, u32 height);

        // 4-Cascade Frustum Split & Light-Space Matrix calculation with sub-texel stabilization
        void CalculateCascades(const PerspectiveCamera& camera, const glm::vec3& lightDir, float maxDistance = 150.0f, float lambda = 0.85f);

        const std::vector<glm::mat4>& GetCascadeMatrices() const { return m_CascadeMatrices; }
        const std::vector<float>& GetCascadeSplitDistances() const { return m_CascadeSplitDistances; }
        const glm::mat4& GetLightSpaceMatrix(u32 cascadeIndex = 0) const {
            if (m_CascadeMatrices.empty()) return m_FallbackLightSpaceMatrix;
            return m_CascadeMatrices[std::min<size_t>(cascadeIndex, m_CascadeMatrices.size() - 1)];
        }

        // Calculate legacy single light-space view-projection matrix oriented towards focusCenter
        glm::mat4 CalculateLightSpaceMatrix(const glm::vec3& lightDir, const glm::vec3& focusCenter = glm::vec3(0.0f), float orthoSizeOverride = 0.0f) const;

    private:
        void Invalidate();

    private:
        u32 m_RendererID = 0;
        u32 m_DepthTexture = 0; // GL_TEXTURE_2D_ARRAY (4 cascades)
        ShadowMapSpecification m_Specification;

        std::vector<glm::mat4> m_CascadeMatrices;
        std::vector<float> m_CascadeSplitDistances;
        glm::mat4 m_FallbackLightSpaceMatrix{ 1.0f };
    };

}
