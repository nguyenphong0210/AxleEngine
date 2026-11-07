#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/Log.hpp>

#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>

namespace Axle::RHI {
    static std::atomic<uint64_t> s_ValidationErrors{0};
    uint64_t GetRHIValidationErrorCount() noexcept { return s_ValidationErrors.load(); }

    static void NRI_CALL NriMessageCallback(nri::Message messageType, const char* file, uint32_t line, const char* message, void* userArg) {
        (void)userArg;
        if (messageType == nri::Message::ERROR) {
            s_ValidationErrors.fetch_add(1);
            AXLE_CORE_ERROR("[NRI Error] ({0}:{1}) {2}", file ? file : "", line, message ? message : "");
        } else if (messageType == nri::Message::WARNING) {
            AXLE_CORE_WARN("[NRI Warning] ({0}:{1}) {2}", file ? file : "", line, message ? message : "");
        } else {
            AXLE_CORE_TRACE("[NRI Info] {0}", message ? message : "");
        }
    }

    static std::string GetVendorName(nri::Vendor vendor) {
        switch (vendor) {
            case nri::Vendor::NVIDIA:   return "NVIDIA";
            case nri::Vendor::AMD:      return "AMD";
            case nri::Vendor::INTEL:    return "Intel";
            default:                    return "Unknown Vendor";
        }
    }

    RHIDevice& RHIDevice::Get() {
        static RHIDevice s_Instance;
        return s_Instance;
    }

    RHIDevice::~RHIDevice() {
        if (m_Initialized) {
            Shutdown();
        }
    }

    std::vector<RHIAdapterInfo> RHIDevice::EnumerateAdapters() {
        uint32_t adapterNum = 0;
        nri::Result res = nri::nriEnumerateAdapters(nullptr, adapterNum);
        if (res != nri::Result::SUCCESS || adapterNum == 0) {
            return {};
        }

        std::vector<nri::AdapterDesc> nriAdapters(adapterNum);
        res = nri::nriEnumerateAdapters(nriAdapters.data(), adapterNum);
        if (res != nri::Result::SUCCESS) {
            return {};
        }

        std::vector<RHIAdapterInfo> result;
        result.reserve(adapterNum);

        for (uint32_t i = 0; i < adapterNum; ++i) {
            const auto& src = nriAdapters[i];
            RHIAdapterInfo info;
            info.Name = src.name;
            info.DedicatedVideoMemory = src.videoMemorySize;
            info.SharedSystemMemory = src.sharedSystemMemorySize;
            info.DeviceId = src.deviceId;
            info.VendorId = static_cast<uint32_t>(src.vendor);
            info.VendorName = GetVendorName(src.vendor);
            info.IsDiscreteGPU = (src.architecture == nri::Architecture::DISCRETE);
            info.IsIntegrated = (src.architecture == nri::Architecture::INTEGRATED);
            info.IsSoftware = (src.architecture == nri::Architecture::SOFTWARE);
            info.SupportsD3D12 = ((src.supportedGraphicsAPIs & nri::GraphicsAPI::D3D12) != 0);
            info.SupportsVulkan = ((src.supportedGraphicsAPIs & nri::GraphicsAPI::VK) != 0);

            result.push_back(info);
        }
        return result;
    }

    int32_t RHIDevice::FindSoftwareAdapterIndex(RHIType backend) {
        uint32_t nriAdapterNum = 0;
        nri::nriEnumerateAdapters(nullptr, nriAdapterNum);
        if (nriAdapterNum == 0) return -1;
        std::vector<nri::AdapterDesc> nriAdapters(nriAdapterNum);
        nri::nriEnumerateAdapters(nriAdapters.data(), nriAdapterNum);

        nri::GraphicsAPI targetApi = nri::GraphicsAPI::NONE;
        if (backend == RHIType::Direct3D12) targetApi = nri::GraphicsAPI::D3D12;
        else if (backend == RHIType::Vulkan) targetApi = nri::GraphicsAPI::VK;

        for (uint32_t i = 0; i < nriAdapterNum; ++i) {
            if (nriAdapters[i].architecture == nri::Architecture::SOFTWARE) {
                if (targetApi == nri::GraphicsAPI::NONE || (nriAdapters[i].supportedGraphicsAPIs & targetApi) != 0) {
                    return static_cast<int32_t>(i);
                }
            }
        }
        return -1;
    }

