if(AXLE_BUILD_EDITOR AND AXLE_RUNTIME_PLUGIN_GRAPH)
    message(FATAL_ERROR "Selected shipping composition requires AXLE_BUILD_EDITOR=OFF")
endif()
set(composition "${CMAKE_CURRENT_BINARY_DIR}/generated/RuntimeModules.cpp")
if(AXLE_RUNTIME_PLUGIN_GRAPH)
    include("${CMAKE_CURRENT_LIST_DIR}/StaticModules.cmake")
    include("${AXLE_RUNTIME_PLUGIN_GRAPH}")
    set(composition "${AXLE_RUNTIME_COMPOSITION_SOURCE}")
else()
    file(GENERATE OUTPUT "${composition}" CONTENT
        "#include \"RuntimeComposition.hpp\"\nnamespace Axle {\nstd::string CompiledRuntimeSnapshot() { return {}; }\nstd::vector<std::string> CompiledRuntimeSelection() { return {}; }\nvoid RegisterRuntimeModules(PluginRuntime::RegistrationRegistry&, PluginServiceSession&, std::vector<std::unique_ptr<PluginRuntime::NativePlugin>>&) {}\n}\n")
endif()
add_library(AxleRuntimePlayer STATIC
    src/RuntimeLayer.cpp
    src/RuntimeStartup.cpp
    src/RuntimeRenderContext.cpp
    src/RuntimeComposition.cpp
    "${composition}"
)
add_library(Axle::RuntimePlayer ALIAS AxleRuntimePlayer)

add_executable(AxleRuntime EXCLUDE_FROM_ALL src/Main.cpp)
if(AXLE_RUNTIME_OUTPUT_DIRECTORY)
    set_target_properties(AxleRuntime PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${AXLE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>")
endif()
target_link_libraries(AxleRuntime PRIVATE Axle::RuntimePlayer)

target_include_directories(AxleRuntimePlayer PUBLIC
    src
)
target_include_directories(AxleRuntimePlayer PRIVATE "${CMAKE_BINARY_DIR}/PluginSDK/$<CONFIG>")

target_link_libraries(AxleRuntimePlayer PUBLIC
    Axle::Core
    Axle::RHI
    Axle::Platform
    Axle::Renderer
    Axle::Audio
    Axle::Asset
    Axle::Physics
    Axle::Scene
    Axle::Project
    Axle::UI2D
    Axle::Animation
    Axle::VFX
    Axle::PluginServices
)
if(AXLE_RUNTIME_PLUGIN_GRAPH)
    axle_link_selected_runtime(AxleRuntimePlayer)
endif()

if(NOT AXLE_BUILD_EDITOR)
    set(scriptRegistration "#include <Axle/ScriptRegistry.hpp>\n")
    set(projectScripts)
    if(AXLE_RUNTIME_SCRIPTS_ROOT)
        file(GLOB_RECURSE projectScripts CONFIGURE_DEPENDS "${AXLE_RUNTIME_SCRIPTS_ROOT}/*.hpp" "${AXLE_RUNTIME_SCRIPTS_ROOT}/*.cpp")
    endif()
    foreach(script IN LISTS projectScripts)
        if(script MATCHES "\\.hpp$")
            string(APPEND scriptRegistration "#include \"${script}\"\n")
        else()
            target_sources(AxleRuntime PRIVATE "${script}")
        endif()
    endforeach()
    string(APPEND scriptRegistration "namespace Axle { void InitializeCompiledScripts() {} }\n")
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/RuntimeScripts.cpp" CONTENT "${scriptRegistration}")
    target_sources(AxleRuntimePlayer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/RuntimeScripts.cpp")
    target_compile_definitions(AxleRuntimePlayer PUBLIC AXLE_SHIPPING)
    if(MSVC)
        target_link_options(AxleRuntime PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/AxleRuntime.map")
    endif()
    add_custom_command(TARGET AxleRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Engine/Resources" "$<TARGET_FILE_DIR:AxleRuntime>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxcompiler.dll" "$<TARGET_FILE_DIR:AxleRuntime>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64/dxil.dll" "$<TARGET_FILE_DIR:AxleRuntime>"
        VERBATIM)
    return()
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/RuntimeScripts.cpp" CONTENT "namespace Axle { void InitializeCompiledScripts() {} }\n")
target_sources(AxleRuntimePlayer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/RuntimeScripts.cpp")

add_library(AxleRuntimeScripts SHARED EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/Editor/src/Scripts/ScriptsModule.cpp")
set_target_properties(AxleRuntimeScripts PROPERTIES OUTPUT_NAME AxleScripts
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/AutoScripts.inc"
    CONTENT "// Empty shipping baseline; project scripts are compiled by their declared build.\n")
target_include_directories(AxleRuntimeScripts PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_SOURCE_DIR}/Engine/Scene/include")
target_compile_definitions(AxleRuntimeScripts PRIVATE AXLE_SCRIPT_BUILD_DLL AXLE_SHIPPING)
target_link_libraries(AxleRuntimeScripts PRIVATE Axle::SceneData Axle::Platform)
add_dependencies(AxleRuntime AxleRuntimeScripts)

# Stage engine resources and scripts only; project assets are supplied by export
add_custom_command(TARGET AxleRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:AxleRuntimeScripts>
    $<TARGET_FILE_DIR:AxleRuntime>/$<TARGET_FILE_NAME:AxleRuntimeScripts>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:AxleRuntimeHost> $<TARGET_FILE_DIR:AxleRuntime>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Engine/Resources
    $<TARGET_FILE_DIR:AxleRuntime>/Resources
)
