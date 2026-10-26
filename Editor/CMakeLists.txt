add_executable(AxleEditor
    src/Main.cpp
    src/EditorLayer.cpp
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

# Dedicated Test Executable for Automated Verifications
add_executable(AxleTests
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
)

if (MSVC)
    target_compile_options(AxleTests PRIVATE /bigobj)
endif()

add_custom_command(TARGET AxleTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:AxleTests>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Engine/Resources
    $<TARGET_FILE_DIR:AxleTests>/Resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxcompiler.dll
    $<TARGET_FILE_DIR:AxleTests>/dxcompiler.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxil.dll
    $<TARGET_FILE_DIR:AxleTests>/dxil.dll
)

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