    int32_t RHIDevice::FindDiscreteAdapterIndex(RHIType backend) {
        uint32_t nriAdapterNum = 0;
        nri::nriEnumerateAdapters(nullptr, nriAdapterNum);
        if (nriAdapterNum == 0) return -1;
        std::vector<nri::AdapterDesc> nriAdapters(nriAdapterNum);
        nri::nriEnumerateAdapters(nriAdapters.data(), nriAdapterNum);

        nri::GraphicsAPI targetApi = nri::GraphicsAPI::NONE;
        if (backend == RHIType::Direct3D12) targetApi = nri::GraphicsAPI::D3D12;
        else if (backend == RHIType::Vulkan) targetApi = nri::GraphicsAPI::VK;

        for (uint32_t i = 0; i < nriAdapterNum; ++i) {
            if (nriAdapters[i].architecture == nri::Architecture::DISCRETE) {
                if (targetApi == nri::GraphicsAPI::NONE || (nriAdapters[i].supportedGraphicsAPIs & targetApi) != 0) {
                    return static_cast<int32_t>(i);
                }
            }
        }
        return -1;
    }

    bool RHIDevice::Init(const RHIDeviceDesc& desc) {
        if (m_Initialized) {
            AXLE_CORE_WARN("RHIDevice is already initialized. Shutting down previous instance before re-initialization.");
            if (!Shutdown()) return false;
        }

        m_FirstError.reset();
        m_State = RHIDeviceState::Uninitialized;
        m_Desc = desc;
        m_Adapters = EnumerateAdapters();

        AXLE_CORE_INFO("==================================================");
        AXLE_CORE_INFO(" Initializing Axle RHI Subsystem (NVIDIA NRI)");
        AXLE_CORE_INFO("==================================================");
        AXLE_CORE_INFO("Enumerated {0} physical GPU adapter(s):", m_Adapters.size());
        for (size_t i = 0; i < m_Adapters.size(); ++i) {
            const auto& a = m_Adapters[i];
            AXLE_CORE_INFO("  [{0}] {1} (VRAM: {2} MB, Discrete: {3}, Software: {4}, D3D12: {5}, Vulkan: {6})",
                i, a.Name, a.DedicatedVideoMemory / (1024 * 1024),
                a.IsDiscreteGPU ? "Yes" : "No",
                a.IsSoftware ? "Yes" : "No",
                a.SupportsD3D12 ? "Yes" : "No",
                a.SupportsVulkan ? "Yes" : "No");
        }

        // Fetch native NRI adapter descriptors
        uint32_t nriAdapterNum = 0;
        nri::nriEnumerateAdapters(nullptr, nriAdapterNum);
        std::vector<nri::AdapterDesc> nriAdapters(nriAdapterNum);
        if (nriAdapterNum > 0) {
            nri::nriEnumerateAdapters(nriAdapters.data(), nriAdapterNum);
        }

        const nri::AdapterDesc* selectedAdapter = nullptr;
        if (!nriAdapters.empty()) {
            if (desc.AdapterIndex >= 0 && desc.AdapterIndex < static_cast<int32_t>(nriAdapters.size())) {
                selectedAdapter = &nriAdapters[desc.AdapterIndex];
            } else if (desc.PreferSoftware) {
                nri::GraphicsAPI targetApi = (desc.PreferredBackend == RHIType::Vulkan) 
                    ? nri::GraphicsAPI::VK 
                    : nri::GraphicsAPI::D3D12;
                for (size_t i = 0; i < nriAdapters.size(); ++i) {
                    if (nriAdapters[i].architecture == nri::Architecture::SOFTWARE &&
                        (nriAdapters[i].supportedGraphicsAPIs & targetApi) != 0) {
                        selectedAdapter = &nriAdapters[i];
                        break;
                    }
                }
                if (!selectedAdapter) {
                    for (size_t i = 0; i < nriAdapters.size(); ++i) {
                        if (nriAdapters[i].architecture == nri::Architecture::SOFTWARE) {
                            selectedAdapter = &nriAdapters[i];
                            break;
                        }
                    }
                }
                if (selectedAdapter) {
                    AXLE_CORE_INFO("PreferSoftware active: selected software adapter [{0}]", selectedAdapter->name);
                } else {
                    AXLE_CORE_WARN("PreferSoftware requested, but no software adapter available. Using default adapter.");
                }
            }
            if (!selectedAdapter) {
                selectedAdapter = &nriAdapters[0]; // Already sorted by NRI: discrete GPU with highest VRAM first
            }
        }

        // Determine fallback order based on PreferredBackend
        std::vector<RHIType> tryOrder;
        if (desc.PreferredBackend == RHIType::Direct3D12) {
            tryOrder.push_back(RHIType::Direct3D12);
            if (desc.AllowFallback) {
                tryOrder.push_back(RHIType::Vulkan);
                tryOrder.push_back(RHIType::OpenGL);
            }
        } else if (desc.PreferredBackend == RHIType::Vulkan) {
            tryOrder.push_back(RHIType::Vulkan);
            if (desc.AllowFallback) {
                tryOrder.push_back(RHIType::Direct3D12);
                tryOrder.push_back(RHIType::OpenGL);
            }
        } else if (desc.PreferredBackend == RHIType::OpenGL) {
            tryOrder.push_back(RHIType::OpenGL);
        } else {
            // None or unspecified default
            tryOrder = { RHIType::Direct3D12 };
            if (desc.AllowFallback) {
                tryOrder.push_back(RHIType::Vulkan);
                tryOrder.push_back(RHIType::OpenGL);
            }
        }

        bool success = false;
        for (RHIType backend : tryOrder) {
            if (backend == RHIType::OpenGL) {
                InitOpenGLFallback();
                success = true;
                break;
            }

            AXLE_CORE_INFO("Attempting to initialize RHI backend: {0}...", RHITypeToString(backend));
            if (TryInitNRI(backend, desc, selectedAdapter)) {
                success = true;
                break;
            } else {
                AXLE_CORE_WARN("Failed to initialize backend {0}. Trying next fallback...", RHITypeToString(backend));
            }
        }

        if (!success) {
            AXLE_CORE_CRITICAL("Failed to initialize any RHI graphics backend!");
            return false;
        }

        const auto native = m_Device;
        if (!m_Resources.Open([native] { if (native) nri::nriDestroyDevice(native); })) return false;
        m_Initialized = true;
        m_State = RHIDeviceState::Ready;
        return true;
    }

