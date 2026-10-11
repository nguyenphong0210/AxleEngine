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
    Axle::Asset
    Axle::UI
    Axle::Physics
    Axle::Scene
    Axle::Vehicle
    Axle::Project
)

# Synchronize Sandbox/assets to binary output directory on post-build
add_custom_command(TARGET SandboxApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:SandboxApp>/assets
)