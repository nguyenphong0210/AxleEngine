add_executable(AxleEditor
    src/Main.cpp
    src/EditorLayer.cpp
)

target_include_directories(AxleEditor PRIVATE
    src
)

target_link_libraries(AxleEditor PRIVATE
    Axle::Core
    Axle::Platform
    Axle::Renderer
    Axle::Asset
    Axle::UI
    Axle::Physics
    Axle::Scene
    Axle::Vehicle
    Axle::Project
)

# Synchronize Editor/assets to binary output directory on post-build
add_custom_command(TARGET AxleEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:AxleEditor>/assets
)

# Dedicated Test Executable for Automated Verifications
add_executable(AxleTests
    src/Test.cpp
)

target_include_directories(AxleTests PRIVATE
    src
)

target_link_libraries(AxleTests PRIVATE
    Axle::Core
    Axle::Platform
    Axle::Renderer
    Axle::Asset
    Axle::UI
    Axle::Physics
    Axle::Scene
    Axle::Vehicle
    Axle::Project
)