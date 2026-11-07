add_library(AxleRHI STATIC
    src/RHIDevice.cpp
    src/RHIResource.cpp
    src/RHIValidation.cpp
    src/RHISurface.cpp
    src/RHISwapChain.cpp
    src/RHIShader.cpp
    src/RHIPipeline.cpp
    src/RHIBarrier.cpp
    src/RHICommandBuffer.cpp
    src/RHIFrameContext.cpp
    src/RHIBuffer.cpp
    src/RHIStreamer.cpp
    src/RHITexture.cpp
    src/RHISwitcher.cpp
    src/RHIMesh.cpp
    src/RHIMaterial.cpp
    src/RHISampler.cpp
    src/RHIDescriptorPool.cpp
    src/RHIBindless.cpp
    src/RHILight.cpp
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

if(WIN32)
    # The pinned NRI build supplies the Vulkan declarations used for validation
    # prerequisite discovery; no SDK installation or vendor update is required.
    target_include_directories(AxleRHI PRIVATE "${CMAKE_BINARY_DIR}/_deps/vulkan_headers-src/include")
endif()
