add_library(AxleAsset STATIC
    src/AssetSerializer.cpp
    src/AssetMetadataSerializer.cpp
    src/AssetManager.cpp
    src/AssetCooker.cpp
    src/AssetDSL.cpp
    src/TextureDescriptor.cpp
    ${CMAKE_SOURCE_DIR}/ThirdParty/ufbx/ufbx.c
)
add_library(Axle::Asset ALIAS AxleAsset)

target_include_directories(AxleAsset PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/cgltf>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/ufbx>
)

target_link_libraries(AxleAsset
    PUBLIC
        Axle::Core
        Axle::Renderer
        Axle::RHI
)
