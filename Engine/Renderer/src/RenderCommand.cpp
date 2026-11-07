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
        if (!glad_glViewport) return;
        glViewport(static_cast<GLint>(x), static_cast<GLint>(y),
                   static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    }

    void RenderCommand::SetClearColor(const glm::vec4& color) {
        if (!glad_glClearColor) return;
        glClearColor(color.r, color.g, color.b, color.a);
    }

    void RenderCommand::Clear() {
        if (!glad_glClear) return;
        if (glad_glEnable) glEnable(GL_DEPTH_TEST);
        if (glad_glDepthFunc) glDepthFunc(GL_LESS);
        if (glad_glDepthMask) glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void RenderCommand::BindDefaultFramebuffer() {
        if (!glad_glBindFramebuffer) return;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void RenderCommand::SetDepthMask(bool writeEnabled) {
        if (!glad_glDepthMask) return;
        glDepthMask(writeEnabled ? GL_TRUE : GL_FALSE);
    }

    void RenderCommand::SetDepthFuncLEqual() {
        if (!glad_glDepthFunc) return;
        glDepthFunc(GL_LEQUAL);
    }

    void RenderCommand::SetDepthFuncLess() {
        if (!glad_glDepthFunc) return;
        glDepthFunc(GL_LESS);
    }

    void RenderCommand::SetCullFace(bool enabled) {
        if (!glad_glEnable || !glad_glDisable) return;
        if (enabled) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
    }

    void RenderCommand::DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) {
        if (!vertexArray) return;
        vertexArray->Bind();
        const u32 count = vertexArray->GetIndexBuffer() ? vertexArray->GetIndexBuffer()->GetCount() : 0;
        if (count == 0) return;
        if (!glad_glDrawElements) return;
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr);
    }

    void RenderCommand::DrawIndexedInstanced(const std::shared_ptr<VertexArray>& vertexArray, u32 instanceCount) {
        if (!vertexArray || instanceCount == 0) return;
        vertexArray->Bind();
        const u32 count = vertexArray->GetIndexBuffer() ? vertexArray->GetIndexBuffer()->GetCount() : 0;
        if (count == 0) return;
        if (!glad_glDrawElementsInstanced) return;
        glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(instanceCount));
    }

    void RenderCommand::DrawLines(const std::shared_ptr<VertexArray>& vertexArray, u32 vertexCount) {
        if (!vertexArray || vertexCount == 0) return;
        vertexArray->Bind();
        if (!glad_glDrawArrays) return;
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertexCount));
    }

    void RenderCommand::DrawDebugLines(const std::shared_ptr<VertexArray>& vertexArray, u32 vertexCount) {
        const bool blend=glIsEnabled(GL_BLEND);
        GLint srcRGB,dstRGB,srcAlpha,dstAlpha,eqRGB,eqAlpha;GLboolean depthMask;
        glGetIntegerv(GL_BLEND_SRC_RGB,&srcRGB);glGetIntegerv(GL_BLEND_DST_RGB,&dstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,&eqRGB);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&eqAlpha);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
        DrawLines(vertexArray, vertexCount);
        glDepthMask(depthMask);glBlendFuncSeparate(srcRGB,dstRGB,srcAlpha,dstAlpha);glBlendEquationSeparate(eqRGB,eqAlpha);
        if(!blend)glDisable(GL_BLEND);
    }

    void RenderCommand::SetLineWidth(f32 width) {
        if (!glad_glLineWidth) return;
        glLineWidth(width);
    }

    void RenderCommand::SetWireframeMode(bool enabled) {
        if (!glad_glPolygonMode) return;
        glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
    }

    void RenderCommand::SetMaterialState(SurfaceType surface, CullMode cull, bool depthWrite) {
        if (!glad_glEnable) return;
        glFrontFace(GL_CCW);
        if(cull==CullMode::None)glDisable(GL_CULL_FACE);
        else{glEnable(GL_CULL_FACE);glCullFace(cull==CullMode::Front?GL_FRONT:GL_BACK);}
        glDepthMask(depthWrite?GL_TRUE:GL_FALSE);
        if(surface==SurfaceType::Transparent||surface==SurfaceType::Additive) {
            glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA,surface==SurfaceType::Additive?GL_ONE:GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        } else glDisable(GL_BLEND);
    }

    void RenderCommand::SetBlendEnabled(bool enabled) {
        if (!glad_glEnable || !glad_glDisable) return;
        if (enabled) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
    }

    void RenderCommand::SetBlendAdditive() {
        if (!glad_glBlendFunc) return;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }

    void RenderCommand::SetBlendAlpha() {
        if (!glad_glBlendFunc) return;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void RenderCommand::SetBlendMultiply() {
        if (!glad_glBlendFunc) return;
        glBlendFunc(GL_DST_COLOR, GL_ZERO);
    }

}
