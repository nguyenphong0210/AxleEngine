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

# Embed GLFW source code directly into the build tree
add_subdirectory(ThirdParty/glfw)

# Load modules according to architectural hierarchy
add_subdirectory(Modules/Foundation)
add_subdirectory(Modules/Platform)

# Load test application
add_subdirectory(Sandbox)