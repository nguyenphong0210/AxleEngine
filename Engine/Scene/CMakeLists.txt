add_library(AxleScene STATIC
    src/Scene.cpp
    src/Entity.cpp
    src/SceneSerializer.cpp
    src/AssemblySerializer.cpp
    src/ScriptRegistry.cpp
    src/ScriptHotReloadManager.cpp
    src/ScriptCompiler.cpp
    src/SceneRenderer.cpp
    src/RHISceneRenderer.cpp
    src/RHIObjectPicker.cpp
    src/RHIMeshRegistry.cpp
    src/CharacterVFX.cpp
)
add_library(Axle::Scene ALIAS AxleScene)

target_include_directories(AxleScene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/entt/src>
)

target_link_libraries(AxleScene
    PUBLIC
        Axle::Core
        Axle::RHI
        Axle::Renderer
        Axle::Animation
        Axle::Physics
        Axle::Vehicle
        Axle::Asset
        Axle::Audio
        Axle::VFX
        Axle::UI2D
)

