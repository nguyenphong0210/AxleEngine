#include <Axle/Window.hpp>
#include <Axle/Input.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/VertexArray.hpp>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <Axle/Log.hpp>
#include <memory>

int main() {
    Axle::Log::Init();
    AXLE_CORE_INFO("Initializing Axle Engine...");
    AXLE_INFO("Starting SandboxApp...");

    Axle::WindowProps props;
    props.Title = "Axle Engine - Uniforms & Matrix Transform";
    props.Width = 1280;
    props.Height = 720;
    props.VSync = true;

    Axle::Window window(props);

    if (!Axle::RenderCommand::Init(reinterpret_cast<Axle::GLProcAddressFn>(glfwGetProcAddress))) {
        AXLE_CORE_CRITICAL("Failed to initialize RenderCommand!");
        return -1;
    }

    Axle::RenderCommand::SetViewport(0, 0, window.GetWidth(), window.GetHeight());
    window.SetWindowResizeCallback([](const Axle::WindowResizeEvent& e) {
        Axle::RenderCommand::SetViewport(0, 0, e.Width, e.Height);
    });

    // 1. Shaders supporting MVP transform matrices
    const std::string vertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec4 a_Color;

        uniform mat4 u_Transform;

        out vec4 v_Color;

        void main() {
            v_Color = a_Color;
            gl_Position = u_Transform * vec4(a_Position, 1.0);
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

    // 2. Triangle Geometry
    float vertices[3 * 7] = {
        -0.5f, -0.5f, 0.0f,   1.0f, 0.2f, 0.2f, 1.0f,
         0.5f, -0.5f, 0.0f,   0.2f, 1.0f, 0.2f, 1.0f,
         0.0f,  0.5f, 0.0f,   0.2f, 0.4f, 1.0f, 1.0f
    };

    auto vertexArray = std::make_shared<Axle::VertexArray>();
    auto vertexBuffer = std::make_shared<Axle::VertexBuffer>(vertices, static_cast<Axle::u32>(sizeof(vertices)));
    vertexBuffer->SetLayout({
        { Axle::ShaderDataType::Float3, "a_Position" },
        { Axle::ShaderDataType::Float4, "a_Color" }
    });
    vertexArray->AddVertexBuffer(vertexBuffer);

    uint32_t indices[3] = { 0, 1, 2 };
    auto indexBuffer = std::make_shared<Axle::IndexBuffer>(indices, 3);
    vertexArray->SetIndexBuffer(indexBuffer);

    Axle::RenderCommand::SetClearColor({ 0.12f, 0.14f, 0.18f, 1.0f });

    float rotationAngle = 0.0f;

    while (!window.ShouldClose()) {
        window.PollEvents();

        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Escape)) {
            AXLE_INFO("Escape key pressed. Exiting...");
            break;
        }

        Axle::RenderCommand::Clear();

        shader->Bind();

        // Increment rotation over time
        rotationAngle += 0.02f;
        glm::mat4 transform = glm::rotate(glm::mat4(1.0f), rotationAngle, glm::vec3(0.0f, 0.0f, 1.0f));

        // Upload matrix uniform to GPU
        shader->SetMat4("u_Transform", transform);

        Axle::RenderCommand::DrawIndexed(vertexArray);

        window.SwapBuffers();
    }

    return 0;
}