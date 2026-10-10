#include <Axle/Window.hpp>
#include <Axle/Input.hpp>
#include <iostream>

int main() {
    Axle::WindowProps props;
    props.Title = "Axle Sandbox - Full GLFW Platform";
    props.Width = 1280;
    props.Height = 720;
    props.VSync = true;

    Axle::Window window(props);

    // Register event callbacks
    window.SetWindowResizeCallback([](const Axle::WindowResizeEvent& e) {
        std::cout << "[Event] Window Resized: " << e.Width << "x" << e.Height << "\n";
    });

    window.SetWindowCloseCallback([](const Axle::WindowCloseEvent&) {
        std::cout << "[Event] Window Close Requested!\n";
    });

    window.SetKeyPressedCallback([](const Axle::KeyPressedEvent& e) {
        std::cout << "[Event] Key Pressed: " << static_cast<Axle::u16>(e.Key) 
                  << " (Repeat: " << e.IsRepeat << ")\n";
    });

    window.SetMouseScrolledCallback([](const Axle::MouseScrolledEvent& e) {
        std::cout << "[Event] Mouse Scrolled: X=" << e.XOffset << ", Y=" << e.YOffset << "\n";
    });

    window.SetMouseButtonPressedCallback([](const Axle::MouseButtonPressedEvent& e) {
        if (e.Button == Axle::MouseCode::ButtonLeft) {
            std::cout << "[Event] Left Mouse Button Clicked!\n";
        } else if (e.Button == Axle::MouseCode::ButtonRight) {
            std::cout << "[Event] Right Mouse Button Clicked!\n";
        }
    });

    window.SetMouseButtonReleasedCallback([](const Axle::MouseButtonReleasedEvent& e) {
        if (e.Button == Axle::MouseCode::ButtonLeft) {
            std::cout << "[Event] Left Mouse Button Released!\n";
        }
    });

    std::cout << "Platform initialized successfully. Test inputs (Resize window, scroll, press keys, ESC to exit)...\n";

    while (!window.ShouldClose()) {
        window.PollEvents();

        // Polling check: ESC to exit
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Escape)) {
            std::cout << "[Polling] Escape key detected. Exiting loop.\n";
            break;
        }

        // Polling check: Space key
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Space)) {
            auto [mx, my] = Axle::Input::GetMousePosition();
            std::cout << "[Polling] Space held! Mouse at (" << mx << ", " << my << ")\n";
        }

        window.SwapBuffers();
    }

    return 0;
}