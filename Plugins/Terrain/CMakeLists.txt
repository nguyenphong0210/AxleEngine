cmake_minimum_required(VERSION 3.22)
project(AxleTerrain LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TERRAIN_SOURCES
    src/TerrainPlugin.cpp
    src/TerrainData.cpp
    src/TerrainCollider.cpp
    src/TerrainMesh.cpp
    src/TerrainSculptor.cpp
    src/TerrainSplineCarver.cpp
    src/TerrainEditor.cpp
    src/TerrainSerializer.cpp
)

set(TERRAIN_HEADERS
    include/AxleTerrain/TerrainPlugin.hpp
    include/AxleTerrain/TerrainData.hpp
    include/AxleTerrain/TerrainCollider.hpp
    include/AxleTerrain/TerrainChunk.hpp
    include/AxleTerrain/TerrainMesh.hpp
    include/AxleTerrain/TerrainSculptor.hpp
    include/AxleTerrain/TerrainSplineCarver.hpp
    include/AxleTerrain/TerrainComponent.hpp
    include/AxleTerrain/TerrainEditor.hpp
    include/AxleTerrain/TerrainSerializer.hpp
)

add_library(AxleTerrain STATIC
    ${TERRAIN_SOURCES}
    ${TERRAIN_HEADERS}
)
add_library(Axle::Terrain ALIAS AxleTerrain)

target_include_directories(AxleTerrain PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/entt/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/imgui>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/imgui/backends>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/ImGuizmo/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/enkiTS/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/JoltPhysics>
)

target_link_libraries(AxleTerrain PUBLIC
    Axle::Core
    Axle::Renderer
    Axle::Physics
    Axle::Scene
    Axle::UI
    Axle::Spline
)
