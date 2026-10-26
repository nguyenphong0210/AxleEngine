#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/Log.hpp>

#include <cstring>
#include <algorithm>

namespace Axle::RHI {

    static void NRI_CALL NriMessageCallback(nri::Message messageType, const char* file, uint32_t line, const char* message, void* userArg) {
        (void)userArg;
        if (messageType == nri::Message::ERROR) {
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
            Shutdown();
        }

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
            tryOrder = { RHIType::Direct3D12, RHIType::Vulkan, RHIType::OpenGL };
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

        m_Initialized = true;
        return true;
    }

    bool RHIDevice::TryInitNRI(RHIType backend, const RHIDeviceDesc& desc, const nri::AdapterDesc* adapterDesc) {
        nri::DeviceCreationDesc creationDesc = {};
        if (backend == RHIType::Direct3D12) {
            creationDesc.graphicsAPI = nri::GraphicsAPI::D3D12;
        } else if (backend == RHIType::Vulkan) {
            creationDesc.graphicsAPI = nri::GraphicsAPI::VK;
        } else {
            return false;
        }

        creationDesc.enableNRIValidation = desc.EnableValidation;
        creationDesc.enableGraphicsAPIValidation = desc.EnableValidation;
        creationDesc.adapterDesc = adapterDesc;

        // Callback interface for diagnostic logging
        nri::CallbackInterface callbackInterface = {};
        callbackInterface.MessageCallback = NriMessageCallback;
        creationDesc.callbackInterface = callbackInterface;

        // Standard Vulkan binding register offsets for HLSL SPIR-V compatibility
        if (backend == RHIType::Vulkan) {
            creationDesc.vkBindingOffsets.sRegister = 0;
            creationDesc.vkBindingOffsets.tRegister = 0;
            creationDesc.vkBindingOffsets.bRegister = 0;
            creationDesc.vkBindingOffsets.uRegister = 0;
        }

        nri::Device* device = nullptr;
        nri::Result res = nri::nriCreateDevice(creationDesc, device);
        if (res != nri::Result::SUCCESS || !device) {
            return false;
        }

        m_Device = device;

        // Retrieve Core, Helper, and Streamer interfaces
        res = nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::CoreInterface), (void*)&m_NRI.Core);
        if (res != nri::Result::SUCCESS) {
            AXLE_CORE_ERROR("Failed to query nri::CoreInterface!");
            nri::nriDestroyDevice(m_Device);
            m_Device = nullptr;
            return false;
        }

        res = nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::HelperInterface), (void*)&m_NRI.Helper);
        if (res != nri::Result::SUCCESS) {
            AXLE_CORE_WARN("Failed to query nri::HelperInterface (some memory utilities may be unavailable).");
        }

        res = nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::StreamerInterface), (void*)&m_NRI.Streamer);
        if (res != nri::Result::SUCCESS) {
            AXLE_CORE_WARN("Failed to query nri::StreamerInterface (staging streaming may be unavailable).");
        }

        res = nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::SwapChainInterface), (void*)&m_NRI.SwapChain);
        if (res != nri::Result::SUCCESS) {
            AXLE_CORE_WARN("Failed to query nri::SwapChainInterface (presentation may be unavailable).");
        }

        res = m_NRI.Core.GetQueue(*m_Device, nri::QueueType::GRAPHICS, 0, m_GraphicsQueue);
        if (res != nri::Result::SUCCESS || !m_GraphicsQueue) {
            AXLE_CORE_ERROR("Failed to obtain primary Graphics Queue!");
        }

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

    void RHIDevice::WaitIdle() {
        if (m_Initialized && m_GraphicsQueue && m_NRI.Core.QueueWaitIdle) {
            m_NRI.Core.QueueWaitIdle(m_GraphicsQueue);
        }
    }

    void RHIDevice::Shutdown() {
        if (!m_Initialized) return;

        AXLE_CORE_INFO("Shutting down RHI Device ({0})...", RHITypeToString(m_Capabilities.ActiveBackend));

        WaitIdle();

        RHIPipelineCache::Get().Clear();

        RHIMaterial::ReleaseDefaultTextures();

        if (m_Device) {
            nri::nriDestroyDevice(m_Device);
            m_Device = nullptr;
        }

        m_GraphicsQueue = nullptr;
        m_NRI = {};
        m_Capabilities = {};
        m_Adapters.clear();
        m_Initialized = false;

        AXLE_CORE_INFO("RHI Device successfully shut down.");
    }

}
