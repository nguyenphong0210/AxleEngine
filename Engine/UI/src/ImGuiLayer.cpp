#include <Axle/ImGuiLayer.hpp>
#include <Axle/Log.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#if defined(AXLE_HAS_D3D12)
#if defined(APIENTRY)
#undef APIENTRY
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui_impl_dx12.h>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#endif

#if defined(AXLE_HAS_VULKAN)
#include <imgui_impl_vulkan.h>
#endif
#include <filesystem>

namespace Axle {

    ::ImFont* ImGuiLayer::s_RegularFont = nullptr;
    ::ImFont* ImGuiLayer::s_BoldFont    = nullptr;
    ::ImFont* ImGuiLayer::s_HeaderFont  = nullptr;
    ::ImFont* ImGuiLayer::s_CodeFont    = nullptr;

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
        const bool isNearBottom = (mouse.y >= vpY + vpH - edgeThreshold);

        if (isNearLeft && isNearTop) {
            zone = SnapZone::TopLeft;
        } else if (isNearRight && isNearTop) {
            zone = SnapZone::TopRight;
        } else if (isNearLeft && isNearBottom) {
            zone = SnapZone::BottomLeft;
        } else if (isNearRight && isNearBottom) {
            zone = SnapZone::BottomRight;
        } else if (isNearTop) {
            zone = SnapZone::TopMaximize;
        } else if (isNearLeft) {
            zone = SnapZone::LeftHalf;
        } else if (isNearRight) {
            zone = SnapZone::RightHalf;
        }

        if (zone == SnapZone::None) {
            return;
        }

        ImVec2 targetPos, targetSize;
        const float halfW = vpW * 0.5f;
        const float halfH = vpH * 0.5f;

        switch (zone) {
            case SnapZone::TopLeft:
                targetPos = ImVec2(vpX, vpY);
                targetSize = ImVec2(halfW, halfH);
                break;
            case SnapZone::TopRight:
                targetPos = ImVec2(vpX + halfW, vpY);
                targetSize = ImVec2(halfW, halfH);
                break;
            case SnapZone::BottomLeft:
                targetPos = ImVec2(vpX, vpY + halfH);
                targetSize = ImVec2(halfW, halfH);
                break;
            case SnapZone::BottomRight:
                targetPos = ImVec2(vpX + halfW, vpY + halfH);
                targetSize = ImVec2(halfW, halfH);
                break;
            case SnapZone::LeftHalf:
                targetPos = ImVec2(vpX, vpY);
                targetSize = ImVec2(halfW, vpH);
                break;
            case SnapZone::RightHalf:
                targetPos = ImVec2(vpX + halfW, vpY);
                targetSize = ImVec2(halfW, vpH);
                break;
            case SnapZone::TopMaximize:
                targetPos = ImVec2(vpX, vpY);
                targetSize = ImVec2(vpW, vpH);
                break;
            default:
                return;
        }

        // Draw translucent ghost outline of snap target
        ImDrawList* fgDraw = ImGui::GetForegroundDrawList();
        if (fgDraw) {
            fgDraw->AddRectFilled(targetPos, ImVec2(targetPos.x + targetSize.x, targetPos.y + targetSize.y),
                IM_COL32(0, 120, 215, 45), 6.0f);
            fgDraw->AddRect(targetPos, ImVec2(targetPos.x + targetSize.x, targetPos.y + targetSize.y),
                IM_COL32(0, 120, 215, 180), 6.0f, 0, 2.0f);
        }

