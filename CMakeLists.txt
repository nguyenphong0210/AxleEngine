cmake_minimum_required(VERSION 3.22)
project(AxleEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AXLE_BUILD_EDITOR "Build the Editor product" ON)
option(AXLE_BUILD_RUNTIME "Configure the standalone Runtime product" ON)
option(AXLE_BUILD_TESTS "Configure explicit regression targets" ON)
option(AXLE_ENABLE_GL_COMPATIBILITY "Allow the OpenGL compatibility backend" ON)
set(AXLE_RUNTIME_PLUGIN_GRAPH "" CACHE FILEPATH "Validated, frozen Runtime plugin graph")

enable_testing()

# Disable unnecessary parts of GLFW to speed up the build
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

# Configure enkiTS Task Scheduler options
set(ENKITS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENKITS_INSTALL OFF CACHE BOOL "" FORCE)
set(ENKITS_BUILD_SHARED OFF CACHE BOOL "" FORCE)

# Configure Jolt Physics options to match engine settings
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "Keep the STL/exception ABI consistent across engine and script modules" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)

# Configure NVIDIA Rendering Interface (NRI) options
set(NRI_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(NRI_ENABLE_D3D11_SUPPORT OFF CACHE BOOL "" FORCE)
set(NRI_ENABLE_D3D12_SUPPORT ON CACHE BOOL "" FORCE)
set(NRI_ENABLE_VK_SUPPORT ON CACHE BOOL "" FORCE)
set(NRI_ENABLE_WGPU_SUPPORT OFF CACHE BOOL "" FORCE)
set(NRI_ENABLE_METAL_SUPPORT OFF CACHE BOOL "" FORCE)
set(NRI_ENABLE_NONE_SUPPORT ON CACHE BOOL "" FORCE)
set(NRI_ENABLE_VALIDATION_SUPPORT ON CACHE BOOL "" FORCE)
set(NRI_ENABLE_NVTX_SUPPORT OFF CACHE BOOL "" FORCE)
set(NRI_ENABLE_AMDAGS OFF CACHE BOOL "" FORCE)
set(NRI_ENABLE_NVAPI OFF CACHE BOOL "" FORCE)
set(NRI_ENABLE_AGILITY_SDK_SUPPORT ON CACHE BOOL "" FORCE)

# Embed ThirdParty libraries directly into the build tree
add_subdirectory(ThirdParty/glfw)
add_subdirectory(ThirdParty/glad)
add_subdirectory(ThirdParty/spdlog)
add_subdirectory(ThirdParty/enkiTS)
add_subdirectory(ThirdParty/JoltPhysics/Build ThirdParty/JoltPhysics)
add_subdirectory(ThirdParty/NRI)


# Load engine modules according to architectural hierarchy
add_subdirectory(Engine/Core)
add_subdirectory(Engine/Platform)
add_subdirectory(Engine/RHI)
add_subdirectory(Engine/Renderer)
add_subdirectory(Engine/Audio)
add_subdirectory(Engine/Asset)
add_subdirectory(Engine/Animation)
add_subdirectory(Engine/VFX)
add_subdirectory(Engine/Project)
add_subdirectory(Engine/Plugin)
add_subdirectory(Engine/UI)
add_subdirectory(Engine/Physics)
add_subdirectory(Engine/SceneData)
add_subdirectory(Engine/Scene)
add_subdirectory(Engine/PluginSDK)
add_subdirectory(Engine/PluginRuntime)
add_subdirectory(Engine/PluginServices)

# Load development-only prototypes for the explicit Editor integration suite.
if(AXLE_BUILD_TESTS AND AXLE_BUILD_EDITOR)
    add_subdirectory(Plugins/Spline)
    add_subdirectory(Plugins/Terrain)
endif()

# Load Axle Editor
if(AXLE_BUILD_EDITOR)
    add_subdirectory(Editor)
endif()

# Load Axle Standalone Runtime (Game Player)
if(AXLE_BUILD_RUNTIME)
    add_subdirectory(Runtime)
endif()
# Tests currently exercise Editor composition as well as CPU-only libraries.
if(AXLE_BUILD_TESTS)
    if(AXLE_BUILD_EDITOR AND NOT AXLE_BUILD_RUNTIME)
        message(FATAL_ERROR "The Editor integration suite requires Runtime configuration; use AXLE_BUILD_TESTS=OFF or the Editor-free CPU suite")
    endif()
    add_subdirectory(Tests)
endif()
