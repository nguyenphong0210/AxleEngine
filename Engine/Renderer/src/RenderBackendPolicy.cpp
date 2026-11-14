#include <Axle/RenderBackendPolicy.hpp>
#include <utility>

namespace Axle {
    namespace {
        size_t Index(BackendFeature feature) { return static_cast<size_t>(feature); }
    }

    void RenderBackendPolicy::Set(BackendFeature feature, BackendFeatureState state, std::string reason) {
        m_Features[Index(feature)] = { state, std::move(reason) };
    }

    RenderBackendPolicy RenderBackendPolicy::FromBackend(RendererBackendType backend) {
        RenderBackendPolicy policy;
        switch (backend) {
            case RendererBackendType::ModernRHI_D3D12:
                policy.m_Backend = RHI::RHIType::Direct3D12;
                policy.m_BackendName = "Direct3D 12";
                policy.m_QualityTier = BackendQualityTier::Standard;
                break;
            case RendererBackendType::ModernRHI_Vulkan:
                policy.m_Backend = RHI::RHIType::Vulkan;
                policy.m_BackendName = "Vulkan 1.3";
                policy.m_QualityTier = BackendQualityTier::Standard;
                break;
            case RendererBackendType::OpenGL_Compatibility:
                policy.m_Backend = RHI::RHIType::OpenGL;
                policy.m_BackendName = "OpenGL 4.5 Compatibility";
                policy.m_QualityTier = BackendQualityTier::Compatibility;
                break;
            case RendererBackendType::None:
            default:
                policy.m_Backend = RHI::RHIType::None;
                policy.m_BackendName = "None";
                policy.m_QualityTier = BackendQualityTier::None;
                break;
        }

        const bool modern = policy.m_Backend == RHI::RHIType::Direct3D12 || policy.m_Backend == RHI::RHIType::Vulkan;
        policy.Set(BackendFeature::Compute, modern ? BackendFeatureState::Native : BackendFeatureState::Fallback,
                   modern ? "native compute path" : "compatibility backend has no renderer compute contract");
        policy.Set(BackendFeature::StorageTextures, modern ? BackendFeatureState::Native : BackendFeatureState::Disabled,
                   modern ? "native UAV/storage texture path" : "storage textures unavailable on compatibility backend");
        policy.Set(BackendFeature::TimestampQueries, modern ? BackendFeatureState::Native : BackendFeatureState::Unavailable,
                   modern ? "native timestamp query path" : "GPU timestamps unavailable on compatibility backend");
        policy.Set(BackendFeature::DescriptorSets, modern ? BackendFeatureState::Native : BackendFeatureState::Fallback,
                   modern ? "native descriptor-set path" : "legacy descriptor binding path");
        policy.Set(BackendFeature::HDRFormats, modern ? BackendFeatureState::Native : BackendFeatureState::Fallback,
                   modern ? "floating-point HDR targets" : "SDR framebuffer only");
        policy.Set(BackendFeature::MSAA, modern ? BackendFeatureState::Native : BackendFeatureState::Fallback,
                   modern ? "native multisample targets" : "single-sample compatibility target");
        policy.Set(BackendFeature::FXAA, modern ? BackendFeatureState::Native : BackendFeatureState::Fallback,
                   modern ? "native post-process FXAA" : "FXAA pass unavailable on compatibility renderer");
        policy.Set(BackendFeature::RayTracing, BackendFeatureState::Fallback,
                   "ray tracing is optional; SSR/IBL fallback remains authoritative");

        policy.m_PassCapabilities.NativeRHI = modern;
        policy.m_PassCapabilities.Compute = modern;
        policy.m_PassCapabilities.StorageTextures = modern;
        policy.m_PassCapabilities.Timestamps = modern;
        policy.m_PassCapabilities.RayTracing = false;
        return policy;
    }

