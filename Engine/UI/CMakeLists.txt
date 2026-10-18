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
        Axle::Core
        Axle::Platform
        Axle::Renderer
        Axle::Asset
        Axle::Physics
        Axle::Scene
        Axle::Vehicle
        Axle::Audio
    PRIVATE
        glfw
        glad
)
