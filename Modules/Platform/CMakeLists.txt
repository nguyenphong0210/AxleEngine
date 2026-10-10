add_library(AxlePlatform STATIC
    src/Window.cpp
)
add_library(Axle::Platform ALIAS AxlePlatform)

target_include_directories(AxlePlatform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    ${CMAKE_SOURCE_DIR}/ThirdParty/glfw/include
)

target_link_libraries(AxlePlatform
    PUBLIC 
        Axle::Foundation
    PRIVATE 
        glfw
)