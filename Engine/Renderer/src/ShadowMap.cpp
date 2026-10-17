#include <Axle/ShadowMap.hpp>
#include <Axle/Window.hpp>
#include <Axle/Log.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace Axle {

    ShadowMap::ShadowMap(const ShadowMapSpecification& spec)
        : m_Specification(spec) {
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

        // 1. Create 32-bit floating-point depth texture attachment with hardware bilinear PCF
        glCreateTextures(GL_TEXTURE_2D, 1, &m_DepthTexture);
        glBindTexture(GL_TEXTURE_2D, m_DepthTexture);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
            static_cast<GLsizei>(m_Specification.Width),
            static_cast<GLsizei>(m_Specification.Height),
            0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

        // Hardware Bilinear PCF comparison mode: enables GPU 4-tap hardware depth interpolation
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        // Border color 1.0 (white): Out-of-bounds coordinates will never be in shadow
        const float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        // 2. Create depth-only framebuffer
        glCreateFramebuffers(1, &m_RendererID);
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthTexture, 0);

        // Explicitly declare that no color buffer is bound or written to
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            AXLE_CORE_ERROR("OpenGL Shadow Map Framebuffer is incomplete! Status: 0x{0:x}", 
                glCheckFramebufferStatus(GL_FRAMEBUFFER));
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void ShadowMap::Bind() {
        if (!Window::HasActiveContext()) return;
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glViewport(0, 0, static_cast<GLsizei>(m_Specification.Width), static_cast<GLsizei>(m_Specification.Height));
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 3.0f);
    }

    void ShadowMap::Unbind() {
        if (!Window::HasActiveContext()) return;
        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void ShadowMap::Clear() {
        if (!Window::HasActiveContext()) return;
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glClear(GL_DEPTH_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void ShadowMap::BindTexture(u32 slot) const {
        if (!Window::HasActiveContext()) return;
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_DepthTexture);
    }

    void ShadowMap::UnbindTexture(u32 slot) const {
        if (!Window::HasActiveContext()) return;
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, 0);
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