    bool RHIDevice::TryInitNRI(RHIType backend, const RHIDeviceDesc& desc, const nri::AdapterDesc* adapterDesc) {
        if (desc.EnableValidation && !IsGraphicsAPIValidationAvailable(backend)) {
            AXLE_CORE_ERROR("Backend {0}: requested graphics API validation is unavailable", RHITypeToString(backend));
            return false;
        }
        nri::DeviceCreationDesc creationDesc = {};
        if (backend == RHIType::Direct3D12) {
            creationDesc.graphicsAPI = nri::GraphicsAPI::D3D12;
        } else if (backend == RHIType::Vulkan) {
            creationDesc.graphicsAPI = nri::GraphicsAPI::VK;
        } else {
            return false;
        }

        creationDesc.enableNRIValidation = desc.EnableValidation || desc.EnableNRIValidation;
        creationDesc.enableGraphicsAPIValidation = desc.EnableValidation;
        creationDesc.adapterDesc = adapterDesc;

        // Callback interface for diagnostic logging
        nri::CallbackInterface callbackInterface = {};
        callbackInterface.MessageCallback = NriMessageCallback;
        // NRI's default calls DebugBreak even for recoverable startup failures.
        // Keep the diagnostic callback and consume the returned operation result.
        callbackInterface.AbortExecution = [](void*) {};
        creationDesc.callbackInterface = callbackInterface;

        // Standard Vulkan binding register offsets for HLSL SPIR-V compatibility
        // Shift samplers (sRegister = 128) to prevent binding index collisions with textures (t0..t127) in shared spaces
        if (backend == RHIType::Vulkan) {
            creationDesc.vkBindingOffsets.sRegister = 128;
            creationDesc.vkBindingOffsets.tRegister = 0;
            creationDesc.vkBindingOffsets.bRegister = 0;
            creationDesc.vkBindingOffsets.uRegister = 0;
        }

        nri::Device* device = nullptr;
        NRIInterfaces interfaces;
        nri::Queue* queue = nullptr;
        auto res = ScopedRHIFault::Invoke("CreateDevice", [&] { return nri::nriCreateDevice(creationDesc, device); });
        if (res != nri::Result::SUCCESS || !device) {
            if (device) nri::nriDestroyDevice(device);
            return false;
        }
        auto reject = [&](const char* operation) {
            AXLE_CORE_ERROR("Backend {0}: required {1} unavailable", RHITypeToString(backend), operation);
            nri::nriDestroyDevice(device);
            return false;
        };
        // No candidate state is visible to resource callers until every required
        // interface and queue is usable. Current upload paths require both helpers.
        res = ScopedRHIFault::Invoke("CoreInterface", [&] { return nri::nriGetInterface(*device, NRI_INTERFACE(nri::CoreInterface), &interfaces.Core); });
        if (res != nri::Result::SUCCESS || !interfaces.Core.GetQueue || !interfaces.Core.GetDeviceDesc ||
            !interfaces.Core.CreateTexture || !interfaces.Core.CreateBuffer || !interfaces.Core.QueueSubmit ||
            !interfaces.Core.CreateCommittedTexture || !interfaces.Core.CreateCommittedBuffer ||
            !interfaces.Core.CreateTextureView || !interfaces.Core.CreateBufferView ||
            !interfaces.Core.DestroyTexture || !interfaces.Core.DestroyBuffer || !interfaces.Core.DestroyDescriptor ||
            !interfaces.Core.GetFormatSupport || !interfaces.Core.GetBufferDesc ||
            !interfaces.Core.GetTextureMemoryDesc || !interfaces.Core.GetBufferMemoryDesc ||
            !interfaces.Core.AllocateMemory || !interfaces.Core.FreeMemory ||
            !interfaces.Core.BindTextureMemory || !interfaces.Core.BindBufferMemory ||
            !interfaces.Core.MapBuffer || !interfaces.Core.UnmapBuffer || !interfaces.Core.GetFenceValue || !interfaces.Core.CreateFence ||
            !interfaces.Core.GetDeviceNativeObject || !interfaces.Core.GetQueueNativeObject)
            return reject("CoreInterface");
        res = ScopedRHIFault::Invoke("HelperInterface", [&] { return nri::nriGetInterface(*device, NRI_INTERFACE(nri::HelperInterface), &interfaces.Helper); });
        if (res != nri::Result::SUCCESS || !interfaces.Helper.AllocateAndBindMemory || !interfaces.Helper.UploadData)
            return reject("HelperInterface");
        res = ScopedRHIFault::Invoke("StreamerInterface", [&] { return nri::nriGetInterface(*device, NRI_INTERFACE(nri::StreamerInterface), &interfaces.Streamer); });
        if (res != nri::Result::SUCCESS || !interfaces.Streamer.CreateStreamer)
            return reject("StreamerInterface");
        res = ScopedRHIFault::Invoke("SwapChainInterface", [&] { return nri::nriGetInterface(*device, NRI_INTERFACE(nri::SwapChainInterface), &interfaces.SwapChain); });
        if (!desc.Headless && (res != nri::Result::SUCCESS || !interfaces.SwapChain.CreateSwapChain || !interfaces.SwapChain.QueuePresent))
            return reject("SwapChainInterface");
        if (res != nri::Result::SUCCESS) interfaces.SwapChain = {};
        if (backend == RHIType::Vulkan) {
            res = ScopedRHIFault::Invoke("WrapperVKInterface", [&] { return nri::nriGetInterface(*device, NRI_INTERFACE(nri::WrapperVKInterface), &interfaces.WrapperVK); });
            if (!desc.Headless && (res != nri::Result::SUCCESS || !interfaces.WrapperVK.GetInstanceVK ||
                !interfaces.WrapperVK.GetPhysicalDeviceVK || !interfaces.WrapperVK.GetInstanceProcAddrVK || !interfaces.WrapperVK.GetQueueFamilyIndexVK))
                return reject("WrapperVKInterface");
            if (res != nri::Result::SUCCESS) interfaces.WrapperVK = {};
        }
        res = ScopedRHIFault::Invoke("GetGraphicsQueue", [&] { return interfaces.Core.GetQueue(*device, nri::QueueType::GRAPHICS, 0, queue); });
        if (res != nri::Result::SUCCESS || !queue) return reject("GraphicsQueue");
        m_Device = device;
        m_NRI = interfaces;
        m_GraphicsQueue = queue;
        m_Capabilities.ActiveBackend = backend;
        PopulateCapabilities();
        return true;
    }

