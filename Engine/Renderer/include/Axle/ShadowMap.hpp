#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace Axle {

    struct ShadowMapSpecification {
        u32 Width = 2048;
        u32 Height = 2048;
        float NearPlane = 1.0f;
        float FarPlane = 160.0f;
        float OrthoSize = 45.0f; // Half-size of orthographic frustum (90m x 90m coverage)
    };

    class ShadowMap {
    public:
        explicit ShadowMap(const ShadowMapSpecification& spec = {});
        ~ShadowMap();

        void Bind();
        void Unbind();
        void Clear();

        void BindTexture(u32 slot = 1) const;
        void UnbindTexture(u32 slot = 1) const;

        u32 GetDepthTextureID() const { return m_DepthTexture; }
        u32 GetRendererID() const { return m_RendererID; }
        u32 GetWidth() const { return m_Specification.Width; }
        u32 GetHeight() const { return m_Specification.Height; }
        const ShadowMapSpecification& GetSpecification() const { return m_Specification; }

        void Resize(u32 width, u32 height);

        // Calculate light-space view-projection matrix oriented towards focusCenter
        glm::mat4 CalculateLightSpaceMatrix(const glm::vec3& lightDir, const glm::vec3& focusCenter = glm::vec3(0.0f)) const;

    private:
        void Invalidate();

    private:
        u32 m_RendererID = 0;
        u32 m_DepthTexture = 0;
        ShadowMapSpecification m_Specification;
    };

}
