#include <Axle/ImGuiLayer.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIPresentationContext.hpp>
#include <Axle/ImageSource.hpp>
#include <Axle/ImageTextureCache.hpp>
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
#include <stdexcept>
#include <array>
#include <utility>
#include <unordered_set>
#include "VulkanWindowBridge.hpp"

namespace Axle {
    struct ImGuiDescriptorArena {
        RHI::RHIResourceLease Lifetime;
        std::shared_ptr<bool> NativeAlive = std::make_shared<bool>(true);
        bool Closing = false;
        uint32_t Capacity = 0, Allocated = 0, Stride = 0;
        uint64_t CPUStart = 0, GPUStart = 0;
        std::vector<uint32_t> FreeIndices;
        std::unordered_set<uint32_t> LiveIndices;
        std::unordered_set<uint64_t> LiveHandles;
        std::function<void(uint64_t)> FreeNative;
        bool Allocate(uint64_t& cpu, uint64_t& gpu) {
            cpu = gpu = 0;
            if (Closing || !Lifetime.IsLive()) return false;
            uint32_t index;
            if (!FreeIndices.empty()) { index = FreeIndices.back(); FreeIndices.pop_back(); }
            else { if (Allocated == Capacity) return false; index = Allocated++; }
            LiveIndices.insert(index);
            cpu = CPUStart + static_cast<uint64_t>(index) * Stride;
            gpu = GPUStart + static_cast<uint64_t>(index) * Stride;
            return true;
        }
        void Free(uint64_t id) {
            if (!id || !*NativeAlive) return;
            if (FreeNative) { if (LiveHandles.erase(id)) FreeNative(id); return; }
            if (!Stride || id < GPUStart || (id - GPUStart) % Stride) return;
            const auto index = static_cast<uint32_t>((id - GPUStart) / Stride);
            if (index < Capacity && LiveIndices.erase(index) && !Closing) FreeIndices.push_back(index);
        }
    };
    struct GuiFrameState {
        bool Active = false, Closing = false;
        bool NativeRecorded = false;
        const void* PreparedCommands = nullptr;
        RHI::RHIRecordingLease Uses;
        GuiDeferredReleaser RetireOutsideFrame;
        std::vector<std::function<void()>> Deferred;
    };
    struct GuiTextureRegistration {
        uint64_t ID = 0;
        RHI::RHIResourceLease Generation;
        std::weak_ptr<GuiFrameState> Frame;
        ~GuiTextureRegistration() {
            // Invalidation can follow AddImage using an ID cached by a caller.
            // Preserve that registration through the current draw-list boundary,
            // including any intermediate state-preparation queue drain.
            if (auto frame = Frame.lock(); frame && frame->Active && !frame->Closing)
                RHI::RHIDevice::Get().GetResources().AppendPin(frame->Uses, Generation);
        }
    };
    bool GuiTextureLease::IsValid() const {
        if (!m_Registration || !m_Registration->Generation.IsLive()) return false;
        auto frame = m_Registration->Frame.lock();
        return frame && !frame->Closing;
    }
    uint64_t GuiTextureLease::GetID() const {
        if (!RHI::RHIDevice::Get().GetResources().IsOwnerThread()) return 0;
        if (!IsValid()) return 0;
        auto frame = m_Registration->Frame.lock();
        if (frame->Active && !RHI::RHIDevice::Get().GetResources().AppendPin(frame->Uses, m_Registration->Generation)) return 0;
        return m_Registration->ID;
    }
    struct ImGuiDescriptorCallbackState { ImGuiLayer* Owner = nullptr; std::shared_ptr<ImGuiDescriptorArena> Arena; };
    struct ImGuiBackendRetirement { std::function<void()> Cleanup; };
    class GuiApplicationRenderer final : public ApplicationRenderContext {
    public:
        RHI::RHIPresentationContext Presenter;
        RHI::RHIPresentationContext::Recorder Prepare, Draw;
        std::function<void()> Complete;
        std::function<void()> Detach;
        ~GuiApplicationRenderer() override { Shutdown(); }
        bool Present(const Window& window) override {
            struct Finish { std::function<void()>& Complete; ~Finish() { if (Complete) Complete(); } } finish{Complete};
            int width = 0, height = 0; glfwGetFramebufferSize(window.GetNativeWindow(), &width, &height);
            if (glfwGetWindowAttrib(window.GetNativeWindow(), GLFW_ICONIFIED)) width = height = 0;
            try {
                const auto outcome = Presenter.RenderFrame(static_cast<uint32_t>(width), static_cast<uint32_t>(height), window.IsVSync(), Prepare, Draw);
                return outcome != RHI::RHIPresentOutcome::Failed;
            } catch (const std::exception& error) {
                RHI::RHIDevice::Get().ObserveResult(nri::Result::FAILURE, "RecordPresentation", error.what());
                Presenter.Shutdown(); return false;
            }
        }
        void Shutdown() override { Presenter.Shutdown(); if (Detach) Detach(); }
    };

