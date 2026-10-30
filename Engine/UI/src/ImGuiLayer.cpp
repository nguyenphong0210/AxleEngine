#include <Axle/ImGuiLayer.hpp>
#include <Axle/Log.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#if defined(AXLE_HAS_VULKAN)
#if !defined(VK_NO_PROTOTYPES)
#define VK_NO_PROTOTYPES 1
#endif
#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#include <vulkan/vulkan.h>
#include <imgui_impl_vulkan.h>
#endif

#if defined(_WIN32)
#if defined(APIENTRY)
#undef APIENTRY
#endif
#if !defined(GLFW_EXPOSE_NATIVE_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#endif

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#include <GLFW/glfw3native.h>
#endif

#if defined(AXLE_HAS_D3D12)
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui_impl_dx12.h>
#endif

#if defined(AXLE_HAS_D3D12) || defined(AXLE_HAS_VULKAN)
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#endif
#include <filesystem>

namespace Axle {

#if defined(AXLE_HAS_VULKAN)
    struct VulkanDispatchTable {
        PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
        PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR = nullptr;
        PFN_vkQueueSubmit vkQueueSubmit = nullptr;
        PFN_vkQueuePresentKHR vkQueuePresentKHR = nullptr;
        PFN_vkWaitForFences vkWaitForFences = nullptr;
        PFN_vkResetFences vkResetFences = nullptr;
        PFN_vkResetCommandPool vkResetCommandPool = nullptr;
        PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
        PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
        PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass = nullptr;
        PFN_vkCmdEndRenderPass vkCmdEndRenderPass = nullptr;
        PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR = nullptr;
    };
    static VulkanDispatchTable s_VkFuncs;
#endif

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
        if (!m_Window) {
            AXLE_CORE_ERROR("ImGuiLayer::InitVulkan: GLFW window is null!");
            return;
        }

        auto& rhiDevice = RHI::RHIDevice::Get();
        if (!rhiDevice.IsInitialized() || rhiDevice.GetBackend() != RHI::RHIType::Vulkan) {
            AXLE_CORE_WARN("ImGuiLayer::InitVulkan: RHI Device is not initialized or not Vulkan; falling back to OpenGL!");
            InitOpenGL();
            m_BackendType = ImGuiBackendType::OpenGL;
            return;
        }

        auto* nriDev = rhiDevice.GetNRIDevice();
        auto* nriQueue = rhiDevice.GetGraphicsQueue();
        if (!nriDev || !nriQueue) {
            AXLE_CORE_ERROR("ImGuiLayer::InitVulkan: NRI device or graphics queue is null!");
            return;
        }

        const auto& core = rhiDevice.GetNRICore();
        const auto& wrapperVK = rhiDevice.GetNRIWrapperVK();

        VkInstance vkInstance = static_cast<VkInstance>(wrapperVK.GetInstanceVK(*nriDev));
        VkPhysicalDevice vkPhysDevice = static_cast<VkPhysicalDevice>(wrapperVK.GetPhysicalDeviceVK(*nriDev));
        VkDevice vkDevice = static_cast<VkDevice>(core.GetDeviceNativeObject(nriDev));
        VkQueue vkQueue = static_cast<VkQueue>(core.GetQueueNativeObject(nriQueue));
        uint32_t queueFamily = wrapperVK.GetQueueFamilyIndexVK(*nriQueue);

        if (!vkInstance || !vkPhysDevice || !vkDevice || !vkQueue) {
            AXLE_CORE_ERROR("ImGuiLayer::InitVulkan: Failed to query native Vulkan handles from NRI!");
            return;
        }

