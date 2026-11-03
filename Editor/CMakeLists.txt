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

# Synchronize engine resources only; project assets are never bundled into Editor
add_custom_command(TARGET AxleEditor POST_BUILD
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

# Development convenience only. Clear this cache entry for a distributed Editor.
set(AXLE_EDITOR_DEFAULT_PROJECT "${CMAKE_SOURCE_DIR}/Samples/TestProject/TestProject.axleproj"
    CACHE FILEPATH "Project opened by Editor when no explicit project is supplied (empty disables)")
set(AXLE_EDITOR_DEFAULT_PROJECT_PATH "")
if(AXLE_EDITOR_DEFAULT_PROJECT)
    get_filename_component(AXLE_EDITOR_DEFAULT_PROJECT_PATH "${AXLE_EDITOR_DEFAULT_PROJECT}"
        ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    file(TO_CMAKE_PATH "${AXLE_EDITOR_DEFAULT_PROJECT_PATH}" AXLE_EDITOR_DEFAULT_PROJECT_PATH)
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/EditorStartupConfig.hpp"
    CONTENT "#pragma once\n#define AXLE_EDITOR_DEFAULT_PROJECT R\"axle(${AXLE_EDITOR_DEFAULT_PROJECT_PATH})axle\"\n")
