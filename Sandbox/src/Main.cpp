#include <Axle/Types.hpp>
#include <Axle/Log.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/Window.hpp>
#include <Axle/Input.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/PerspectiveCameraController.hpp>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>

int main() {
    Axle::Log::Init();
    AXLE_CORE_INFO("Starting 3D Sandbox with Free-Fly Camera...");

    Axle::WindowProps props;
    props.Title = "Axle Engine - 3D Free-Fly Camera";
    props.Width = 1280;
    props.Height = 720;
    props.VSync = true;

    Axle::Window window(props);

    if (!Axle::RenderCommand::Init(reinterpret_cast<Axle::GLProcAddressFn>(glfwGetProcAddress))) {
        AXLE_CORE_CRITICAL("Failed to initialize renderer context!");
        return -1;
    }

    Axle::RenderCommand::SetViewport(0, 0, window.GetWidth(), window.GetHeight());

    // Camera Controller dùng Axle::f32
    auto cameraController = std::make_unique<Axle::PerspectiveCameraController>(
        45.0f,
        static_cast<Axle::f32>(window.GetWidth()) / static_cast<Axle::f32>(window.GetHeight()),
        0.1f,
        1000.0f
    );

    window.SetWindowResizeCallback([&cameraController](const Axle::WindowResizeEvent& e) {
        if (e.Width > 0 && e.Height > 0) {
            Axle::RenderCommand::SetViewport(0, 0, e.Width, e.Height);
            cameraController->OnResize(e.Width, e.Height);
        }
    });

    // Shaders
    const std::string vertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec4 a_Color;

        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;

        out vec4 v_Color;

        void main() {
            v_Color = a_Color;
            gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
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

    // Cube Mesh Data dùng Axle::f32
    Axle::f32 vertices[8 * 7] = {
        -0.5f, -0.5f, -0.5f,   0.9f, 0.2f, 0.2f, 1.0f,
         0.5f, -0.5f, -0.5f,   0.2f, 0.9f, 0.2f, 1.0f,
         0.5f,  0.5f, -0.5f,   0.2f, 0.2f, 0.9f, 1.0f,
        -0.5f,  0.5f, -0.5f,   0.9f, 0.9f, 0.2f, 1.0f,
        -0.5f, -0.5f,  0.5f,   0.9f, 0.2f, 0.9f, 1.0f,
         0.5f, -0.5f,  0.5f,   0.2f, 0.9f, 0.9f, 1.0f,
         0.5f,  0.5f,  0.5f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,   0.3f, 0.3f, 0.3f, 1.0f
    };

    Axle::u32 indices[36] = {
        4, 5, 6,  6, 7, 4,
        1, 0, 3,  3, 2, 1,
        0, 4, 7,  7, 3, 0,
        5, 1, 2,  2, 6, 5,
        7, 6, 2,  2, 3, 7,
        0, 1, 5,  5, 4, 0
    };

    auto vertexArray = std::make_shared<Axle::VertexArray>();
    auto vertexBuffer = std::make_shared<Axle::VertexBuffer>(vertices, static_cast<Axle::u32>(sizeof(vertices)));
    vertexBuffer->SetLayout({
        { Axle::ShaderDataType::Float3, "a_Position" },
        { Axle::ShaderDataType::Float4, "a_Color" }
    });
    vertexArray->AddVertexBuffer(vertexBuffer);

    auto indexBuffer = std::make_shared<Axle::IndexBuffer>(indices, 36);
    vertexArray->SetIndexBuffer(indexBuffer);

    Axle::RenderCommand::SetClearColor({ 0.10f, 0.12f, 0.15f, 1.0f });

    Axle::f32 lastFrameTime = 0.0f;

    while (!window.ShouldClose()) {
        window.PollEvents();

        const auto time = static_cast<Axle::f32>(glfwGetTime());
        Axle::Timestep ts = time - lastFrameTime;
        lastFrameTime = time;

        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Escape)) {
            break;
        }

        // Cập nhật Camera controller
        cameraController->OnUpdate(ts);

        Axle::RenderCommand::Clear();

        shader->Bind();

        glm::mat4 model = glm::mat4(1.0f);

        shader->SetMat4("u_ViewProjection", cameraController->GetCamera().GetViewProjectionMatrix());
        shader->SetMat4("u_Transform", model);

        Axle::RenderCommand::DrawIndexed(vertexArray);

        window.SwapBuffers();
    }

    return 0;
}