add_executable(SandboxApp
    src/Main.cpp
)

target_link_libraries(SandboxApp PRIVATE
    Axle::Core
    Axle::Platform
    Axle::Renderer
    Axle::UI
    Axle::Physics
)