add_library(AxleCore STATIC
    src/Log.cpp
    src/LayerStack.cpp
    src/Application.cpp
    src/FileSystem.cpp
    src/UUID.cpp
    src/JobSystem.cpp
    src/Package.cpp
)
add_library(Axle::Core ALIAS AxleCore)
add_library(Axle::Foundation ALIAS AxleCore)

target_include_directories(AxleCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glfw/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Engine/Platform/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Engine/Renderer/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Engine/Physics/include>
)

target_link_libraries(AxleCore PUBLIC
    spdlog::spdlog
    glfw
    enkiTS
)