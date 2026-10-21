#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/Window.hpp>
#include <Axle/Log.hpp>
#include <string>
#include <vector>

namespace Axle::RHI {

    struct RHIBackendConfig {
        RHIType PreferredBackend = RHIType::Direct3D12;
        bool AllowFallback = true;
        int32_t AdapterIndex = -1;
        bool EnableValidation = false;
        std::string RawArgument = "";
    };

    class RHISwitcher {
    public:
        // Parse a CLI argument (e.g. "--rhi=dx12", "--rhi=vulkan", "--rhi=opengl", "--no-fallback", etc.)
        static bool ParseArgument(const std::string& arg, RHIBackendConfig& outConfig);

        // Parse whole command line argument list
        static RHIBackendConfig ParseCommandLine(int argc, char** argv);

        // Bidirectional mapping between RHIType and GraphicsAPI
        static GraphicsAPI ToGraphicsAPI(RHIType type);
        static RHIType ToRHIType(GraphicsAPI api);

        // Resolves requested backend against hardware and initializes RHIDevice with graceful fallback
        // Fallback chain: Direct3D12 -> Vulkan -> OpenGL, or Vulkan -> Direct3D12 -> OpenGL
        static bool ResolveAndInitialize(const RHIBackendConfig& config);

        // Query active backend from RHIDevice
        static RHIType GetActiveBackend();
        static GraphicsAPI GetActiveGraphicsAPI();
    };

}
