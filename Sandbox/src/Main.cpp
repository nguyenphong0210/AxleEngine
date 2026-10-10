#include <Axle/Window.hpp>

int main() {
    Axle::WindowProps props;
    props.Title = "Axle Sandbox - Step 1";
    props.Width = 1280;
    props.Height = 720;

    Axle::Window window(props);

    while (!window.ShouldClose()) {
        window.PollEvents();
        window.SwapBuffers();
    }

    return 0;
}