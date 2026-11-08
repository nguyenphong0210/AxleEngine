add_library(AxlePluginRuntime STATIC
    src/PluginRuntime.cpp
)
add_library(Axle::PluginRuntime ALIAS AxlePluginRuntime)
target_include_directories(AxlePluginRuntime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(AxlePluginRuntime PUBLIC Axle::Plugin)
if(UNIX AND NOT APPLE)
    target_link_libraries(AxlePluginRuntime PRIVATE dl)
endif()
if(MSVC)
    target_compile_options(AxlePluginRuntime PRIVATE /W4 /FS)
endif()