    void RHIDevice::InitOpenGLFallback() {
        m_Capabilities = {};
        m_Capabilities.ActiveBackend = RHIType::OpenGL;
        m_Capabilities.AdapterName = !m_Adapters.empty() ? m_Adapters[0].Name : "OpenGL 4.5 Core Compatibility";
        m_Capabilities.DedicatedVideoMemory = !m_Adapters.empty() ? m_Adapters[0].DedicatedVideoMemory : (2ULL << 30);

        // Baseline OpenGL 4.5 capabilities
        m_Capabilities.IsSoftwareAdapter = false;
        m_Capabilities.IsHeadless = m_Desc.Headless;
        m_Capabilities.IsRayTracingSupported = false;
        m_Capabilities.RayTracingTier = 0;
        m_Capabilities.IsMeshShaderSupported = false;
        m_Capabilities.IsBindlessSupported = false;
        m_Capabilities.BindlessTier = 0;
        m_Capabilities.IsConservativeRasterSupported = false;
        m_Capabilities.ConservativeRasterTier = 0;
        m_Capabilities.IsEnhancedBarriersSupported = false;
        m_Capabilities.IsDescriptorHeapSupported = false;

        m_Capabilities.MaxTextureDimension2D = 16384;
        m_Capabilities.MaxComputeWorkGroupInvocations = 1024;
        m_Capabilities.MaxComputeSharedMemorySize = 32768;

        AXLE_CORE_WARN("==================================================");
        AXLE_CORE_WARN(" RHI Active Backend: OpenGL 4.5 (Legacy Fallback)");
        AXLE_CORE_WARN(" Modern features (Ray Tracing, Mesh Shaders) are unavailable.");
        AXLE_CORE_WARN("==================================================");
    }

