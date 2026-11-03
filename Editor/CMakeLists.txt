add_executable(AxleEditor
    src/Main.cpp
    src/EditorLayer.cpp
    src/RuntimeBuild.cpp
)

target_include_directories(AxleEditor PRIVATE
    src
)

target_link_libraries(AxleEditor PRIVATE
    Axle::Core
    Axle::Platform
    Axle::RHI
    Axle::Renderer
    Axle::Audio
    Axle::Asset
    Axle::UI
    Axle::Physics
    Axle::Scene
    Axle::Vehicle
    Axle::Project
    Axle::Spline
    Axle::Terrain
    Axle::Animation
    Axle::VFX
)

if(MSVC)
    target_compile_options(AxleEditor PRIVATE "$<$<CONFIG:Release>:/Zi>")
    target_link_options(AxleEditor PRIVATE "$<$<CONFIG:Release>:/DEBUG>" "$<$<CONFIG:Release>:/MAP:AxleEditor.map>")
endif()

# ==============================================================================
# User Gameplay Scripts Module (Hot-Reloadable Shared Library)
# ==============================================================================
file(GLOB_RECURSE AXLE_USER_SCRIPT_SOURCES
    "src/Scripts/*.cpp"
    "src/Scripts/*.hpp"
    "assets/scripts/*.cpp"
    "assets/scripts/*.hpp"
)

# Automatically generate AutoScripts.inc to include all header-only scripts into ScriptsModule.cpp
set(SCRIPT_INCLUDES_CONTENT "// Auto-generated script includes for Header-only scripts\n#pragma once\n")
foreach(SCRIPT_FILE ${AXLE_USER_SCRIPT_SOURCES})
    if(SCRIPT_FILE MATCHES "\\.hpp$")
        get_filename_component(SCRIPT_NAME ${SCRIPT_FILE} NAME)
        if(NOT SCRIPT_NAME STREQUAL "ScriptsModule.hpp")
            file(TO_CMAKE_PATH "${SCRIPT_FILE}" NORMALIZED_SCRIPT_FILE)
            string(APPEND SCRIPT_INCLUDES_CONTENT "#include \"${NORMALIZED_SCRIPT_FILE}\"\n")
        endif()
    endif()
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/AutoScripts.inc" CONTENT "${SCRIPT_INCLUDES_CONTENT}")

add_library(AxleScripts SHARED
    src/Scripts/ScriptsModule.cpp
    ${AXLE_USER_SCRIPT_SOURCES}
)

target_include_directories(AxleScripts PUBLIC
    src
    src/Scripts
    assets/scripts
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(AxleScripts PRIVATE
    AXLE_SCRIPTS_DLL
    AXLE_SCRIPT_BUILD_DLL
)

target_link_libraries(AxleScripts PRIVATE
    Axle::Core
    Axle::Platform
    Axle::Renderer
    Axle::Scene
    Axle::Physics
    Axle::Vehicle
    Axle::UI
)

# Synchronize Editor/assets to binary output directory on post-build
add_custom_command(TARGET AxleEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:AxleEditor>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Engine/Resources
    $<TARGET_FILE_DIR:AxleEditor>/Resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxcompiler.dll
    $<TARGET_FILE_DIR:AxleEditor>/dxcompiler.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxil.dll
    $<TARGET_FILE_DIR:AxleEditor>/dxil.dll
)

add_dependencies(AxleEditor AxleScripts)

# Export builds this exact configured Runtime target on demand, not as an Editor dependency.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/RuntimeBuildConfig.hpp"
    CONTENT "#pragma once\n#define AXLE_RUNTIME_CMAKE R\"axle(${CMAKE_COMMAND})axle\"\n#define AXLE_RUNTIME_BUILD_DIRECTORY R\"axle(${CMAKE_BINARY_DIR})axle\"\n#define AXLE_RUNTIME_EXECUTABLE_DIRECTORY R\"axle($<TARGET_FILE_DIR:AxleRuntime>)axle\"\n#define AXLE_RUNTIME_CONFIGURATION \"$<CONFIG>\"\n")
