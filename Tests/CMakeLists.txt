add_executable(AxleStaticPluginLifetimeTests EXCLUDE_FROM_ALL src/Test_StaticPluginLifetime.cpp)
target_link_libraries(AxleStaticPluginLifetimeTests PRIVATE Axle::PluginRuntime)
add_test(NAME StaticPluginLifetimeTests COMMAND $<TARGET_FILE:AxleStaticPluginLifetimeTests>)
add_executable(AxleArchitectureStressTests EXCLUDE_FROM_ALL src/ArchitectureStressMain.cpp)
target_link_libraries(AxleArchitectureStressTests PRIVATE Axle::Physics Axle::SceneData)
add_test(NAME ArchitectureStressTests COMMAND $<TARGET_FILE:AxleArchitectureStressTests>)
set_tests_properties(ArchitectureStressTests PROPERTIES LABELS "architecture;ecs-jobs;physics;cpu" TIMEOUT 120)
if(NOT AXLE_BUILD_EDITOR)
    add_executable(AxlePluginFoundationTests EXCLUDE_FROM_ALL src/Test_PluginFoundation.cpp)
    target_link_libraries(AxlePluginFoundationTests PRIVATE Axle::Plugin Axle::Project)
    add_test(NAME PluginFoundationTests COMMAND $<TARGET_FILE:AxlePluginFoundationTests>)
    add_custom_target(AxleAllTests DEPENDS AxlePluginFoundationTests AxleStaticPluginLifetimeTests AxleArchitectureStressTests)
    return()
endif()

# Dedicated Test Executable for Automated Verifications
add_executable(AxleTests EXCLUDE_FROM_ALL
    src/Test.cpp
    src/Test_Milestone1.cpp
    src/Test_Milestone2.cpp
    src/Test_Milestone3.cpp
    src/Test_Milestone4.cpp
    src/Test_Milestone5.cpp
    src/Test_Milestone6.cpp
    src/Test_Milestone7.cpp
    src/Test_Milestone8_Part1.cpp
    src/Test_Milestone8_Part2.cpp
    src/Test_Milestone8_Part3.cpp
    src/Test_Milestone8_Part4.cpp
    src/Test_Milestone9.cpp
    src/Test_Milestone10.cpp
    src/Test_MaterialRendering.cpp
    src/Test_MaterialMeshImport.cpp
    src/Test_MaterialAcceptance.cpp
    src/Test_VFXRendering.cpp
)

if (MSVC)
    target_compile_options(AxleTests PRIVATE /bigobj /FS)
endif()

# Tests own their writable workspace; fixture source data is never an Editor asset root.
set(AXLE_TEST_WORKSPACE "${CMAKE_CURRENT_BINARY_DIR}/Work/$<CONFIG>")
add_custom_target(AxleTestResources
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AXLE_TEST_WORKSPACE}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/Assets" "${AXLE_TEST_WORKSPACE}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Engine/Resources" "${AXLE_TEST_WORKSPACE}/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxcompiler.dll" "${AXLE_TEST_WORKSPACE}/dxcompiler.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxil.dll" "${AXLE_TEST_WORKSPACE}/dxil.dll"
    VERBATIM)

