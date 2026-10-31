# ==============================================================================
# AxleUI2D Static Library (Decoupled In-Game UI Subsystem)
# ==============================================================================
add_library(AxleUI2D STATIC
    src/UI2D/UITypes.cpp
    src/UI2D/RectTransform.cpp
    src/UI2D/Canvas2D.cpp
    src/UI2D/UIFont.cpp
    src/UI2D/UINode.cpp
    src/UI2D/UIDocument.cpp
    src/UI2D/UIParser.cpp
)
add_library(Axle::UI2D ALIAS AxleUI2D)

target_include_directories(AxleUI2D PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
)

target_link_libraries(AxleUI2D
    PUBLIC
        Axle::Core
        Axle::Platform
        Axle::Renderer
        Axle::RHI
)

# ==============================================================================
# AxleUI Static Library (Editor Tooling UI via Dear ImGui)
# ==============================================================================
add_library(AxleUI STATIC
    src/ImGuiLayer.cpp
    src/DebugPanel.cpp
    src/ConsolePanel.cpp
    src/SceneHierarchyPanel.cpp
    src/EntityInspector.cpp
    src/AssetInspector.cpp
    src/ContentBrowserPanel.cpp
    src/AssetThumbnailManager.cpp
    src/AssetPreviewPanel.cpp
    src/AssemblyEditorPanel.cpp
    src/VFXEditorPanel.cpp
    src/CurveEditorWidget.cpp
    src/RHIViewportDescriptor.cpp
    src/RectTransformGizmo.cpp
    src/UIPalettePanel.cpp
    src/UICanvasImGuiRenderer.cpp
    src/UICanvasDesignerPanel.cpp
    ${CMAKE_SOURCE_DIR}/ThirdParty/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/ThirdParty/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/ThirdParty/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/ThirdParty/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/ThirdParty/imgui/imgui_demo.cpp
    ${CMAKE_SOURCE_DIR}/ThirdParty/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_SOURCE_DIR}/ThirdParty/imgui/backends/imgui_impl_opengl3.cpp
    ${CMAKE_SOURCE_DIR}/ThirdParty/ImGuizmo/src/ImGuizmo.cpp
)
add_library(Axle::UI ALIAS AxleUI)

target_include_directories(AxleUI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/imgui>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/imgui/backends>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/ImGuizmo/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/ThirdParty/glm>
)

target_link_libraries(AxleUI
    PUBLIC
        Axle::UI2D
        Axle::Core
        Axle::Platform
        Axle::Renderer
        Axle::Asset
        Axle::Physics
        Axle::Scene
        Axle::Vehicle
        Axle::Audio
        Axle::VFX
        Axle::RHI
    PRIVATE
        glfw
        glad
)

if (WIN32)
    target_sources(AxleUI PRIVATE
        ${CMAKE_SOURCE_DIR}/ThirdParty/imgui/backends/imgui_impl_dx12.cpp
    )
    target_link_libraries(AxleUI PRIVATE
        d3d12
        dxgi
        d3dcompiler
    )
    target_compile_definitions(AxleUI PUBLIC AXLE_HAS_D3D12=1)
endif()

find_package(Vulkan QUIET)
if (Vulkan_FOUND)
    target_sources(AxleUI PRIVATE
        ${CMAKE_SOURCE_DIR}/ThirdParty/imgui/backends/imgui_impl_vulkan.cpp
    )
    target_link_libraries(AxleUI PRIVATE Vulkan::Vulkan)
    target_compile_definitions(AxleUI PUBLIC AXLE_HAS_VULKAN=1)
elseif (vulkan_headers_SOURCE_DIR OR EXISTS "${CMAKE_BINARY_DIR}/_deps/vulkan_headers-src/include")
    if (vulkan_headers_SOURCE_DIR)
        set(VK_HEADERS_DIR "${vulkan_headers_SOURCE_DIR}/include")
    else()
        set(VK_HEADERS_DIR "${CMAKE_BINARY_DIR}/_deps/vulkan_headers-src/include")
    endif()
    target_include_directories(AxleUI PUBLIC ${VK_HEADERS_DIR})
    target_sources(AxleUI PRIVATE
        ${CMAKE_SOURCE_DIR}/ThirdParty/imgui/backends/imgui_impl_vulkan.cpp
    )
    target_compile_definitions(AxleUI PUBLIC 
        AXLE_HAS_VULKAN=1 
        IMGUI_IMPL_VULKAN_NO_PROTOTYPES=1
        VK_NO_PROTOTYPES=1
    )
    if (WIN32)
        target_compile_definitions(AxleUI PUBLIC VK_USE_PLATFORM_WIN32_KHR=1)
    endif()
endif()