target_include_directories(AxleEditor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>")

# Dedicated Test Executable for Automated Verifications
add_executable(AxleTests EXCLUDE_FROM_ALL
    src/Test.cpp
    src/Tests/Test_Milestone1.cpp
    src/Tests/Test_Milestone2.cpp
    src/Tests/Test_Milestone3.cpp
    src/Tests/Test_Milestone4.cpp
    src/Tests/Test_Milestone5.cpp
    src/Tests/Test_Milestone6.cpp
    src/Tests/Test_Milestone7.cpp
    src/Tests/Test_Milestone8_Part1.cpp
    src/Tests/Test_Milestone8_Part2.cpp
    src/Tests/Test_Milestone8_Part3.cpp
    src/Tests/Test_Milestone8_Part4.cpp
    src/Tests/Test_Milestone9.cpp
    src/Tests/Test_Milestone10.cpp
    src/Tests/Test_MaterialRendering.cpp
    src/Tests/Test_MaterialMeshImport.cpp
    src/Tests/Test_MaterialAcceptance.cpp
    src/Tests/Test_VFXRendering.cpp
)

if (MSVC)
    target_compile_options(AxleTests PRIVATE /bigobj /FS)
endif()

# Legacy integration tests stage their own data without building the Editor executable.
add_custom_target(AxleTestResources
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:AxleTests>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Engine/Resources" "$<TARGET_FILE_DIR:AxleTests>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxcompiler.dll" "$<TARGET_FILE_DIR:AxleTests>/dxcompiler.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxil.dll" "$<TARGET_FILE_DIR:AxleTests>/dxil.dll"
    VERBATIM)
add_dependencies(AxleTests AxleTestResources)

target_include_directories(AxleTests PRIVATE
    src
    src/Tests
    assets/scripts
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
    Axle::Vehicle
    Axle::Project
    Axle::Spline
    Axle::Terrain
    Axle::Animation
    Axle::VFX
)

add_dependencies(AxleTests AxleScripts)

# Dedicated CPU-Only Material Unit Test Target (Phase 38.3)
add_executable(AxleMaterialUnitTests EXCLUDE_FROM_ALL
    src/Tests/MaterialUnitTestMain.cpp
    src/Tests/Test_MaterialSerializationUnit.cpp
    src/Tests/Test_MaterialTextureSelectionUnit.cpp
    src/Tests/Test_MaterialBindingPlanUnit.cpp
    src/Tests/Test_MaterialProgramCacheUnit.cpp
    src/Tests/Test_ShaderDragDropUnit.cpp
    src/Tests/Test_MaterialAcceptance.cpp
)

target_include_directories(AxleMaterialUnitTests PRIVATE
    src
    src/Tests
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
    src/Tests/VFXUnitTestMain.cpp
    src/Tests/Test_VFXAuthoringUnit.cpp
    src/Tests/Test_VFXDefinitionUnit.cpp
    src/Tests/Test_VFXPersistenceUnit.cpp
    src/Tests/Test_VFXDocumentUnit.cpp
    src/Tests/Test_VFXPreviewIsolationUnit.cpp
    src/Tests/Test_VFXPlaybackUnit.cpp
    src/Tests/Test_VFXRenderPlanUnit.cpp
    src/Tests/Test_VFXEventsAndMeshUnit.cpp
)

target_include_directories(AxleVFXUnitTests PRIVATE
    src
    src/Tests
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
add_executable(AxleVFXGPUTests EXCLUDE_FROM_ALL src/Tests/VFXGPUTestMain.cpp src/Tests/Test_VFXRendering.cpp)
target_include_directories(AxleVFXGPUTests PRIVATE src/Tests)
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
add_executable(AxleAnimationUnitTests EXCLUDE_FROM_ALL src/Tests/AnimationUnitTestMain.cpp src/Tests/Test_AnimationCorrections.cpp src/Tests/Test_AnimationAssetCorrections.cpp src/Tests/Test_AnimationImporterCorrections.cpp src/Tests/Test_AnimationControllerCorrections.cpp src/Tests/Test_AnimationSceneCorrections.cpp src/Tests/Test_AnimationPackageCorrections.cpp src/Tests/Test_AnimationBakeValidation.cpp src/Tests/Test_AnimationDocumentUnit.cpp)
target_include_directories(AxleAnimationUnitTests PRIVATE src/Tests)
target_compile_definitions(AxleAnimationUnitTests PRIVATE AXLE_ANIMATION_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(AxleAnimationUnitTests PRIVATE Axle::Animation Axle::Scene)
if(MSVC)
    target_compile_options(AxleAnimationUnitTests PRIVATE /bigobj /FS)
endif()
add_test(NAME AnimationUnitTests COMMAND $<TARGET_FILE:AxleAnimationUnitTests>)
add_test(NAME AnimationHarnessFailure COMMAND $<TARGET_FILE:AxleAnimationUnitTests> --self-test-failure)
set_tests_properties(AnimationUnitTests AnimationHarnessFailure PROPERTIES LABELS "animation;cpu")
set_tests_properties(AnimationHarnessFailure PROPERTIES WILL_FAIL TRUE)

# Explicit developer/test build. Neither this target nor its executables are in ALL.
set(AXLE_TEST_TARGETS AxleTests AxleMaterialUnitTests AxleVFXUnitTests AxleVFXGPUTests AxleAnimationUnitTests)
if(WIN32)
    add_executable(AxleBuildWorkflowTests EXCLUDE_FROM_ALL src/Tests/Test_BuildWorkflow.cpp src/RuntimeBuild.cpp)
    target_include_directories(AxleBuildWorkflowTests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>")
    target_link_libraries(AxleBuildWorkflowTests PRIVATE Axle::Asset)
    list(APPEND AXLE_TEST_TARGETS AxleBuildWorkflowTests)
    add_test(NAME BuildWorkflowFixtureConfigure COMMAND "${CMAKE_COMMAND}"
        -S "${CMAKE_CURRENT_SOURCE_DIR}/src/Tests/BuildWorkflowFixture"
        -B "${CMAKE_CURRENT_BINARY_DIR}/Build Workflow Fixture")
    set_tests_properties(BuildWorkflowFixtureConfigure PROPERTIES FIXTURES_SETUP BuildWorkflow)
    add_test(NAME BuildWorkflowTests COMMAND $<TARGET_FILE:AxleBuildWorkflowTests>
        "${CMAKE_COMMAND}" "${CMAKE_CURRENT_BINARY_DIR}/Build Workflow Fixture" "$<CONFIG>")
    set_tests_properties(BuildWorkflowTests PROPERTIES FIXTURES_REQUIRED BuildWorkflow RUN_SERIAL TRUE TIMEOUT 30)
endif()
add_custom_target(AxleAllTests DEPENDS ${AXLE_TEST_TARGETS})