        // When mouse button released, commit snapping
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            ImGui::SetWindowPos(movingWindow, targetPos, ImGuiCond_Always);
            ImGui::SetWindowSize(movingWindow, targetSize, ImGuiCond_Always);
        }
    }

    ImGuiLayer::ImGuiLayer(ImGuiBackendType backend)
        : m_BackendType(backend) {
    }

    ImGuiLayer::~ImGuiLayer() {
        if (m_Initialized) {
            Shutdown();
        }
    }

    void ImGuiLayer::Init(GLFWwindow* window) {
        if (m_Initialized) return;

        m_Window = window;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        // --- Crisp Typography: High-DPI Vector Fonts (Unity Style Segoe UI) ---
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 3;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = false;

        const char* regularFontPath = "C:/Windows/Fonts/segoeui.ttf";
        const char* boldFontPath    = "C:/Windows/Fonts/segoeuib.ttf";
        const char* codeFontPath    = "C:/Windows/Fonts/consola.ttf";

        if (std::filesystem::exists(regularFontPath)) {
            s_RegularFont = io.Fonts->AddFontFromFileTTF(regularFontPath, 15.5f, &fontConfig);
            io.FontDefault = s_RegularFont;
        } else {
            s_RegularFont = io.Fonts->AddFontDefault();
        }

        if (std::filesystem::exists(boldFontPath)) {
            s_BoldFont = io.Fonts->AddFontFromFileTTF(boldFontPath, 15.5f, &fontConfig);
            s_HeaderFont = io.Fonts->AddFontFromFileTTF(boldFontPath, 17.5f, &fontConfig);
        } else {
            s_BoldFont = s_RegularFont;
            s_HeaderFont = s_RegularFont;
        }

        if (std::filesystem::exists(codeFontPath)) {
            s_CodeFont = io.Fonts->AddFontFromFileTTF(codeFontPath, 13.5f, &fontConfig);
        } else {
            s_CodeFont = s_RegularFont;
        }

        SetDarkThemeColors();

        // --- Unity Editor Ergonomic Metrics ---
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding    = 0.0f; // Flat docked panels like Unity
        style.ChildRounding     = 3.0f;
        style.FrameRounding     = 3.5f; // Unity input/button rounded edges
        style.PopupRounding     = 4.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding      = 3.0f;
        style.TabRounding       = 3.0f;

        style.WindowPadding     = ImVec2(8.0f, 8.0f);
        style.FramePadding      = ImVec2(7.0f, 4.0f);
        style.CellPadding       = ImVec2(4.0f, 3.0f);
        style.ItemSpacing       = ImVec2(6.0f, 4.0f);
        style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
        style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
        style.IndentSpacing     = 14.0f;
        style.ScrollbarSize     = 13.0f;
        style.GrabMinSize       = 10.0f;

        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;
        style.FrameBorderSize   = 1.0f;
        style.TabBorderSize     = 1.0f;

        switch (m_BackendType) {
            case ImGuiBackendType::OpenGL:
                InitOpenGL();
                break;
            case ImGuiBackendType::Direct3D12:
                InitD3D12();
                break;
            case ImGuiBackendType::Vulkan:
                InitVulkan();
                break;
        }

        m_Initialized = true;
    }

    void ImGuiLayer::InitOpenGL() {
        if (m_Window) {
            int clientApi = glfwGetWindowAttrib(m_Window, GLFW_CLIENT_API);
            if (clientApi != GLFW_OPENGL_API) {
                AXLE_CORE_ERROR("ImGuiLayer::InitOpenGL: Cannot initialize OpenGL backend because window has no OpenGL context (GLFW_CLIENT_API is {0})!", clientApi);
                return;
            }
            ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
        }
        ImGui_ImplOpenGL3_Init("#version 450");
        AXLE_CORE_INFO("ImGui Layer initialized successfully for OpenGL (Docking enabled)");
    }

    void ImGuiLayer::InitD3D12() {
#if defined(AXLE_HAS_D3D12)
        if (m_Window) {
            ImGui_ImplGlfw_InitForOther(m_Window, true);
        }

        auto& rhiDevice = RHI::RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) {
            AXLE_CORE_ERROR("ImGuiLayer::InitD3D12: RHIDevice is not initialized! Falling back to OpenGL.");
            InitOpenGL();
            m_BackendType = ImGuiBackendType::OpenGL;
            return;
        }

        const auto& core = rhiDevice.GetNRICore();
        auto* nriDev = rhiDevice.GetNRIDevice();
        auto* d3d12Device = static_cast<ID3D12Device*>(core.GetDeviceNativeObject(nriDev));
        auto* d3d12Queue = static_cast<ID3D12CommandQueue*>(core.GetQueueNativeObject(rhiDevice.GetGraphicsQueue()));

        if (!d3d12Device || !d3d12Queue) {
            AXLE_CORE_ERROR("ImGuiLayer::InitD3D12: Failed to retrieve native D3D12 device or queue!");
            InitOpenGL();
            m_BackendType = ImGuiBackendType::OpenGL;
            return;
        }

        // Create SRV Descriptor Heap for ImGui textures & viewports
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 1024;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        heapDesc.NodeMask = 0;

        HRESULT hr = d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_D3D12SrvHeap));
        if (FAILED(hr) || !m_D3D12SrvHeap) {
            AXLE_CORE_ERROR("ImGuiLayer::InitD3D12: Failed to create SRV Descriptor Heap! HRESULT=0x{0:x}", static_cast<uint32_t>(hr));
            return;
        }

        m_D3D12DescriptorSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_AllocatedDescriptorCount = 0;
        m_FreeDescriptorIndices.clear();

        ImGui_ImplDX12_InitInfo initInfo = {};
        initInfo.Device = d3d12Device;
        initInfo.CommandQueue = d3d12Queue;
        initInfo.NumFramesInFlight = 3;
        initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
        initInfo.SrvDescriptorHeap = m_D3D12SrvHeap;
        initInfo.UserData = this;
        initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
            auto* self = static_cast<ImGuiLayer*>(info->UserData);
            self->AllocateD3D12Descriptor(outCpu, outGpu);
        };
        initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
            auto* self = static_cast<ImGuiLayer*>(info->UserData);
            self->FreeD3D12Descriptor(&cpu, &gpu);
        };

        if (!ImGui_ImplDX12_Init(&initInfo)) {
            AXLE_CORE_ERROR("ImGuiLayer::InitD3D12: ImGui_ImplDX12_Init failed!");
            m_D3D12SrvHeap->Release();
            m_D3D12SrvHeap = nullptr;
            return;
        }

        // Create native D3D12 SwapChain for the GLFW window if m_Window is valid
        if (m_Window) {
            HWND hwnd = glfwGetWin32Window(m_Window);
            if (hwnd) {
                int w = 0, h = 0;
                glfwGetFramebufferSize(m_Window, &w, &h);
                m_D3D12Width = static_cast<uint32_t>(w > 0 ? w : 1280);
                m_D3D12Height = static_cast<uint32_t>(h > 0 ? h : 720);

                IDXGIFactory4* dxgiFactory = nullptr;
                HRESULT scHr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
                if (SUCCEEDED(scHr) && dxgiFactory) {
                    DXGI_SWAP_CHAIN_DESC1 sd = {};
                    sd.BufferCount = 3;
                    sd.Width = m_D3D12Width;
                    sd.Height = m_D3D12Height;
                    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    sd.Flags = 0;
                    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                    sd.SampleDesc.Count = 1;
                    sd.SampleDesc.Quality = 0;
                    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                    sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
                    sd.Scaling = DXGI_SCALING_STRETCH;
                    sd.Stereo = FALSE;

                    IDXGISwapChain1* swapChain1 = nullptr;
                    scHr = dxgiFactory->CreateSwapChainForHwnd(d3d12Queue, hwnd, &sd, nullptr, nullptr, &swapChain1);
                    if (SUCCEEDED(scHr) && swapChain1) {
                        swapChain1->QueryInterface(IID_PPV_ARGS(&m_D3D12SwapChain));
                        swapChain1->Release();
                    }
                    dxgiFactory->Release();
                }

                if (m_D3D12SwapChain) {
                    // Create RTV Descriptor Heap
                    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
                    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                    rtvHeapDesc.NumDescriptors = 3;
                    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                    d3d12Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_D3D12RtvHeap));
                    m_D3D12RtvDescriptorSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

                    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_D3D12RtvHeap->GetCPUDescriptorHandleForHeapStart();
                    for (UINT i = 0; i < 3; ++i) {
                        m_D3D12SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_D3D12RenderTargets[i]));
                        d3d12Device->CreateRenderTargetView(m_D3D12RenderTargets[i], nullptr, rtvHandle);
                        rtvHandle.ptr += m_D3D12RtvDescriptorSize;
                    }

                    // Create Command Allocators & List
                    for (UINT i = 0; i < 3; ++i) {
                        d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_D3D12CommandAllocators[i]));
                    }
                    d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_D3D12CommandAllocators[0], nullptr, IID_PPV_ARGS(&m_D3D12CommandList));
                    m_D3D12CommandList->Close();

                    // Create Frame Synchronization Fence
                    d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_D3D12Fence));
                    m_D3D12FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                }
            }
        }

        AXLE_CORE_INFO("ImGui Layer initialized successfully for Direct3D 12 (SRV Heap: 1024 descriptors{0})",
                       m_D3D12SwapChain ? ", Window SwapChain: Active" : "");
