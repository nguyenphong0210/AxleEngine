cmake_minimum_required(VERSION 3.22)
project(AxlePluginBuildFixture NONE)
add_custom_target(CustomPlugin_Runtime
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${CMAKE_BINARY_DIR}/output"
        "-DFLAGS=${CMAKE_BINARY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/WritePlugin.cmake"
    VERBATIM)
