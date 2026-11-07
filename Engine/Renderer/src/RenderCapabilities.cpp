#include <Axle/RenderCapabilities.hpp>

namespace Axle {

    RendererCapabilities QueryRendererCapabilities(RendererBackendType backend) {
        RendererCapabilities caps;
        caps.Backend = backend;

        switch (backend) {
            case RendererBackendType::ModernRHI_D3D12:
                caps.BackendName = "Direct3D 12 (Modern RHI)";
                caps.SupportsMSAA = true;
                caps.MaxMSAASamples = 8;
                caps.SupportsHDR = true;
                caps.SupportsToneMapping = true;
                caps.SupportsFXAA = true;
                caps.SupportsCascadedShadows = true;
                caps.SupportsGPUSkinning = true;
                caps.SupportsCustomHLSLShaders = true;
                caps.SupportsRefractionCopy = true;
                caps.SupportsComputeShaders = true;
                caps.SupportsAsyncCompute = true;
                caps.SupportsRayTracing = false;
                caps.SupportsWireframeMode = false;
                caps.AdvancedFeatureDiagnostic.clear();
                break;

            case RendererBackendType::ModernRHI_Vulkan:
                caps.BackendName = "Vulkan (Modern RHI)";
                caps.SupportsMSAA = true;
                caps.MaxMSAASamples = 8;
                caps.SupportsHDR = true;
                caps.SupportsToneMapping = true;
                caps.SupportsFXAA = true;
                caps.SupportsCascadedShadows = true;
                caps.SupportsGPUSkinning = true;
                caps.SupportsCustomHLSLShaders = true;
                caps.SupportsRefractionCopy = true;
                caps.SupportsComputeShaders = true;
                caps.SupportsAsyncCompute = true;
                caps.SupportsRayTracing = false;
                caps.SupportsWireframeMode = false;
                caps.AdvancedFeatureDiagnostic.clear();
                break;

            case RendererBackendType::OpenGL_Compatibility:
                caps.BackendName = "OpenGL Compatibility";
                caps.SupportsMSAA = true;
                caps.MaxMSAASamples = 4;
                caps.SupportsHDR = false;
                caps.SupportsToneMapping = false;
                caps.SupportsFXAA = false;
                caps.SupportsCascadedShadows = true;
                caps.SupportsGPUSkinning = true;
                caps.SupportsCustomHLSLShaders = false;
                caps.SupportsRefractionCopy = false;
                caps.SupportsComputeShaders = false;
                caps.SupportsAsyncCompute = false;
                caps.SupportsRayTracing = false;
                caps.SupportsWireframeMode = true;
                caps.AdvancedFeatureDiagnostic = "OpenGL Compatibility: Advanced passes (HDR floating-point targets, FXAA post-processing, screen-space refraction copies, custom HLSL shaders) are not supported. Authored scene values are preserved and rendered using standard GL PBR forward shading.";
                break;

            case RendererBackendType::None:
            default:
                caps.BackendName = "None";
                caps.AdvancedFeatureDiagnostic = "No rendering backend available";
                break;
        }

        return caps;
    }

}
