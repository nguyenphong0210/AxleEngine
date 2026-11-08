axle_add_plugin_module(VehicleRuntime runtime
    Runtime/src/VehiclePluginRuntime.cpp
    Runtime/src/VehicleComponents.cpp
)
target_include_directories(VehicleRuntime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/include"
)

axle_add_plugin_module(VehicleEditor editor
    Editor/src/VehiclePluginEditor.cpp
)
if(TARGET VehicleEditor)
target_include_directories(VehicleEditor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Editor/include"
)
target_link_libraries(VehicleEditor PRIVATE VehicleRuntime)
endif()
