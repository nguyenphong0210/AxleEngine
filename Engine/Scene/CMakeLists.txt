set(native_sky_source "${CMAKE_SOURCE_DIR}/Engine/Resources/Shaders/AtmosphericSkybox.hlsl")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${native_sky_source}")
file(READ "${native_sky_source}" AXLE_NATIVE_SKY)
string(REPLACE "\n" "\n)AXLESKY\"\nR\"AXLESKY(" AXLE_NATIVE_SKY "${AXLE_NATIVE_SKY}")
configure_file(src/NativeSkySource.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/NativeSkySource.hpp" @ONLY)

set(native_forward_source "${CMAKE_SOURCE_DIR}/Engine/Resources/Shaders/ForwardPBR.hlsl")
set(native_forward_common "${CMAKE_SOURCE_DIR}/Engine/Resources/Shaders/PBRCommon.hlsli")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${native_forward_source}" "${native_forward_common}")
file(READ "${native_forward_source}" AXLE_NATIVE_FORWARD)
file(READ "${native_forward_common}" AXLE_NATIVE_FORWARD_COMMON)
string(REPLACE "#include \"PBRCommon.hlsli\"" "${AXLE_NATIVE_FORWARD_COMMON}" AXLE_NATIVE_FORWARD "${AXLE_NATIVE_FORWARD}")
string(REPLACE "\n" "\n)AXLEFORWARD\"\nR\"AXLEFORWARD(" AXLE_NATIVE_FORWARD "${AXLE_NATIVE_FORWARD}")
configure_file(src/NativeForwardSource.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/NativeForwardSource.hpp" @ONLY)

set(cascade_depth_source "${CMAKE_SOURCE_DIR}/Engine/Resources/Shaders/CascadedShadowDepth.hlsl")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${cascade_depth_source}")
file(READ "${cascade_depth_source}" AXLE_CASCADE_DEPTH)
configure_file(src/CascadeDepthSource.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/CascadeDepthSource.hpp" @ONLY)

add_library(AxleScene STATIC
    src/Scene.cpp
    src/RuntimeEntity.cpp
    src/RuntimeComponentCodec.cpp
    src/ScenePhysicsBridge.cpp
    src/SceneAudioBridge.cpp
    src/SceneScriptBridge.cpp
    src/SceneCharacterBridge.cpp
    src/SceneRenderBridge.cpp
    src/SceneExtraction.cpp
    src/RenderService.cpp
    src/RenderRollout.cpp
    src/RenderSnapshotQueue.cpp
    src/RenderWorker.cpp
    src/RenderWorkerCapture.cpp
    src/MeshResourceResolver.cpp
    src/AnimationSceneBinding.cpp
    src/SceneSerializer.cpp
    src/AssemblySerializer.cpp
    src/MaterialComponentCodec.cpp
    src/ScriptRegistry.cpp
    src/ScriptHotReloadManager.cpp
    src/ScriptCompiler.cpp
    src/SceneRenderer.cpp
    src/MaterialTextureCache.cpp
    src/RHISceneRenderer.cpp
    src/RHICascadedShadows.cpp
    src/RHIEnvironment.cpp
    src/RHIObjectPicker.cpp
    src/RHIMeshRegistry.cpp
    src/CharacterVFX.cpp
    src/CustomMaterialProgram.cpp
    src/MaterialGraphProgram.cpp
    src/MaterialGraphLayout.cpp
    src/MaterialGraphResources.cpp
    src/MaterialGraphRuntime.cpp
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
        Axle::PhysicsDebug
        Axle::Asset
        Axle::Audio
        Axle::VFX
        Axle::UI2D
)

if (MSVC)
    target_compile_options(AxleScene PRIVATE /FS)
endif()


target_include_directories(AxleScene PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
