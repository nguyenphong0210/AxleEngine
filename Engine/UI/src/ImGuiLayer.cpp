#include <Axle/ImGuiLayer.hpp>
#include <Axle/Log.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

namespace Axle {

    static void UpdateWindowSnapping() {
        ImGuiContext* g = ImGui::GetCurrentContext();
        if (!g || !g->MovingWindow) return;

        ImGuiWindow* movingWindow = g->MovingWindow->RootWindowDockTree ? g->MovingWindow->RootWindowDockTree : g->MovingWindow;
        if (!movingWindow || (movingWindow->Flags & ImGuiWindowFlags_NoMove) || (movingWindow->Flags & ImGuiWindowFlags_NoResize)) {
            return;
        }

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        if (!vp) return;

        const float vpX = vp->Pos.x;
        const float vpY = vp->Pos.y;
        const float vpW = vp->Size.x;
        const float vpH = vp->Size.y;

        const ImVec2 mouse = ImGui::GetIO().MousePos;

        // Snapping detection zones (Windows 11 style)
        const float cornerThreshold = 50.0f;
        const float edgeThreshold = 18.0f;

        enum class SnapZone {
            None,
            TopLeft,
            TopRight,
            BottomLeft,
            BottomRight,
            LeftHalf,
            RightHalf,
            TopMaximize
        };

        SnapZone zone = SnapZone::None;

        const bool isNearLeft = (mouse.x <= vpX + edgeThreshold);
        const bool isNearRight = (mouse.x >= vpX + vpW - edgeThreshold);
        const bool isNearTop = (mouse.y <= vpY + edgeThreshold);

        if (mouse.x <= vpX + cornerThreshold && mouse.y <= vpY + cornerThreshold) {
            zone = SnapZone::TopLeft;
        } else if (mouse.x >= vpX + vpW - cornerThreshold && mouse.y <= vpY + cornerThreshold) {
            zone = SnapZone::TopRight;
        } else if (mouse.x <= vpX + cornerThreshold && mouse.y >= vpY + vpH - cornerThreshold) {
            zone = SnapZone::BottomLeft;
        } else if (mouse.x >= vpX + vpW - cornerThreshold && mouse.y >= vpY + vpH - cornerThreshold) {
            zone = SnapZone::BottomRight;
        } else if (isNearLeft) {
            zone = SnapZone::LeftHalf;
        } else if (isNearRight) {
            zone = SnapZone::RightHalf;
        } else if (isNearTop) {
            zone = SnapZone::TopMaximize;
        }

        if (zone == SnapZone::None) return;

        // Compute snap tile bounds with padding
        const float pad = 8.0f;
        ImVec2 snapPos, snapSize;

        switch (zone) {
            case SnapZone::TopLeft:
                snapPos = ImVec2(vpX + pad, vpY + pad);
                snapSize = ImVec2((vpW - pad * 3.0f) * 0.5f, (vpH - pad * 3.0f) * 0.5f);
                break;
            case SnapZone::TopRight:
                snapPos = ImVec2(vpX + (vpW + pad) * 0.5f, vpY + pad);
                snapSize = ImVec2((vpW - pad * 3.0f) * 0.5f, (vpH - pad * 3.0f) * 0.5f);
                break;
            case SnapZone::BottomLeft:
                snapPos = ImVec2(vpX + pad, vpY + (vpH + pad) * 0.5f);
                snapSize = ImVec2((vpW - pad * 3.0f) * 0.5f, (vpH - pad * 3.0f) * 0.5f);
                break;
            case SnapZone::BottomRight:
                snapPos = ImVec2(vpX + (vpW + pad) * 0.5f, vpY + (vpH + pad) * 0.5f);
                snapSize = ImVec2((vpW - pad * 3.0f) * 0.5f, (vpH - pad * 3.0f) * 0.5f);
                break;
            case SnapZone::LeftHalf:
                snapPos = ImVec2(vpX + pad, vpY + pad);
                snapSize = ImVec2((vpW - pad * 3.0f) * 0.5f, vpH - pad * 2.0f);
                break;
            case SnapZone::RightHalf:
                snapPos = ImVec2(vpX + (vpW + pad) * 0.5f, vpY + pad);
                snapSize = ImVec2((vpW - pad * 3.0f) * 0.5f, vpH - pad * 2.0f);
                break;
            case SnapZone::TopMaximize:
                snapPos = ImVec2(vpX + pad, vpY + pad);
                snapSize = ImVec2(vpW - pad * 2.0f, vpH - pad * 2.0f);
                break;
            default:
                return;
        }

        // Draw Windows 11 style blue translucent snapping highlight overlay
        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
        const ImVec2 maxPos = ImVec2(snapPos.x + snapSize.x, snapPos.y + snapSize.y);
        fgDrawList->AddRectFilled(snapPos, maxPos, IM_COL32(30, 144, 255, 55), 8.0f);
        fgDrawList->AddRect(snapPos, maxPos, IM_COL32(80, 185, 255, 220), 8.0f, 0, 2.5f);

        // Snap window position and dimensions upon releasing the mouse
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (movingWindow->DockNode) {
                ImGui::DockContextQueueUndockWindow(g, movingWindow);
            }
            ImGui::SetWindowPos(movingWindow, snapPos, ImGuiCond_Always);
            ImGui::SetWindowSize(movingWindow, snapSize, ImGuiCond_Always);
        }
    }

    ImGuiLayer::~ImGuiLayer() {
        if (m_Initialized) {
            Shutdown();
        }
    }

    void ImGuiLayer::Init(GLFWwindow* window) {
        if (m_Initialized) return;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // Do NOT enable ImGuiConfigFlags_ViewportsEnable so all ImGui windows
        // remain strictly inside the application window boundaries.

        SetDarkThemeColors();

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 450");

        m_Initialized = true;
        AXLE_CORE_INFO("ImGui Layer initialized successfully (Docking enabled, Viewports restricted to main window)");
    }

    void ImGuiLayer::Shutdown() {
        if (!m_Initialized) return;

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        m_Initialized = false;
        AXLE_CORE_INFO("ImGui Layer shut down");
    }

    void ImGuiLayer::Begin() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiLayer::End() {
        // Evaluate Windows 11 style edge and corner snapping
        UpdateWindowSnapping();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void ImGuiLayer::SetDarkThemeColors() {
        ImGui::StyleColorsDark();

        auto& colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg] = ImVec4{ 0.10f, 0.11f, 0.13f, 0.90f };

        // Headers
        colors[ImGuiCol_Header] = ImVec4{ 0.20f, 0.22f, 0.25f, 1.0f };
        colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.30f, 0.33f, 0.38f, 1.0f };
        colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.16f, 0.18f, 1.0f };

        // Buttons
        colors[ImGuiCol_Button] = ImVec4{ 0.20f, 0.22f, 0.26f, 1.0f };
        colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.32f, 0.36f, 0.42f, 1.0f };
        colors[ImGuiCol_ButtonActive] = ImVec4{ 0.16f, 0.18f, 0.20f, 1.0f };

        // Frame BG
        colors[ImGuiCol_FrameBg] = ImVec4{ 0.15f, 0.16f, 0.18f, 1.0f };
        colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.22f, 0.24f, 0.28f, 1.0f };
        colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.18f, 0.20f, 0.22f, 1.0f };

        // Tabs
        colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.16f, 0.18f, 1.0f };
        colors[ImGuiCol_TabHovered] = ImVec4{ 0.35f, 0.38f, 0.45f, 1.0f };
        colors[ImGuiCol_TabActive] = ImVec4{ 0.24f, 0.27f, 0.32f, 1.0f };
        colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.12f, 0.13f, 0.15f, 1.0f };
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.18f, 0.20f, 0.23f, 1.0f };

        // Title
        colors[ImGuiCol_TitleBg] = ImVec4{ 0.12f, 0.13f, 0.15f, 1.0f };
        colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.16f, 0.18f, 0.22f, 1.0f };
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.12f, 0.13f, 0.15f, 1.0f };
    }

}
