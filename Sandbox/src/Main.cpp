#include <Axle/Types.hpp>
#include <Axle/Log.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/Window.hpp>
#include <Axle/Input.hpp>
#include <Axle/KeyCodes.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/PerspectiveCameraController.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <Axle/DebugPanel.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/PhysicsScene.hpp>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>

int main() {
    Axle::Log::Init();
    Axle::PhysicsEngine::Init();
    AXLE_CORE_INFO("Starting 3D Sandbox with Free-Fly Camera & Jolt Physics...");

    Axle::WindowProps props;
    props.Title = "Axle Engine - 3D Free-Fly Camera & Physics";
    props.Width = 1280;
    props.Height = 720;
    props.VSync = true;

    Axle::Window window(props);

    if (!Axle::RenderCommand::Init(reinterpret_cast<Axle::GLProcAddressFn>(glfwGetProcAddress))) {
        AXLE_CORE_CRITICAL("Failed to initialize renderer context!");
        return -1;
    }

    Axle::RenderCommand::SetViewport(0, 0, window.GetWidth(), window.GetHeight());

    auto imguiLayer = std::make_unique<Axle::ImGuiLayer>();
    imguiLayer->Init(window.GetNativeWindow());

    Axle::DebugPanel debugPanel;

    // 1. Initialize Jolt Physics Scene
    auto physicsScene = std::make_unique<Axle::PhysicsScene>();

    // Sàn tĩnh kích thước 100m x 1m x 100m (half-extents: 50 x 0.5 x 50) đặt tại Y = -0.5f => Mặt sàn tại Y = 0.0f
    const Axle::BodyID floorBodyID = physicsScene->CreateBox(
        glm::vec3(0.0f, -0.5f, 0.0f),
        glm::vec3(50.0f, 0.5f, 50.0f),
        false, // Static
        0.6f,  // Friction
        0.2f   // Restitution
    );

    // Khối hộp rơi tự do kích thước 1m x 1m x 1m (half-extents: 0.5 x 0.5 x 0.5) xuất phát tại Y = 10.0f
    const Axle::BodyID boxBodyID = physicsScene->CreateBox(
        glm::vec3(0.0f, 10.0f, 0.0f),
        glm::vec3(0.5f, 0.5f, 0.5f),
        true,  // Dynamic
        0.5f,  // Friction
        0.4f   // Restitution (nảy nhẹ khi chạm sàn)
    );

    // Tối ưu broad-phase sau khi thêm sàn tĩnh
    physicsScene->OptimizeBroadPhase();

    // 2. Camera Controller góc nhìn bao quát sàn và khối hộp
    auto cameraController = std::make_unique<Axle::PerspectiveCameraController>(
        45.0f,
        static_cast<Axle::f32>(window.GetWidth()) / static_cast<Axle::f32>(window.GetHeight()),
        0.1f,
        1000.0f
    );
    cameraController->SetPosition(glm::vec3(0.0f, 6.0f, 18.0f));
    cameraController->SetRotation(glm::vec3(-12.0f, 0.0f, 0.0f));

    window.SetWindowResizeCallback([&cameraController](const Axle::WindowResizeEvent& e) {
        if (e.Width > 0 && e.Height > 0) {
            Axle::RenderCommand::SetViewport(0, 0, e.Width, e.Height);
            cameraController->OnResize(e.Width, e.Height);
        }
    });

    // 3. Shaders với Uniform Tint
    const std::string vertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec4 a_Color;

        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;
        uniform vec4 u_ColorTint;

        out vec4 v_Color;

        void main() {
            v_Color = a_Color * u_ColorTint;
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

    // 4. Mesh Unit Cube [-0.5, 0.5] với màu đa sắc cho 6 mặt
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

        // Nhấn phím 'R' để thả rơi lại khối hộp từ Y = 10m
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::R)) {
            physicsScene->SetBodyPositionAndRotation(boxBodyID, glm::vec3(0.0f, 10.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
            physicsScene->SetLinearVelocity(boxBodyID, glm::vec3(0.0f));
        }

        // Cập nhật Camera controller
        cameraController->OnUpdate(ts);

        // Mô phỏng Jolt Physics
        physicsScene->Step(ts);

        // Render pass
        Axle::RenderCommand::Clear();

        shader->Bind();
        shader->SetMat4("u_ViewProjection", cameraController->GetCamera().GetViewProjectionMatrix());

        // 1. Render Sàn tĩnh (scale từ unit cube 1x1x1 thành 100x1x100)
        const glm::mat4 floorTransform = physicsScene->GetBodyTransform(floorBodyID) * glm::scale(glm::mat4(1.0f), glm::vec3(100.0f, 1.0f, 100.0f));
        shader->SetMat4("u_Transform", floorTransform);
        shader->SetFloat4("u_ColorTint", glm::vec4(0.25f, 0.28f, 0.32f, 1.0f));
        Axle::RenderCommand::DrawIndexed(vertexArray);

        // 2. Render Khối hộp rơi tự do (đồng bộ ma trận transform trực tiếp từ Jolt Physics)
        const glm::mat4 boxTransform = physicsScene->GetBodyTransform(boxBodyID);
        shader->SetMat4("u_Transform", boxTransform);
        shader->SetFloat4("u_ColorTint", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        Axle::RenderCommand::DrawIndexed(vertexArray);

        // 3. Render UI & Debug Telemetry
        imguiLayer->Begin();
        Axle::TelemetryData telemetry{
            .DeltaTime = ts.GetSeconds(),
            .FPS = ts.GetSeconds() > 0.0f ? 1.0f / ts.GetSeconds() : 0.0f,
            .CameraPosition = cameraController->GetCamera().GetPosition(),
            .CameraRotation = cameraController->GetCamera().GetRotation(),
            .HasPhysics = true,
            .BoxPosition = physicsScene->GetBodyPosition(boxBodyID),
            .BoxVelocity = physicsScene->GetLinearVelocity(boxBodyID),
            .IsBoxActive = physicsScene->IsActive(boxBodyID),
            .TotalBodies = physicsScene->GetNumBodies(),
            .ActiveBodies = physicsScene->GetNumActiveBodies()
        };
        debugPanel.OnImGuiRender(telemetry);
        imguiLayer->End();

        window.SwapBuffers();
    }

    imguiLayer->Shutdown();
    physicsScene.reset(); // Destroy bodies & physics scene before shutting down engine
    Axle::PhysicsEngine::Shutdown();

    return 0;
}