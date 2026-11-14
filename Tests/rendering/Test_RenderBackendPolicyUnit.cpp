#include "RenderTestCommon.hpp"
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHISwitcher.hpp>
#include <Axle/RenderBackendPolicy.hpp>

namespace Axle::RenderTests {

    static bool Test_ParseArgumentCLI() {
        RHI::RHIBackendConfig cfg;

        // D3D12 variants
        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--rhi=dx12", cfg), "parse --rhi=dx12");
        RENDER_CHECK(cfg.PreferredBackend == RHI::RHIType::Direct3D12, "backend is Direct3D12");

        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("-rhi=d3d12", cfg), "parse -rhi=d3d12");
        RENDER_CHECK(cfg.PreferredBackend == RHI::RHIType::Direct3D12, "backend is Direct3D12");

        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--backend=direct3d12", cfg), "parse --backend=direct3d12");
        RENDER_CHECK(cfg.PreferredBackend == RHI::RHIType::Direct3D12, "backend is Direct3D12");

        // Vulkan variants
        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--rhi=vulkan", cfg), "parse --rhi=vulkan");
        RENDER_CHECK(cfg.PreferredBackend == RHI::RHIType::Vulkan, "backend is Vulkan");

        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--rhi=vk", cfg), "parse --rhi=vk");
        RENDER_CHECK(cfg.PreferredBackend == RHI::RHIType::Vulkan, "backend is Vulkan");

        // OpenGL variants
        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--rhi=opengl", cfg), "parse --rhi=opengl");
        RENDER_CHECK(cfg.PreferredBackend == RHI::RHIType::OpenGL, "backend is OpenGL");

        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--rhi=gl", cfg), "parse --rhi=gl");
        RENDER_CHECK(cfg.PreferredBackend == RHI::RHIType::OpenGL, "backend is OpenGL");

        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--rhi=legacy", cfg), "parse --rhi=legacy");
        RENDER_CHECK(cfg.PreferredBackend == RHI::RHIType::OpenGL, "backend is OpenGL");

        // Auto / Default
        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--rhi=auto", cfg), "parse --rhi=auto");
        RENDER_CHECK(cfg.AllowFallback == true, "auto enables fallback");

        // Fallback controls
        cfg.AllowFallback = true;
        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--no-fallback", cfg), "parse --no-fallback");
        RENDER_CHECK(cfg.AllowFallback == false, "--no-fallback disables fallback");

        cfg.AllowFallback = true;
        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--strict-rhi", cfg), "parse --strict-rhi");
        RENDER_CHECK(cfg.AllowFallback == false, "--strict-rhi disables fallback");

        // Validation flags
        cfg.EnableValidation = false;
        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--rhi-validation", cfg), "parse --rhi-validation");
        RENDER_CHECK(cfg.EnableValidation == true, "validation enabled");

        cfg.EnableValidation = false;
        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--gpu-validation", cfg), "parse --gpu-validation");
        RENDER_CHECK(cfg.EnableValidation == true, "validation enabled");

        // Adapter index
        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--adapter=3", cfg), "parse --adapter=3");
        RENDER_CHECK(cfg.AdapterIndex == 3, "adapter index is 3");

        // Unknown / Invalid
        bool invalidResult = RHI::RHISwitcher::ParseArgument("--rhi=unsupported_api", cfg);
        RENDER_CHECK(!invalidResult, "invalid backend argument returns false");
        RENDER_CHECK(cfg.AllowFallback == true, "invalid argument defaults to fallback allowed");

