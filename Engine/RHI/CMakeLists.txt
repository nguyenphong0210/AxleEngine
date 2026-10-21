add_library(AxleRHI STATIC
    src/RHIDevice.cpp
    src/RHISurface.cpp
    src/RHISwapChain.cpp
    src/RHIShader.cpp
    src/RHIPipeline.cpp
)
add_library(Axle::RHI ALIAS AxleRHI)

target_include_directories(AxleRHI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/NRI/Include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/dxc/inc>
)

target_link_libraries(AxleRHI
    PUBLIC
        Axle::Core
        Axle::Platform
        NRI
)