    std::unique_ptr<ApplicationRenderContext> ImGuiLayer::CreateRenderContext(const Window& window) {
        if (m_BackendType == ImGuiBackendType::OpenGL) return {};
        if (!m_Initialized || m_ComposedPresentation || !m_DescriptorCallbacks) throw std::runtime_error("Invalid UI composition owner");
        auto result = std::make_unique<GuiApplicationRenderer>();
        RHI::RHISwapChainDesc desc; desc.VSync = window.IsVSync(); desc.FramesInFlight = 3;
        if (!result->Presenter.Init(window.GetNativeWindow(), desc)) throw std::runtime_error("Application presentation initialization failed");
        std::weak_ptr<ImGuiDescriptorCallbackState> weak = m_DescriptorCallbacks;
        result->Prepare = [weak](RHI::RHICommandBuffer& commands, nri::Format) {
            auto state = weak.lock(); return state && state->Owner && state->Owner->PrepareGuiTextures(&commands);
        };
        result->Draw = [weak](RHI::RHICommandBuffer& commands, nri::Format format) {
            auto state = weak.lock(); return state && state->Owner && state->Owner->Render(&commands, format);
        };
        result->Complete = [weak] { if (auto state = weak.lock(); state && state->Owner) state->Owner->FinishGuiFrame(); };
        result->Detach = [weak] {
            if (auto state = weak.lock(); state && state->Owner) {
                auto* owner = state->Owner;
                if (owner->m_GuiFrame && owner->m_GuiFrame->Active && ImGui::GetCurrentContext() && ImGui::GetCurrentContext()->WithinFrameScope)
                    ImGui::EndFrame();
                owner->FinishGuiFrame(); owner->m_ComposedPresentation = false;
            }
        };
        m_ComposedPresentation = true; return result;
    }
    bool ImGuiLayer::IsOperational() const {
        const auto state = RHI::RHIDevice::Get().GetState();
        return m_Initialized && (m_BackendType == ImGuiBackendType::OpenGL ||
            (state != RHI::RHIDeviceState::Lost && state != RHI::RHIDeviceState::Stopping));
    }


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
        Shutdown();
    }

    void ImGuiLayer::Init(GLFWwindow* window) {
        ImageTextureCache::SetSourceResolver(LoadImageSource);
        if (m_Initialized) return;

        m_Window = window;
        m_GuiFrame = std::make_shared<GuiFrameState>();
        m_GuiFrame->RetireOutsideFrame = m_BackendType == ImGuiBackendType::OpenGL ?
            GuiDeferredReleaser([](std::function<void()> release) { if (release) release(); }) :
            RHI::RHIDevice::Get().GetResources().GetDeferredReleaser();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        m_ContextCreated = true;

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

        try {
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

            bool ready = ImGui::GetIO().BackendRendererUserData && (!m_Window || ImGui::GetIO().BackendPlatformUserData);
            if (!ready) {
                Shutdown();
                throw std::runtime_error("UI backend initialization failed; product candidate must be rolled back");
            }
            m_Initialized = true;
            if (m_BackendType != ImGuiBackendType::OpenGL) {
                m_BackendRetirement = std::make_shared<ImGuiBackendRetirement>();
                auto retirement = m_BackendRetirement;
                m_BackendLease = RHI::RHIDevice::Get().GetResources().Publish(RHI::RHIResourceKind::UIBackend,
                    "Native UI backend", {}, [retirement] { if (auto cleanup = std::move(retirement->Cleanup)) cleanup(); },
                    {m_DescriptorArena->Lifetime});
                if (!m_BackendLease.IsLive()) throw std::runtime_error("Native UI backend lifetime publication failed");
                if (!m_DescriptorCallbacks || !m_DescriptorCallbacks->Owner) {
                    m_DescriptorCallbacks = std::make_shared<ImGuiDescriptorCallbackState>();
                    m_DescriptorCallbacks->Owner = this;
                }
                std::weak_ptr<ImGuiDescriptorCallbackState> owner = m_DescriptorCallbacks;
                RHI::RHIDevice::Get().GetResources().BeforeDeviceClose([owner] {
                    if (auto state = owner.lock(); state && state->Owner) state->Owner->Shutdown();
                });
            }
        } catch (...) {
            Shutdown();
            throw;
        }
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
        if (!ImGui_ImplOpenGL3_Init("#version 450") || !ImGui_ImplOpenGL3_CreateDeviceObjects())
            throw std::runtime_error("OpenGL UI device objects failed");
        AXLE_CORE_INFO("ImGui Layer initialized successfully for OpenGL (Docking enabled)");
    }

    void ImGuiLayer::InitD3D12() {
#if defined(AXLE_HAS_D3D12)
        if (m_Window) {
            ImGui_ImplGlfw_InitForOther(m_Window, true);
        }

        auto& rhiDevice = RHI::RHIDevice::Get();
        if (!rhiDevice.IsNativeReady()) {
            AXLE_CORE_ERROR("ImGuiLayer::InitD3D12: RHIDevice is not initialized! Rejecting product candidate.");
            return;
        }

        const auto& core = rhiDevice.GetNRICore();
        auto* nriDev = rhiDevice.GetNRIDevice();
        auto* d3d12Device = static_cast<ID3D12Device*>(core.GetDeviceNativeObject(nriDev));
        auto* d3d12Queue = static_cast<ID3D12CommandQueue*>(core.GetQueueNativeObject(rhiDevice.GetGraphicsQueue()));

        if (!d3d12Device || !d3d12Queue) {
            AXLE_CORE_ERROR("ImGuiLayer::InitD3D12: Failed to retrieve native D3D12 device or queue!");
            return;
        }

        // Create SRV Descriptor Heap for ImGui textures & viewports
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = D3D12DescriptorCapacity;
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

        m_DescriptorArena = std::make_shared<ImGuiDescriptorArena>();
        auto arena = m_DescriptorArena;
        arena->Capacity = D3D12DescriptorCapacity; arena->Stride = m_D3D12DescriptorSize;
        arena->CPUStart = m_D3D12SrvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
        arena->GPUStart = m_D3D12SrvHeap->GetGPUDescriptorHandleForHeapStart().ptr;
        auto* heap = m_D3D12SrvHeap; heap->AddRef();
        auto alive = arena->NativeAlive;
        arena->Lifetime = rhiDevice.GetResources().Publish(RHI::RHIResourceKind::Pool, "ImGui D3D12 arena", {},
            [heap, alive] { *alive = false; heap->Release(); });
        if (!arena->Lifetime.IsLive()) { heap->Release(); throw std::runtime_error("GUI arena publication failed"); }

        ImGui_ImplDX12_InitInfo initInfo = {};
        initInfo.Device = d3d12Device;
        initInfo.CommandQueue = d3d12Queue;
        initInfo.NumFramesInFlight = 3;
        initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
        initInfo.SrvDescriptorHeap = m_D3D12SrvHeap;
        m_DescriptorCallbacks = std::make_shared<ImGuiDescriptorCallbackState>();
        m_DescriptorCallbacks->Owner = this;
        m_DescriptorCallbacks->Arena = arena;
        initInfo.UserData = m_DescriptorCallbacks.get();
        initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
            auto* state = static_cast<ImGuiDescriptorCallbackState*>(info->UserData);
            uint64_t cpu = 0, gpu = 0;
            if (state->Arena) state->Arena->Allocate(cpu, gpu);
            outCpu->ptr = cpu; outGpu->ptr = gpu;
        };
        initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
            auto* state = static_cast<ImGuiDescriptorCallbackState*>(info->UserData);
            if (state->Arena) state->Arena->Free(gpu.ptr);
        };

        if (!ImGui_ImplDX12_Init(&initInfo)) {
            AXLE_CORE_ERROR("ImGuiLayer::InitD3D12: ImGui_ImplDX12_Init failed!");
            m_D3D12SrvHeap->Release();
            m_D3D12SrvHeap = nullptr;
            return;
        }

        if (!ImGui_ImplDX12_CreateDeviceObjects()) throw std::runtime_error("D3D12 UI device objects failed");

        AXLE_CORE_INFO("ImGui D3D12 recording bridge initialized");
