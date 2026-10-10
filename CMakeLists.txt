cmake_minimum_required(VERSION 3.22)
project(AxleEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Disable unnecessary parts of GLFW to speed up the build
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

# Configure Jolt Physics options to match engine settings
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)

# Embed ThirdParty libraries directly into the build tree
add_subdirectory(ThirdParty/glfw)
add_subdirectory(ThirdParty/glad)
add_subdirectory(ThirdParty/spdlog)
add_subdirectory(ThirdParty/JoltPhysics/Build ThirdParty/JoltPhysics)

# Load engine modules according to architectural hierarchy
add_subdirectory(Engine/Core)
add_subdirectory(Engine/Platform)
add_subdirectory(Engine/Renderer)
add_subdirectory(Engine/UI)
add_subdirectory(Engine/Physics)
add_subdirectory(Engine/Scene)
add_subdirectory(Engine/Vehicle)

# Load test application
add_subdirectory(Sandbox)