        return true;
    }

    static bool Test_ParseCommandLine() {
        // Test simulated multi-argument invocation
        const char* argv1[] = { "AxleEditor.exe", "--rhi=vulkan", "--no-fallback", "--rhi-validation", "--adapter=1" };
        int argc1 = sizeof(argv1) / sizeof(argv1[0]);

        auto cfg1 = RHI::RHISwitcher::ParseCommandLine(argc1, const_cast<char**>(argv1));
        RENDER_CHECK(cfg1.PreferredBackend == RHI::RHIType::Vulkan, "preferred Vulkan");
        RENDER_CHECK(cfg1.AllowFallback == false, "no fallback");
        RENDER_CHECK(cfg1.EnableValidation == true, "validation on");
        RENDER_CHECK(cfg1.AdapterIndex == 1, "adapter 1");

        const char* argv2[] = { "AxleRuntime.exe", "--rhi=opengl", "--strict-rhi" };
        int argc2 = sizeof(argv2) / sizeof(argv2[0]);

        auto cfg2 = RHI::RHISwitcher::ParseCommandLine(argc2, const_cast<char**>(argv2));
        RENDER_CHECK(cfg2.PreferredBackend == RHI::RHIType::OpenGL, "preferred OpenGL");
        RENDER_CHECK(cfg2.AllowFallback == false, "strict rhi");

        return true;
    }

    static bool Test_DeviceDescPolicyContracts() {
        RHI::RHIDeviceDesc defaultDesc;
        RENDER_CHECK(defaultDesc.PreferredBackend == RHI::RHIType::Direct3D12, "default preferred D3D12 on Windows");
        RENDER_CHECK(defaultDesc.AllowFallback == true, "default allows fallback");
        RENDER_CHECK(defaultDesc.EnableValidation == false, "default validation false");
        RENDER_CHECK(defaultDesc.AdapterIndex == -1, "default adapter -1 (auto)");
        RENDER_CHECK(defaultDesc.PreferSoftware == false, "default prefer software false");
        RENDER_CHECK(defaultDesc.Headless == false, "default headless false");

        return true;
    }

    static bool Test_RenderBackendFeaturePolicy() {
        auto gl = RenderBackendPolicy::FromBackend(RendererBackendType::OpenGL_Compatibility);
        RENDER_CHECK(gl.GetQualityTier() == BackendQualityTier::Compatibility &&
                     !gl.Supports(BackendFeature::Compute) &&
                     gl.GetDecision(BackendFeature::TimestampQueries).State == BackendFeatureState::Unavailable,
                     "OpenGL compatibility policy exposes an intentionally reduced feature set");
        SceneRenderSettings authored;
        authored.EnableToneMapping = true;
        authored.EnableBloom = true;
        authored.EnableAutoExposure = true;
        authored.EnableTAA = true;
        authored.EnableGTAO = true;
        authored.EnableContactShadows = true;
        authored.EnableSSR = true;
        authored.EnableVolumetricFog = true;
        authored.EnableDDGI = true;
        authored.EnableFXAA = true;
        authored.MSAASamples = 4;
        const auto resolvedGL = gl.ResolveSettings(authored);
        RENDER_CHECK(resolvedGL.MSAASamples == 1 && !resolvedGL.EnableToneMapping && !resolvedGL.EnableBloom &&
                     !resolvedGL.EnableAutoExposure && !resolvedGL.EnableTAA && !resolvedGL.EnableFXAA &&
                     !resolvedGL.EnableGTAO && !resolvedGL.EnableSSR && !resolvedGL.EnableDDGI,
                     "OpenGL fallback resolves unsupported post and compute features without mutating authored settings");
        RENDER_CHECK(gl.CollectFallbacks(authored).size() >= 7, "OpenGL fallback reports explicit feature reasons");

        RHI::RHIDeviceCapabilities native;
        native.ActiveBackend = RHI::RHIType::Vulkan;
        native.MaxComputeWorkGroupInvocations = 1024;
        native.TimestampFrequencyHz = 1000000;
        native.SupportsComputeShaders = true;
        native.SupportsStorageTextures = true;
        native.SupportsTimestampQueries = true;
        native.SupportsDescriptorSets = true;
        native.SupportsHDRFormats = true;
        native.SupportsMSAATargets = true;
        native.IsRayTracingSupported = true;
        const auto vk = RenderBackendPolicy::FromDevice(native);
        RENDER_CHECK(vk.GetQualityTier() == BackendQualityTier::Standard &&
                     vk.Supports(BackendFeature::Compute) && vk.Supports(BackendFeature::StorageTextures) &&
                     vk.Supports(BackendFeature::TimestampQueries) &&
                     vk.GetDecision(BackendFeature::RayTracing).State == BackendFeatureState::Fallback &&
                     vk.GetPassCapabilities().NativeRHI && !vk.GetPassCapabilities().RayTracing,
                     "native Vulkan device facts enable supported pass capabilities without falsely enabling deferred RT");

        native.MaxComputeWorkGroupInvocations = 0;
        native.TimestampFrequencyHz = 0;
        native.SupportsComputeShaders = false;
        native.SupportsStorageTextures = false;
        native.SupportsTimestampQueries = false;
        native.IsRayTracingSupported = false;
        const auto degraded = RenderBackendPolicy::FromDevice(native);
        const auto resolvedDegraded = degraded.ResolveSettings(authored);
        RENDER_CHECK(!degraded.Supports(BackendFeature::Compute) &&
                     degraded.GetDecision(BackendFeature::TimestampQueries).State == BackendFeatureState::Unavailable &&
                     !resolvedDegraded.EnableGTAO && !resolvedDegraded.EnableSSR,
                     "partial native capability loss disables only dependent passes and reports unavailable telemetry");
        return true;
    }

} // namespace Axle::RenderTests

bool RunRenderBackendPolicyUnitTests() {
    std::cout << "  [RenderPolicy] Running backend policy unit tests...\n";
    if (!Axle::RenderTests::Test_ParseArgumentCLI()) return false;
    if (!Axle::RenderTests::Test_ParseCommandLine()) return false;
    if (!Axle::RenderTests::Test_DeviceDescPolicyContracts()) return false;
    if (!Axle::RenderTests::Test_RenderBackendFeaturePolicy()) return false;
    std::cout << "  [RenderPolicy] All backend policy unit tests passed.\n";
    return true;
}
