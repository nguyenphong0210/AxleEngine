add_library(AxleRenderer STATIC
    src/RenderCommand.cpp
    src/Shader.cpp
    src/Buffer.cpp
    src/VertexArray.cpp
    src/PerspectiveCamera.cpp
    src/PerspectiveCameraController.cpp
    src/PrimitiveMesh.cpp
)
add_library(Axle::Renderer ALIAS AxleRenderer)

target_include_directories(AxleRenderer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(AxleRenderer
    PUBLIC
        Axle::Core
        Axle::Platform
    PRIVATE
        glad
)