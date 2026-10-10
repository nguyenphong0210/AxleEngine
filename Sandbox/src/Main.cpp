#include <Axle/Application.hpp>
#include "SandboxLayer.hpp"

int main() {
    Axle::ApplicationSpecification spec{
        .Name = "Axle Engine - 3D Free-Fly Camera & Jolt Physics",
        .Width = 1280,
        .Height = 720,
        .VSync = true
    };

    auto app = std::make_unique<Axle::Application>(spec);
    app->PushLayer(std::make_shared<SandboxLayer>());
    app->Run();

    return 0;
}