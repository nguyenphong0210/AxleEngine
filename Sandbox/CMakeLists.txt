add_executable(SandboxApp
    src/Main.cpp
    src/SandboxLayer.cpp
)

target_include_directories(SandboxApp PRIVATE
    src
)

target_link_libraries(SandboxApp PRIVATE
    Axle::Core
    Axle::Platform
    Axle::Renderer
    Axle::UI
    Axle::Physics
)