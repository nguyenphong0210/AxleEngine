add_library(AxleCore STATIC
    src/Log.cpp
    src/FileSystem.cpp
    src/UUID.cpp
    src/JobSystem.cpp
    src/Package.cpp
    src/DecodedImage.cpp
    src/stb_image.cpp
)
add_library(Axle::Core ALIAS AxleCore)
add_library(Axle::Foundation ALIAS AxleCore)
target_include_directories(AxleCore PRIVATE "${CMAKE_SOURCE_DIR}/ThirdParty/stb")

target_include_directories(AxleCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
)

target_link_libraries(AxleCore PUBLIC
    spdlog::spdlog
    enkiTS
)
