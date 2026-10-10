add_library(AxleCore STATIC
    src/Log.cpp
)
add_library(Axle::Core ALIAS AxleCore)
add_library(Axle::Foundation ALIAS AxleCore)

target_include_directories(AxleCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
)

target_link_libraries(AxleCore PUBLIC
    spdlog::spdlog
)