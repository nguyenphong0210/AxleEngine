add_library(AxleScene STATIC
    src/SceneData.cpp
    src/ComponentCodec.cpp
    src/Scene.cpp
    src/Entity.cpp
    src/SceneCommandBuffer.cpp
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
add_library(Axle::SceneData ALIAS AxleScene)

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
        Axle::Asset
        Axle::Audio
        Axle::VFX
        Axle::UI2D
)

