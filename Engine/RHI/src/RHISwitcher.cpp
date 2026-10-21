#include <Axle/RHI/RHISwitcher.hpp>
#include <algorithm>
#include <cctype>

namespace Axle::RHI {

    bool RHISwitcher::ParseArgument(const std::string& arg, RHIBackendConfig& outConfig) {
        std::string lowerArg = arg;
        std::transform(lowerArg.begin(), lowerArg.end(), lowerArg.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        std::string prefix = "--rhi=";
        size_t pos = lowerArg.find(prefix);
        if (pos == std::string::npos) {
            prefix = "-rhi=";
            pos = lowerArg.find(prefix);
        }
        if (pos == std::string::npos) {
            prefix = "--backend=";
            pos = lowerArg.find(prefix);
        }

        if (pos != std::string::npos) {
            std::string val = lowerArg.substr(pos + prefix.length());
            outConfig.RawArgument = val;

            if (val == "dx12" || val == "d3d12" || val == "direct3d12" || val == "d3d") {
                outConfig.PreferredBackend = RHIType::Direct3D12;
                return true;
            } else if (val == "vk" || val == "vulkan") {
                outConfig.PreferredBackend = RHIType::Vulkan;
                return true;
            } else if (val == "gl" || val == "opengl" || val == "legacy") {
                outConfig.PreferredBackend = RHIType::OpenGL;
                return true;
            } else if (val == "auto" || val == "default") {
#if defined(_WIN32)
                outConfig.PreferredBackend = RHIType::Direct3D12;
#else
                outConfig.PreferredBackend = RHIType::Vulkan;
#endif
                outConfig.AllowFallback = true;
                return true;
            } else {
                AXLE_CORE_WARN("Unknown RHI backend '{0}' requested via CLI; defaulting to Direct3D 12 with fallback.", val);
                outConfig.PreferredBackend = RHIType::Direct3D12;
                outConfig.AllowFallback = true;
                return false;
            }
        }

        if (lowerArg == "--no-fallback" || lowerArg == "--strict-rhi") {
            outConfig.AllowFallback = false;
            return true;
        }

        if (lowerArg == "--rhi-validation" || lowerArg == "--gpu-validation") {
            outConfig.EnableValidation = true;
            return true;
        }

        if (lowerArg.rfind("--adapter=", 0) == 0) {
            try {
                outConfig.AdapterIndex = std::stoi(lowerArg.substr(10));
                return true;
            } catch (...) {}
        }

        return false;
    }

    RHIBackendConfig RHISwitcher::ParseCommandLine(int argc, char** argv) {
        RHIBackendConfig config;
#if defined(_WIN32)
        config.PreferredBackend = RHIType::Direct3D12;
#else
        config.PreferredBackend = RHIType::Vulkan;
#endif
        config.AllowFallback = true;

        for (int i = 1; i < argc; ++i) {
            ParseArgument(argv[i], config);
        }

        return config;
    }

    GraphicsAPI RHISwitcher::ToGraphicsAPI(RHIType type) {
        switch (type) {
            case RHIType::Direct3D12: return GraphicsAPI::Direct3D12;
            case RHIType::Vulkan:     return GraphicsAPI::Vulkan;
            case RHIType::OpenGL:     return GraphicsAPI::OpenGL;
            default:                  return GraphicsAPI::None;
        }
    }

    RHIType RHISwitcher::ToRHIType(GraphicsAPI api) {
        switch (api) {
            case GraphicsAPI::Direct3D12: return RHIType::Direct3D12;
            case GraphicsAPI::Vulkan:     return RHIType::Vulkan;
            case GraphicsAPI::OpenGL:     return RHIType::OpenGL;
            default:                  return RHIType::None;
        }
    }

    bool RHISwitcher::ResolveAndInitialize(const RHIBackendConfig& config) {
        auto& rhiDevice = RHIDevice::Get();
        if (rhiDevice.IsInitialized()) {
            if (rhiDevice.GetBackend() == config.PreferredBackend) {
                return true;
            }
            rhiDevice.Shutdown();
        }

        RHIDeviceDesc desc;
        desc.PreferredBackend = config.PreferredBackend;
        desc.AllowFallback = config.AllowFallback;
        desc.AdapterIndex = config.AdapterIndex;
        desc.EnableValidation = config.EnableValidation;

        AXLE_CORE_INFO("RHISwitcher: Resolving graphics backend. Preferred: '{0}' (Fallback enabled: {1})",
            RHITypeToString(config.PreferredBackend), config.AllowFallback ? "Yes" : "No");

        bool initOk = rhiDevice.Init(desc);
        if (!initOk) {
            AXLE_CORE_ERROR("RHISwitcher: Failed to initialize any backend matching config!");
            return false;
        }

        AXLE_CORE_INFO("RHISwitcher: Backend successfully resolved and initialized to: '{0}'",
            RHITypeToString(rhiDevice.GetBackend()));
        return true;
    }

    RHIType RHISwitcher::GetActiveBackend() {
        return RHIDevice::Get().IsInitialized() ? RHIDevice::Get().GetBackend() : RHIType::None;
    }

    GraphicsAPI RHISwitcher::GetActiveGraphicsAPI() {
        return ToGraphicsAPI(GetActiveBackend());
    }

}
