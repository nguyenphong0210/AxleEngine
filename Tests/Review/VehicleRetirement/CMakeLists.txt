# Opt-in review fixture injected through CMAKE_PROJECT_INCLUDE. No production target edits.
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR
   OR NOT PROJECT_NAME STREQUAL "AxleEngine"
   OR TARGET AxleVehicleRetirementReview)
    return()
endif()

add_executable(AxleVehicleRetirementReview EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/VehicleRetirementReview.cpp")
target_compile_features(AxleVehicleRetirementReview PRIVATE cxx_std_20)
target_compile_definitions(AxleVehicleRetirementReview PRIVATE
    AXLE_RETIREMENT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(AxleVehicleRetirementReview PRIVATE
    Axle::Scene Axle::UI Axle::UI2D Axle::Project)
set_target_properties(AxleVehicleRetirementReview PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/opt00-review/bin")
if(MSVC)
    target_compile_options(AxleVehicleRetirementReview PRIVATE /bigobj /FS)
endif()