# Demo scripts belong to fixture tests, not the production Editor/Runtime module.
file(GLOB AXLE_FIXTURE_SCRIPT_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/Assets/scripts/*.hpp")
list(FILTER AXLE_FIXTURE_SCRIPT_HEADERS EXCLUDE REGEX "CarController|RacingHUD|VehicleChaseCamera")
set(AXLE_FIXTURE_INCLUDES "// Generated test-fixture script includes\n")
foreach(script IN LISTS AXLE_FIXTURE_SCRIPT_HEADERS)
    string(APPEND AXLE_FIXTURE_INCLUDES "#include \"${script}\"\n")
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/AutoScripts.inc" CONTENT "${AXLE_FIXTURE_INCLUDES}")
add_library(AxleTestScripts SHARED EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/Editor/src/Scripts/ScriptsModule.cpp")
set_target_properties(AxleTestScripts PROPERTIES OUTPUT_NAME AxleScripts)
target_include_directories(AxleTestScripts PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" Fixtures/Assets/scripts)
target_compile_definitions(AxleTestScripts PRIVATE AXLE_SCRIPTS_DLL AXLE_SCRIPT_BUILD_DLL)
target_link_libraries(AxleTestScripts PRIVATE Axle::Scene Axle::UI)
add_dependencies(AxleTestScripts AxleTestResources)
add_custom_command(TARGET AxleTestScripts POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:AxleTestScripts>" "${AXLE_TEST_WORKSPACE}/$<TARGET_FILE_NAME:AxleTestScripts>"
    VERBATIM)
add_dependencies(AxleTests AxleTestResources AxleTestScripts)

target_include_directories(AxleTests PRIVATE
    src
    Fixtures/Assets/scripts
)

target_link_libraries(AxleTests PRIVATE
    Axle::Core
    Axle::Platform
    Axle::RHI
    Axle::Renderer
    Axle::Audio
    Axle::Asset
    Axle::UI
    Axle::UI2D
    Axle::Physics
    Axle::Scene
    Axle::Project
    Axle::Spline
    Axle::Terrain
    Axle::TerrainEditor
    Axle::Animation
    Axle::VFX
)



# Dedicated CPU-Only Material Unit Test Target (Phase 38.3)
add_executable(AxleMaterialUnitTests EXCLUDE_FROM_ALL
    src/MaterialUnitTestMain.cpp
    src/Test_MaterialSerializationUnit.cpp
    src/Test_MaterialTextureSelectionUnit.cpp
    src/Test_MaterialBindingPlanUnit.cpp
    src/Test_MaterialProgramCacheUnit.cpp
    src/Test_ShaderDragDropUnit.cpp
    src/Test_MaterialAcceptance.cpp
)

target_include_directories(AxleMaterialUnitTests PRIVATE
    src
)

target_link_libraries(AxleMaterialUnitTests PRIVATE
    Axle::Core
    Axle::Platform
    Axle::Renderer
    Axle::Scene
)

if (MSVC)
    target_compile_options(AxleMaterialUnitTests PRIVATE /bigobj /FS)
endif()

add_test(NAME MaterialUnitTests COMMAND $<TARGET_FILE:AxleMaterialUnitTests>)

# Dedicated CPU-Only VFX Unit Test Target (Phase 39)
add_executable(AxleVFXUnitTests EXCLUDE_FROM_ALL
    src/VFXUnitTestMain.cpp
    src/Test_VFXAuthoringUnit.cpp
    src/Test_VFXDefinitionUnit.cpp
    src/Test_VFXPersistenceUnit.cpp
    src/Test_VFXDocumentUnit.cpp
    src/Test_VFXPreviewIsolationUnit.cpp
    src/Test_VFXPlaybackUnit.cpp
    src/Test_VFXRenderPlanUnit.cpp
    src/Test_VFXEventsAndMeshUnit.cpp
)

target_include_directories(AxleVFXUnitTests PRIVATE
    src
)

target_link_libraries(AxleVFXUnitTests PRIVATE
    Axle::Core
    Axle::Platform
    Axle::Renderer
    Axle::Asset
    Axle::VFX
    Axle::Scene
    Axle::UI
)

if (MSVC)
    target_compile_options(AxleVFXUnitTests PRIVATE /bigobj /FS)
endif()

add_test(NAME VFXUnitTests COMMAND $<TARGET_FILE:AxleVFXUnitTests>)



# Opt-in physical GPU verification. CPU-only environments never silently substitute a mock.
add_executable(AxleVFXGPUTests EXCLUDE_FROM_ALL src/VFXGPUTestMain.cpp src/Test_VFXRendering.cpp)
target_include_directories(AxleVFXGPUTests PRIVATE src)
target_link_libraries(AxleVFXGPUTests PRIVATE Axle::Scene Axle::VFX Axle::Asset Axle::UI)
target_compile_definitions(AxleVFXGPUTests PRIVATE AXLE_VFX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(WIN32)
    add_custom_command(TARGET AxleVFXGPUTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxcompiler.dll $<TARGET_FILE_DIR:AxleVFXGPUTests>/dxcompiler.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxil.dll $<TARGET_FILE_DIR:AxleVFXGPUTests>/dxil.dll)
endif()
option(AXLE_ENABLE_VFX_GPU_TESTS "Register physical DX12/Vulkan VFX readback tests" OFF)
if(AXLE_ENABLE_VFX_GPU_TESTS)
    foreach(backend D3D12 Vulkan)
        add_test(NAME VFXGPU_${backend} COMMAND $<TARGET_FILE:AxleVFXGPUTests>)
        set_tests_properties(VFXGPU_${backend} PROPERTIES ENVIRONMENT "AXLE_VFX_TEST_BACKEND=${backend}" LABELS "gpu;vfx" RUN_SERIAL TRUE)
    endforeach()
endif()

# Animation foundations run without application/window/graphics initialization.
add_executable(AxleAnimationUnitTests EXCLUDE_FROM_ALL src/AnimationUnitTestMain.cpp src/Test_AnimationCorrections.cpp src/Test_AnimationAssetCorrections.cpp src/Test_AnimationImporterCorrections.cpp src/Test_AnimationControllerCorrections.cpp src/Test_AnimationSceneCorrections.cpp src/Test_AnimationPackageCorrections.cpp src/Test_AnimationBakeValidation.cpp src/Test_AnimationDocumentUnit.cpp)
target_include_directories(AxleAnimationUnitTests PRIVATE src)
target_compile_definitions(AxleAnimationUnitTests PRIVATE AXLE_ANIMATION_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(AxleAnimationUnitTests PRIVATE Axle::Animation Axle::Scene)
if(MSVC)
    target_compile_options(AxleAnimationUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME AnimationUnitTests COMMAND $<TARGET_FILE:AxleAnimationUnitTests>)
add_test(NAME AnimationHarnessFailure COMMAND $<TARGET_FILE:AxleAnimationUnitTests> --self-test-failure)
set_tests_properties(AnimationUnitTests AnimationHarnessFailure PROPERTIES LABELS "animation;cpu")
set_tests_properties(AnimationHarnessFailure PROPERTIES WILL_FAIL TRUE)

add_executable(AxleProjectUnitTests EXCLUDE_FROM_ALL src/Test_ProjectStartup.cpp)
target_include_directories(AxleProjectUnitTests PRIVATE src "${CMAKE_SOURCE_DIR}/Editor/src")
target_link_libraries(AxleProjectUnitTests PRIVATE Axle::Project Axle::Scene Axle::UI)
add_test(NAME ProjectStartupTests COMMAND $<TARGET_FILE:AxleProjectUnitTests>)

add_executable(AxlePluginFoundationTests EXCLUDE_FROM_ALL src/Test_PluginFoundation.cpp)
target_link_libraries(AxlePluginFoundationTests PRIVATE Axle::Plugin Axle::Project)
add_test(NAME PluginFoundationTests COMMAND $<TARGET_FILE:AxlePluginFoundationTests>)
set_tests_properties(PluginFoundationTests PROPERTIES LABELS "plugin;cpu")

add_executable(AxlePluginSelectionTests EXCLUDE_FROM_ALL src/Test_PluginSelectionModel.cpp)
target_link_libraries(AxlePluginSelectionTests PRIVATE Axle::Plugin)
add_test(NAME PluginSelectionTests COMMAND $<TARGET_FILE:AxlePluginSelectionTests>)
set_tests_properties(PluginSelectionTests PROPERTIES LABELS "plugin;cpu")

add_executable(AxlePluginsPanelWorkflowTests EXCLUDE_FROM_ALL src/Test_PluginsPanelWorkflow.cpp "${CMAKE_SOURCE_DIR}/Editor/src/PluginsPanel.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/ProjectPluginCreator.cpp")
target_include_directories(AxlePluginsPanelWorkflowTests PRIVATE src "${CMAKE_SOURCE_DIR}/Editor/src")
target_link_libraries(AxlePluginsPanelWorkflowTests PRIVATE Axle::Plugin Axle::Project Axle::Core Axle::UI)
add_test(NAME PluginsPanelWorkflowTests COMMAND $<TARGET_FILE:AxlePluginsPanelWorkflowTests>)
set_tests_properties(PluginsPanelWorkflowTests PROPERTIES LABELS "plugin;cpu")
add_executable(AxlePluginDocumentGuardTests EXCLUDE_FROM_ALL src/Test_PluginDocumentGuard.cpp "${CMAKE_SOURCE_DIR}/Editor/src/DocumentGuard.cpp")
target_include_directories(AxlePluginDocumentGuardTests PRIVATE "${CMAKE_SOURCE_DIR}/Editor/src")
add_test(NAME PluginDocumentGuardTests COMMAND $<TARGET_FILE:AxlePluginDocumentGuardTests>)
add_executable(AxlePluginRestartTests EXCLUDE_FROM_ALL src/Test_PluginRestart.cpp "${CMAKE_SOURCE_DIR}/Editor/src/PluginRestart.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/PluginRestartSnapshot.cpp")
target_include_directories(AxlePluginRestartTests PRIVATE "${CMAKE_SOURCE_DIR}/Editor/src")
target_link_libraries(AxlePluginRestartTests PRIVATE Axle::Plugin)
add_test(NAME PluginRestartTests COMMAND $<TARGET_FILE:AxlePluginRestartTests>)
set_tests_properties(PluginRestartTests PROPERTIES LABELS "plugin;editor;native" TIMEOUT 30)
set_tests_properties(PluginDocumentGuardTests PROPERTIES LABELS "plugin;editor;cpu")

# Explicit baseline toolchain; failure probes must validate their exact cause.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Dedicated CPU-Only Asset Identity and Safe Persistence Test Target (AF-01)
add_executable(AxleAssetUnitTests EXCLUDE_FROM_ALL src/Test_AssetIdentityAndPersistence.cpp)
target_include_directories(AxleAssetUnitTests PRIVATE src)
target_link_libraries(AxleAssetUnitTests PRIVATE Axle::Asset Axle::Project Axle::Scene)
if (MSVC)
    target_compile_options(AxleAssetUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME AssetIdentityAndPersistenceTests COMMAND $<TARGET_FILE:AxleAssetUnitTests>)
set_tests_properties(AssetIdentityAndPersistenceTests PROPERTIES LABELS "asset;cpu" TIMEOUT 45)
add_test(NAME AssetHarnessFailure COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/verify_failure_probe.py" $<TARGET_FILE:AxleAssetUnitTests> --case asset-probe)
set_tests_properties(AssetHarnessFailure PROPERTIES LABELS "asset;cpu" TIMEOUT 15)

# Dedicated Headless Asset Registry and Format Unit Tests (AF-04)
add_executable(AxleAssetFormatUnitTests EXCLUDE_FROM_ALL src/Test_AssetUnit.cpp)
target_include_directories(AxleAssetFormatUnitTests PRIVATE src)
target_link_libraries(AxleAssetFormatUnitTests PRIVATE Axle::Core Axle::Asset Axle::Renderer Axle::Physics)
if (MSVC)
    target_compile_options(AxleAssetFormatUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME AssetFormatUnitTests COMMAND $<TARGET_FILE:AxleAssetFormatUnitTests>)
set_tests_properties(AssetFormatUnitTests PROPERTIES LABELS "asset;cpu" TIMEOUT 30)

# Dedicated CPU-Only Render Contracts & Always-Active Harness (P41-00)
add_executable(AxleRHIFoundationTests EXCLUDE_FROM_ALL rendering/RHIFoundationMain.cpp)
target_include_directories(AxleRHIFoundationTests PRIVATE rendering)
target_link_libraries(AxleRHIFoundationTests PRIVATE Axle::RHI Axle::Renderer Axle::UI glad glfw)
add_test(NAME RHIFoundationPolicy COMMAND $<TARGET_FILE:AxleRHIFoundationTests> policy)
set_tests_properties(RHIFoundationPolicy PROPERTIES LABELS "render;cpu" TIMEOUT 30)

add_executable(AxleRHIResourceTests EXCLUDE_FROM_ALL rendering/RHIResourceMain.cpp)
target_include_directories(AxleRHIResourceTests PRIVATE rendering)
target_link_libraries(AxleRHIResourceTests PRIVATE Axle::RHI)
add_test(NAME RHIResourcePolicy COMMAND $<TARGET_FILE:AxleRHIResourceTests> policy)
set_tests_properties(RHIResourcePolicy PROPERTIES LABELS "render;cpu" TIMEOUT 30)

add_executable(AxleRHIResourceViewTests EXCLUDE_FROM_ALL rendering/RHIResourceViewMain.cpp)
target_include_directories(AxleRHIResourceViewTests PRIVATE rendering)
target_link_libraries(AxleRHIResourceViewTests PRIVATE Axle::UI Axle::Scene)

add_executable(AxleRenderServiceTests EXCLUDE_FROM_ALL rendering/RenderServiceMain.cpp)
add_executable(AxleRenderAcceptanceTests EXCLUDE_FROM_ALL rendering/RenderAcceptanceMain.cpp rendering/RenderAcceptanceMeasure.cpp)
target_include_directories(AxleRenderAcceptanceTests PRIVATE rendering)
target_link_libraries(AxleRenderAcceptanceTests PRIVATE Axle::Scene)
add_test(NAME RenderAcceptancePolicy COMMAND $<TARGET_FILE:AxleRenderAcceptanceTests>)
set_tests_properties(RenderAcceptancePolicy PROPERTIES LABELS "render;cpu;acceptance" TIMEOUT 30)
target_include_directories(AxleRenderServiceTests PRIVATE rendering)
target_link_libraries(AxleRenderServiceTests PRIVATE Axle::UI Axle::Scene glad glfw)

add_executable(AxleRuntimeRenderTests EXCLUDE_FROM_ALL rendering/RuntimeProductMain.cpp)
target_include_directories(AxleRuntimeRenderTests PRIVATE rendering)
target_link_libraries(AxleRuntimeRenderTests PRIVATE Axle::RuntimePlayer glad glfw)
target_compile_definitions(AxleRuntimeRenderTests PRIVATE
    AXLE_RUNTIME_TEST_RESOURCES="${CMAKE_SOURCE_DIR}/Engine/Resources"
    AXLE_RUNTIME_TEST_SCRIPTS="$<TARGET_FILE:AxleRuntimeScripts>")
add_dependencies(AxleRuntimeRenderTests AxleRuntimeScripts)

add_executable(AxlePreviewRenderTests EXCLUDE_FROM_ALL rendering/PreviewProductMain.cpp)
target_include_directories(AxlePreviewRenderTests PRIVATE rendering)
target_link_libraries(AxlePreviewRenderTests PRIVATE Axle::UI Axle::Scene glad glfw)

add_executable(AxleRHIErrorTests EXCLUDE_FROM_ALL rendering/RHIErrorMain.cpp)
target_include_directories(AxleRHIErrorTests PRIVATE rendering)
target_link_libraries(AxleRHIErrorTests PRIVATE Axle::RHI)
add_test(NAME RHIErrorPolicy COMMAND $<TARGET_FILE:AxleRHIErrorTests> policy)
set_tests_properties(RHIErrorPolicy PROPERTIES LABELS "render;cpu" TIMEOUT 30)

add_executable(AxleRHIIdentityTests EXCLUDE_FROM_ALL rendering/RHIIdentityMain.cpp)
target_include_directories(AxleRHIIdentityTests PRIVATE rendering)
target_link_libraries(AxleRHIIdentityTests PRIVATE Axle::RHI)
add_test(NAME RHIIdentityPolicy COMMAND $<TARGET_FILE:AxleRHIIdentityTests> policy)
set_tests_properties(RHIIdentityPolicy PROPERTIES LABELS "render;cpu" TIMEOUT 30)

add_executable(AxleRHIRetirementTests EXCLUDE_FROM_ALL rendering/RHIRetirementMain.cpp)
target_include_directories(AxleRHIRetirementTests PRIVATE rendering)
target_link_libraries(AxleRHIRetirementTests PRIVATE Axle::RHI)
add_test(NAME RHIRetirementPolicy COMMAND $<TARGET_FILE:AxleRHIRetirementTests> policy)
set_tests_properties(RHIRetirementPolicy PROPERTIES LABELS "render;cpu" TIMEOUT 30)

add_executable(AxleRHIStateTests EXCLUDE_FROM_ALL rendering/RHIStateMain.cpp)
target_include_directories(AxleRHIStateTests PRIVATE rendering)
target_link_libraries(AxleRHIStateTests PRIVATE Axle::RHI)
add_test(NAME RHIStatePolicy COMMAND $<TARGET_FILE:AxleRHIStateTests> policy)
set_tests_properties(RHIStatePolicy PROPERTIES LABELS "render;cpu" TIMEOUT 30)

add_executable(AxleRHIFrameTests EXCLUDE_FROM_ALL rendering/RHIFrameMain.cpp)
add_executable(AxleRenderLifetimeStressTests EXCLUDE_FROM_ALL rendering/RenderLifetimeStressMain.cpp rendering/RenderWorkerStress.cpp rendering/RenderPluginStress.cpp rendering/RenderTransientStress.cpp)
target_include_directories(AxleRenderLifetimeStressTests PRIVATE rendering)
target_link_libraries(AxleRenderLifetimeStressTests PRIVATE Axle::Scene Axle::PluginRuntime)
add_test(NAME RenderLifetimeStressCPU COMMAND $<TARGET_FILE:AxleRenderLifetimeStressTests>)
set_tests_properties(RenderLifetimeStressCPU PROPERTIES LABELS "render;cpu;stress" TIMEOUT 60)
add_executable(AxleTransientGraphTests EXCLUDE_FROM_ALL rendering/TransientGraphMain.cpp)
target_include_directories(AxleTransientGraphTests PRIVATE rendering)
target_link_libraries(AxleTransientGraphTests PRIVATE Axle::Renderer Axle::RHI)
add_test(NAME TransientGraphPolicy COMMAND $<TARGET_FILE:AxleTransientGraphTests> policy)
set_tests_properties(TransientGraphPolicy PROPERTIES LABELS "render;cpu" TIMEOUT 30)
target_include_directories(AxleRHIFrameTests PRIVATE rendering)
target_link_libraries(AxleRHIFrameTests PRIVATE Axle::RHI)

add_executable(AxleRHIPresentationTests EXCLUDE_FROM_ALL rendering/RHIPresentationMain.cpp)
target_include_directories(AxleRHIPresentationTests PRIVATE rendering)
target_link_libraries(AxleRHIPresentationTests PRIVATE Axle::RHI)

add_executable(AxleImageSourceTests EXCLUDE_FROM_ALL rendering/ImageSourceMain.cpp)
target_include_directories(AxleImageSourceTests PRIVATE rendering)
target_link_libraries(AxleImageSourceTests PRIVATE Axle::Scene glad glfw)
add_test(NAME ImageSourcePolicy COMMAND $<TARGET_FILE:AxleImageSourceTests> policy)
set_tests_properties(ImageSourcePolicy PROPERTIES LABELS "render;cpu" TIMEOUT 45)

add_executable(AxlePrimitiveTests EXCLUDE_FROM_ALL rendering/PrimitiveMain.cpp)
target_include_directories(AxlePrimitiveTests PRIVATE rendering)
target_link_libraries(AxlePrimitiveTests PRIVATE Axle::UI Axle::Scene glad glfw)
add_test(NAME PrimitiveIdentityTests COMMAND $<TARGET_FILE:AxlePrimitiveTests> policy)
set_tests_properties(PrimitiveIdentityTests PROPERTIES LABELS "render;cpu" TIMEOUT 45)

add_executable(AxleRenderUnitTests EXCLUDE_FROM_ALL
    rendering/RenderUnitTestMain.cpp
    rendering/Test_RenderContractsUnit.cpp
    rendering/Test_RenderBackendPolicyUnit.cpp
    rendering/Test_RenderPrimitiveIdentityUnit.cpp
    rendering/Test_RenderWorldExtractionUnit.cpp
    rendering/Test_RenderServiceUnit.cpp
    rendering/Test_RenderSnapshotQueueUnit.cpp
    rendering/Test_RenderWorkerUnit.cpp
    rendering/Test_RenderParallelRecordingUnit.cpp
    rendering/Test_RenderTransientGraphUnit.cpp
)
target_include_directories(AxleRenderUnitTests PRIVATE rendering src)
target_link_libraries(AxleRenderUnitTests PRIVATE
    Axle::Core
    Axle::Platform
    Axle::RHI
    Axle::Renderer
    Axle::Asset
    Axle::Scene
)
if (MSVC)
    target_compile_options(AxleRenderUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME RenderUnitTests COMMAND $<TARGET_FILE:AxleRenderUnitTests>)
add_test(NAME RenderHarnessFailure COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/verify_failure_probe.py" $<TARGET_FILE:AxleRenderUnitTests> --case render-probe)
set_tests_properties(RenderUnitTests RenderHarnessFailure PROPERTIES LABELS "render;cpu")
set_tests_properties(RenderUnitTests RenderHarnessFailure PROPERTIES TIMEOUT 30)

option(AXLE_ENABLE_RENDER_GPU_TESTS "Register shared physical D3D12/Vulkan render readback tests" OFF)
if(AXLE_ENABLE_RENDER_GPU_TESTS)
    foreach(backend D3D12 Vulkan)
        add_test(NAME RenderGPU_${backend} COMMAND $<TARGET_FILE:AxleVFXGPUTests>)
        set_tests_properties(RenderGPU_${backend} PROPERTIES
            ENVIRONMENT "AXLE_VFX_TEST_BACKEND=${backend}" LABELS "render;gpu;shared-vfx" RUN_SERIAL TRUE TIMEOUT 120)
    endforeach()
endif()
option(AXLE_ENABLE_RENDER_WINDOW_TESTS "Register opt-in real Editor startup/present smoke" OFF)
if(AXLE_ENABLE_RENDER_WINDOW_TESTS)
    foreach(backend dx12 vulkan gl)
        add_test(NAME RenderWindow_${backend} COMMAND $<TARGET_FILE:AxleEditor> --verify-empty-startup --rhi=${backend})
        set_tests_properties(RenderWindow_${backend} PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:AxleEditor>" LABELS "render;gpu;window;product" RUN_SERIAL TRUE TIMEOUT 90)
    endforeach()
endif()

# Dedicated CPU-Only Plugin Baseline & Retirement Harness (PLG-00)
add_executable(AxlePluginUnitTests EXCLUDE_FROM_ALL
    plugin/PluginUnitTestMain.cpp
    plugin/Test_PluginBaselineUnit.cpp
)
target_include_directories(AxlePluginUnitTests PRIVATE plugin src)
target_link_libraries(AxlePluginUnitTests PRIVATE
    Axle::Core
    Axle::Physics
    Axle::Scene
    Axle::UI
    Axle::Spline
    Axle::Terrain
)
if (MSVC)
    target_compile_options(AxlePluginUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME PluginUnitTests COMMAND $<TARGET_FILE:AxlePluginUnitTests>)
add_test(NAME PluginHarnessFailure COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/verify_failure_probe.py" $<TARGET_FILE:AxlePluginUnitTests> --case plugin-probe)
set_tests_properties(PluginUnitTests PluginHarnessFailure PROPERTIES LABELS "plugin;cpu")
set_tests_properties(PluginUnitTests PluginHarnessFailure PROPERTIES TIMEOUT 30)
# PLG-06 native loader/transaction fixture. These are real shared libraries;
# this slice intentionally does not claim PLG-04/05 host/build integration.
add_library(AxlePluginRuntimeGood SHARED plugin/PluginRuntimeFixture.cpp)
add_library(AxlePluginRuntimeFail SHARED plugin/PluginRuntimeFixture.cpp)
foreach(fixtureTarget AxlePluginRuntimeGood AxlePluginRuntimeFail)
    target_link_libraries(${fixtureTarget} PRIVATE Axle::PluginSDK)
    target_compile_definitions(${fixtureTarget} PRIVATE AXLE_PLUGIN_BUILD_CONFIGURATION="$<CONFIG>")
endforeach()
target_compile_definitions(AxlePluginRuntimeFail PRIVATE AXLE_FIXTURE_FAIL)
add_dependencies(AxleRenderLifetimeStressTests AxlePluginRuntimeGood)
if(WIN32)
    add_test(NAME RenderLifetimeStressPlugin COMMAND $<TARGET_FILE:AxleRenderLifetimeStressTests> plugin $<TARGET_FILE:AxlePluginRuntimeGood> "$<CONFIG>")
    set_tests_properties(RenderLifetimeStressPlugin PROPERTIES LABELS "render;plugin;cpu;stress" TIMEOUT 90)
endif()
option(AXLE_ENABLE_OPT11_LIFETIME_NATIVE_TESTS "Register physical GPU/window OPT-11 lifetime stress" OFF)
if(AXLE_ENABLE_OPT11_LIFETIME_NATIVE_TESTS AND WIN32)
    foreach(backend dx12 vulkan)
        foreach(mode worker exception sync native submit lost plugin)
            add_test(NAME RenderLifetimeStress_${backend}_${mode} COMMAND $<TARGET_FILE:AxleRenderLifetimeStressTests>
                ${backend}_${mode} $<TARGET_FILE:AxlePluginRuntimeGood> "$<CONFIG>")
            set_tests_properties(RenderLifetimeStress_${backend}_${mode} PROPERTIES
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS "render;gpu;stress" RUN_SERIAL TRUE TIMEOUT 180)
        endforeach()
        add_test(NAME RenderLifetimeWindow_${backend} COMMAND $<TARGET_FILE:AxleRHIPresentationTests> ${backend} stress)
        set_tests_properties(RenderLifetimeWindow_${backend} PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" LABELS "render;gpu;window;stress" RUN_SERIAL TRUE TIMEOUT 180)
    endforeach()
endif()
add_executable(AxlePluginRuntimeTests plugin/PluginRuntimeMain.cpp)
target_link_libraries(AxlePluginRuntimeTests PRIVATE Axle::PluginRuntime)
add_dependencies(AxlePluginRuntimeTests AxlePluginRuntimeGood AxlePluginRuntimeFail)
add_test(NAME PluginRuntimeTests COMMAND AxlePluginRuntimeTests
    $<TARGET_FILE:AxlePluginRuntimeGood> $<TARGET_FILE:AxlePluginRuntimeFail> "$<CONFIG>")
set_tests_properties(PluginRuntimeTests PROPERTIES LABELS "plugin;cpu" TIMEOUT 30)

# Dedicated Foundation, Application & Services Unit Test Target (AF-02)
add_executable(AxleFoundationUnitTests EXCLUDE_FROM_ALL
    src/Test_CoreFoundationAndServicesUnit.cpp
)
target_include_directories(AxleFoundationUnitTests PRIVATE src)
target_link_libraries(AxleFoundationUnitTests PRIVATE
    Axle::Core
    Axle::Platform
)
if (MSVC)
    target_compile_options(AxleFoundationUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME FoundationUnitTests COMMAND $<TARGET_FILE:AxleFoundationUnitTests>)
set_tests_properties(FoundationUnitTests PROPERTIES LABELS "foundation;cpu" TIMEOUT 30)

# Dedicated Scene Command Buffer & Mutation Boundaries Unit Test Target (EJ-09)
add_executable(AxleSceneMutationUnitTests EXCLUDE_FROM_ALL
    src/Test_SceneCommandAndMutationUnit.cpp
)
target_include_directories(AxleSceneMutationUnitTests PRIVATE src)
target_link_libraries(AxleSceneMutationUnitTests PRIVATE
    Axle::Scene
    Axle::Physics
    Axle::Core
)
if (MSVC)
    target_compile_options(AxleSceneMutationUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME SceneMutationUnitTests COMMAND $<TARGET_FILE:AxleSceneMutationUnitTests>)
set_tests_properties(SceneMutationUnitTests PROPERTIES LABELS "scene;cpu" TIMEOUT 30)

# Dedicated SceneData & System Bridge Boundaries Unit Test Target (AF-03)
add_executable(AxleSceneDataUnitTests EXCLUDE_FROM_ALL
    src/Test_SceneDataUnit.cpp
)
target_include_directories(AxleSceneDataUnitTests PRIVATE src)
target_link_libraries(AxleSceneDataUnitTests PRIVATE
    Axle::Scene
    Axle::Core
)
if (MSVC)
    target_compile_options(AxleSceneDataUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME SceneDataUnitTests COMMAND $<TARGET_FILE:AxleSceneDataUnitTests>)
set_tests_properties(SceneDataUnitTests PROPERTIES LABELS "scene;data;cpu" TIMEOUT 30)

# Dedicated Shared Runtime Phases and Script API Unit Test Target (EJ-11)
add_executable(AxleSharedRuntimePhasesUnitTests EXCLUDE_FROM_ALL
    src/Test_SharedRuntimePhasesUnit.cpp
)
target_include_directories(AxleSharedRuntimePhasesUnitTests PRIVATE src)
target_link_libraries(AxleSharedRuntimePhasesUnitTests PRIVATE
    Axle::Scene
    Axle::Physics
    Axle::Core
)
if (MSVC)
    target_compile_options(AxleSharedRuntimePhasesUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME SharedRuntimePhasesUnitTests COMMAND $<TARGET_FILE:AxleSharedRuntimePhasesUnitTests>)
set_tests_properties(SharedRuntimePhasesUnitTests PROPERTIES LABELS "scene;sim;cpu" TIMEOUT 30)

# Dedicated Simulation Clock, Inputs and Pause/Step Unit Test Target (EJ-12)
add_executable(AxleSimulationClockUnitTests EXCLUDE_FROM_ALL
    src/Test_SimulationClockAndInputUnit.cpp
)
target_include_directories(AxleSimulationClockUnitTests PRIVATE src)
target_link_libraries(AxleSimulationClockUnitTests PRIVATE
    Axle::Scene
    Axle::Physics
    Axle::Core
    Axle::Platform
)
if (MSVC)
    target_compile_options(AxleSimulationClockUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME SimulationClockUnitTests COMMAND $<TARGET_FILE:AxleSimulationClockUnitTests>)
set_tests_properties(SimulationClockUnitTests PROPERTIES LABELS "scene;sim;clock;cpu" TIMEOUT 30)

# Dedicated Editor Cutover Unit Test Target (EJ-13)
add_executable(AxleEditorCutoverUnitTests EXCLUDE_FROM_ALL
    src/Test_EditorCutoverUnit.cpp
)
target_include_directories(AxleEditorCutoverUnitTests PRIVATE src)
target_link_libraries(AxleEditorCutoverUnitTests PRIVATE
    Axle::Scene
    Axle::Physics
    Axle::Core
    Axle::Platform
)
if (MSVC)
    target_compile_options(AxleEditorCutoverUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME EditorCutoverUnitTests COMMAND $<TARGET_FILE:AxleEditorCutoverUnitTests>)
set_tests_properties(EditorCutoverUnitTests PROPERTIES LABELS "editor;sim;scene;cpu" TIMEOUT 30)

# Dedicated Runtime Cutover and Product Parity Unit Test Target (EJ-14)
add_executable(AxleRuntimeProductParityUnitTests EXCLUDE_FROM_ALL
    src/Test_RuntimeProductParityUnit.cpp
)
target_include_directories(AxleRuntimeProductParityUnitTests PRIVATE src)
target_link_libraries(AxleRuntimeProductParityUnitTests PRIVATE
    Axle::Scene
    Axle::Physics
    Axle::Core
    Axle::Platform
)
if (MSVC)
    target_compile_options(AxleRuntimeProductParityUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME RuntimeProductParityUnitTests COMMAND $<TARGET_FILE:AxleRuntimeProductParityUnitTests>)
set_tests_properties(RuntimeProductParityUnitTests PROPERTIES LABELS "runtime;sim;scene;cpu" TIMEOUT 30)

# Dedicated CPU-Only Jobs and ECS Harness (EJ-00)
add_executable(AxleJobsECSTests EXCLUDE_FROM_ALL
    src/JobsECSTestMain.cpp
)

target_include_directories(AxleJobsECSTests PRIVATE
    src
)

target_link_libraries(AxleJobsECSTests PRIVATE
    Axle::Core
    Axle::Physics
    Axle::Scene
)

if (MSVC)
    target_compile_options(AxleJobsECSTests PRIVATE /bigobj /FS)
endif()

# Python is part of the explicit baseline test toolchain; missing it must fail configure.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
function(axle_add_jobs_control name scenario)
    add_test(NAME ${name} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/scripts/run_jobs_ecs_harness.py" "$<TARGET_FILE:AxleJobsECSTests>"
        --case "${scenario}" --workspace "${CMAKE_CURRENT_BINARY_DIR}/baseline/$<CONFIG>/workspace"
        --report "${CMAKE_CURRENT_BINARY_DIR}/baseline/$<CONFIG>/${name}.json")
    set_tests_properties(${name} PROPERTIES LABELS "ecs-jobs;cpu" TIMEOUT 25)
endfunction()
axle_add_jobs_control(JobsECS_Control ecsj-03-bridge-control)
axle_add_jobs_control(JobsECS_QueuedDelivery queued-delivery-control)
axle_add_jobs_control(JobsECS_ThreadIdentity ecsj-02-thread-identity)
axle_add_jobs_control(JobsECS_LifecycleAdmission ej01-lifecycle-admission)
axle_add_jobs_control(JobsECS_ReviewFixes ej01-review-fixes)
axle_add_jobs_control(JobsECS_DeferredExecutor ej02-deferred-executor)
axle_add_jobs_control(JobsECS_CompilerThread ecsj-01-compiler-thread)
axle_add_jobs_control(JobsECS_CompilerHandoff ej03-compiler-handoff)
axle_add_jobs_control(JobsECS_ReloadCommit ej04-reload-commit)
axle_add_jobs_control(JobsECS_PhysicsLifetime ej05-physics-lifetime)
axle_add_jobs_control(JobsECS_PhysicsWaits ej06-physics-waits)
axle_add_jobs_control(JobsECS_HandlesShutdown ej07-handles-shutdown)
axle_add_jobs_control(JobsECS_Lifecycle current-format-lifecycle)
axle_add_jobs_control(JobsECS_HarnessFailure deliberate-failure)
axle_add_jobs_control(JobsECS_Watchdog watchdog-hang)

add_test(NAME ArchitectureBoundaryChecks COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/scripts/check_architecture.py" --root "${CMAKE_SOURCE_DIR}")
add_test(NAME ArchitectureForbiddenEdgeFixture COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/scripts/check_architecture.py"
    --fixture "${CMAKE_SOURCE_DIR}/Tests/Fixtures/ArchitectureViolationFixture" --expect-violation)
add_test(NAME BaselineToolRegressions COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/scripts/test_baseline_tools.py")
add_test(NAME AcceptanceEvidenceRegressions COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/scripts/test_architecture_acceptance.py")
set_tests_properties(ArchitectureBoundaryChecks ArchitectureForbiddenEdgeFixture BaselineToolRegressions AcceptanceEvidenceRegressions
    PROPERTIES LABELS "architecture;cpu" TIMEOUT 60)

# Explicit developer/test build. Neither this target nor its executables are in ALL.
add_executable(AxleOpt0203ReviewTests EXCLUDE_FROM_ALL src/Test_Opt0203Review.cpp)
if(WIN32)
    foreach(variant A B BadABI BadBuild BadExports BadMetadata ThrowBind)
        add_library(AxleReviewScript${variant} SHARED EXCLUDE_FROM_ALL src/ReviewScriptModule.cpp)
        target_link_libraries(AxleReviewScript${variant} PRIVATE Axle::SceneData Axle::Platform)
        target_include_directories(AxleReviewScript${variant} PRIVATE "${CMAKE_SOURCE_DIR}/Engine/Scene/include")
        target_compile_definitions(AxleReviewScript${variant} PRIVATE AXLE_SCRIPT_BUILD_DLL)
    endforeach()
    target_compile_definitions(AxleReviewScriptB PRIVATE REVIEW_GENERATION=2)
    target_compile_definitions(AxleReviewScriptBadABI PRIVATE REVIEW_BAD_ABI)
    target_compile_definitions(AxleReviewScriptBadBuild PRIVATE REVIEW_BAD_BUILD)
    target_compile_definitions(AxleReviewScriptBadExports PRIVATE REVIEW_BAD_EXPORTS)
    target_compile_definitions(AxleReviewScriptBadMetadata PRIVATE REVIEW_BAD_METADATA)
    target_compile_definitions(AxleReviewScriptThrowBind PRIVATE REVIEW_THROW_BIND REVIEW_GENERATION=3)
    add_executable(AxleScriptLifecycleReviewTests EXCLUDE_FROM_ALL src/Test_ScriptLifecycleReview.cpp)
    target_link_libraries(AxleScriptLifecycleReviewTests PRIVATE Axle::Scene)
    add_dependencies(AxleScriptLifecycleReviewTests AxleReviewScriptA AxleReviewScriptB AxleReviewScriptBadABI AxleReviewScriptBadBuild AxleReviewScriptBadExports AxleReviewScriptBadMetadata AxleReviewScriptThrowBind)
    add_test(NAME ScriptLifecycleReviewTests COMMAND $<TARGET_FILE:AxleScriptLifecycleReviewTests>
        $<TARGET_FILE:AxleReviewScriptA> $<TARGET_FILE:AxleReviewScriptB> $<TARGET_FILE:AxleReviewScriptBadABI> $<TARGET_FILE:AxleReviewScriptBadBuild> $<TARGET_FILE:AxleReviewScriptBadExports> $<TARGET_FILE:AxleReviewScriptBadMetadata> $<TARGET_FILE:AxleReviewScriptThrowBind>)
    set_tests_properties(ScriptLifecycleReviewTests PROPERTIES LABELS "script;review;cpu" TIMEOUT 30 RUN_SERIAL TRUE)
endif()
add_executable(AxleSceneDataBoundaryTests EXCLUDE_FROM_ALL src/Test_SceneDataBoundary.cpp)
target_link_libraries(AxleSceneDataBoundaryTests PRIVATE Axle::SceneData)
add_test(NAME SceneDataBoundaryTests COMMAND $<TARGET_FILE:AxleSceneDataBoundaryTests>)
set_tests_properties(SceneDataBoundaryTests PROPERTIES LABELS "scene;data;cpu" TIMEOUT 30)
target_link_libraries(AxleOpt0203ReviewTests PRIVATE Axle::Scene)
if(MSVC)
    target_compile_options(AxleOpt0203ReviewTests PRIVATE /bigobj /FS)
endif()
add_test(NAME Opt0203ReviewRegressions COMMAND $<TARGET_FILE:AxleOpt0203ReviewTests>)
set_tests_properties(Opt0203ReviewRegressions PROPERTIES LABELS "review;ecs-jobs;sim;cpu" TIMEOUT 60)

set(AXLE_TEST_TARGETS AxleStaticPluginLifetimeTests AxleSceneDataBoundaryTests AxleOpt0203ReviewTests AxleFoundationUnitTests AxleSceneMutationUnitTests AxleSimulationClockUnitTests AxleEditorCutoverUnitTests AxleRuntimeProductParityUnitTests AxleProjectUnitTests AxleAssetUnitTests AxleAssetFormatUnitTests AxleSceneDataUnitTests AxleSharedRuntimePhasesUnitTests AxleRenderUnitTests AxlePluginUnitTests AxleTests AxleMaterialUnitTests AxleVFXUnitTests AxleVFXGPUTests AxleAnimationUnitTests AxleJobsECSTests)
list(APPEND AXLE_TEST_TARGETS AxlePluginFoundationTests AxlePluginSelectionTests AxlePluginsPanelWorkflowTests AxlePluginRuntimeTests AxlePluginDocumentGuardTests AxlePluginRestartTests)
list(APPEND AXLE_TEST_TARGETS AxlePrimitiveTests AxleRHIIdentityTests AxleRHIRetirementTests AxleRHIStateTests AxleRHIFrameTests AxleRHIPresentationTests AxleImageSourceTests)
list(APPEND AXLE_TEST_TARGETS AxleTransientGraphTests)
list(APPEND AXLE_TEST_TARGETS AxleRenderLifetimeStressTests)
list(APPEND AXLE_TEST_TARGETS AxleRenderAcceptanceTests)
if(WIN32)
    add_executable(AxleBuildWorkflowTests EXCLUDE_FROM_ALL src/Test_BuildWorkflow.cpp "${CMAKE_SOURCE_DIR}/Editor/src/RuntimeBuild.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/OwnedProcess.cpp")
    target_include_directories(AxleBuildWorkflowTests PRIVATE src "${CMAKE_SOURCE_DIR}/Editor/src" "${CMAKE_BINARY_DIR}/Editor/generated/$<CONFIG>")
    target_link_libraries(AxleBuildWorkflowTests PRIVATE Axle::Asset Axle::Project Axle::Plugin)
    add_dependencies(AxleBuildWorkflowTests AxleRuntime)
    target_compile_definitions(AxleBuildWorkflowTests PRIVATE AXLE_TEST_RUNTIME_EXE="$<TARGET_FILE:AxleRuntime>" AXLE_TEST_RUNTIME_HOST="$<TARGET_FILE:AxleRuntimeHost>")
    list(APPEND AXLE_TEST_TARGETS AxleBuildWorkflowTests AxleScriptLifecycleReviewTests)
    add_test(NAME BuildWorkflowFixtureConfigure COMMAND "${CMAKE_COMMAND}"
        -S "${CMAKE_CURRENT_SOURCE_DIR}/src/BuildWorkflowFixture"
        -B "${CMAKE_CURRENT_BINARY_DIR}/Build Workflow Fixture")
    set_tests_properties(BuildWorkflowFixtureConfigure PROPERTIES FIXTURES_SETUP BuildWorkflow)
    add_test(NAME BuildWorkflowTests COMMAND $<TARGET_FILE:AxleBuildWorkflowTests>
        "${CMAKE_COMMAND}" "${CMAKE_CURRENT_BINARY_DIR}/Build Workflow Fixture" "$<CONFIG>")
    set_tests_properties(BuildWorkflowTests PROPERTIES FIXTURES_REQUIRED BuildWorkflow RUN_SERIAL TRUE TIMEOUT 30)

    add_executable(AxlePluginBuildWorkflowTests EXCLUDE_FROM_ALL src/Test_PluginBuildWorkflow.cpp "${CMAKE_SOURCE_DIR}/Editor/src/PluginBuild.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/OwnedProcess.cpp")
    target_include_directories(AxlePluginBuildWorkflowTests PRIVATE src "${CMAKE_SOURCE_DIR}/Editor/src" "${CMAKE_BINARY_DIR}/PluginSDK/$<CONFIG>")
    target_link_libraries(AxlePluginBuildWorkflowTests PRIVATE Axle::Plugin Axle::Core Axle::PluginRuntime Axle::RuntimeHost)
    add_dependencies(AxlePluginBuildWorkflowTests AxlePluginSDKPackage)
    add_custom_command(TARGET AxlePluginBuildWorkflowTests POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:AxleRuntimeHost>" "$<TARGET_FILE_DIR:AxlePluginBuildWorkflowTests>")
    list(APPEND AXLE_TEST_TARGETS AxlePluginBuildWorkflowTests)
    add_executable(AxleProjectPluginCreatorTests EXCLUDE_FROM_ALL src/Test_ProjectPluginCreator.cpp
        "${CMAKE_SOURCE_DIR}/Editor/src/ProjectPluginCreator.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/PluginsPanel.cpp"
        "${CMAKE_SOURCE_DIR}/Editor/src/PluginBuild.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/OwnedProcess.cpp")
    target_include_directories(AxleProjectPluginCreatorTests PRIVATE "${CMAKE_SOURCE_DIR}/Editor/src" "${CMAKE_BINARY_DIR}/PluginSDK/$<CONFIG>")
    target_link_libraries(AxleProjectPluginCreatorTests PRIVATE Axle::Plugin Axle::Project Axle::UI Axle::RuntimeHost)
    add_dependencies(AxleProjectPluginCreatorTests AxlePluginSDKPackage)
    add_custom_command(TARGET AxleProjectPluginCreatorTests POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:AxleRuntimeHost>" "$<TARGET_FILE_DIR:AxleProjectPluginCreatorTests>")
    list(APPEND AXLE_TEST_TARGETS AxleProjectPluginCreatorTests)
    add_test(NAME ProjectPluginCreatorTests COMMAND $<TARGET_FILE:AxleProjectPluginCreatorTests> "${CMAKE_COMMAND}" "${CMAKE_SOURCE_DIR}/Templates/ProjectPlugin")
    set_tests_properties(ProjectPluginCreatorTests PROPERTIES LABELS "plugin;editor;native" TIMEOUT 150 RUN_SERIAL TRUE)
    add_test(NAME PluginBuildWorkflowTests COMMAND $<TARGET_FILE:AxlePluginBuildWorkflowTests>
        "${CMAKE_COMMAND}" "${CMAKE_CURRENT_SOURCE_DIR}/src/PluginBuildFixture" "$<CONFIG>")
    set_tests_properties(PluginBuildWorkflowTests PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
    add_executable(AxlePluginServicesTests EXCLUDE_FROM_ALL src/Test_PluginServices.cpp "${CMAKE_SOURCE_DIR}/Editor/src/PluginBuild.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/OwnedProcess.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/PluginEditorServices.cpp")
    target_include_directories(AxlePluginServicesTests PRIVATE "${CMAKE_SOURCE_DIR}/Editor/src" "${CMAKE_BINARY_DIR}/PluginSDK/$<CONFIG>")
    target_link_libraries(AxlePluginServicesTests PRIVATE Axle::PluginServices Axle::UI Axle::EditorHost)
    add_dependencies(AxlePluginServicesTests AxlePluginSDKPackage)
    add_custom_command(TARGET AxlePluginServicesTests POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:AxleRuntimeHost>" "$<TARGET_FILE_DIR:AxlePluginServicesTests>")
    add_custom_command(TARGET AxlePluginServicesTests POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:AxleEditorHost>" "$<TARGET_FILE_DIR:AxlePluginServicesTests>")
    list(APPEND AXLE_TEST_TARGETS AxlePluginServicesTests)
    add_test(NAME PluginServicesTests COMMAND $<TARGET_FILE:AxlePluginServicesTests> "${CMAKE_COMMAND}" "${CMAKE_CURRENT_SOURCE_DIR}/src/PluginBuildFixture")
    add_executable(AxlePluginSessionTests EXCLUDE_FROM_ALL src/Test_PluginSession.cpp
        "${CMAKE_SOURCE_DIR}/Editor/src/PluginRestartSnapshot.cpp"
        "${CMAKE_SOURCE_DIR}/Editor/src/PluginSession.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/PluginBuild.cpp"
        "${CMAKE_SOURCE_DIR}/Editor/src/PluginEditorServices.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/OwnedProcess.cpp")
    target_include_directories(AxlePluginSessionTests PRIVATE "${CMAKE_SOURCE_DIR}/Editor/src" "${CMAKE_BINARY_DIR}/PluginSDK/$<CONFIG>")
    target_link_libraries(AxlePluginSessionTests PRIVATE Axle::PluginServices Axle::UI Axle::EditorHost)
    add_dependencies(AxlePluginSessionTests AxlePluginSDKPackage)
    add_custom_command(TARGET AxlePluginSessionTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:AxleRuntimeHost>" "$<TARGET_FILE_DIR:AxlePluginSessionTests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:AxleEditorHost>" "$<TARGET_FILE_DIR:AxlePluginSessionTests>" VERBATIM)
    list(APPEND AXLE_TEST_TARGETS AxlePluginSessionTests)
    add_executable(AxleOptionalPluginFoundationTests EXCLUDE_FROM_ALL src/Test_OptionalPluginFoundation.cpp
        "${CMAKE_SOURCE_DIR}/Editor/src/PluginRestartSnapshot.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/PluginSession.cpp"
        "${CMAKE_SOURCE_DIR}/Editor/src/PluginBuild.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/PluginEditorServices.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/OwnedProcess.cpp")
    target_include_directories(AxleOptionalPluginFoundationTests PRIVATE "${CMAKE_SOURCE_DIR}/Editor/src" "${CMAKE_BINARY_DIR}/PluginSDK/$<CONFIG>")
    target_link_libraries(AxleOptionalPluginFoundationTests PRIVATE Axle::PluginServices Axle::UI Axle::EditorHost)
    add_dependencies(AxleOptionalPluginFoundationTests AxlePluginSDKPackage)
    add_custom_command(TARGET AxleOptionalPluginFoundationTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:AxleRuntimeHost>" "$<TARGET_FILE_DIR:AxleOptionalPluginFoundationTests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:AxleEditorHost>" "$<TARGET_FILE_DIR:AxleOptionalPluginFoundationTests>" VERBATIM)
    list(APPEND AXLE_TEST_TARGETS AxleOptionalPluginFoundationTests)
    add_test(NAME OptionalPluginFoundationTests COMMAND $<TARGET_FILE:AxleOptionalPluginFoundationTests> "${CMAKE_COMMAND}" "${CMAKE_SOURCE_DIR}/Plugins")
    set_tests_properties(OptionalPluginFoundationTests PROPERTIES LABELS "plugin;native;foundation" TIMEOUT 180 RUN_SERIAL TRUE)
    add_test(NAME PluginSessionTests COMMAND $<TARGET_FILE:AxlePluginSessionTests> "${CMAKE_COMMAND}" "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/create_project_plugin.py")
    set_tests_properties(PluginSessionTests PROPERTIES TIMEOUT 150 LABELS "plugin;editor;native")
    add_executable(AxlePluginSubmissionTests EXCLUDE_FROM_ALL src/Test_PluginSubmission.cpp
        "${CMAKE_SOURCE_DIR}/Editor/src/PluginRestartSnapshot.cpp"
        "${CMAKE_SOURCE_DIR}/Editor/src/PluginSession.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/PluginBuild.cpp"
        "${CMAKE_SOURCE_DIR}/Editor/src/PluginEditorServices.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/OwnedProcess.cpp")
    target_include_directories(AxlePluginSubmissionTests PRIVATE "${CMAKE_SOURCE_DIR}/Editor/src" "${CMAKE_BINARY_DIR}/PluginSDK/$<CONFIG>")
    target_link_libraries(AxlePluginSubmissionTests PRIVATE Axle::PluginServices Axle::UI Axle::EditorHost)
    add_dependencies(AxlePluginSubmissionTests AxlePluginSDKPackage)
    add_custom_command(TARGET AxlePluginSubmissionTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:AxleRuntimeHost>" "$<TARGET_FILE_DIR:AxlePluginSubmissionTests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:AxleEditorHost>" "$<TARGET_FILE_DIR:AxlePluginSubmissionTests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxcompiler.dll" "$<TARGET_FILE_DIR:AxlePluginSubmissionTests>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxil.dll" "$<TARGET_FILE_DIR:AxlePluginSubmissionTests>" VERBATIM)
    list(APPEND AXLE_TEST_TARGETS AxlePluginSubmissionTests)
    set_tests_properties(PluginServicesTests PROPERTIES RUN_SERIAL TRUE LABELS "plugin;scene;cpu" TIMEOUT 180)

    add_executable(AxleVehiclePluginDataTests EXCLUDE_FROM_ALL
        src/Test_VehiclePluginData.cpp
        "${CMAKE_SOURCE_DIR}/Plugins/Vehicle/Runtime/src/VehicleComponents.cpp")
    target_include_directories(AxleVehiclePluginDataTests PRIVATE
        "${CMAKE_SOURCE_DIR}/Plugins/Vehicle/Runtime/include"
        "${CMAKE_SOURCE_DIR}/Engine/PluginSDK/include")
    target_link_libraries(AxleVehiclePluginDataTests PRIVATE Axle::Plugin Axle::Core)
    list(APPEND AXLE_TEST_TARGETS AxleVehiclePluginDataTests)
    add_test(NAME VehiclePluginDataTests COMMAND $<TARGET_FILE:AxleVehiclePluginDataTests> "${CMAKE_SOURCE_DIR}")
    set_tests_properties(VehiclePluginDataTests PROPERTIES LABELS "plugin;cpu;vehicle")

    add_executable(AxleVehiclePhysicsTests EXCLUDE_FROM_ALL
        src/Test_VehiclePhysics.cpp
        "${CMAKE_SOURCE_DIR}/Plugins/Vehicle/Runtime/src/VehicleComponents.cpp"
        "${CMAKE_SOURCE_DIR}/Plugins/Vehicle/Runtime/src/VehiclePhysics.cpp")
    target_include_directories(AxleVehiclePhysicsTests PRIVATE
        "${CMAKE_SOURCE_DIR}/Plugins/Vehicle/Runtime/include"
        "${CMAKE_SOURCE_DIR}/Engine/PluginSDK/include"
        "${CMAKE_SOURCE_DIR}/Engine/Physics/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/JoltPhysics"
        "${CMAKE_SOURCE_DIR}/ThirdParty/glm")
    target_link_libraries(AxleVehiclePhysicsTests PRIVATE Axle::Physics Axle::Core Axle::Plugin)
    list(APPEND AXLE_TEST_TARGETS AxleVehiclePhysicsTests)
    add_test(NAME VehiclePhysicsTests COMMAND $<TARGET_FILE:AxleVehiclePhysicsTests> "${CMAKE_SOURCE_DIR}")
    set_tests_properties(VehiclePhysicsTests PROPERTIES LABELS "plugin;physics;vehicle;cpu")

    add_executable(AxleVehicleEditorTests EXCLUDE_FROM_ALL
        src/Test_VehicleEditorAuthoring.cpp
        "${CMAKE_SOURCE_DIR}/Plugins/Vehicle/Runtime/src/VehicleComponents.cpp"
        "${CMAKE_SOURCE_DIR}/Plugins/Vehicle/Runtime/src/VehiclePhysics.cpp"
        "${CMAKE_SOURCE_DIR}/Plugins/Vehicle/Runtime/src/VehiclePluginRuntime.cpp")
    target_include_directories(AxleVehicleEditorTests PRIVATE
        "${CMAKE_SOURCE_DIR}/Plugins/Vehicle/Runtime/include"
        "${CMAKE_SOURCE_DIR}/Plugins/Vehicle/Editor/include"
        "${CMAKE_SOURCE_DIR}/Engine/PluginSDK/include"
        "${CMAKE_SOURCE_DIR}/Engine/Physics/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/JoltPhysics"
        "${CMAKE_SOURCE_DIR}/ThirdParty/glm")
    target_link_libraries(AxleVehicleEditorTests PRIVATE Axle::Scene Axle::Physics Axle::Core Axle::Plugin Axle::RuntimeHost)
    add_custom_command(TARGET AxleVehicleEditorTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:AxleRuntimeHost>" "$<TARGET_FILE_DIR:AxleVehicleEditorTests>")
    list(APPEND AXLE_TEST_TARGETS AxleVehicleEditorTests)
    add_test(NAME VehicleEditorTests COMMAND $<TARGET_FILE:AxleVehicleEditorTests> "${CMAKE_SOURCE_DIR}")
    set_tests_properties(VehicleEditorTests PROPERTIES LABELS "plugin;editor;vehicle;cpu")

    add_executable(AxleVehicleAudioVFXTests EXCLUDE_FROM_ALL
        src/Test_VehicleAudioVFX.cpp
        "${CMAKE_SOURCE_DIR}/Plugins/Vehicle/Runtime/src/VehicleComponents.cpp")
    target_include_directories(AxleVehicleAudioVFXTests PRIVATE
        "${CMAKE_SOURCE_DIR}/Plugins/Vehicle/Runtime/include"
        "${CMAKE_SOURCE_DIR}/Engine/PluginSDK/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/glm")
    target_link_libraries(AxleVehicleAudioVFXTests PRIVATE Axle::Core Axle::Plugin)
    list(APPEND AXLE_TEST_TARGETS AxleVehicleAudioVFXTests)
    add_test(NAME VehicleAudioVFXTests COMMAND $<TARGET_FILE:AxleVehicleAudioVFXTests> "${CMAKE_SOURCE_DIR}")
    set_tests_properties(VehicleAudioVFXTests PROPERTIES LABELS "plugin;audio;vfx;vehicle;cpu")

    add_executable(AxleSplinePluginTests EXCLUDE_FROM_ALL
        src/Test_SplinePlugin.cpp)
    target_include_directories(AxleSplinePluginTests PRIVATE
        "${CMAKE_SOURCE_DIR}/Plugins/Spline/Runtime/include"
        "${CMAKE_SOURCE_DIR}/Engine/PluginSDK/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/glm"
        "${CMAKE_SOURCE_DIR}/ThirdParty/entt/src"
        "${CMAKE_SOURCE_DIR}/ThirdParty/JoltPhysics")
    target_link_libraries(AxleSplinePluginTests PRIVATE Axle::SplineRuntime Axle::Physics Axle::Scene Axle::Core Axle::Plugin)
    list(APPEND AXLE_TEST_TARGETS AxleSplinePluginTests)
    add_test(NAME SplinePluginTests COMMAND $<TARGET_FILE:AxleSplinePluginTests> "${CMAKE_SOURCE_DIR}")
    set_tests_properties(SplinePluginTests PROPERTIES LABELS "plugin;spline;physics;cpu")
endif()
if (TARGET AxleTerrainRuntime)
    add_executable(AxleTerrainPluginTests src/Test_TerrainPlugin.cpp)
    target_include_directories(AxleTerrainPluginTests PRIVATE
        "${CMAKE_SOURCE_DIR}/Plugins/Terrain/include"
        "${CMAKE_SOURCE_DIR}/Plugins/Terrain/Runtime/include"
        "${CMAKE_SOURCE_DIR}/Plugins/Terrain/Editor/include"
        "${CMAKE_SOURCE_DIR}/Plugins/Spline/include"
        "${CMAKE_SOURCE_DIR}/Plugins/Spline/Runtime/include"
        "${CMAKE_SOURCE_DIR}/Engine/PluginSDK/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/glm"
        "${CMAKE_SOURCE_DIR}/ThirdParty/entt/src"
        "${CMAKE_SOURCE_DIR}/ThirdParty/JoltPhysics")
    target_link_libraries(AxleTerrainPluginTests PRIVATE Axle::TerrainRuntime Axle::TerrainEditor Axle::SplineRuntime Axle::Physics Axle::Scene Axle::Core Axle::Plugin)
    list(APPEND AXLE_TEST_TARGETS AxleTerrainPluginTests)
    add_test(NAME TerrainPluginTests COMMAND $<TARGET_FILE:AxleTerrainPluginTests> "${CMAKE_SOURCE_DIR}")
    set_tests_properties(TerrainPluginTests PROPERTIES LABELS "plugin;terrain;physics;cpu")
endif()
add_executable(AxleProjectScriptBoundaryTests EXCLUDE_FROM_ALL src/Test_ProjectScriptBoundary.cpp)
target_include_directories(AxleProjectScriptBoundaryTests PRIVATE src "${CMAKE_SOURCE_DIR}/Runtime/src")
target_link_libraries(AxleProjectScriptBoundaryTests PRIVATE
    Axle::RuntimePlayer
    Axle::Project
    Axle::Scene
    Axle::Physics
    Axle::Core
    Axle::Platform
)
if (MSVC)
    target_compile_options(AxleProjectScriptBoundaryTests PRIVATE /bigobj /FS)
endif()
list(APPEND AXLE_TEST_TARGETS AxleProjectScriptBoundaryTests)
add_dependencies(AxleProjectScriptBoundaryTests AxleRuntimeScripts)
add_custom_command(TARGET AxleProjectScriptBoundaryTests POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:AxleRuntimeHost>" "$<TARGET_FILE_DIR:AxleProjectScriptBoundaryTests>" VERBATIM)
add_test(NAME ProjectScriptBoundaryTests COMMAND $<TARGET_FILE:AxleProjectScriptBoundaryTests> $<TARGET_FILE:AxleRuntimeScripts>)
set_tests_properties(ProjectScriptBoundaryTests PROPERTIES LABELS "script;scene;cpu")
add_custom_target(AxleAllTests DEPENDS ${AXLE_TEST_TARGETS} AxleRHIFoundationTests AxleRHIResourceTests AxleRHIResourceViewTests AxleRHIErrorTests AxleRenderServiceTests AxleRuntimeRenderTests AxlePreviewRenderTests AxleArchitectureStressTests)

# Explicit review gate for known OPT-06 contract failures. Keep out of the
# routine regression suite until the incomplete plugin candidate is repaired.
add_executable(AxleOpt06ReviewProbes EXCLUDE_FROM_ALL
    plugin/Opt06ReviewProbes.cpp "${CMAKE_SOURCE_DIR}/Editor/src/PluginBuild.cpp" "${CMAKE_SOURCE_DIR}/Editor/src/OwnedProcess.cpp")
target_include_directories(AxleOpt06ReviewProbes PRIVATE "${CMAKE_SOURCE_DIR}/Editor/src")
target_link_libraries(AxleOpt06ReviewProbes PRIVATE Axle::Plugin Axle::PluginRuntime Axle::Scene)
add_dependencies(AxleOpt06ReviewProbes AxlePluginRuntimeGood)
if(MSVC)
    target_compile_options(AxleOpt06ReviewProbes PRIVATE /bigobj /FS)
endif()

foreach(testTarget IN LISTS AXLE_TEST_TARGETS)
    target_include_directories(${testTarget} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(${testTarget} PRIVATE
        AXLE_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        AXLE_TEST_WORKSPACE_DIR="${AXLE_TEST_WORKSPACE}")
    add_dependencies(${testTarget} AxleTestResources)
endforeach()
