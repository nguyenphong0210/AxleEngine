#pragma once
#include <Axle/Types.hpp>
#include <string>

namespace Axle {

    enum class RendererBackendType : uint8_t {
        ModernRHI_D3D12,
        ModernRHI_Vulkan,
        OpenGL_Compatibility,
        None
    };

    struct RendererCapabilities {
        RendererBackendType Backend = RendererBackendType::None;
        std::string BackendName = "None";
        bool SupportsMSAA = false;
        uint8_t MaxMSAASamples = 1;
        bool SupportsHDR = false;
        bool SupportsToneMapping = false;
        bool SupportsFXAA = false;
        bool SupportsCascadedShadows = false;
        bool SupportsGPUSkinning = false;
        bool SupportsCustomHLSLShaders = false;
        bool SupportsRefractionCopy = false;
        bool SupportsComputeShaders = false;
        bool SupportsAsyncCompute = false;
        bool SupportsRayTracing = false;
        bool SupportsWireframeMode = true;

        // Diagnostics for unsupported features
        std::string AdvancedFeatureDiagnostic;
    };

    RendererCapabilities QueryRendererCapabilities(RendererBackendType backend);

}
