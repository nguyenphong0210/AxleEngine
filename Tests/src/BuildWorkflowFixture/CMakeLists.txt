cmake_minimum_required(VERSION 3.22)
project(AxleBuildWorkflowFixture NONE)
add_custom_target(AxleRuntime
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${CMAKE_BINARY_DIR}/output"
        "-DFLAGS=${CMAKE_BINARY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/WriteRuntime.cmake"
    VERBATIM)
