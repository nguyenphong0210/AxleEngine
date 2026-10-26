add_library(AxleVFX STATIC
    src/ParticlePool.cpp
    src/ParticleEmitter.cpp
    src/ParticleSystem.cpp
    src/ParticleRenderer.cpp
    src/RHIParticleRenderer.cpp
    src/CurlNoise.cpp
    src/ParticleCollision.cpp
    src/ParticleForceField.cpp
)
add_library(Axle::VFX ALIAS AxleVFX)

target_include_directories(AxleVFX PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
)

target_link_libraries(AxleVFX
    PUBLIC
        Axle::Core
        Axle::Renderer
        Axle::RHI
    PRIVATE
        glad
)
