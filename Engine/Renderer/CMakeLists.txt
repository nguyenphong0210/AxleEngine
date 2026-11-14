foreach(stage VERTEX FRAGMENT)
    if(stage STREQUAL "VERTEX")
        set(ext vert)
    else()
        set(ext frag)
    endif()
    set(source "${CMAKE_SOURCE_DIR}/Engine/Resources/Shaders/Checkerboard.${ext}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" AXLE_SCENE_${stage})
    # MSVC caps one string token; concatenate canonical source one line per token.
    string(REPLACE "\n" "\n)AXLESCENE\"\nR\"AXLESCENE(" AXLE_SCENE_${stage} "${AXLE_SCENE_${stage}}")
endforeach()
configure_file(src/SceneShaderSource.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/SceneShaderSource.hpp" @ONLY)

foreach(stage HLSL VERTEX FRAGMENT)
    if(stage STREQUAL "HLSL")
        set(ext hlsl)
    elseif(stage STREQUAL "VERTEX")
        set(ext vert)
    else()
        set(ext frag)
    endif()
    set(source "${CMAKE_SOURCE_DIR}/Engine/Resources/Shaders/Canvas2D.${ext}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" AXLE_CANVAS_${stage})
endforeach()
configure_file(src/CanvasShaderSource.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/CanvasShaderSource.hpp" @ONLY)

foreach(part COMMON TEMPLATE)
    if(part STREQUAL "COMMON")
        set(source "${CMAKE_SOURCE_DIR}/Engine/Resources/Shaders/PBRCommon.hlsli")
    else()
        set(source "${CMAKE_SOURCE_DIR}/Engine/Resources/Shaders/MaterialSurface.hlsl")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" AXLE_MATERIAL_${part})
    string(REPLACE "\n" "\n)AXLEMATERIAL\"\nR\"AXLEMATERIAL(" AXLE_MATERIAL_${part} "${AXLE_MATERIAL_${part}}")
endforeach()
configure_file(src/MaterialSurfaceSource.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/MaterialSurfaceSource.hpp" @ONLY)

add_library(AxleRenderer STATIC
    src/RenderCommand.cpp
    src/Shader.cpp
    src/BuiltinSceneShader.cpp
    src/MaterialSurfaceShader.cpp
    src/Buffer.cpp
    src/VertexArray.cpp
    src/PerspectiveCamera.cpp
    src/PerspectiveCameraController.cpp
    src/PrimitiveMesh.cpp
    src/Framebuffer.cpp
    src/ShadowMap.cpp
    src/CascadedShadowMap.cpp
    src/LocalShadowMap.cpp
    src/EnvironmentBake.cpp
    src/Texture2D.cpp
    src/ImageTextureCache.cpp
    src/Skybox.cpp
    src/Material.cpp
    src/ShaderPropertyReflection.cpp
    src/ShaderPropertyPersistence.cpp
    src/Cubemap.cpp
    src/SkinningPipeline.cpp
    src/MaterialBindingPlan.cpp
    src/CustomShaderInterface.cpp
    src/ShaderDragDropDecoder.cpp
    src/RHIECSStreamer.cpp
    src/MaterialCache.cpp
    src/RenderCapabilities.cpp
    src/RenderContracts.cpp
    src/CanvasRenderer.cpp
    src/RenderGraph.cpp
    src/RenderTransientGraph.cpp
    src/RenderTransientFrame.cpp
    src/RenderPass.cpp
    src/ResolvedSurface.cpp
    src/ViewContext.cpp
)
add_library(Axle::Renderer ALIAS AxleRenderer)

target_include_directories(AxleRenderer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/stb>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/entt/src>
)

target_link_libraries(AxleRenderer
    PUBLIC
        Axle::Core
        Axle::Platform
        Axle::RHI
    PRIVATE
        glad
)

target_include_directories(AxleRenderer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
