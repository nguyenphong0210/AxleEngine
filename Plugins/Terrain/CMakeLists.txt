cmake_minimum_required(VERSION 3.22)
project(AxleTerrain LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TERRAIN_RUNTIME_SOURCES
    Runtime/src/TerrainData.cpp
    Runtime/src/TerrainCollider.cpp
    Runtime/src/TerrainMesh.cpp
    Runtime/src/TerrainSerializer.cpp
    Runtime/src/TerrainSplineCarver.cpp
    Runtime/src/TerrainPlugin.cpp
    Runtime/src/TerrainPluginRuntime.cpp
)

set(TERRAIN_RUNTIME_HEADERS
    Runtime/include/AxleTerrain/TerrainComponent.hpp
    Runtime/include/AxleTerrain/TerrainSculptTypes.hpp
    Runtime/include/AxleTerrain/TerrainData.hpp
    Runtime/include/AxleTerrain/TerrainChunk.hpp
    Runtime/include/AxleTerrain/TerrainCollider.hpp
    Runtime/include/AxleTerrain/TerrainMesh.hpp
    Runtime/include/AxleTerrain/TerrainSerializer.hpp
    Runtime/include/AxleTerrain/TerrainSplineCarver.hpp
    Runtime/include/AxleTerrain/TerrainPlugin.hpp
    Runtime/include/AxleTerrain/TerrainPluginRuntime.hpp
)

add_library(AxleTerrainRuntime STATIC
    ${TERRAIN_RUNTIME_SOURCES}
    ${TERRAIN_RUNTIME_HEADERS}
)
add_library(Axle::TerrainRuntime ALIAS AxleTerrainRuntime)
add_library(AxleTerrain ALIAS AxleTerrainRuntime)
add_library(Axle::Terrain ALIAS AxleTerrainRuntime)

target_include_directories(AxleTerrainRuntime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Runtime/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Engine/PluginSDK/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/entt/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/enkiTS/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/JoltPhysics>
)

target_link_libraries(AxleTerrainRuntime PUBLIC
    Axle::Core
    Axle::Renderer
    Axle::Physics
    Axle::Scene
    Axle::SplineRuntime
)

set(TERRAIN_EDITOR_SOURCES
    Editor/src/TerrainSculptor.cpp
    Editor/src/TerrainEditor.cpp
    Editor/src/TerrainPluginEditor.cpp
)

set(TERRAIN_EDITOR_HEADERS
    Editor/include/AxleTerrainEditor/TerrainSculptor.hpp
    Editor/include/AxleTerrainEditor/TerrainEditor.hpp
    Editor/include/AxleTerrainEditor/TerrainPluginEditor.hpp
)

add_library(AxleTerrainEditor STATIC
    ${TERRAIN_EDITOR_SOURCES}
    ${TERRAIN_EDITOR_HEADERS}
)
add_library(Axle::TerrainEditor ALIAS AxleTerrainEditor)

target_include_directories(AxleTerrainEditor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Runtime/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Editor/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Engine/PluginSDK/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/entt/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/imgui>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/imgui/backends>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/ImGuizmo/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/enkiTS/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/JoltPhysics>
)

target_link_libraries(AxleTerrainEditor PUBLIC
    AxleTerrainRuntime
    AxleSplineRuntime
    AxleSplineEditor
    Axle::Core
    Axle::Scene
)
