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
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui_impl_dx12.h>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#endif

#if defined(AXLE_HAS_VULKAN)
#include <imgui_impl_vulkan.h>
#endif

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

        SetDarkThemeColors();

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;

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

        AXLE_CORE_INFO("ImGui Layer initialized successfully for Direct3D 12 (SRV Heap: 1024 descriptors)");
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
        AXLE_CORE_WARN("Vulkan ImGui backend requested but not available; falling back to OpenGL!");
        InitOpenGL();
        m_BackendType = ImGuiBackendType::OpenGL;
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
        ImGui_ImplDX12_Shutdown();
        if (m_Window) {
            ImGui_ImplGlfw_Shutdown();
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
        switch (m_BackendType) {
            case ImGuiBackendType::OpenGL:
                ImGui_ImplOpenGL3_NewFrame();
                if (m_Window) ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                break;
            case ImGuiBackendType::Direct3D12:
#if defined(AXLE_HAS_D3D12)
                ImGui_ImplDX12_NewFrame();
                if (m_Window) ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
#endif
                break;
            case ImGuiBackendType::Vulkan:
#if defined(AXLE_HAS_VULKAN)
                ImGui_ImplVulkan_NewFrame();
                if (m_Window) ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
#endif
                break;
        }
    }

    void ImGuiLayer::End() {
        UpdateWindowSnapping();
        ImGui::Render();

        if (m_BackendType == ImGuiBackendType::OpenGL) {
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
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
        colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };

        // Headers
        colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

        // Buttons
        colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

        // Frame BG
        colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

        // Tabs
        colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
        colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
        colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };

        // Title
        colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    }

}
