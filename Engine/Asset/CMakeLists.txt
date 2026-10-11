add_library(AxleAsset STATIC
    src/AssetSerializer.cpp
)
add_library(Axle::Asset ALIAS AxleAsset)

target_include_directories(AxleAsset PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(AxleAsset
    PUBLIC
        Axle::Core
        Axle::Renderer
)
