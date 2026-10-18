add_executable(AxleRuntime
    src/Main.cpp
    src/RuntimeLayer.cpp
)

target_include_directories(AxleRuntime PRIVATE
    src
)

target_link_libraries(AxleRuntime PRIVATE
    Axle::Core
    Axle::Platform
    Axle::Renderer
    Axle::Audio
    Axle::Asset
    Axle::Physics
    Axle::Vehicle
    Axle::Scene
    Axle::Project
    Axle::UI
    Axle::Spline
    Axle::Terrain
)

add_dependencies(AxleRuntime AxleScripts)

# Synchronize assets, resources, and scripts DLL to runtime binary output directory
add_custom_command(TARGET AxleRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:AxleScripts>
    $<TARGET_FILE_DIR:AxleRuntime>/$<TARGET_FILE_NAME:AxleScripts>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Editor/assets
    $<TARGET_FILE_DIR:AxleRuntime>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Engine/Resources
    $<TARGET_FILE_DIR:AxleRuntime>/Resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:AxleRuntime>
    $<TARGET_FILE_DIR:AxleEditor>/$<TARGET_FILE_NAME:AxleRuntime>
)
