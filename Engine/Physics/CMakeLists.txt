add_library(AxlePhysics STATIC
    src/PhysicsEngine.cpp
    src/PhysicsScene.cpp
    src/PhysicsDebugRenderer.cpp
    src/RHIPhysicsDebugRenderer.cpp
    src/JobSystemEnkiTS.cpp
    src/CharacterController.cpp
)
add_library(Axle::Physics ALIAS AxlePhysics)

target_include_directories(AxlePhysics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(AxlePhysics
    PUBLIC
        Axle::Core
        Axle::Renderer
        Axle::RHI
        Axle::Asset
        Jolt
)
