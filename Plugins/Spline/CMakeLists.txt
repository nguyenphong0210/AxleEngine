cmake_minimum_required(VERSION 3.22)
project(AxleSpline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SPLINE_SOURCES
    src/SplinePlugin.cpp
    src/Curve3D.cpp
    src/TrackGenerator.cpp
    src/SplineEditor.cpp
    src/SplineSerializer.cpp
)

set(SPLINE_HEADERS
    include/AxleSpline/SplinePlugin.hpp
    include/AxleSpline/Curve3D.hpp
    include/AxleSpline/SplineComponent.hpp
    include/AxleSpline/TrackGenerator.hpp
    include/AxleSpline/SplineEditor.hpp
    include/AxleSpline/SplineSerializer.hpp
)

add_library(AxleSpline STATIC
    ${SPLINE_SOURCES}
    ${SPLINE_HEADERS}
)
add_library(Axle::Spline ALIAS AxleSpline)

target_include_directories(AxleSpline PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/entt/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/imgui>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/imgui/backends>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/ImGuizmo/src>
)

target_link_libraries(AxleSpline PUBLIC
    Axle::Core
    Axle::Renderer
    Axle::Physics
    Axle::Scene
    Axle::UI
)
