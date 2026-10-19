#include <Axle/Framebuffer.hpp>
#include <Axle/Log.hpp>
#include <glad/glad.h>

namespace Axle {

    class OpenGLFramebuffer : public Framebuffer {
    public:
        explicit OpenGLFramebuffer(const FramebufferSpecification& spec)
            : m_Specification(spec) {
            Invalidate();
        }

        ~OpenGLFramebuffer() override {
            if (glad_glDeleteFramebuffers && m_RendererID) {
                glDeleteFramebuffers(1, &m_RendererID);
                glDeleteTextures(1, &m_ColorAttachment);
                glDeleteTextures(1, &m_DepthAttachment);
            }
        }

        void Invalidate() {
            if (!glad_glCreateFramebuffers || !glad_glBindFramebuffer) return;

            if (m_RendererID) {
                glDeleteFramebuffers(1, &m_RendererID);
                glDeleteTextures(1, &m_ColorAttachment);
                glDeleteTextures(1, &m_DepthAttachment);
                m_RendererID = 0;
                m_ColorAttachment = 0;
                m_DepthAttachment = 0;
            }

            glCreateFramebuffers(1, &m_RendererID);
            glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

            // Color attachment texture (RGBA8)
            glCreateTextures(GL_TEXTURE_2D, 1, &m_ColorAttachment);
            glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 
                         static_cast<GLsizei>(m_Specification.Width), 
                         static_cast<GLsizei>(m_Specification.Height), 
                         0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);

            // Depth/Stencil attachment texture (DEPTH24_STENCIL8)
            glCreateTextures(GL_TEXTURE_2D, 1, &m_DepthAttachment);
            glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, 
                         static_cast<GLsizei>(m_Specification.Width), 
                         static_cast<GLsizei>(m_Specification.Height), 
                         0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);

            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                AXLE_CORE_ERROR("OpenGL Framebuffer is incomplete! Status: 0x{0:x}", glCheckFramebufferStatus(GL_FRAMEBUFFER));
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void Bind() override {
            if (!glad_glBindFramebuffer) return;
            glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
            if (glad_glViewport) {
                glViewport(0, 0, static_cast<GLsizei>(m_Specification.Width), static_cast<GLsizei>(m_Specification.Height));
            }
        }

        void Unbind() override {
            if (!glad_glBindFramebuffer) return;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void Resize(u32 width, u32 height) override {
            if (width == 0 || height == 0 || width > 8192 || height > 8192) {
                AXLE_CORE_WARN("Attempted to resize Framebuffer to invalid dimensions ({0}, {1})", width, height);
                return;
            }
            if (m_Specification.Width == width && m_Specification.Height == height) {
                return;
            }

            m_Specification.Width = width;
            m_Specification.Height = height;
            Invalidate();
        }

        u32 GetColorAttachmentRendererID() const override { return m_ColorAttachment; }
        u32 GetDepthAttachmentRendererID() const override { return m_DepthAttachment; }
        const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

    private:
        u32 m_RendererID = 0;
        u32 m_ColorAttachment = 0;
        u32 m_DepthAttachment = 0;
        FramebufferSpecification m_Specification;
    };

    std::shared_ptr<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec) {
        return std::make_shared<OpenGLFramebuffer>(spec);
    }

}
