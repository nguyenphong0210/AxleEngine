# Graphics consumers of Physics; the simulation target has no dependency on this adapter.
add_library(AxlePhysicsDebug STATIC
    src/PhysicsDebugRenderer.cpp
    src/RHIPhysicsDebugRenderer.cpp
)
add_library(Axle::PhysicsDebug ALIAS AxlePhysicsDebug)
target_include_directories(AxlePhysicsDebug PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(AxlePhysicsDebug PUBLIC
    Axle::Physics
    Axle::Renderer
    Axle::RHI
)
