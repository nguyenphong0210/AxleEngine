add_library(AxlePluginSDK INTERFACE)
add_library(Axle::PluginSDK ALIAS AxlePluginSDK)
target_include_directories(AxlePluginSDK INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/include/Axle/PluginSDK.hpp" sdkHeaderHash)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/include/Axle/PluginABI.hpp" abiHeaderHash)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AxlePluginSDK.cmake.in" sdkRecipeHash)
string(SHA256 AXLE_PLUGIN_SDK_HASH "${sdkHeaderHash};${abiHeaderHash};${sdkRecipeHash};Windows-x64;CXX20;MD;ABI2")
set(AXLE_PLUGIN_TOOLCHAIN_ID "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}-MSVC${MSVC_VERSION}-x64-MD")
set(AXLE_PLUGIN_SDK_DIRECTORY "${CMAKE_BINARY_DIR}/PluginSDK/$<CONFIG>")

add_library(AxleRuntimeHost SHARED src/RuntimeHost.cpp)
add_library(Axle::RuntimeHost ALIAS AxleRuntimeHost)
target_link_libraries(AxleRuntimeHost PUBLIC Axle::PluginSDK)
target_compile_definitions(AxleRuntimeHost PRIVATE AXLE_RUNTIME_HOST_EXPORTS
    AXLE_PLUGIN_HOST_SDK="${AXLE_PLUGIN_SDK_HASH}-$<CONFIG>"
    AXLE_PLUGIN_HOST_TOOLCHAIN="${AXLE_PLUGIN_TOOLCHAIN_ID}-$<CONFIG>")
add_library(AxleEditorHost SHARED src/EditorHost.cpp)
add_library(Axle::EditorHost ALIAS AxleEditorHost)
target_link_libraries(AxleEditorHost PUBLIC Axle::PluginSDK Axle::RuntimeHost)
target_compile_definitions(AxleEditorHost PRIVATE AXLE_EDITOR_HOST_EXPORTS)

configure_file(cmake/AxlePluginSDK.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/AxlePluginSDK.cmake.gen" @ONLY)
file(GENERATE OUTPUT "${AXLE_PLUGIN_SDK_DIRECTORY}/AxlePluginSDK.cmake"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/AxlePluginSDK.cmake.gen")
file(GENERATE OUTPUT "${AXLE_PLUGIN_SDK_DIRECTORY}/PluginSDKConfig.hpp"
    CONTENT "#pragma once\n#define AXLE_PLUGIN_SDK_DIRECTORY R\"axle(${AXLE_PLUGIN_SDK_DIRECTORY})axle\"\n#define AXLE_PLUGIN_SDK_FINGERPRINT \"${AXLE_PLUGIN_SDK_HASH}-$<CONFIG>\"\n#define AXLE_PLUGIN_TOOLCHAIN_FINGERPRINT \"${AXLE_PLUGIN_TOOLCHAIN_ID}-$<CONFIG>\"\n#define AXLE_PLUGIN_BUILD_CONFIGURATION \"$<CONFIG>\"\n#define AXLE_PLUGIN_CMAKE_GENERATOR R\"axle(${CMAKE_GENERATOR})axle\"\n#define AXLE_PLUGIN_CMAKE_TOOLSET R\"axle(${CMAKE_GENERATOR_TOOLSET})axle\"\n#define AXLE_PLUGIN_CMAKE_INSTANCE R\"axle(${CMAKE_GENERATOR_INSTANCE})axle\"\n")
foreach(host AxleRuntimeHost AxleEditorHost)
    add_custom_command(TARGET ${host} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${AXLE_PLUGIN_SDK_DIRECTORY}/lib" "${AXLE_PLUGIN_SDK_DIRECTORY}/bin"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/include" "${AXLE_PLUGIN_SDK_DIRECTORY}/include"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${host}>" "${AXLE_PLUGIN_SDK_DIRECTORY}/bin/"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_LINKER_FILE:${host}>" "${AXLE_PLUGIN_SDK_DIRECTORY}/lib/"
        VERBATIM)
endforeach()
add_custom_target(AxlePluginSDKPackage DEPENDS AxleRuntimeHost AxleEditorHost)
