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
add_executable(AxleRenderUnitTests EXCLUDE_FROM_ALL
    rendering/RenderUnitTestMain.cpp
    rendering/Test_RenderContractsUnit.cpp
    rendering/Test_RenderBackendPolicyUnit.cpp
    rendering/Test_RenderPrimitiveIdentityUnit.cpp
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
set_tests_properties(ArchitectureBoundaryChecks ArchitectureForbiddenEdgeFixture BaselineToolRegressions
    PROPERTIES LABELS "architecture;cpu" TIMEOUT 60)

# Explicit developer/test build. Neither this target nor its executables are in ALL.
set(AXLE_TEST_TARGETS AxleProjectUnitTests AxleAssetUnitTests AxleAssetFormatUnitTests AxleSceneDataUnitTests AxleSharedRuntimePhasesUnitTests AxleRenderUnitTests AxlePluginUnitTests AxleTests AxleMaterialUnitTests AxleVFXUnitTests AxleVFXGPUTests AxleAnimationUnitTests AxleJobsECSTests)
if(WIN32)
    add_executable(AxleBuildWorkflowTests EXCLUDE_FROM_ALL src/Test_BuildWorkflow.cpp "${CMAKE_SOURCE_DIR}/Editor/src/RuntimeBuild.cpp")
    target_include_directories(AxleBuildWorkflowTests PRIVATE src "${CMAKE_SOURCE_DIR}/Editor/src" "${CMAKE_BINARY_DIR}/Editor/generated/$<CONFIG>")
    target_link_libraries(AxleBuildWorkflowTests PRIVATE Axle::Asset)
    list(APPEND AXLE_TEST_TARGETS AxleBuildWorkflowTests)
    add_test(NAME BuildWorkflowFixtureConfigure COMMAND "${CMAKE_COMMAND}"
        -S "${CMAKE_CURRENT_SOURCE_DIR}/src/BuildWorkflowFixture"
        -B "${CMAKE_CURRENT_BINARY_DIR}/Build Workflow Fixture")
    set_tests_properties(BuildWorkflowFixtureConfigure PROPERTIES FIXTURES_SETUP BuildWorkflow)
    add_test(NAME BuildWorkflowTests COMMAND $<TARGET_FILE:AxleBuildWorkflowTests>
        "${CMAKE_COMMAND}" "${CMAKE_CURRENT_BINARY_DIR}/Build Workflow Fixture" "$<CONFIG>")
    set_tests_properties(BuildWorkflowTests PROPERTIES FIXTURES_REQUIRED BuildWorkflow RUN_SERIAL TRUE TIMEOUT 30)
endif()
add_custom_target(AxleAllTests DEPENDS ${AXLE_TEST_TARGETS})

foreach(testTarget IN LISTS AXLE_TEST_TARGETS)
    target_include_directories(${testTarget} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(${testTarget} PRIVATE
        AXLE_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        AXLE_TEST_WORKSPACE_DIR="${AXLE_TEST_WORKSPACE}")
    add_dependencies(${testTarget} AxleTestResources)
endforeach()
