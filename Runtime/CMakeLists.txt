add_library(AxleRuntimePlayer STATIC
    src/RuntimeLayer.cpp
    src/RuntimeStartup.cpp
    src/RuntimeRenderContext.cpp
)
add_library(Axle::RuntimePlayer ALIAS AxleRuntimePlayer)

add_executable(AxleRuntime EXCLUDE_FROM_ALL src/Main.cpp)
target_link_libraries(AxleRuntime PRIVATE Axle::RuntimePlayer)

target_include_directories(AxleRuntimePlayer PUBLIC
    src
)

target_link_libraries(AxleRuntimePlayer PUBLIC
    Axle::Core
    Axle::RHI
    Axle::Platform
    Axle::Renderer
    Axle::Audio
    Axle::Asset
    Axle::Physics
    Axle::Scene
    Axle::Project
    Axle::UI2D
    Axle::Animation
    Axle::VFX
)

add_library(AxleRuntimeScripts SHARED EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/Editor/src/Scripts/ScriptsModule.cpp")
set_target_properties(AxleRuntimeScripts PROPERTIES OUTPUT_NAME AxleScripts
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/AutoScripts.inc"
    CONTENT "// Empty shipping baseline; project scripts are compiled by their declared build.\n")
target_include_directories(AxleRuntimeScripts PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_SOURCE_DIR}/Engine/Scene/include")
target_compile_definitions(AxleRuntimeScripts PRIVATE AXLE_SCRIPT_BUILD_DLL AXLE_SHIPPING)
target_link_libraries(AxleRuntimeScripts PRIVATE Axle::SceneData Axle::Platform)
add_dependencies(AxleRuntime AxleRuntimeScripts)

# Stage engine resources and scripts only; project assets are supplied by export
add_custom_command(TARGET AxleRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:AxleRuntimeScripts>
    $<TARGET_FILE_DIR:AxleRuntime>/$<TARGET_FILE_NAME:AxleRuntimeScripts>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Engine/Resources
    $<TARGET_FILE_DIR:AxleRuntime>/Resources
)