        // Dynamic Vulkan loader for ImGui
        auto pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(wrapperVK.GetInstanceProcAddrVK(*nriDev));
        if (!pfnGetInstanceProcAddr) {
#if defined(_WIN32)
            HMODULE hVK = GetModuleHandleA("vulkan-1.dll");
            if (!hVK) hVK = LoadLibraryA("vulkan-1.dll");
            if (hVK) {
                pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(hVK, "vkGetInstanceProcAddr"));
            }
#endif
        }

        if (!pfnGetInstanceProcAddr) {
            AXLE_CORE_ERROR("ImGuiLayer::InitVulkan: Failed to locate vkGetInstanceProcAddr!");
            return;
        }

        struct ImGuiLoaderContext {
            PFN_vkGetInstanceProcAddr getProcAddr;
            VkInstance instance;
        } loaderCtx{ pfnGetInstanceProcAddr, vkInstance };

        auto loaderFunc = [](const char* function_name, void* user_data) -> PFN_vkVoidFunction {
            auto* ctx = static_cast<ImGuiLoaderContext*>(user_data);
            if (!ctx || !ctx->getProcAddr) return nullptr;
            PFN_vkVoidFunction ptr = ctx->getProcAddr(ctx->instance, function_name);
            if (!ptr) {
                ptr = ctx->getProcAddr(VK_NULL_HANDLE, function_name);
            }
            return ptr;
        };

        if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, loaderFunc, &loaderCtx)) {
            AXLE_CORE_ERROR("ImGuiLayer::InitVulkan: Failed to load Vulkan functions for Dear ImGui!");
            return;
        }

        // Populate our local dispatch table for presentation & synchronization
        #define AXLE_LOAD_VK_FUNC(fn) s_VkFuncs.fn = reinterpret_cast<PFN_##fn>(pfnGetInstanceProcAddr(vkInstance, #fn))
        AXLE_LOAD_VK_FUNC(vkDeviceWaitIdle);
        AXLE_LOAD_VK_FUNC(vkAcquireNextImageKHR);
        AXLE_LOAD_VK_FUNC(vkQueueSubmit);
        AXLE_LOAD_VK_FUNC(vkQueuePresentKHR);
        AXLE_LOAD_VK_FUNC(vkWaitForFences);
        AXLE_LOAD_VK_FUNC(vkResetFences);
        AXLE_LOAD_VK_FUNC(vkResetCommandPool);
        AXLE_LOAD_VK_FUNC(vkBeginCommandBuffer);
        AXLE_LOAD_VK_FUNC(vkEndCommandBuffer);
        AXLE_LOAD_VK_FUNC(vkCmdBeginRenderPass);
        AXLE_LOAD_VK_FUNC(vkCmdEndRenderPass);
        AXLE_LOAD_VK_FUNC(vkDestroySurfaceKHR);
        #undef AXLE_LOAD_VK_FUNC

        // Allocate Vulkan Window Data
        m_VulkanWindowData = new ImGui_ImplVulkanH_Window();
        ImGui_ImplVulkanH_Window* wd = m_VulkanWindowData;

        // Create Surface via GLFW with Win32 fallback
        VkResult err = glfwCreateWindowSurface(vkInstance, m_Window, nullptr, &wd->Surface);
        if (err != VK_SUCCESS || wd->Surface == VK_NULL_HANDLE) {
#if defined(_WIN32)
            auto pfnCreateWin32Surface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(pfnGetInstanceProcAddr(vkInstance, "vkCreateWin32SurfaceKHR"));
            if (pfnCreateWin32Surface) {
                VkWin32SurfaceCreateInfoKHR sci = {};
                sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                sci.hinstance = GetModuleHandle(nullptr);
                sci.hwnd = glfwGetWin32Window(m_Window);
                err = pfnCreateWin32Surface(vkInstance, &sci, nullptr, &wd->Surface);
            }
#endif
            if (err != VK_SUCCESS || wd->Surface == VK_NULL_HANDLE) {
                AXLE_CORE_ERROR("ImGuiLayer::InitVulkan: Failed to create Vulkan surface! (VkResult: {0})", static_cast<int>(err));
                delete m_VulkanWindowData;
                m_VulkanWindowData = nullptr;
                return;
            }
        }

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(m_Window, &fbW, &fbH);
        m_VulkanWidth = static_cast<uint32_t>(fbW > 0 ? fbW : 1280);
        m_VulkanHeight = static_cast<uint32_t>(fbH > 0 ? fbH : 720);

        // Select Surface Format
        const VkFormat requestSurfaceImageFormats[] = {
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8A8_SRGB,
            VK_FORMAT_R8G8B8A8_SRGB
        };
        const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
            vkPhysDevice,
            wd->Surface,
            requestSurfaceImageFormats,
            static_cast<int>(sizeof(requestSurfaceImageFormats) / sizeof(requestSurfaceImageFormats[0])),
            requestSurfaceColorSpace
        );

        // Select Present Mode (Mailbox > Immediate > FIFO)
        VkPresentModeKHR present_modes[] = {
            VK_PRESENT_MODE_MAILBOX_KHR,
            VK_PRESENT_MODE_IMMEDIATE_KHR,
            VK_PRESENT_MODE_FIFO_KHR
        };
        wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
            vkPhysDevice,
            wd->Surface,
            present_modes,
            static_cast<int>(sizeof(present_modes) / sizeof(present_modes[0]))
        );

        // Create Swapchain, RenderPass, and Command Buffers for Window
        ImGui_ImplVulkanH_CreateOrResizeWindow(
            vkInstance,
            vkPhysDevice,
            vkDevice,
            wd,
            queueFamily,
            nullptr,
            static_cast<int>(m_VulkanWidth),
            static_cast<int>(m_VulkanHeight),
            m_VulkanMinImageCount,
            0
        );

        // Initialize ImGui GLFW implementation
        ImGui_ImplGlfw_InitForVulkan(m_Window, true);

        // Initialize ImGui Vulkan implementation with automatic descriptor pool management
        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = vkInstance;
        initInfo.PhysicalDevice = vkPhysDevice;
        initInfo.Device = vkDevice;
        initInfo.QueueFamily = queueFamily;
        initInfo.Queue = vkQueue;
        initInfo.DescriptorPoolSize = 1024;
        initInfo.MinImageCount = m_VulkanMinImageCount;
        initInfo.ImageCount = wd->ImageCount;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.PipelineInfoMain.RenderPass = wd->RenderPass;
        initInfo.PipelineInfoMain.Subpass = 0;

        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            AXLE_CORE_ERROR("ImGuiLayer::InitVulkan: ImGui_ImplVulkan_Init failed!");
            return;
        }

        AXLE_CORE_INFO("ImGui Layer initialized successfully for Vulkan 1.3 (Surface SwapChain: {0}x{1}, Images: {2}, PresentMode: {3})",
                       m_VulkanWidth, m_VulkanHeight, wd->ImageCount, static_cast<int>(wd->PresentMode));
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
        auto& rhiDevice = RHI::RHIDevice::Get();
        if (rhiDevice.IsInitialized() && rhiDevice.GetBackend() == RHI::RHIType::Vulkan) {
            auto* nriDev = rhiDevice.GetNRIDevice();
            const auto& core = rhiDevice.GetNRICore();
            const auto& wrapperVK = rhiDevice.GetNRIWrapperVK();
            auto vkDevice = static_cast<VkDevice>(core.GetDeviceNativeObject(nriDev));
            auto vkInstance = static_cast<VkInstance>(wrapperVK.GetInstanceVK(*nriDev));

            if (vkDevice && s_VkFuncs.vkDeviceWaitIdle) {
                s_VkFuncs.vkDeviceWaitIdle(vkDevice);
            }

            ImGui_ImplVulkan_Shutdown();
            if (m_Window) {
                ImGui_ImplGlfw_Shutdown();
            }

            if (m_VulkanWindowData) {
                ImGui_ImplVulkanH_DestroyWindow(vkInstance, vkDevice, m_VulkanWindowData, nullptr);
                if (m_VulkanWindowData->Surface != VK_NULL_HANDLE && s_VkFuncs.vkDestroySurfaceKHR) {
                    s_VkFuncs.vkDestroySurfaceKHR(vkInstance, m_VulkanWindowData->Surface, nullptr);
                    m_VulkanWindowData->Surface = VK_NULL_HANDLE;
                }
                delete m_VulkanWindowData;
                m_VulkanWindowData = nullptr;
            }
        } else {
            ImGui_ImplVulkan_Shutdown();
            if (m_Window) {
                ImGui_ImplGlfw_Shutdown();
            }
            if (m_VulkanWindowData) {
                delete m_VulkanWindowData;
                m_VulkanWindowData = nullptr;
            }
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
#if defined(AXLE_HAS_VULKAN)
        else if (m_BackendType == ImGuiBackendType::Vulkan && m_VulkanWindowData) {
            auto& rhiDevice = RHI::RHIDevice::Get();
            if (!rhiDevice.IsInitialized() || rhiDevice.GetBackend() != RHI::RHIType::Vulkan) return;

            auto* nriDev = rhiDevice.GetNRIDevice();
            auto* nriQueue = rhiDevice.GetGraphicsQueue();
            if (!nriDev || !nriQueue) return;

            const auto& core = rhiDevice.GetNRICore();
            const auto& wrapperVK = rhiDevice.GetNRIWrapperVK();

            VkInstance vkInstance = static_cast<VkInstance>(wrapperVK.GetInstanceVK(*nriDev));
            VkPhysicalDevice vkPhysDevice = static_cast<VkPhysicalDevice>(wrapperVK.GetPhysicalDeviceVK(*nriDev));
            VkDevice vkDevice = static_cast<VkDevice>(core.GetDeviceNativeObject(nriDev));
            VkQueue vkQueue = static_cast<VkQueue>(core.GetQueueNativeObject(nriQueue));
            uint32_t queueFamily = wrapperVK.GetQueueFamilyIndexVK(*nriQueue);

            ImGui_ImplVulkanH_Window* wd = m_VulkanWindowData;
            ImDrawData* draw_data = ImGui::GetDrawData();
            const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

            // Handle window resize for Vulkan swapchain
            if (m_Window) {
                int fbW = 0, fbH = 0;
                glfwGetFramebufferSize(m_Window, &fbW, &fbH);
                if (fbW > 0 && fbH > 0 && (m_VulkanSwapChainRebuild || m_VulkanWidth != static_cast<uint32_t>(fbW) || m_VulkanHeight != static_cast<uint32_t>(fbH))) {
                    m_VulkanWidth = static_cast<uint32_t>(fbW);
                    m_VulkanHeight = static_cast<uint32_t>(fbH);
                    ImGui_ImplVulkan_SetMinImageCount(m_VulkanMinImageCount);
                    ImGui_ImplVulkanH_CreateOrResizeWindow(
                        vkInstance, vkPhysDevice, vkDevice, wd, queueFamily, nullptr,
                        static_cast<int>(m_VulkanWidth), static_cast<int>(m_VulkanHeight),
                        m_VulkanMinImageCount, 0
                    );
                    wd->FrameIndex = 0;
                    m_VulkanSwapChainRebuild = false;
                }
            }

            if (!is_minimized && wd->Swapchain != VK_NULL_HANDLE && s_VkFuncs.vkAcquireNextImageKHR && s_VkFuncs.vkQueueSubmit && s_VkFuncs.vkQueuePresentKHR) {
                VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
                VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

                VkResult err = s_VkFuncs.vkAcquireNextImageKHR(vkDevice, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
                if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
                    m_VulkanSwapChainRebuild = true;
                    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
                }

                ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
                s_VkFuncs.vkWaitForFences(vkDevice, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
                s_VkFuncs.vkResetFences(vkDevice, 1, &fd->Fence);

                s_VkFuncs.vkResetCommandPool(vkDevice, fd->CommandPool, 0);
                VkCommandBufferBeginInfo beginInfo = {};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                s_VkFuncs.vkBeginCommandBuffer(fd->CommandBuffer, &beginInfo);

                VkRenderPassBeginInfo rpInfo = {};
                rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rpInfo.renderPass = wd->RenderPass;
                rpInfo.framebuffer = fd->Framebuffer;
                rpInfo.renderArea.extent.width = wd->Width;
                rpInfo.renderArea.extent.height = wd->Height;
                rpInfo.clearValueCount = 1;
                rpInfo.pClearValues = &wd->ClearValue;
                s_VkFuncs.vkCmdBeginRenderPass(fd->CommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

                // Record Dear ImGui primitives into Vulkan command buffer
                ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

                s_VkFuncs.vkCmdEndRenderPass(fd->CommandBuffer);
                s_VkFuncs.vkEndCommandBuffer(fd->CommandBuffer);

                VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                VkSubmitInfo submitInfo = {};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.waitSemaphoreCount = 1;
                submitInfo.pWaitSemaphores = &image_acquired_semaphore;
                submitInfo.pWaitDstStageMask = &wait_stage;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &fd->CommandBuffer;
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pSignalSemaphores = &render_complete_semaphore;

                s_VkFuncs.vkQueueSubmit(vkQueue, 1, &submitInfo, fd->Fence);

                if (!m_VulkanSwapChainRebuild) {
                    VkPresentInfoKHR presentInfo = {};
                    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                    presentInfo.waitSemaphoreCount = 1;
                    presentInfo.pWaitSemaphores = &render_complete_semaphore;
                    presentInfo.swapchainCount = 1;
                    presentInfo.pSwapchains = &wd->Swapchain;
                    presentInfo.pImageIndices = &wd->FrameIndex;

                    err = s_VkFuncs.vkQueuePresentKHR(vkQueue, &presentInfo);
                    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
                        m_VulkanSwapChainRebuild = true;
                    }
                    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
                }
            }
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
#if defined(AXLE_HAS_VULKAN)
        if (m_BackendType == ImGuiBackendType::Vulkan) {
            const auto& core = RHI::RHIDevice::Get().GetNRICore();
            auto vkCmd = static_cast<VkCommandBuffer>(core.GetCommandBufferNativeObject(cmdBuffer->GetNative()));
            if (vkCmd) {
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmd);
            }
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

        // Base text & background (Authentic Unity 2022/6 Dark Slate)
        colors[ImGuiCol_Text]                  = ImVec4(0.83f, 0.83f, 0.83f, 1.0f); // #D4D4D4 (Unity Neutral Text)
        colors[ImGuiCol_TextDisabled]          = ImVec4(0.48f, 0.48f, 0.48f, 1.0f); // #7A7A7A
        colors[ImGuiCol_WindowBg]              = ImVec4(0.157f, 0.157f, 0.157f, 1.0f); // #282828 (Unity Signature Slate Panel)
        colors[ImGuiCol_ChildBg]               = ImVec4(0.137f, 0.137f, 0.137f, 1.0f); // #232323
        colors[ImGuiCol_PopupBg]               = ImVec4(0.165f, 0.165f, 0.165f, 0.98f); // #2A2A2A
        colors[ImGuiCol_Border]                = ImVec4(0.118f, 0.118f, 0.118f, 1.0f); // #1E1E1E (Subtle 1px border)
        colors[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        // Frame background (Inputs, checkboxes, combo boxes)
        colors[ImGuiCol_FrameBg]               = ImVec4(0.137f, 0.137f, 0.137f, 1.0f); // #232323 (slightly sunken)
        colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.180f, 0.180f, 0.180f, 1.0f); // #2E2E2E
        colors[ImGuiCol_FrameBgActive]         = ImVec4(0.220f, 0.220f, 0.220f, 1.0f); // #383838

        // Title and Menu Bars
        colors[ImGuiCol_TitleBg]               = ImVec4(0.125f, 0.125f, 0.125f, 1.0f); // #202020
        colors[ImGuiCol_TitleBgActive]         = ImVec4(0.157f, 0.157f, 0.157f, 1.0f); // #282828
        colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.125f, 0.125f, 0.125f, 0.75f);
        colors[ImGuiCol_MenuBarBg]             = ImVec4(0.145f, 0.145f, 0.145f, 1.0f); // #252525 (Unity Main Menu Bar)

        // Scrollbars
        colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.130f, 0.130f, 0.130f, 0.50f);
        colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.280f, 0.280f, 0.280f, 1.0f); // #474747
        colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.360f, 0.360f, 0.360f, 1.0f); // #5C5C5C
        colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.440f, 0.440f, 0.440f, 1.0f); // #707070

        // Sliders & Checkmarks (Unity Selection Blue #3A79B8)
        colors[ImGuiCol_CheckMark]             = ImVec4(0.23f, 0.47f, 0.72f, 1.0f); // #3A79B8
        colors[ImGuiCol_SliderGrab]            = ImVec4(0.35f, 0.35f, 0.35f, 1.0f); // #595959
        colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.23f, 0.47f, 0.72f, 1.0f); // #3A79B8

        // Buttons (Unity Dark Slate buttons)
        colors[ImGuiCol_Button]                = ImVec4(0.215f, 0.215f, 0.215f, 1.0f); // #373737
        colors[ImGuiCol_ButtonHovered]         = ImVec4(0.275f, 0.275f, 0.275f, 1.0f); // #464646
        colors[ImGuiCol_ButtonActive]          = ImVec4(0.180f, 0.380f, 0.620f, 1.0f); // #2E619E (Unity Active Blue)

        // Headers (CollapsingHeader, TreeNodes)
        colors[ImGuiCol_Header]                = ImVec4(0.200f, 0.200f, 0.200f, 1.0f); // #333333
        colors[ImGuiCol_HeaderHovered]         = ImVec4(0.250f, 0.250f, 0.250f, 1.0f); // #404040
        colors[ImGuiCol_HeaderActive]          = ImVec4(0.180f, 0.380f, 0.620f, 0.85f); // #2E619E

        // Separators
        colors[ImGuiCol_Separator]             = ImVec4(0.125f, 0.125f, 0.125f, 1.0f); // #202020
        colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.230f, 0.470f, 0.720f, 0.78f);
        colors[ImGuiCol_SeparatorActive]       = ImVec4(0.230f, 0.470f, 0.720f, 1.0f);

        // Resize Grips
        colors[ImGuiCol_ResizeGrip]            = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
        colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.23f, 0.47f, 0.72f, 0.75f);
        colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.23f, 0.47f, 0.72f, 0.90f);

        // Tabs (Unity Tab bar styling: Active tab blends into WindowBg)
        colors[ImGuiCol_Tab]                   = ImVec4(0.125f, 0.125f, 0.125f, 1.0f); // #202020 (Dimmed inactive tab)
        colors[ImGuiCol_TabHovered]            = ImVec4(0.220f, 0.220f, 0.220f, 1.0f); // #383838
        colors[ImGuiCol_TabActive]             = ImVec4(0.157f, 0.157f, 0.157f, 1.0f); // #282828 (Exact match with WindowBg!)
        colors[ImGuiCol_TabUnfocused]          = ImVec4(0.110f, 0.110f, 0.110f, 1.0f); // #1C1C1C
        colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.145f, 0.145f, 0.145f, 1.0f); // #252525

        // Docking Preview & Empty space
        colors[ImGuiCol_DockingPreview]        = ImVec4(0.23f, 0.47f, 0.72f, 0.45f);
        colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);

        // Tables & Lists
        colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
        colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
        colors[ImGuiCol_TableBorderLight]      = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
        colors[ImGuiCol_TableRowBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.015f);

        // Highlight, Drag Drop and Selection
        colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.18f, 0.38f, 0.62f, 0.50f);
        colors[ImGuiCol_DragDropTarget]        = ImVec4(0.23f, 0.47f, 0.72f, 0.95f);
        colors[ImGuiCol_NavHighlight]          = ImVec4(0.23f, 0.47f, 0.72f, 0.80f);
    }

}