#else
        AXLE_CORE_WARN("D3D12 ImGui backend requested but not supported on this platform; rejecting product candidate!");
#endif
    }

    void ImGuiLayer::InitVulkan() {
#if defined(AXLE_HAS_VULKAN)
        auto& rhiDevice = RHI::RHIDevice::Get();
        if (!rhiDevice.IsNativeReady() || rhiDevice.GetBackend() != RHI::RHIType::Vulkan) {
            AXLE_CORE_WARN("ImGuiLayer::InitVulkan: RHI Device is not initialized or not Vulkan; rejecting product candidate!");
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

        if (m_Window) ImGui_ImplGlfw_InitForVulkan(m_Window, true);

        // The shared arena survives the UI owner while GUI generations retain it.
        m_DescriptorArena = std::make_shared<ImGuiDescriptorArena>();
        auto arena = m_DescriptorArena;
        const auto createPool = WindowVKProc<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool");
        const auto destroyPool = WindowVKProc<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool");
        const auto freeSets = WindowVKProc<PFN_vkFreeDescriptorSets>("vkFreeDescriptorSets");
        VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024},
            {VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE}};
        VkDescriptorPoolCreateInfo poolDesc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolDesc.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolDesc.maxSets = 1024 + IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE;
        poolDesc.poolSizeCount = 2; poolDesc.pPoolSizes = poolSizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        CheckedWindowVK("CreateGuiDescriptorPool", [&] { return createPool(vkDevice, &poolDesc, nullptr, &pool); });
        auto alive = arena->NativeAlive;
        arena->Lifetime = rhiDevice.GetResources().Publish(RHI::RHIResourceKind::Pool, "ImGui Vulkan arena", {},
            [vkDevice, pool, destroyPool, alive] { *alive = false; destroyPool(vkDevice, pool, nullptr); });
        if (!arena->Lifetime.IsLive()) { destroyPool(vkDevice, pool, nullptr); throw std::runtime_error("GUI arena publication failed"); }
        arena->FreeNative = [vkDevice, pool, freeSets, alive](uint64_t id) {
            if (!*alive) return;
            auto descriptor = reinterpret_cast<VkDescriptorSet>(id);
            const auto result = freeSets(vkDevice, pool, 1, &descriptor);
            if (result != VK_SUCCESS) AXLE_CORE_ERROR("GUI descriptor cleanup failed: {0}", static_cast<int>(result));
        };

        // Initialize the native renderer bridge using the externally owned pool.
        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.CheckVkResultFn = [](VkResult result) {
            if (result != VK_SUCCESS) {
                RHI::RHIDevice::Get().ObserveResult(result == VK_ERROR_DEVICE_LOST ? nri::Result::DEVICE_LOST : nri::Result::FAILURE, "NativeUIBridge");
                throw std::runtime_error("Vulkan UI backend failed: " + std::to_string(result));
            }
        };
        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = vkInstance;
        initInfo.PhysicalDevice = vkPhysDevice;
        initInfo.Device = vkDevice;
        initInfo.QueueFamily = queueFamily;
        initInfo.Queue = vkQueue;
        initInfo.DescriptorPool = pool;
        initInfo.DescriptorPoolSize = 0;
        initInfo.MinImageCount = 3;
        initInfo.ImageCount = 3;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.UseDynamicRendering = true;
        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
        m_VulkanColorFormat = static_cast<uint32_t>(colorFormat);

        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            AXLE_CORE_ERROR("ImGuiLayer::InitVulkan: ImGui_ImplVulkan_Init failed!");
            return;
        }

        AXLE_CORE_INFO("ImGui Vulkan dynamic-rendering bridge initialized");
