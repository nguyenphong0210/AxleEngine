#include <Axle/RenderCommand.hpp>
#include <Axle/Log.hpp>
#include <glad/glad.h>

namespace Axle {

    bool RenderCommand::Init(GLProcAddressFn procAddressLoader) {
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(procAddressLoader))) {
            AXLE_CORE_CRITICAL("[OpenGL Error] Failed to initialize GLAD loader!");
            return false;
        }

        AXLE_CORE_INFO("Renderer Initialized:");
        AXLE_CORE_INFO("  Vendor:   {0}", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
        AXLE_CORE_INFO("  GPU:      {0}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        AXLE_CORE_INFO("  Version:  {0}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

        glEnable(GL_DEPTH_TEST);
        return true;
    }

    void RenderCommand::SetViewport(u32 x, u32 y, u32 width, u32 height) {
        glViewport(static_cast<GLint>(x), static_cast<GLint>(y), 
                   static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    }

    void RenderCommand::SetClearColor(const glm::vec4& color) {
        glClearColor(color.r, color.g, color.b, color.a);
    }

    void RenderCommand::Clear() {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void RenderCommand::DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) {
        vertexArray->Bind();
        const u32 count = vertexArray->GetIndexBuffer() ? vertexArray->GetIndexBuffer()->GetCount() : 0;
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr);
    }

    void RenderCommand::DrawLines(const std::shared_ptr<VertexArray>& vertexArray, u32 vertexCount) {
        vertexArray->Bind();
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertexCount));
    }

    void RenderCommand::SetLineWidth(f32 width) {
        glLineWidth(width);
    }

}