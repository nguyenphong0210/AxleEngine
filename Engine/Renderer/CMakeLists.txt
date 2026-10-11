add_library(AxleRenderer STATIC
    src/RenderCommand.cpp
    src/Shader.cpp
    src/Buffer.cpp
    src/VertexArray.cpp
    src/PerspectiveCamera.cpp
    src/PerspectiveCameraController.cpp
    src/PrimitiveMesh.cpp
    src/Framebuffer.cpp
    src/ShadowMap.cpp
    src/Texture2D.cpp
    src/stb_image.cpp
)
add_library(Axle::Renderer ALIAS AxleRenderer)

target_include_directories(AxleRenderer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/stb>
)

target_link_libraries(AxleRenderer
    PUBLIC
        Axle::Core
        Axle::Platform
    PRIVATE
        glad
)