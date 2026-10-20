#pragma once
#include <Axle/Types.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace Axle::RHI {

    enum class RHIType {
        None = 0,
        Direct3D12,
        Vulkan,
        OpenGL // Legacy fallback
    };

    inline const char* RHITypeToString(RHIType type) {
        switch (type) {
            case RHIType::Direct3D12: return "Direct3D 12";
            case RHIType::Vulkan:     return "Vulkan 1.3";
            case RHIType::OpenGL:     return "OpenGL 4.5";
            default:                  return "None";
        }
    }

    inline RHIType StringToRHIType(const std::string& str) {
        if (str == "dx12" || str == "d3d12" || str == "Direct3D12" || str == "Direct3D 12") {
            return RHIType::Direct3D12;
        }
        if (str == "vk" || str == "vulkan" || str == "Vulkan" || str == "Vulkan 1.3") {
            return RHIType::Vulkan;
        }
        if (str == "gl" || str == "opengl" || str == "OpenGL" || str == "OpenGL 4.5") {
            return RHIType::OpenGL;
        }
        return RHIType::None;
    }

    struct RHIAdapterInfo {
        std::string Name;
        uint64_t DedicatedVideoMemory = 0;
        uint64_t SharedSystemMemory = 0;
        uint32_t DeviceId = 0;
        uint32_t VendorId = 0;
        std::string VendorName;
        bool IsDiscreteGPU = false;
        bool SupportsD3D12 = false;
        bool SupportsVulkan = false;
    };

    struct RHIDeviceCapabilities {
        std::string AdapterName;
        RHIType ActiveBackend = RHIType::None;
        uint64_t DedicatedVideoMemory = 0;

        // Modern hardware features
        bool IsRayTracingSupported = false;
        uint32_t RayTracingTier = 0; // DXR 1.0 (Tier 1), DXR 1.1 (Tier 2), etc.

        bool IsMeshShaderSupported = false;

        bool IsBindlessSupported = false;
        uint32_t BindlessTier = 0; // Tier 1 (unbounded arrays), Tier 2 (SM 6.6 dynamic resources)

        bool IsConservativeRasterSupported = false;
        uint32_t ConservativeRasterTier = 0;

        bool IsEnhancedBarriersSupported = false;
        bool IsDescriptorHeapSupported = false;

        // Resource & Compute Limits
        uint32_t MaxTextureDimension2D = 0;
        uint32_t MaxComputeWorkGroupInvocations = 0;
        uint32_t MaxComputeSharedMemorySize = 0;
    };

    struct RHIDeviceDesc {
        RHIType PreferredBackend = RHIType::Direct3D12;
        bool EnableValidation = false;
        bool AllowFallback = true;
        int32_t AdapterIndex = -1; // -1 = automatically select best discrete GPU
    };

}
