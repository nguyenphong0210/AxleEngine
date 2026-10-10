#include <Axle/RenderCommand.hpp>
#include <glad/glad.h>
#include <iostream>

namespace Axle {

    bool RenderCommand::Init(GLProcAddressFn procAddressLoader) {
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(procAddressLoader))) {
            std::cerr << "[OpenGL Error] Failed to initialize GLAD loader!\n";
            return false;
        }

        std::cout << "[Renderer Info] Vendor:   " << glGetString(GL_VENDOR) << "\n";
        std::cout << "[Renderer Info] GPU:      " << glGetString(GL_RENDERER) << "\n";
        std::cout << "[Renderer Info] Version:  " << glGetString(GL_VERSION) << "\n";

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
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

}