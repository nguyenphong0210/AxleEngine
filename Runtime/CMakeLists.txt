add_library(AxleRuntimePlayer STATIC
    src/RuntimeLayer.cpp
    src/RuntimeStartup.cpp
    src/RuntimeRenderContext.cpp
)
add_library(Axle::RuntimePlayer ALIAS AxleRuntimePlayer)

add_executable(AxleRuntime EXCLUDE_FROM_ALL src/Main.cpp)
target_link_libraries(AxleRuntime PRIVATE Axle::RuntimePlayer)

target_include_directories(AxleRuntimePlayer PUBLIC
    src
)

target_link_libraries(AxleRuntimePlayer PUBLIC
    Axle::Core
    Axle::RHI
    Axle::Platform
    Axle::Renderer
    Axle::Audio
    Axle::Asset
    Axle::Physics
    Axle::Scene
    Axle::Project
    Axle::UI2D
    Axle::Spline
    Axle::Terrain
    Axle::Animation
    Axle::VFX
)

add_dependencies(AxleRuntime AxleScripts)

# Stage engine resources and scripts only; project assets are supplied by export
add_custom_command(TARGET AxleRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:AxleScripts>
    $<TARGET_FILE_DIR:AxleRuntime>/$<TARGET_FILE_NAME:AxleScripts>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Engine/Resources
    $<TARGET_FILE_DIR:AxleRuntime>/Resources
)
