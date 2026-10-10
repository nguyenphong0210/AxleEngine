add_library(AxleFoundation STATIC
    src/Log.cpp
)
add_library(Axle::Foundation ALIAS AxleFoundation)

target_include_directories(AxleFoundation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
)

target_link_libraries(AxleFoundation PUBLIC
    spdlog::spdlog
)