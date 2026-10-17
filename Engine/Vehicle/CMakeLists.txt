add_library(AxleVehicle STATIC
    src/Vehicle.cpp
    src/VehicleAudio.cpp
)
add_library(Axle::Vehicle ALIAS AxleVehicle)

target_include_directories(AxleVehicle PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
)

target_link_libraries(AxleVehicle
    PUBLIC
        Axle::Core
        Axle::Physics
        Axle::Renderer
        Axle::Audio
        Jolt
)
