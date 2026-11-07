add_library(AxleScene STATIC
    src/Scene.cpp
    src/RuntimeEntity.cpp
    src/RuntimeComponentCodec.cpp
    src/ScenePhysicsBridge.cpp
    src/SceneAudioBridge.cpp
    src/SceneScriptBridge.cpp
    src/SceneCharacterBridge.cpp
    src/SceneRenderBridge.cpp
    src/AnimationSceneBinding.cpp
    src/SceneSerializer.cpp
    src/AssemblySerializer.cpp
    src/ScriptRegistry.cpp
    src/ScriptHotReloadManager.cpp
    src/ScriptCompiler.cpp
    src/SceneRenderer.cpp
    src/MaterialTextureCache.cpp
    src/RHISceneRenderer.cpp
    src/RHIObjectPicker.cpp
    src/RHIMeshRegistry.cpp
    src/CharacterVFX.cpp
    src/CustomMaterialProgram.cpp
    src/SimulationClock.cpp
    src/SimulationInput.cpp
    src/SimulationDriver.cpp
)
add_library(Axle::Scene ALIAS AxleScene)

target_include_directories(AxleScene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/entt/src>
)

target_link_libraries(AxleScene
    PUBLIC
        Axle::SceneData
        Axle::Core
        Axle::RHI
        Axle::Renderer
        Axle::Animation
        Axle::Physics
        Axle::Asset
        Axle::Audio
        Axle::VFX
        Axle::UI2D
)