#else
        AXLE_CORE_WARN("D3D12 ImGui backend requested but not supported on this platform; falling back to OpenGL!");
        InitOpenGL();
        m_BackendType = ImGuiBackendType::OpenGL;
#endif
    }

    void ImGuiLayer::InitVulkan() {
#if defined(AXLE_HAS_VULKAN)
        if (m_Window) {
            ImGui_ImplGlfw_InitForVulkan(m_Window, true);
        }
        AXLE_CORE_INFO("ImGui Layer initialized successfully for Vulkan");
#else
        AXLE_CORE_WARN("Vulkan ImGui backend requested but not available without Vulkan SDK!");
#endif
    }

    void ImGuiLayer::Shutdown() {
        if (!m_Initialized) return;

        switch (m_BackendType) {
            case ImGuiBackendType::OpenGL:
                ShutdownOpenGL();
                break;
            case ImGuiBackendType::Direct3D12:
                ShutdownD3D12();
                break;
            case ImGuiBackendType::Vulkan:
                ShutdownVulkan();
                break;
        }

        ImGui::DestroyContext();
        m_Initialized = false;
        m_Window = nullptr;
        AXLE_CORE_INFO("ImGui Layer shut down cleanly");
    }

    void ImGuiLayer::ShutdownOpenGL() {
        ImGui_ImplOpenGL3_Shutdown();
        if (m_Window) {
            ImGui_ImplGlfw_Shutdown();
        }
    }

    void ImGuiLayer::ShutdownD3D12() {
#if defined(AXLE_HAS_D3D12)
        auto& rhiDevice = RHI::RHIDevice::Get();
        if (rhiDevice.IsInitialized() && m_D3D12Fence && m_D3D12FenceEvent) {
            const auto& core = rhiDevice.GetNRICore();
            auto* d3d12Queue = static_cast<ID3D12CommandQueue*>(core.GetQueueNativeObject(rhiDevice.GetGraphicsQueue()));
            if (d3d12Queue) {
                m_D3D12FenceValueCounter++;
                d3d12Queue->Signal(m_D3D12Fence, m_D3D12FenceValueCounter);
                m_D3D12Fence->SetEventOnCompletion(m_D3D12FenceValueCounter, (HANDLE)m_D3D12FenceEvent);
                WaitForSingleObject((HANDLE)m_D3D12FenceEvent, INFINITE);
            }
        }

        ImGui_ImplDX12_Shutdown();
        if (m_Window) {
            ImGui_ImplGlfw_Shutdown();
        }

        if (m_D3D12FenceEvent) {
            CloseHandle((HANDLE)m_D3D12FenceEvent);
            m_D3D12FenceEvent = nullptr;
        }
        if (m_D3D12Fence) {
            m_D3D12Fence->Release();
            m_D3D12Fence = nullptr;
        }
        if (m_D3D12CommandList) {
            m_D3D12CommandList->Release();
            m_D3D12CommandList = nullptr;
        }
        for (UINT i = 0; i < 3; ++i) {
            if (m_D3D12CommandAllocators[i]) {
                m_D3D12CommandAllocators[i]->Release();
                m_D3D12CommandAllocators[i] = nullptr;
            }
            if (m_D3D12RenderTargets[i]) {
                m_D3D12RenderTargets[i]->Release();
                m_D3D12RenderTargets[i] = nullptr;
            }
        }
        if (m_D3D12RtvHeap) {
            m_D3D12RtvHeap->Release();
            m_D3D12RtvHeap = nullptr;
        }
        if (m_D3D12SwapChain) {
            m_D3D12SwapChain->Release();
            m_D3D12SwapChain = nullptr;
        }

        if (m_D3D12SrvHeap) {
            m_D3D12SrvHeap->Release();
            m_D3D12SrvHeap = nullptr;
        }
        m_AllocatedDescriptorCount = 0;
        m_FreeDescriptorIndices.clear();
#endif
    }

    void ImGuiLayer::ShutdownVulkan() {
#if defined(AXLE_HAS_VULKAN)
        ImGui_ImplVulkan_Shutdown();
        if (m_Window) {
            ImGui_ImplGlfw_Shutdown();
        }
#endif
    }

    void ImGuiLayer::Begin() {
        if (!m_Initialized) return;

        switch (m_BackendType) {
            case ImGuiBackendType::OpenGL:
                if (ImGui::GetIO().BackendRendererUserData != nullptr) {
                    ImGui_ImplOpenGL3_NewFrame();
                    if (m_Window) ImGui_ImplGlfw_NewFrame();
                    ImGui::NewFrame();
                }
                break;
            case ImGuiBackendType::Direct3D12:
#if defined(AXLE_HAS_D3D12)
                if (ImGui::GetIO().BackendRendererUserData != nullptr) {
                    ImGui_ImplDX12_NewFrame();
                    if (m_Window) ImGui_ImplGlfw_NewFrame();
                    ImGui::NewFrame();
                }
#endif
                break;
            case ImGuiBackendType::Vulkan:
#if defined(AXLE_HAS_VULKAN)
                if (ImGui::GetIO().BackendRendererUserData != nullptr) {
                    ImGui_ImplVulkan_NewFrame();
                    if (m_Window) ImGui_ImplGlfw_NewFrame();
                    ImGui::NewFrame();
                }
#endif
                break;
        }
    }

    void ImGuiLayer::End() {
        if (!m_Initialized || ImGui::GetIO().BackendRendererUserData == nullptr) return;

        UpdateWindowSnapping();
        ImGui::Render();

        if (m_BackendType == ImGuiBackendType::OpenGL) {
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
#if defined(AXLE_HAS_D3D12)
        else if (m_BackendType == ImGuiBackendType::Direct3D12 && m_D3D12SwapChain && m_D3D12CommandList) {
            auto& rhiDevice = RHI::RHIDevice::Get();
            if (!rhiDevice.IsInitialized()) return;
            const auto& core = rhiDevice.GetNRICore();
            auto* d3d12Device = static_cast<ID3D12Device*>(core.GetDeviceNativeObject(rhiDevice.GetNRIDevice()));
            auto* d3d12Queue = static_cast<ID3D12CommandQueue*>(core.GetQueueNativeObject(rhiDevice.GetGraphicsQueue()));
            if (!d3d12Device || !d3d12Queue) return;

            // Handle window resize for D3D12 swapchain
            if (m_Window) {
                int fbW = 0, fbH = 0;
                glfwGetFramebufferSize(m_Window, &fbW, &fbH);
                uint32_t curW = static_cast<uint32_t>(fbW > 0 ? fbW : 1280);
                uint32_t curH = static_cast<uint32_t>(fbH > 0 ? fbH : 720);

                if ((curW != m_D3D12Width || curH != m_D3D12Height) && curW > 0 && curH > 0) {
                    m_D3D12FenceValueCounter++;
                    d3d12Queue->Signal(m_D3D12Fence, m_D3D12FenceValueCounter);
                    m_D3D12Fence->SetEventOnCompletion(m_D3D12FenceValueCounter, (HANDLE)m_D3D12FenceEvent);
                    WaitForSingleObject((HANDLE)m_D3D12FenceEvent, INFINITE);

                    for (UINT i = 0; i < 3; ++i) {
                        if (m_D3D12RenderTargets[i]) {
                            m_D3D12RenderTargets[i]->Release();
                            m_D3D12RenderTargets[i] = nullptr;
                        }
                    }
                    m_D3D12Width = curW;
                    m_D3D12Height = curH;
                    m_D3D12SwapChain->ResizeBuffers(3, m_D3D12Width, m_D3D12Height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);

                    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_D3D12RtvHeap->GetCPUDescriptorHandleForHeapStart();
                    for (UINT i = 0; i < 3; ++i) {
                        m_D3D12SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_D3D12RenderTargets[i]));
                        d3d12Device->CreateRenderTargetView(m_D3D12RenderTargets[i], nullptr, rtvHandle);
                        rtvHandle.ptr += m_D3D12RtvDescriptorSize;
                    }
                }
            }

            UINT backBufferIdx = m_D3D12SwapChain->GetCurrentBackBufferIndex();

            // Wait for frame fence
            if (m_D3D12Fence->GetCompletedValue() < m_D3D12FenceValues[backBufferIdx]) {
                m_D3D12Fence->SetEventOnCompletion(m_D3D12FenceValues[backBufferIdx], (HANDLE)m_D3D12FenceEvent);
                WaitForSingleObject((HANDLE)m_D3D12FenceEvent, INFINITE);
            }

            m_D3D12CommandAllocators[backBufferIdx]->Reset();
            m_D3D12CommandList->Reset(m_D3D12CommandAllocators[backBufferIdx], nullptr);

            // Transition to RENDER_TARGET
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_D3D12RenderTargets[backBufferIdx];
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_D3D12CommandList->ResourceBarrier(1, &barrier);

            // Clear and bind RTV
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_D3D12RtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtvHandle.ptr += backBufferIdx * m_D3D12RtvDescriptorSize;
            const float clearColor[4] = { 0.10f, 0.11f, 0.13f, 1.0f };
            m_D3D12CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
            m_D3D12CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

            // Set descriptor heap and render ImGui
            ID3D12DescriptorHeap* heaps[] = { m_D3D12SrvHeap };
            m_D3D12CommandList->SetDescriptorHeaps(1, heaps);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_D3D12CommandList);

            // Transition to PRESENT
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            m_D3D12CommandList->ResourceBarrier(1, &barrier);

            m_D3D12CommandList->Close();

            ID3D12CommandList* ppCommandLists[] = { m_D3D12CommandList };
            d3d12Queue->ExecuteCommandLists(1, ppCommandLists);

            m_D3D12SwapChain->Present(1, 0);

            m_D3D12FenceValueCounter++;
            m_D3D12FenceValues[backBufferIdx] = m_D3D12FenceValueCounter;
            d3d12Queue->Signal(m_D3D12Fence, m_D3D12FenceValueCounter);
        }
