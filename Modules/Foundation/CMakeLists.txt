add_library(AxleFoundation INTERFACE)
add_library(Axle::Foundation ALIAS AxleFoundation)

target_include_directories(AxleFoundation INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)