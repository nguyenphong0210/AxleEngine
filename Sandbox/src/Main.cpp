#include <Axle/Window.hpp>
#include <Axle/Input.hpp>
#include <Axle/RenderCommand.hpp>
#include <GLFW/glfw3.h>
#include <iostream>

int main() {
    Axle::WindowProps props;
    props.Title = "Axle Engine - OpenGL 4.5 Context";
    props.Width = 1280;
    props.Height = 720;
    props.VSync = true;

    Axle::Window window(props);

    // Initialize OpenGL via GLAD
    // Pass glfwGetProcAddress explicitly
    if (!Axle::RenderCommand::Init(reinterpret_cast<Axle::GLProcAddressFn>(glfwGetProcAddress))) {
        return -1;
    }

    // Set initial viewport
    Axle::RenderCommand::SetViewport(0, 0, window.GetWidth(), window.GetHeight());

    // Hook window resize callback to update viewport automatically
    window.SetWindowResizeCallback([](const Axle::WindowResizeEvent& e) {
        Axle::RenderCommand::SetViewport(0, 0, e.Width, e.Height);
    });

    // Default clear color: slate gray
    glm::vec4 clearColor = { 0.12f, 0.14f, 0.18f, 1.0f };

    while (!window.ShouldClose()) {
        window.PollEvents();

        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Escape)) {
            break;
        }

        // Change color dynamically based on mouse click
        if (Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonLeft)) {
            clearColor = { 0.15f, 0.35f, 0.25f, 1.0f }; // Dark forest green
        } else if (Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight)) {
            clearColor = { 0.35f, 0.15f, 0.15f, 1.0f }; // Dark crimson
        } else {
            clearColor = { 0.12f, 0.14f, 0.18f, 1.0f }; // Default slate
        }

        Axle::RenderCommand::SetClearColor(clearColor);
        Axle::RenderCommand::Clear();

        window.SwapBuffers();
    }

    return 0;
}