#endif
    }

    void ImGuiLayer::Render(RHI::RHICommandBuffer* cmdBuffer) {
        if (!cmdBuffer || !m_Initialized) return;
#if defined(AXLE_HAS_D3D12)
        if (m_BackendType == ImGuiBackendType::Direct3D12) {
            const auto& core = RHI::RHIDevice::Get().GetNRICore();
            auto* nativeCmd = static_cast<ID3D12GraphicsCommandList*>(core.GetCommandBufferNativeObject(cmdBuffer->GetNative()));
            Render(nativeCmd);
        }
#endif
    }

    void ImGuiLayer::Render(ID3D12GraphicsCommandList* cmdList) {
        if (!cmdList || !m_Initialized) return;
#if defined(AXLE_HAS_D3D12)
        if (m_BackendType == ImGuiBackendType::Direct3D12 && m_D3D12SrvHeap) {
            ID3D12DescriptorHeap* heaps[] = { m_D3D12SrvHeap };
            cmdList->SetDescriptorHeaps(1, heaps);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
        }
#endif
    }

    uint64_t ImGuiLayer::RegisterTextureDescriptor(uint64_t cpuHandlePtr) {
#if defined(AXLE_HAS_D3D12)
        if (m_BackendType != ImGuiBackendType::Direct3D12 || !m_D3D12SrvHeap || cpuHandlePtr == 0) {
            return 0;
        }
        auto& rhiDevice = RHI::RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) return 0;

        const auto& core = rhiDevice.GetNRICore();
        auto* d3d12Device = static_cast<ID3D12Device*>(core.GetDeviceNativeObject(rhiDevice.GetNRIDevice()));
        if (!d3d12Device) return 0;

        D3D12_CPU_DESCRIPTOR_HANDLE outCpu = {};
        D3D12_GPU_DESCRIPTOR_HANDLE outGpu = {};
        AllocateD3D12Descriptor(&outCpu, &outGpu);

        D3D12_CPU_DESCRIPTOR_HANDLE srcCpu = {};
        srcCpu.ptr = cpuHandlePtr;

        d3d12Device->CopyDescriptorsSimple(1, outCpu, srcCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return outGpu.ptr;
#else
        (void)cpuHandlePtr;
        return 0;
#endif
    }

    void ImGuiLayer::UnregisterTextureDescriptor(uint64_t gpuHandlePtr) {
#if defined(AXLE_HAS_D3D12)
        if (m_BackendType != ImGuiBackendType::Direct3D12 || !m_D3D12SrvHeap || gpuHandlePtr == 0) {
            return;
        }
        D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = m_D3D12SrvHeap->GetGPUDescriptorHandleForHeapStart();
        if (gpuHandlePtr < gpuStart.ptr) return;

        uint32_t index = static_cast<uint32_t>((gpuHandlePtr - gpuStart.ptr) / m_D3D12DescriptorSize);
        m_FreeDescriptorIndices.push_back(index);
#else
        (void)gpuHandlePtr;
#endif
    }

    void ImGuiLayer::AllocateD3D12Descriptor(void* outCpuHandle, void* outGpuHandle) {
#if defined(AXLE_HAS_D3D12)
        if (!m_D3D12SrvHeap || !outCpuHandle || !outGpuHandle) return;

        uint32_t index = 0;
        if (!m_FreeDescriptorIndices.empty()) {
            index = m_FreeDescriptorIndices.back();
            m_FreeDescriptorIndices.pop_back();
        } else {
            index = m_AllocatedDescriptorCount++;
        }

        auto* outCpu = static_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(outCpuHandle);
        auto* outGpu = static_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(outGpuHandle);

        D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = m_D3D12SrvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = m_D3D12SrvHeap->GetGPUDescriptorHandleForHeapStart();

        outCpu->ptr = cpuStart.ptr + static_cast<SIZE_T>(index) * m_D3D12DescriptorSize;
        outGpu->ptr = gpuStart.ptr + static_cast<UINT64>(index) * m_D3D12DescriptorSize;
#else
        (void)outCpuHandle;
        (void)outGpuHandle;
#endif
    }

    void ImGuiLayer::FreeD3D12Descriptor(void* cpuHandle, void* gpuHandle) {
#if defined(AXLE_HAS_D3D12)
        (void)gpuHandle;
        if (!m_D3D12SrvHeap || !cpuHandle) return;

        auto* cpu = static_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(cpuHandle);
        D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = m_D3D12SrvHeap->GetCPUDescriptorHandleForHeapStart();
        if (cpu->ptr < cpuStart.ptr) return;

        uint32_t index = static_cast<uint32_t>((cpu->ptr - cpuStart.ptr) / m_D3D12DescriptorSize);
        m_FreeDescriptorIndices.push_back(index);
#else
        (void)cpuHandle;
        (void)gpuHandle;
#endif
    }

    void ImGuiLayer::SetDarkThemeColors() {
        ImGui::StyleColorsDark();

        auto& colors = ImGui::GetStyle().Colors;

        // Base text & background (Unity Charcoal Dark)
        colors[ImGuiCol_Text]                  = ImVec4(0.85f, 0.85f, 0.85f, 1.0f); // #D9D9D9
        colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.0f); // #808080
        colors[ImGuiCol_WindowBg]              = ImVec4(0.155f, 0.155f, 0.155f, 1.0f); // #282828 (Unity Signature Slate)
        colors[ImGuiCol_ChildBg]               = ImVec4(0.133f, 0.133f, 0.133f, 1.0f); // #222222
        colors[ImGuiCol_PopupBg]               = ImVec4(0.165f, 0.165f, 0.165f, 0.98f); // #2A2A2A
        colors[ImGuiCol_Border]                = ImVec4(0.10f, 0.10f, 0.10f, 1.0f); // #1A1A1A (Subtle 1px border)
        colors[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        // Frame background (Inputs, check boxes, combo boxes)
        colors[ImGuiCol_FrameBg]               = ImVec4(0.165f, 0.165f, 0.165f, 1.0f); // #2A2A2A
        colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.22f, 0.22f, 0.22f, 1.0f); // #383838
        colors[ImGuiCol_FrameBgActive]         = ImVec4(0.26f, 0.26f, 0.26f, 1.0f); // #424242

        // Title and Menu Bars
        colors[ImGuiCol_TitleBg]               = ImVec4(0.12f, 0.12f, 0.12f, 1.0f); // #1F1F1F
        colors[ImGuiCol_TitleBgActive]         = ImVec4(0.155f, 0.155f, 0.155f, 1.0f); // #282828
        colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.12f, 0.12f, 0.12f, 0.75f);
        colors[ImGuiCol_MenuBarBg]             = ImVec4(0.12f, 0.12f, 0.12f, 1.0f); // #1F1F1F

        // Scrollbars
        colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.13f, 0.13f, 0.13f, 0.60f);
        colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.28f, 0.28f, 0.28f, 1.0f); // #474747
        colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.36f, 0.36f, 0.36f, 1.0f); // #5C5C5C
        colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.44f, 0.44f, 0.44f, 1.0f); // #707070

        // Sliders & Checkmarks (Unity Selection Blue #3A79B8)
        colors[ImGuiCol_CheckMark]             = ImVec4(0.23f, 0.47f, 0.72f, 1.0f); // #3A79B8
        colors[ImGuiCol_SliderGrab]            = ImVec4(0.38f, 0.38f, 0.38f, 1.0f); // #606060
        colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.23f, 0.47f, 0.72f, 1.0f); // #3A79B8

        // Buttons
        colors[ImGuiCol_Button]                = ImVec4(0.22f, 0.22f, 0.22f, 1.0f); // #383838
        colors[ImGuiCol_ButtonHovered]         = ImVec4(0.28f, 0.28f, 0.28f, 1.0f); // #484848
        colors[ImGuiCol_ButtonActive]          = ImVec4(0.18f, 0.18f, 0.18f, 1.0f); // #2E2E2E

        // Headers (CollapsingHeader, TreeNodes)
        colors[ImGuiCol_Header]                = ImVec4(0.20f, 0.20f, 0.20f, 1.0f); // #333333
        colors[ImGuiCol_HeaderHovered]         = ImVec4(0.26f, 0.26f, 0.26f, 1.0f); // #424242
        colors[ImGuiCol_HeaderActive]          = ImVec4(0.23f, 0.47f, 0.72f, 0.85f); // #3A79B8

        // Separators
        colors[ImGuiCol_Separator]             = ImVec4(0.12f, 0.12f, 0.12f, 1.0f); // #1F1F1F
        colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.23f, 0.47f, 0.72f, 0.78f);
        colors[ImGuiCol_SeparatorActive]       = ImVec4(0.23f, 0.47f, 0.72f, 1.0f);

        // Resize Grips
        colors[ImGuiCol_ResizeGrip]            = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
        colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.23f, 0.47f, 0.72f, 0.75f);
        colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.23f, 0.47f, 0.72f, 0.90f);

        // Tabs (Unity Tab bar styling)
        colors[ImGuiCol_Tab]                   = ImVec4(0.14f, 0.14f, 0.14f, 1.0f); // #242424
        colors[ImGuiCol_TabHovered]            = ImVec4(0.24f, 0.24f, 0.24f, 1.0f); // #3D3D3D
        colors[ImGuiCol_TabActive]             = ImVec4(0.19f, 0.19f, 0.19f, 1.0f); // #303030
        colors[ImGuiCol_TabUnfocused]          = ImVec4(0.12f, 0.12f, 0.12f, 1.0f); // #1F1F1F
        colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.16f, 0.16f, 0.16f, 1.0f); // #292929

        // Docking Preview & Empty space
        colors[ImGuiCol_DockingPreview]        = ImVec4(0.23f, 0.47f, 0.72f, 0.45f);
        colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);

        // Tables & Lists
        colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);
        colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
        colors[ImGuiCol_TableBorderLight]      = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
        colors[ImGuiCol_TableRowBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

        // Highlight, Drag Drop and Selection
        colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.23f, 0.47f, 0.72f, 0.40f);
        colors[ImGuiCol_DragDropTarget]        = ImVec4(0.23f, 0.47f, 0.72f, 0.95f);
        colors[ImGuiCol_NavHighlight]          = ImVec4(0.23f, 0.47f, 0.72f, 0.80f);
    }

}