    void RHIDevice::PopulateCapabilities() {
        if (!m_Device) return;

        const nri::DeviceDesc& desc = m_NRI.Core.GetDeviceDesc(*m_Device);

        m_Capabilities.AdapterName = desc.adapterDesc.name;
        m_Capabilities.DedicatedVideoMemory = desc.adapterDesc.videoMemorySize;
        m_Capabilities.IsSoftwareAdapter = (desc.adapterDesc.architecture == nri::Architecture::SOFTWARE);
        m_Capabilities.IsHeadless = m_Desc.Headless;

        // Hardware feature query
        m_Capabilities.IsRayTracingSupported = (desc.tiers.rayTracing > 0);
        m_Capabilities.RayTracingTier = desc.tiers.rayTracing;

        m_Capabilities.IsMeshShaderSupported = desc.features.meshShader;

        m_Capabilities.IsBindlessSupported = (desc.tiers.bindless > 0);
        m_Capabilities.BindlessTier = desc.tiers.bindless;

        m_Capabilities.IsConservativeRasterSupported = (desc.tiers.conservativeRaster > 0);
        m_Capabilities.ConservativeRasterTier = desc.tiers.conservativeRaster;

        m_Capabilities.IsEnhancedBarriersSupported = desc.features.enhancedBarriers;
        m_Capabilities.IsDescriptorHeapSupported = desc.features.descriptorHeap;

        m_Capabilities.MaxTextureDimension2D = desc.dimensions.texture2DMaxDim;
        m_Capabilities.MaxComputeWorkGroupInvocations = desc.shaderStage.compute.workGroupInvocationMaxNum;
        m_Capabilities.MaxComputeSharedMemorySize = desc.shaderStage.compute.sharedMemoryMaxSize;

        AXLE_CORE_INFO("==================================================");
        AXLE_CORE_INFO(" RHI Active Backend: {0}", RHITypeToString(m_Capabilities.ActiveBackend));
        AXLE_CORE_INFO("  Active GPU: {0}", m_Capabilities.AdapterName);
        AXLE_CORE_INFO("  Dedicated VRAM: {0} MB", m_Capabilities.DedicatedVideoMemory / (1024 * 1024));
        AXLE_CORE_INFO("  Hardware Ray Tracing: {0} (Tier {1})",
            m_Capabilities.IsRayTracingSupported ? "YES" : "NO", m_Capabilities.RayTracingTier);
        AXLE_CORE_INFO("  Hardware Mesh Shaders: {0}",
            m_Capabilities.IsMeshShaderSupported ? "YES" : "NO");
        AXLE_CORE_INFO("  Bindless Resources: {0} (Tier {1})",
            m_Capabilities.IsBindlessSupported ? "YES" : "NO", m_Capabilities.BindlessTier);
        AXLE_CORE_INFO("  Conservative Rasterization: {0} (Tier {1})",
            m_Capabilities.IsConservativeRasterSupported ? "YES" : "NO", m_Capabilities.ConservativeRasterTier);
        AXLE_CORE_INFO("  Enhanced Resource Barriers: {0}",
            m_Capabilities.IsEnhancedBarriersSupported ? "YES" : "NO");
        AXLE_CORE_INFO("  Max Texture 2D: {0}x{0}", m_Capabilities.MaxTextureDimension2D);
        AXLE_CORE_INFO("  Max Compute Group Invocations: {0}", m_Capabilities.MaxComputeWorkGroupInvocations);
        AXLE_CORE_INFO("  Max Compute Shared Memory: {0} KB", m_Capabilities.MaxComputeSharedMemorySize / 1024);
        AXLE_CORE_INFO("==================================================");
    }

