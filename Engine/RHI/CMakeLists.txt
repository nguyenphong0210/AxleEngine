add_library(AxleRHI STATIC
    src/RHIDevice.cpp
    src/RHISurface.cpp
)
add_library(Axle::RHI ALIAS AxleRHI)

target_include_directories(AxleRHI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/NRI/Include>
)

target_link_libraries(AxleRHI
    PUBLIC
        Axle::Core
        Axle::Platform
        NRI
)
