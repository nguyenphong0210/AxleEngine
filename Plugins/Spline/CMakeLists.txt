if(COMMAND axle_add_plugin_module)
    axle_add_plugin_module(SplineRuntime runtime Runtime/src/SplinePluginRuntime.cpp)
    if(TARGET SplineRuntime)
        target_include_directories(SplineRuntime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/include")
    endif()
    axle_add_plugin_module(SplineEditor editor Editor/src/SplinePluginEditor.cpp)
    if(TARGET SplineEditor)
        target_link_libraries(SplineEditor PRIVATE SplineRuntime)
    endif()
    return()
endif()

# Development-only domain prototypes; not the selected SDK DLL graph.

cmake_minimum_required(VERSION 3.22)
project(AxleSpline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Module 1: SplineRuntime (Headless, no ImGui, pure runtime curve math and Jolt physics)
add_library(AxleSplineRuntime STATIC EXCLUDE_FROM_ALL
    Runtime/src/Curve3D.cpp
    Runtime/src/TrackGenerator.cpp
    Runtime/src/SplineSerializer.cpp
    Runtime/src/SplinePlugin.cpp
)
add_library(Axle::SplineRuntime ALIAS AxleSplineRuntime)
add_library(AxleSpline ALIAS AxleSplineRuntime)
add_library(Axle::Spline ALIAS AxleSplineRuntime)

target_include_directories(AxleSplineRuntime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Runtime/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Engine/PluginSDK/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/entt/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/JoltPhysics>
)

target_link_libraries(AxleSplineRuntime PUBLIC
    Axle::Core
    Axle::Renderer
    Axle::Physics
    Axle::Scene
)

# Module 2: SplineEditor (Editor Inspector and Viewport Gizmos)
add_library(AxleSplineEditor STATIC EXCLUDE_FROM_ALL
    Editor/src/SplineEditor.cpp
)
add_library(Axle::SplineEditor ALIAS AxleSplineEditor)

target_include_directories(AxleSplineEditor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Editor/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Runtime/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Engine/PluginSDK/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/entt/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/imgui>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/imgui/backends>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/ImGuizmo/src>
)

target_link_libraries(AxleSplineEditor PUBLIC
    Axle::Core
    Axle::Scene
)