    void ReportRHIFailure(nri::Result result, std::string_view operation, std::string_view resource) {
        RHIDevice::Get().ObserveResult(result, operation, resource);
    }

    bool IsRHIOperationAllowed(std::string_view operation) {
        auto& device = RHIDevice::Get();
        if (operation == "QueueDrain" || operation == "CreateIdleFence" || operation == "WaitFence") return true;
        return device.GetState() != RHIDeviceState::Lost && device.GetState() != RHIDeviceState::Stopping;
    }

    bool RHIDevice::ObserveResult(nri::Result result, std::string_view operation, std::string_view resource, bool timedOut) {
        if (result == nri::Result::SUCCESS) {
            if (operation == "QueueSubmit" || operation == "QueueDrain") m_UnresolvedWork = true;
            return true;
        }
        if (result == nri::Result::NOT_READY && !timedOut) return false;
        const bool ambiguous = operation == "QueueSubmit" || operation == "QueueDrain";
        if (ambiguous) m_UnresolvedWork = true;
        if (!m_FirstError) m_FirstError = RHIGPUError{result, GetBackend() == RHIType::None ? m_Desc.PreferredBackend : GetBackend(), std::string(operation), std::string(resource), ambiguous, timedOut};
        if (m_Initialized) {
            if (result == nri::Result::DEVICE_LOST) m_State = RHIDeviceState::Lost;
            else if (m_State != RHIDeviceState::Lost) {
                if (ambiguous || timedOut || operation == "QueuePresent") m_State = RHIDeviceState::Stopping;
                if (result == nri::Result::OUT_OF_DATE && (operation == "AcquireNextTexture" || operation == "QueuePresent")) m_State = RHIDeviceState::SurfaceRebuildRequired;
            }
            if (m_State == RHIDeviceState::Lost || m_State == RHIDeviceState::Stopping) m_ContextReady = false;
        }
        return false;
    }

