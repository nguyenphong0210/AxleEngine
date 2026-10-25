#include <Axle/ShadowMap.hpp>
#include <Axle/Window.hpp>
#include <Axle/Log.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <array>
#include <algorithm>

namespace Axle {

    ShadowMap::ShadowMap(const ShadowMapSpecification& spec)
        : m_Specification(spec) {
        if (m_Specification.CascadeCount == 0) {
            m_Specification.CascadeCount = CASCADE_COUNT;
        }
        m_CascadeMatrices.resize(m_Specification.CascadeCount, glm::mat4(1.0f));
        m_CascadeSplitDistances.resize(m_Specification.CascadeCount, 0.0f);
        Invalidate();
    }

    ShadowMap::~ShadowMap() {
        if (m_RendererID && Window::HasActiveContext()) {
            glDeleteFramebuffers(1, &m_RendererID);
            glDeleteTextures(1, &m_DepthTexture);
        }
    }

    void ShadowMap::Invalidate() {
        if (!Window::HasActiveContext()) {
            return;
        }

        if (m_RendererID) {
            glDeleteFramebuffers(1, &m_RendererID);
            glDeleteTextures(1, &m_DepthTexture);
            m_RendererID = 0;
            m_DepthTexture = 0;
        }

        if (m_Specification.CascadeCount == 0) {
            m_Specification.CascadeCount = CASCADE_COUNT;
        }

        // 1. Create 32-bit floating-point depth texture 2D array (4 cascade layers) with hardware bilinear PCF
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_DepthTexture);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_DepthTexture);
        glTexImage3D(
            GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
            static_cast<GLsizei>(m_Specification.Width),
            static_cast<GLsizei>(m_Specification.Height),
            static_cast<GLsizei>(m_Specification.CascadeCount),
            0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
        );

        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        // Hardware Bilinear PCF comparison mode: enables GPU 4-tap hardware depth interpolation per layer
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        // Border color 1.0 (white): Coordinates outside light frustum will evaluate as fully lit
        const float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);

        // 2. Create depth-only framebuffer
        glCreateFramebuffers(1, &m_RendererID);
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_DepthTexture, 0, 0);

        // Explicitly declare that no color buffer is bound or written to
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            AXLE_CORE_ERROR("OpenGL Cascaded Shadow Map Framebuffer is incomplete! Status: 0x{0:x}", 
                glCheckFramebufferStatus(GL_FRAMEBUFFER));
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void ShadowMap::Bind() {
        BindCascade(0);
    }

    void ShadowMap::BindCascade(u32 cascadeIndex) {
        if (!Window::HasActiveContext()) return;
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_DepthTexture, 0, static_cast<GLint>(cascadeIndex));
        glViewport(0, 0, static_cast<GLsizei>(m_Specification.Width), static_cast<GLsizei>(m_Specification.Height));
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);

        // Hardware Slope-Scaled Depth Bias:
        // Slope factor = 2.0f, units = 4.0f. Eliminates polygon self-shadowing and depth fight artifacts.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);
    }

    void ShadowMap::Unbind() {
        if (!Window::HasActiveContext()) return;
        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void ShadowMap::Clear() {
        if (!Window::HasActiveContext()) return;
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        for (u32 c = 0; c < m_Specification.CascadeCount; ++c) {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_DepthTexture, 0, static_cast<GLint>(c));
            glClear(GL_DEPTH_BUFFER_BIT);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void ShadowMap::BindTexture(u32 slot) const {
        if (!Window::HasActiveContext()) return;
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_DepthTexture);
    }

    void ShadowMap::UnbindTexture(u32 slot) const {
        if (!Window::HasActiveContext()) return;
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    void ShadowMap::Resize(u32 width, u32 height) {
        if (width == 0 || height == 0 || width > 8192 || height > 8192) {
            AXLE_CORE_WARN("Attempted to resize ShadowMap to invalid dimensions ({0}, {1})", width, height);
            return;
        }
        if (m_Specification.Width == width && m_Specification.Height == height) {
            return;
        }

        m_Specification.Width = width;
        m_Specification.Height = height;
        Invalidate();
    }

    void ShadowMap::CalculateCascades(const PerspectiveCamera& camera, const glm::vec3& lightDir, float maxDistance, float lambda) {
        if (m_Specification.CascadeCount == 0) {
            m_Specification.CascadeCount = CASCADE_COUNT;
        }
        m_CascadeMatrices.resize(m_Specification.CascadeCount, glm::mat4(1.0f));
        m_CascadeSplitDistances.resize(m_Specification.CascadeCount, 0.0f);

        float nearClip = camera.GetNearClip() > 0.01f ? camera.GetNearClip() : 0.1f;
        float farClip = camera.GetFarClip();
        float shadowDistance = std::min(farClip, (maxDistance > 0.0f ? maxDistance : m_Specification.MaxDistance));
        if (shadowDistance <= nearClip) {
            shadowDistance = nearClip + 50.0f;
        }

        float splitWeight = glm::clamp(lambda > 0.0f ? lambda : m_Specification.SplitLambda, 0.0f, 1.0f);

        // 1. Compute split distances using Practical Split Scheme (PSSM - Logarithmic & Linear mix)
        std::vector<float> cascadeSplits(m_Specification.CascadeCount + 1, nearClip);
        for (u32 i = 1; i <= m_Specification.CascadeCount; ++i) {
            float p = static_cast<float>(i) / static_cast<float>(m_Specification.CascadeCount);
            float logSplit = nearClip * std::pow(shadowDistance / nearClip, p);
            float linSplit = nearClip + (shadowDistance - nearClip) * p;
            cascadeSplits[i] = splitWeight * logSplit + (1.0f - splitWeight) * linSplit;
            m_CascadeSplitDistances[i - 1] = cascadeSplits[i];
        }

        glm::vec3 normDir = glm::length(lightDir) > 1e-4f ? glm::normalize(lightDir) : glm::vec3(0.45f, 1.0f, 0.65f);
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(normDir, up)) > 0.98f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        glm::mat4 viewMatrix = camera.GetViewMatrix();
        float fov = camera.GetFOV();
        float aspect = camera.GetAspectRatio();

        // 2. Compute tight, stabilized bounding sphere and light ortho projection per cascade
        for (u32 i = 0; i < m_Specification.CascadeCount; ++i) {
            float prevDist = cascadeSplits[i];
            float curDist = cascadeSplits[i + 1];

            glm::mat4 subProj = glm::perspective(glm::radians(fov), aspect, prevDist, curDist);
            glm::mat4 invSubVP = glm::inverse(subProj * viewMatrix);

            std::array<glm::vec3, 8> corners;
            int cornerIndex = 0;
            for (int x = 0; x < 2; ++x) {
                for (int y = 0; y < 2; ++y) {
                    for (int z = 0; z < 2; ++z) {
                        glm::vec4 pt = invSubVP * glm::vec4(
                            2.0f * x - 1.0f,
                            2.0f * y - 1.0f,
                            2.0f * z - 1.0f,
                            1.0f
                        );
                        corners[cornerIndex++] = glm::vec3(pt) / pt.w;
                    }
                }
            }

            glm::vec3 center(0.0f);
            for (const auto& corner : corners) {
                center += corner;
            }
            center /= 8.0f;

            float radius = 0.0f;
            for (const auto& corner : corners) {
                radius = std::max(radius, glm::length(corner - center));
            }
            // Quantize radius to stabilize bounding sphere against floating-point jitter
            radius = std::ceil(radius * 16.0f) / 16.0f;

            // Place light camera behind center along the directional light vector
            float zExt = 4.0f;
            glm::vec3 lightEye = center + normDir * (radius * zExt);
            glm::mat4 lightView = glm::lookAt(lightEye, center, up);

            float minX = -radius;
            float maxX = radius;
            float minY = -radius;
            float maxY = radius;
            float nearPlane = 0.0f;
            float farPlane = radius * (zExt + 2.0f);

            glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, nearPlane, farPlane);

            // Stabilize shadow edges with sub-texel snapping to eliminate shimmering/swimming
            glm::vec4 shadowOrigin = lightProj * lightView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            shadowOrigin *= (static_cast<float>(m_Specification.Width) / 2.0f);
            glm::vec4 roundedOrigin = glm::round(shadowOrigin);
            glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
            roundOffset *= (2.0f / static_cast<float>(m_Specification.Width));
            roundOffset.z = 0.0f;
            roundOffset.w = 0.0f;
            lightProj[3] += roundOffset;

            m_CascadeMatrices[i] = lightProj * lightView;
        }

        if (!m_CascadeMatrices.empty()) {
            m_FallbackLightSpaceMatrix = m_CascadeMatrices[0];
        }
    }

    glm::mat4 ShadowMap::CalculateLightSpaceMatrix(const glm::vec3& lightDir, const glm::vec3& focusCenter, float orthoSizeOverride) const {
        glm::vec3 normDir = glm::length(lightDir) > 1e-4f ? glm::normalize(lightDir) : glm::vec3(0.45f, 1.0f, 0.65f);
        
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(normDir, up)) > 0.98f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        // Place light camera high above the focus center along the directional light vector
        glm::vec3 lightPos = focusCenter + normDir * 80.0f;
        glm::mat4 lightView = glm::lookAt(lightPos, focusCenter, up);

        float s = (orthoSizeOverride > 0.0f) ? orthoSizeOverride : m_Specification.OrthoSize;
        glm::mat4 lightProj = glm::ortho(-s, s, -s, s, m_Specification.NearPlane, m_Specification.FarPlane);

        // Stabilize shadow edges with sub-texel snapping
        glm::vec4 shadowOrigin = lightProj * lightView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        shadowOrigin *= (static_cast<float>(m_Specification.Width) / 2.0f);
        glm::vec4 roundedOrigin = glm::round(shadowOrigin);
        glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
        roundOffset *= (2.0f / static_cast<float>(m_Specification.Width));
        roundOffset.z = 0.0f;
        roundOffset.w = 0.0f;
        lightProj[3] += roundOffset;

        return lightProj * lightView;
    }

}
