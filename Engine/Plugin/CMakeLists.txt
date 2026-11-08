add_library(AxlePlugin STATIC src/Plugin.cpp src/PluginSelection.cpp include/Axle/Plugin.hpp include/Axle/PluginSelection.hpp)
add_library(Axle::Plugin ALIAS AxlePlugin)
target_include_directories(AxlePlugin PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(AxlePlugin PUBLIC Axle::Core)