#else
        AXLE_CORE_WARN("Vulkan ImGui backend requested but not available without Vulkan SDK!");
#endif
    }

    void ImGuiLayer::Shutdown() {
        m_ComposedPresentation = false;
        if (m_GuiFrame) {
            m_GuiFrame->Closing = true;
            if (m_GuiFrame->Active && m_ContextCreated && ImGui::GetCurrentContext() && ImGui::GetCurrentContext()->WithinFrameScope)
                ImGui::EndFrame();
            m_GuiFrame->Active = false;
            m_GuiFrame->Uses.Reset();
            auto deferred = std::move(m_GuiFrame->Deferred); m_GuiFrame->Deferred.clear();
            for (auto& release : deferred) m_GuiFrame->RetireOutsideFrame(std::move(release));
        }
        if (m_DescriptorArena) m_DescriptorArena->Closing = true;
        if (m_DescriptorCallbacks) m_DescriptorCallbacks->Owner = nullptr;
        auto& device = RHI::RHIDevice::Get();
        const bool nativeOwner = m_ContextCreated && m_BackendType != ImGuiBackendType::OpenGL && device.IsNativeReady();
        const bool drained = !nativeOwner || device.GetState() == RHI::RHIDeviceState::Lost || device.WaitIdle();
        const bool recorded = m_BackendLease && device.GetResources().IsInUse(m_BackendLease);
        if (nativeOwner && ((!drained && device.GetState() != RHI::RHIDeviceState::Lost) || recorded)) {
            if (!drained) device.ObserveResult(nri::Result::FAILURE, "QueueSubmit", "unresolved UI shutdown");
            // The native context and backend objects outlive this layer if a
            // checked drain cannot resolve an ambiguous submission yet.
            auto deferred = std::move(m_DeferredReleases); m_DeferredReleases.clear();
            device.GetResources().DeferUntilDrain([deferred = std::move(deferred)] {
                for (auto& release : deferred) release();
            });
            // Detach the main viewport's renderer-owned storage while GLFW
            // removes its hooks. That storage is restored for deferred renderer
            // shutdown after completion, and remains owned by the context.
            auto* viewport = ImGui::GetMainViewport();
            auto* rendererData = std::exchange(viewport->RendererUserData, nullptr);
            if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplGlfw_Shutdown();
            viewport->RendererUserData = rendererData;
            auto* context = ImGui::GetCurrentContext();
#if defined(AXLE_HAS_D3D12)
            if (m_BackendType == ImGuiBackendType::Direct3D12) {
                auto callbacks = std::move(m_DescriptorCallbacks);
                if (callbacks) callbacks->Owner = nullptr;
                auto* srvHeap = std::exchange(m_D3D12SrvHeap, nullptr);
                auto cleanup = [context, callbacks, srvHeap] {
                    auto* previous = ImGui::GetCurrentContext(); ImGui::SetCurrentContext(context);
                    if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplDX12_Shutdown();
                    if (srvHeap) srvHeap->Release();
                    ImGui::DestroyContext(context);
                    ImGui::SetCurrentContext(previous == context ? nullptr : previous);
                };
                if (m_BackendRetirement) m_BackendRetirement->Cleanup = std::move(cleanup);
                else device.ReleaseNative(std::move(cleanup));
            }
#endif
#if defined(AXLE_HAS_VULKAN)
            if (m_BackendType == ImGuiBackendType::Vulkan) {
                auto cleanup = [context] {
                    auto* previous = ImGui::GetCurrentContext(); ImGui::SetCurrentContext(context);
                    if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplVulkan_Shutdown();
                    ImGui::DestroyContext(context);
                    ImGui::SetCurrentContext(previous == context ? nullptr : previous);
                };
                if (m_BackendRetirement) m_BackendRetirement->Cleanup = std::move(cleanup);
                else device.ReleaseNative(std::move(cleanup));
            }
#endif
            ImGui::SetCurrentContext(nullptr);
            m_ContextCreated = m_Initialized = false; m_Window = nullptr;
            m_BackendLease.Reset(); m_BackendRetirement.reset();
            m_DescriptorArena.reset(); m_GuiFrame.reset();
            return;
        }
        FlushDeferredReleases();
        if (!m_ContextCreated) return;

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
        m_ContextCreated = false;
        m_Initialized = false;
        m_Window = nullptr;
        m_BackendLease.Reset(); m_BackendRetirement.reset();
        m_DescriptorArena.reset(); m_GuiFrame.reset();
        AXLE_CORE_INFO("ImGui Layer shut down cleanly");
    }

    void ImGuiLayer::ShutdownOpenGL() {
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplOpenGL3_Shutdown();
        if (ImGui::GetIO().BackendPlatformUserData) {
            ImGui_ImplGlfw_Shutdown();
        }
    }

    void ImGuiLayer::ShutdownD3D12() {
#if defined(AXLE_HAS_D3D12)
        auto& rhiDevice = RHI::RHIDevice::Get();
        if (m_Initialized && rhiDevice.IsNativeReady()) rhiDevice.WaitIdle();

        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplDX12_Shutdown();
        if (ImGui::GetIO().BackendPlatformUserData) {
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
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplVulkan_Shutdown();
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplGlfw_Shutdown();
#endif
    }

    void ImGuiLayer::Begin() {
        if (!m_Initialized) return;
        if (m_BackendLease && RHI::RHIDevice::Get().GetResources().IsInUse(m_BackendLease)) {
            auto& device = RHI::RHIDevice::Get();
            if (!device.WaitIdle() || device.GetResources().IsInUse(m_BackendLease))
                throw std::runtime_error("Native UI storage is retained by an unsubmitted recording");
        }
        if (m_GuiFrame) {
            if (m_GuiFrame->Active) throw std::runtime_error("UI frame is already active");
            if (!m_GuiFrame->Closing) m_GuiFrame->Active = true;
            m_GuiFrame->NativeRecorded = false; m_GuiFrame->PreparedCommands = nullptr;
        }

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
        if (!m_Initialized || !m_GuiFrame || !m_GuiFrame->Active) return;
        if (m_GuiFrame->NativeRecorded) { FinishGuiFrame(); return; }
        UpdateWindowSnapping(); ImGui::Render();
        if (m_BackendType == ImGuiBackendType::OpenGL) {
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); FinishGuiFrame();
        } else if (!m_ComposedPresentation) FinishGuiFrame(); // Explicitly abandoned offscreen draw list.
    }

    void ImGuiLayer::DeferRelease(std::function<void()> release) {
        GetDeferredReleaser()(std::move(release));
    }

    void ImGuiLayer::FinishGuiFrame() noexcept {
        if (!m_GuiFrame || !m_GuiFrame->Active) return;
        m_GuiFrame->Active = false;
        auto uses = std::move(m_GuiFrame->Uses);
        m_GuiFrame->Uses = {};
        auto deferred = std::move(m_GuiFrame->Deferred); m_GuiFrame->Deferred.clear();
        for (auto& release : deferred) m_DeferredReleases.push_back(std::move(release));
        uses.Reset();
        try { FlushDeferredReleases(); }
        catch (...) { RHI::RHIDevice::Get().ObserveResult(nri::Result::FAILURE, "GUIFrameRetirement"); }
    }

    void ImGuiLayer::FlushDeferredReleases() {
        if (m_DeferredReleases.empty() || (m_GuiFrame && m_GuiFrame->Active)) return;
        auto& device = RHI::RHIDevice::Get();
        if (m_BackendType != ImGuiBackendType::OpenGL) {
            auto releases = std::move(m_DeferredReleases); m_DeferredReleases.clear();
            device.GetResources().DeferUntilDrain([releases = std::move(releases)] {
                for (auto& release : releases) release();
            });
            if (device.IsNativeReady()) device.WaitIdle();
        } else {
            while (!m_DeferredReleases.empty()) {
                auto releases = std::move(m_DeferredReleases); m_DeferredReleases.clear();
                for (auto& release : releases) release();
            }
        }
    }

    uint32_t ImGuiLayer::GetAllocatedDescriptorCount() const {
        return m_DescriptorArena ? m_DescriptorArena->Allocated : m_AllocatedDescriptorCount;
    }

    GuiTextureLease ImGuiLayer::RegisterTexture(const RHI::RHITexture& texture) {
        GuiTextureLease token;
        if (!RHI::RHIDevice::Get().CanCreateResources()) return token;
        if (!m_Initialized || !texture.IsValid() || !m_DescriptorArena || m_DescriptorArena->Closing || !m_GuiFrame) return token;
        if (RHI::ScopedRHIFault::Invoke("RegisterGuiTexture", [] { return nri::Result::SUCCESS; }) != nri::Result::SUCCESS) return token;
        if (texture.GetHandle().Epoch != m_DescriptorArena->Lifetime.GetHandle().Epoch) return token;
        uint64_t id = 0;
        try { id = RegisterTextureDescriptor(texture.GetNativeSRVHandle()); }
        catch (const std::exception& error) { AXLE_CORE_WARN("GUI texture registration failed: {0}", error.what()); return token; }
        if (!id) return token;
        auto arena = m_DescriptorArena;
        auto lease = RHI::RHIDevice::Get().GetResources().Publish(RHI::RHIResourceKind::DescriptorSet,
            "GUI texture registration", {}, [arena, id] { arena->Free(id); }, {texture.AcquireLease(), arena->Lifetime});
        if (!lease.IsLive()) { arena->Free(id); return token; }
        auto registration = std::make_shared<GuiTextureRegistration>();
        registration->ID = id; registration->Generation = std::move(lease); registration->Frame = m_GuiFrame;
        token.m_Registration = std::move(registration);
        return token;
    }

    GuiTextureRegistrar ImGuiLayer::GetTextureRegistrar() const {
        std::weak_ptr<ImGuiDescriptorCallbackState> owner = m_DescriptorCallbacks;
        return [owner](const RHI::RHITexture& texture) {
            auto state = owner.lock();
            return state && state->Owner ? state->Owner->RegisterTexture(texture) : GuiTextureLease{};
        };
    }

    GuiDeferredReleaser ImGuiLayer::GetDeferredReleaser() const {
        std::weak_ptr<GuiFrameState> frame = m_GuiFrame;
        auto outside = m_GuiFrame ? m_GuiFrame->RetireOutsideFrame :
            RHI::RHIDevice::Get().GetResources().GetDeferredReleaser();
        return [frame, outside](std::function<void()> release) {
            if (!release) return;
            if (auto state = frame.lock(); state && state->Active && !state->Closing) state->Deferred.push_back(std::move(release));
            else outside(std::move(release));
        };
    }

    bool ImGuiLayer::Render(RHI::RHICommandBuffer* cmdBuffer, nri::Format colorFormat) {
        if (!cmdBuffer || !m_Initialized || !cmdBuffer->IsRecording() || !m_GuiFrame ||
            !m_GuiFrame->Active || m_GuiFrame->NativeRecorded) return false;
        if (!ImGui::GetDrawData() || !ImGui::GetDrawData()->Valid) return false;
        if (m_GuiFrame->PreparedCommands != cmdBuffer->GetNative() && !PrepareGuiTextures()) return false;
        if (m_BackendType != ImGuiBackendType::OpenGL) {
            if (!cmdBuffer->KeepAlive(m_BackendLease)) return false;
            if (!RHI::RHIDevice::Get().GetResources().RecordUses(cmdBuffer->GetNative(), m_GuiFrame->Uses)) return false;
            for (auto* texture : RHI::RHIDevice::Get().GetResources().GetPinnedTextures(m_GuiFrame->Uses))
                cmdBuffer->TransitionTexture(texture, RHI::RHIResourceLayout::ShaderResource, RHI::RHIResourceLayout::ShaderResource);
        }
#if defined(AXLE_HAS_D3D12)
        if (m_BackendType == ImGuiBackendType::Direct3D12) {
            const auto& core = RHI::RHIDevice::Get().GetNRICore();
            auto* nativeCmd = static_cast<ID3D12GraphicsCommandList*>(core.GetCommandBufferNativeObject(cmdBuffer->GetNative()));
            if (!nativeCmd) return false;
            if (colorFormat != nri::Format::RGBA8_UNORM) return false;
            Render(nativeCmd);
            m_GuiFrame->NativeRecorded = true;
            return true;
        }
#endif
#if defined(AXLE_HAS_VULKAN)
        if (m_BackendType == ImGuiBackendType::Vulkan) {
            const auto& core = RHI::RHIDevice::Get().GetNRICore();
            auto vkCmd = static_cast<VkCommandBuffer>(core.GetCommandBufferNativeObject(cmdBuffer->GetNative()));
            if (vkCmd) {
                const auto nativeFormat = static_cast<VkFormat>(nri::nriConvertNRIFormatToVK(colorFormat));
                if (m_VulkanColorFormat != static_cast<uint32_t>(nativeFormat)) {
                    ImGui_ImplVulkan_PipelineInfo info{}; info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
                    info.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                    info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
                    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &nativeFormat;
                    ImGui_ImplVulkan_CreateMainPipeline(&info); m_VulkanColorFormat = static_cast<uint32_t>(nativeFormat);
                }
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmd);
                m_GuiFrame->NativeRecorded = true;
                return true;
            }
        }
