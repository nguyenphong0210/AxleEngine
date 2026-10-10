add_library(AxleRenderer STATIC
    src/RenderCommand.cpp
)
add_library(Axle::Renderer ALIAS AxleRenderer)

target_include_directories(AxleRenderer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(AxleRenderer
    PUBLIC
        Axle::Foundation
    PRIVATE
        glad
)