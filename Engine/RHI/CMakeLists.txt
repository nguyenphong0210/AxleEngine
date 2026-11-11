add_library(AxleRHI STATIC
    src/RHIDevice.cpp
    src/RHIResource.cpp
    src/RHIValidation.cpp
    src/RHISurface.cpp
    src/RHISwapChain.cpp
    src/RHIShader.cpp
    src/RHISlangCompiler.cpp
    src/RHIPipeline.cpp
    src/RHIBarrier.cpp
    src/RHICommandBuffer.cpp
    src/RHIFrameContext.cpp
    src/RHIPresentationContext.cpp
    src/RHIBuffer.cpp
    src/RHIStreamer.cpp
    src/RHITexture.cpp
    src/RHIViewTargets.cpp
    src/RHISwitcher.cpp
    src/RHIMesh.cpp
    src/RHIMaterial.cpp
    src/RHISampler.cpp
    src/RHIDescriptorPool.cpp
    src/RHIBindless.cpp
    src/RHILight.cpp
    src/RHIParallelRecording.cpp
    src/RHITransientResources.cpp
    src/RHITransientAliasing.cpp
)
add_library(Axle::RHI ALIAS AxleRHI)

option(AXLE_ENABLE_SLANG "Build the optional Slang graph compiler evaluation backend" OFF)
set(AXLE_SLANG_SDK_ROOT "" CACHE PATH "Explicit installed Slang SDK root (optional evaluation only)")
set(AXLE_SLANG_LIBRARY "")
set(AXLE_SLANG_DXC_DIRECTORY "${CMAKE_SOURCE_DIR}/ThirdParty/dxc/bin/x64")
if(AXLE_ENABLE_SLANG)
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "The C12-05 Slang evaluation adapter currently requires Windows/MSVC")
    endif()
    if(NOT AXLE_SLANG_SDK_ROOT)
        message(FATAL_ERROR "AXLE_ENABLE_SLANG requires an explicit AXLE_SLANG_SDK_ROOT")
    endif()
    get_filename_component(AXLE_SLANG_RESOLVED_ROOT "${AXLE_SLANG_SDK_ROOT}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    # A new SDK root must not retain old headers/imports from CMake's find cache.
    unset(AXLE_SLANG_INCLUDE CACHE)
    unset(AXLE_SLANG_IMPORT CACHE)
    find_path(AXLE_SLANG_INCLUDE slang.h PATHS "${AXLE_SLANG_RESOLVED_ROOT}/include/slang" "${AXLE_SLANG_RESOLVED_ROOT}/include" NO_DEFAULT_PATH NO_CACHE REQUIRED)
    find_library(AXLE_SLANG_IMPORT slang-compiler PATHS "${AXLE_SLANG_RESOLVED_ROOT}/lib" NO_DEFAULT_PATH NO_CACHE REQUIRED)
    set(AXLE_SLANG_LIBRARY "${AXLE_SLANG_RESOLVED_ROOT}/bin/slang-compiler.dll")
    if(NOT EXISTS "${AXLE_SLANG_LIBRARY}")
        message(FATAL_ERROR "Optional Slang SDK compiler DLL is missing: ${AXLE_SLANG_LIBRARY}")
    endif()
    target_include_directories(AxleRHI PRIVATE "${AXLE_SLANG_INCLUDE}")
    add_library(Slang::Compiler UNKNOWN IMPORTED)
    set_target_properties(Slang::Compiler PROPERTIES IMPORTED_LOCATION "${AXLE_SLANG_IMPORT}")
    target_link_libraries(AxleRHI PRIVATE Slang::Compiler delayimp)
    # SDK C++ reflection methods are imported from the SDK library. Delay-loading
    # keeps the default DXC path runnable without loading Slang at process start.
    target_link_options(AxleRHI INTERFACE "/DELAYLOAD:slang-compiler.dll")
    # The adapter resolves the C entry point from the configured absolute DLL;
    # the import library is still required as an SDK integrity/configuration
    # probe, but is deliberately not linked into the product. This keeps the
    # default DXC process free of a Slang import table entry.
endif()
configure_file(src/SlangBuildConfig.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/SlangBuildConfig.hpp" @ONLY)
target_include_directories(AxleRHI PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(AxleRHI PRIVATE AXLE_ENABLE_GL_COMPATIBILITY=$<BOOL:${AXLE_ENABLE_GL_COMPATIBILITY}>)

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
