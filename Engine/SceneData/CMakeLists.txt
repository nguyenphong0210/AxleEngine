add_library(AxleSceneData STATIC
    src/SceneData.cpp
    src/ComponentCodec.cpp
    src/Entity.cpp
    src/SceneCommandBuffer.cpp
    src/SceneDataDocument.cpp
)
add_library(Axle::SceneData ALIAS AxleSceneData)
target_include_directories(AxleSceneData PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/entt/src>
)
target_link_libraries(AxleSceneData PUBLIC Axle::Core)
