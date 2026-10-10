#include <Axle/Window.hpp>
#include <Axle/Input.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/VertexArray.hpp>
#include <GLFW/glfw3.h>
#include <memory>
#include <iostream>

int main() {
    Axle::WindowProps props;
    props.Title = "Axle Engine - Colored Triangle";
    props.Width = 1280;
    props.Height = 720;
    props.VSync = true;

    Axle::Window window(props);

    if (!Axle::RenderCommand::Init(reinterpret_cast<Axle::GLProcAddressFn>(glfwGetProcAddress))) {
        return -1;
    }

    Axle::RenderCommand::SetViewport(0, 0, window.GetWidth(), window.GetHeight());
    window.SetWindowResizeCallback([](const Axle::WindowResizeEvent& e) {
        Axle::RenderCommand::SetViewport(0, 0, e.Width, e.Height);
    });

    // 1. Shaders
    const std::string vertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec4 a_Color;

        out vec4 v_Color;

        void main() {
            v_Color = a_Color;
            gl_Position = vec4(a_Position, 1.0);
        }
    )";

    const std::string fragmentSrc = R"(
        #version 450 core
        layout(location = 0) out vec4 o_Color;

        in vec4 v_Color;

        void main() {
            o_Color = v_Color;
        }
    )";

    auto shader = std::make_unique<Axle::Shader>(vertexSrc, fragmentSrc);

    // 2. Geometry data (x, y, z, r, g, b, a)
    float vertices[3 * 7] = {
        -0.5f, -0.5f, 0.0f,   1.0f, 0.2f, 0.2f, 1.0f,  // Bottom left (Red)
         0.5f, -0.5f, 0.0f,   0.2f, 1.0f, 0.2f, 1.0f,  // Bottom right (Green)
         0.0f,  0.5f, 0.0f,   0.2f, 0.4f, 1.0f, 1.0f   // Top center (Blue)
    };

    auto vertexArray = std::make_shared<Axle::VertexArray>();

    auto vertexBuffer = std::make_shared<Axle::VertexBuffer>(vertices, sizeof(vertices));
    vertexBuffer->SetLayout({
        { Axle::ShaderDataType::Float3, "a_Position" },
        { Axle::ShaderDataType::Float4, "a_Color" }
    });
    vertexArray->AddVertexBuffer(vertexBuffer);

    uint32_t indices[3] = { 0, 1, 2 };
    auto indexBuffer = std::make_shared<Axle::IndexBuffer>(indices, 3);
    vertexArray->SetIndexBuffer(indexBuffer);

    Axle::RenderCommand::SetClearColor({ 0.12f, 0.14f, 0.18f, 1.0f });

    while (!window.ShouldClose()) {
        window.PollEvents();

        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Escape)) {
            break;
        }

        Axle::RenderCommand::Clear();

        // Bind shader and submit draw command
        shader->Bind();
        Axle::RenderCommand::DrawIndexed(vertexArray);

        window.SwapBuffers();
    }

    return 0;
}