    RenderBackendPolicy RenderBackendPolicy::FromDevice(const RHI::RHIDeviceCapabilities& capabilities) {
        RendererBackendType backend = RendererBackendType::None;
        switch (capabilities.ActiveBackend) {
            case RHI::RHIType::Direct3D12: backend = RendererBackendType::ModernRHI_D3D12; break;
            case RHI::RHIType::Vulkan: backend = RendererBackendType::ModernRHI_Vulkan; break;
            case RHI::RHIType::OpenGL: backend = RendererBackendType::OpenGL_Compatibility; break;
            case RHI::RHIType::None: default: break;
        }
        auto policy = FromBackend(backend);
        const bool modern = policy.m_Backend == RHI::RHIType::Direct3D12 || policy.m_Backend == RHI::RHIType::Vulkan;
        const bool compute = modern && capabilities.SupportsComputeShaders;
        const bool timestamps = modern && capabilities.SupportsTimestampQueries;
        const bool descriptors = modern && capabilities.SupportsDescriptorSets;
        const bool storage = modern && capabilities.SupportsStorageTextures;
        policy.Set(BackendFeature::Compute, compute ? BackendFeatureState::Native : BackendFeatureState::Disabled,
                   compute ? "device exposes compute workgroups" : "device exposes no usable compute workgroups");
        policy.Set(BackendFeature::StorageTextures, storage ? BackendFeatureState::Native : BackendFeatureState::Disabled,
                   storage ? "device exposes compute UAV/storage path" : "device exposes no usable storage texture path");
        policy.Set(BackendFeature::TimestampQueries, timestamps ? BackendFeatureState::Native : BackendFeatureState::Unavailable,
                   timestamps ? "timestamp frequency is available" : "timestamp frequency is unavailable");
        policy.Set(BackendFeature::DescriptorSets, descriptors ? BackendFeatureState::Native : BackendFeatureState::Fallback,
                   descriptors ? "native descriptor sets are available" : "descriptor sets use compatibility bindings");
        policy.Set(BackendFeature::HDRFormats, capabilities.SupportsHDRFormats ? BackendFeatureState::Native : BackendFeatureState::Fallback,
                   capabilities.SupportsHDRFormats ? "device exposes floating-point HDR formats" : "device exposes SDR formats only");
        policy.Set(BackendFeature::MSAA, capabilities.SupportsMSAATargets ? BackendFeatureState::Native : BackendFeatureState::Fallback,
                   capabilities.SupportsMSAATargets ? "device exposes multisample targets" : "device exposes single-sample targets only");
        // Hardware RT is deliberately not advertised as a renderer pass until
        // the M15 ray-tracing integration owns acceleration structures and
        // frame scheduling. Hardware presence therefore remains a fallback
        // fact, never a false M14 feature claim.
        policy.Set(BackendFeature::RayTracing, BackendFeatureState::Fallback,
                   capabilities.IsRayTracingSupported ? "hardware RT present but M14 pass is not enabled; use SSR/IBL" :
                                                         "hardware RT unavailable; use SSR/IBL");
        policy.m_PassCapabilities.Compute = compute;
        policy.m_PassCapabilities.StorageTextures = storage;
        policy.m_PassCapabilities.Timestamps = timestamps;
        policy.m_PassCapabilities.RayTracing = false;
        if (!modern) policy.m_QualityTier = BackendQualityTier::Compatibility;
        else if (compute && storage) policy.m_QualityTier = BackendQualityTier::Standard;
        return policy;
    }

    BackendFeatureDecision RenderBackendPolicy::GetDecision(BackendFeature feature) const { return m_Features[Index(feature)]; }
    bool RenderBackendPolicy::Supports(BackendFeature feature) const { return GetDecision(feature).State == BackendFeatureState::Native; }

    SceneRenderSettings RenderBackendPolicy::ResolveSettings(const SceneRenderSettings& authored) const {
        SceneRenderSettings resolved = authored;
        if (!Supports(BackendFeature::MSAA)) resolved.MSAASamples = 1;
        if (!Supports(BackendFeature::HDRFormats)) {
            resolved.EnableToneMapping = false;
            resolved.EnableBloom = false;
            resolved.EnableAutoExposure = false;
        }
        if (!Supports(BackendFeature::FXAA)) resolved.EnableFXAA = false;
        if (!Supports(BackendFeature::Compute) || !Supports(BackendFeature::StorageTextures)) {
            resolved.EnableGTAO = false;
            resolved.EnableContactShadows = false;
            resolved.EnableSSR = false;
            resolved.EnableVolumetricFog = false;
            resolved.EnableDDGI = false;
            resolved.EnableBloom = false;
            resolved.EnableAutoExposure = false;
        }
        if (m_QualityTier == BackendQualityTier::Compatibility) resolved.EnableTAA = false;
        return resolved;
    }

    std::vector<std::string> RenderBackendPolicy::CollectFallbacks(const SceneRenderSettings& authored) const {
        const auto resolved = ResolveSettings(authored);
        std::vector<std::string> reasons;
        if (authored.MSAASamples != resolved.MSAASamples) reasons.push_back("msaa_disabled:single_sample_target");
        if (authored.EnableToneMapping && !resolved.EnableToneMapping) reasons.push_back("tone_mapping_disabled:sdr_output");
        if (authored.EnableBloom && !resolved.EnableBloom) reasons.push_back("bloom_disabled:compute_or_hdr_unavailable");
        if (authored.EnableAutoExposure && !resolved.EnableAutoExposure) reasons.push_back("auto_exposure_disabled:compute_or_hdr_unavailable");
        if (authored.EnableFXAA && !resolved.EnableFXAA) reasons.push_back("fxaa_disabled:backend_unsupported");
        if (authored.EnableTAA && !resolved.EnableTAA) reasons.push_back("taa_disabled:compatibility_backend");
        if (authored.EnableGTAO && !resolved.EnableGTAO) reasons.push_back("gtao_disabled:compute_or_storage_unavailable");
        if (authored.EnableContactShadows && !resolved.EnableContactShadows) reasons.push_back("contact_shadows_disabled:compute_or_storage_unavailable");
        if (authored.EnableSSR && !resolved.EnableSSR) reasons.push_back("ssr_disabled:compute_or_storage_unavailable");
        if (authored.EnableVolumetricFog && !resolved.EnableVolumetricFog) reasons.push_back("volumetric_fog_disabled:compute_or_storage_unavailable");
        if (authored.EnableDDGI && !resolved.EnableDDGI) reasons.push_back("ddgi_disabled:compute_or_storage_unavailable");
        return reasons;
    }
}
