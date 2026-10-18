add_library(AxleAnimation STATIC
    src/Skeleton.cpp
    src/AnimationClip.cpp
    src/AnimationPose.cpp
    src/BlendSpace1D.cpp
    src/AnimationStateMachine.cpp
    src/Animator.cpp
    src/SkeletalMesh.cpp
    src/SkeletalMeshImporter.cpp
)
add_library(Axle::Animation ALIAS AxleAnimation)

target_include_directories(AxleAnimation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/cgltf>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/ufbx>
)

target_link_libraries(AxleAnimation
    PUBLIC
        Axle::Core
        Axle::Renderer
        Axle::Asset
)
