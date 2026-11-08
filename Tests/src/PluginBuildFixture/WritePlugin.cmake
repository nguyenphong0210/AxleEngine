# Process/plugin build fixture for testing Axle::BuildPlugins.
if(EXISTS "${FLAGS}/FailBuild.flag")
    message(FATAL_ERROR "Intentional Plugin build failure")
endif()
if(EXISTS "${FLAGS}/SlowBuild.flag")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 20)
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
file(WRITE "${OUTPUT}/CustomPlugin_Runtime.dll" "fresh plugin dll fixture")