    bool RHIDevice::WaitForFence(nri::Fence* fence, uint64_t value, uint32_t timeoutMilliseconds) {
        if (!value) return true;
        if (!m_Resources.IsOwnerThread()) return false;
        if (!IsNativeReady() || !fence || m_State == RHIDeviceState::Lost) return false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
        do {
            uint64_t completed = 0;
            const auto result = ScopedRHIFault::Invoke("WaitFence", [&] {
                completed = m_NRI.Core.GetFenceValue(*fence);
                return completed == UINT64_MAX ? nri::Result::DEVICE_LOST : nri::Result::SUCCESS;
            });
            if (result != nri::Result::SUCCESS) return false;
            if (completed >= value) return true;
            if (std::chrono::steady_clock::now() >= deadline) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (true);
        return ObserveResult(nri::Result::NOT_READY, "WaitFence", "completion deadline", true);
    }

    bool RHIDevice::WaitIdle(uint32_t timeoutMilliseconds) {
        if (!m_Device) return true;
        if (!m_Resources.IsOwnerThread()) return false;
        if (!IsNativeReady() || m_State == RHIDeviceState::Lost) return false;
        if (!m_IdleFence) {
            const auto result = ScopedRHIFault::Invoke("CreateIdleFence", [&] { return m_NRI.Core.CreateFence(*m_Device, 0, m_IdleFence); });
            if (result != nri::Result::SUCCESS || !m_IdleFence) return false;
        }
        nri::FenceSubmitDesc signal{};
        signal.fence = m_IdleFence; signal.value = m_IdleValue + 1; signal.stages = nri::StageBits::ALL;
        nri::QueueSubmitDesc submit{}; submit.signalFences = &signal; submit.signalFenceNum = 1;
        const auto result = ScopedRHIFault::Invoke("QueueDrain", [&] { return m_NRI.Core.QueueSubmit(*m_GraphicsQueue, submit); });
        if (result != nri::Result::SUCCESS) return false;
        m_IdleValue = signal.value;
        if (!WaitForFence(m_IdleFence, m_IdleValue, timeoutMilliseconds)) return false;
        m_UnresolvedWork = false;
        m_PendingSubmissions.clear();
        m_AmbiguousSubmissions.clear();
        while (!m_DeferredCleanup.empty()) {
            auto releases = std::move(m_DeferredCleanup);
            m_DeferredCleanup.clear();
            for (auto& release : releases) release();
        }
        m_Resources.CollectAfterDrain();
        return true;
    }

    bool RHIDevice::Submit(const nri::QueueSubmitDesc& desc, nri::Queue* queue, RHICompletionToken* completion, RHIRecordingLease externalUses) {
        if (completion) *completion = {};
        if (!CanSubmit() || (queue && queue != m_GraphicsQueue) || m_SubmittedValue == UINT64_MAX ||
            (desc.commandBufferNum && !desc.commandBuffers) || (desc.signalFenceNum && !desc.signalFences) ||
            (desc.waitFenceNum && !desc.waitFences) || (!externalUses.Empty() && !externalUses.IsLive())) return false;
        if (!queue) queue = m_GraphicsQueue;
        std::vector<const void*> commands;
        for (uint32_t i = 0; i < desc.commandBufferNum; ++i) commands.push_back(desc.commandBuffers[i]);
        RHIRecordingLease uses = std::move(externalUses);
        if (!m_Resources.SnapshotSubmission(commands.data(), desc.commandBufferNum, uses)) return false;
        auto states = m_Resources.PrepareSubmissionStates(commands.data(), desc.commandBufferNum);
        if (!states) return ObserveResult(nri::Result::INVALID_ARGUMENT, "SubmitResourceStates", "stale recording state");
        if (!m_SubmissionFence) {
            const auto result = ScopedRHIFault::Invoke("CreateSubmissionFence", [&] { return m_NRI.Core.CreateFence(*m_Device, 0, m_SubmissionFence); });
            if (result != nri::Result::SUCCESS || !m_SubmissionFence) return false;
        }
        std::vector<nri::FenceSubmitDesc> signals;
        if (desc.signalFenceNum) signals.assign(desc.signalFences, desc.signalFences + desc.signalFenceNum);
        nri::FenceSubmitDesc signal{};
        signal.fence = m_SubmissionFence; signal.value = m_SubmittedValue + 1; signal.stages = nri::StageBits::ALL;
        signals.push_back(signal);
        m_PendingSubmissions.reserve(m_PendingSubmissions.size() + 1);
        m_AmbiguousSubmissions.reserve(m_AmbiguousSubmissions.size() + 1);
        auto submit = desc; submit.signalFences = signals.data(); submit.signalFenceNum = static_cast<uint32_t>(signals.size());
        const auto result = ScopedRHIFault::Invoke("QueueSubmit", [&] { return m_NRI.Core.QueueSubmit(*queue, submit); });
        if (result != nri::Result::SUCCESS) {
            m_AmbiguousSubmissions.push_back(std::move(uses));
            return false;
        }
        m_SubmittedValue = signal.value;
        RHICompletionToken token{GetEpoch(), signal.value};
        m_PendingSubmissions.push_back({token, std::move(uses)});
        m_Resources.CommitSubmissionStates(states);
        m_Resources.MarkSubmitted(commands.data(), desc.commandBufferNum);
        if (completion) *completion = token;
        return true;
    }

    bool RHIDevice::RetainExternalSubmission(RHIRecordingLease uses, RHICompletionToken* completion) {
        if (completion) *completion = {};
        if (uses.Empty()) return true;
        // Native UI submission may already have happened even when its result or
        // subsequent present fails. Keep the pins on every failure path.
        if (!CanSubmit()) { m_AmbiguousSubmissions.push_back(std::move(uses)); return false; }
        auto retained = uses;
        nri::QueueSubmitDesc submit{};
        if (!Submit(submit, nullptr, completion, std::move(uses))) {
            m_AmbiguousSubmissions.push_back(std::move(retained));
            ObserveResult(nri::Result::FAILURE, "QueueSubmit", "external completion tracking");
            return false;
        }
        return true;
    }

    bool RHIDevice::WaitForCompletion(RHICompletionToken token, uint32_t timeoutMilliseconds) {
        if (!token || token.Epoch != GetEpoch() || token.Value > m_SubmittedValue || !m_SubmissionFence) return false;
        if (!WaitForFence(m_SubmissionFence, token.Value, timeoutMilliseconds)) return false;
        // Keep conservative whole-queue reuse until P41-08 accounts for every arena.
        return WaitIdle(timeoutMilliseconds);
    }

    nri::Result RHIDevice::UploadData(const nri::TextureUploadDesc* textures, uint32_t textureNum,
        const nri::BufferUploadDesc* buffers, uint32_t bufferNum) {
        if (!CanCreateResources() || (textureNum && !textures) || (bufferNum && !buffers)) return nri::Result::INVALID_ARGUMENT;
        for (uint32_t i = 0; i < textureNum; ++i) {
            auto lease = m_Resources.AcquireNative(textures[i].texture);
            if (!lease.IsLive() || m_Resources.IsInUse(lease) || !m_Resources.GetTextureState(lease.GetHandle())) return nri::Result::INVALID_ARGUMENT;
        }
        for (uint32_t i = 0; i < bufferNum; ++i) {
            auto lease = m_Resources.AcquireNative(buffers[i].buffer);
            if (!lease.IsLive() || m_Resources.IsInUse(lease) || !m_Resources.GetBufferState(lease.GetHandle())) return nri::Result::INVALID_ARGUMENT;
        }
        const auto result = ScopedRHIFault::Invoke("UploadData", [&] {
            return m_NRI.Helper.UploadData(*m_GraphicsQueue, textures, textureNum, buffers, bufferNum);
        });
        if (result != nri::Result::SUCCESS) return result;
        for (uint32_t i = 0; i < textureNum; ++i)
            if (!m_Resources.SetTextureState(m_Resources.AcquireNative(textures[i].texture), textures[i].after,
                0, nri::REMAINING, 0, nri::REMAINING, textures[i].planes)) return nri::Result::FAILURE;
        for (uint32_t i = 0; i < bufferNum; ++i)
            if (!m_Resources.SetBufferState(m_Resources.AcquireNative(buffers[i].buffer), buffers[i].after)) return nri::Result::FAILURE;
        return nri::Result::SUCCESS;
    }

    void RHIDevice::ReleaseNative(std::function<void()> release) {
        if (m_UnresolvedWork && m_State == RHIDeviceState::Stopping) m_DeferredCleanup.push_back(std::move(release));
        else release();
    }

    bool RHIDevice::Shutdown() {
        if (!m_Initialized) return true;
        if (!m_Resources.IsOwnerThread()) return false;

        AXLE_CORE_INFO("Shutting down RHI Device ({0})...", RHITypeToString(m_Capabilities.ActiveBackend));

        if (m_State != RHIDeviceState::Lost && !WaitIdle()) {
            m_State = m_State == RHIDeviceState::Lost ? RHIDeviceState::Lost : RHIDeviceState::Stopping;
            if (m_State != RHIDeviceState::Lost) {
                AXLE_CORE_ERROR("RHI shutdown retained native owners: queue completion is unresolved");
                return false;
            }
        }
        m_UnresolvedWork = false;
        m_PendingSubmissions.clear();
        m_AmbiguousSubmissions.clear();
        while (!m_DeferredCleanup.empty()) {
            auto releases = std::move(m_DeferredCleanup); m_DeferredCleanup.clear();
            for (auto& release : releases) release();
        }
        m_State = m_State == RHIDeviceState::Lost ? RHIDeviceState::Lost : RHIDeviceState::Stopping;

        m_Resources.PrepareDeviceClose();
        RHIPipelineCache::Get().Clear();

        RHIMaterial::ReleaseDefaultTextures();

        if (!WaitIdle() && m_State != RHIDeviceState::Lost) return false;

        if (m_IdleFence) { m_NRI.Core.DestroyFence(m_IdleFence); m_IdleFence = nullptr; }
        if (m_SubmissionFence) { m_NRI.Core.DestroyFence(m_SubmissionFence); m_SubmissionFence = nullptr; }
        m_SubmittedValue = 0;
        m_IdleValue = 0;
        m_Resources.CloseAfterDrain();
        m_Device = nullptr;

        m_GraphicsQueue = nullptr;
        m_NRI = {};
        m_Capabilities = {};
        m_Adapters.clear();
        m_Initialized = false;
        m_ContextReady = false;
        m_State = RHIDeviceState::Uninitialized;

        AXLE_CORE_INFO("RHI Device successfully shut down.");
        return true;
    }

}
