add_library(AxleAudio STATIC
    src/miniaudio_impl.cpp
    src/AudioEngine.cpp
    src/AudioClip.cpp
    src/AudioSource.cpp
    src/AudioListener.cpp
)
add_library(Axle::Audio ALIAS AxleAudio)

target_include_directories(AxleAudio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/miniaudio>
)

target_link_libraries(AxleAudio
    PUBLIC
        Axle::Core
)