#endif
        return false;
    }

    bool ImGuiLayer::PrepareGuiTextures(RHI::RHICommandBuffer* commands) {
        if (m_BackendType == ImGuiBackendType::OpenGL || !m_GuiFrame) return true;
        auto& device = RHI::RHIDevice::Get();
        const auto textures = device.GetResources().GetPinnedTextures(m_GuiFrame->Uses);
        if (commands) {
            for (auto* texture : textures) commands->TransitionTexture(texture, RHI::RHIResourceLayout::Undefined, RHI::RHIResourceLayout::ShaderResource);
            m_GuiFrame->PreparedCommands = commands->GetNative(); return commands->IsRecording();
        }
        if (textures.empty()) return true;
        RHI::RHICommandBuffer prepare;
        if (!prepare.Init() || !prepare.Begin()) return false;
        for (auto* texture : textures)
            prepare.TransitionTexture(texture, RHI::RHIResourceLayout::Undefined, RHI::RHIResourceLayout::ShaderResource);
        return prepare.End() && prepare.Submit() && device.WaitIdle();
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
        if (m_BackendType == ImGuiBackendType::Direct3D12 && m_D3D12SrvHeap && cpuHandlePtr != 0) {
            auto& rhiDevice = RHI::RHIDevice::Get();
            if (!rhiDevice.IsNativeReady()) return 0;

            const auto& core = rhiDevice.GetNRICore();
            auto* d3d12Device = static_cast<ID3D12Device*>(core.GetDeviceNativeObject(rhiDevice.GetNRIDevice()));
            if (!d3d12Device) return 0;

            D3D12_CPU_DESCRIPTOR_HANDLE outCpu = {};
            D3D12_GPU_DESCRIPTOR_HANDLE outGpu = {};
            AllocateD3D12Descriptor(&outCpu, &outGpu);
            if (!outCpu.ptr || !outGpu.ptr) return 0;

            D3D12_CPU_DESCRIPTOR_HANDLE srcCpu = {};
            srcCpu.ptr = cpuHandlePtr;

            d3d12Device->CopyDescriptorsSimple(1, outCpu, srcCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return outGpu.ptr;
        }
#endif
#if defined(AXLE_HAS_VULKAN)
        if (m_BackendType == ImGuiBackendType::Vulkan && cpuHandlePtr != 0) {
            auto imageView = reinterpret_cast<VkImageView>(cpuHandlePtr);
            VkDescriptorSet descSet = ImGui_ImplVulkan_AddTexture(imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            const auto id = reinterpret_cast<uint64_t>(descSet);
            if (id && m_DescriptorArena) m_DescriptorArena->LiveHandles.insert(id);
            return id;
        }
#endif
        (void)cpuHandlePtr;
        return 0;
    }

    void ImGuiLayer::UnregisterTextureDescriptor(uint64_t id) {
        if (!id || !m_DescriptorArena) return;
        auto arena = m_DescriptorArena;
        GetDeferredReleaser()([arena, id] { arena->Free(id); });
        if ((!m_GuiFrame || !m_GuiFrame->Active) && RHI::RHIDevice::Get().IsNativeReady()) RHI::RHIDevice::Get().WaitIdle();
    }

    void ImGuiLayer::AllocateD3D12Descriptor(void* cpu, void* gpu) {
#if defined(AXLE_HAS_D3D12)
        if (!cpu || !gpu) return;
        uint64_t cpuValue = 0, gpuValue = 0;
        if (m_DescriptorArena) m_DescriptorArena->Allocate(cpuValue, gpuValue);
        static_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(cpu)->ptr = cpuValue;
        static_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(gpu)->ptr = gpuValue;
#endif
    }
    void ImGuiLayer::FreeD3D12Descriptor(void* cpu, void* gpu) {
#if defined(AXLE_HAS_D3D12)
        if (gpu && m_DescriptorArena) m_DescriptorArena->Free(static_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(gpu)->ptr);
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
