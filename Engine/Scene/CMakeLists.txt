add_library(AxleScene STATIC
    src/Scene.cpp
    src/Entity.cpp
    src/SceneSerializer.cpp
    src/AssemblySerializer.cpp
    src/ScriptRegistry.cpp
    src/ScriptHotReloadManager.cpp
    src/ScriptCompiler.cpp
)
add_library(Axle::Scene ALIAS AxleScene)

target_include_directories(AxleScene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/entt/src>
)

target_link_libraries(AxleScene
    PUBLIC
        Axle::Core
        Axle::Renderer
        Axle::Physics
        Axle::Vehicle
        Axle::Asset
)
