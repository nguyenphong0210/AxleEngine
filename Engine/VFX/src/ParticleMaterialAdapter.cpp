#include <Axle/ParticleMaterialAdapter.hpp>
#include <Axle/FileSystem.hpp>
#include <filesystem>

namespace Axle {

    EffectiveParticleRenderSettings ParticleMaterialAdapter::Resolve(
        const ParticleEmitterConfig& config,
        const EmitterResources& resources,
        MaterialLoaderFunc materialLoader)
    {
        EffectiveParticleRenderSettings settings;

        // 1. Establish baseline from authored emitter configuration and resources
        settings.BaseColor = glm::vec4(1.0f);
        settings.TexturePath = resources.TexturePath;
        settings.NormalMapPath = resources.NormalMapPath;
        settings.MaterialPath = resources.MaterialPath;
        settings.BlendMode = config.BlendMode;
        settings.AlphaCutoff = 0.5f;
        settings.DepthWrite = (config.BlendMode == ParticleBlendMode::Cutout);
        settings.UseTexture = resources.UseTexture || !resources.TexturePath.empty();
        settings.UseNormalMap = resources.UseNormalMap || !resources.NormalMapPath.empty();

        settings.LightingEnabled = config.LightingEnabled;
        settings.WrapLighting = config.WrapLighting;
        settings.ScatteringFactor = config.ScatteringFactor;
        settings.NormalMapStrength = config.NormalMapStrength;
        settings.EmissiveColor = glm::vec3(0.0f);
        settings.EmissiveIntensity = 0.0f;
        settings.UVScale = glm::vec2(1.0f);
        settings.UVOffset = glm::vec2(0.0f);

        // 2. If no material is assigned, return baseline config behavior
        if (resources.MaterialPath.empty()) {
            return settings;
        }

        // 3. Resolve referenced material
        std::shared_ptr<Material> mat = nullptr;
        if (materialLoader) {
            mat = materialLoader(resources.MaterialPath);
        } else {
            auto resolved = FileSystem::ResolveAssetPath(resources.MaterialPath);
            if (std::filesystem::exists(resolved)) {
                mat = std::make_shared<Material>(resolved.stem().string());
                if (!mat->Load(resolved)) {
                    mat.reset();
                }
            }
        }

        if (!mat) {
            VFXDiagnostic diag;
            diag.Severity = VFXDiagnosticSeverity::Warning;
            diag.Code = "VFX_RESOURCE_MISSING";
            diag.Message = "Referenced material asset not found or unreadable: " + resources.MaterialPath;
            settings.Diagnostics.push_back(std::move(diag));
            return settings;
        }

        // 4. Extract particle-compatible material properties
        settings.BaseColor = mat->GetAlbedoColor();

        if (mat->IsAlbedoMapEnabled() && !mat->GetAlbedoPath().empty()) {
            settings.TexturePath = mat->GetAlbedoPath();
            settings.UseTexture = true;
        }

        if (mat->IsNormalMapEnabled() && !mat->GetNormalPath().empty()) {
            settings.NormalMapPath = mat->GetNormalPath();
            settings.UseNormalMap = true;
            settings.NormalMapStrength = mat->GetNormalStrength();
        }

        settings.EmissiveColor = mat->GetEmissionColor();
        settings.EmissiveIntensity = mat->GetEmissionIntensity();
        settings.UVScale = mat->GetUVScale();
        settings.UVOffset = mat->GetUVOffset();

        // 5. Surface & Cutout mapping
        switch (mat->GetSurfaceType()) {
            case SurfaceType::Cutout:
                settings.BlendMode = ParticleBlendMode::Cutout;
                settings.AlphaCutoff = mat->GetAlphaCutoff();
                settings.DepthWrite = true;
                break;
            case SurfaceType::Additive:
                settings.BlendMode = ParticleBlendMode::Additive;
                settings.DepthWrite = false;
                break;
            case SurfaceType::Transparent:
                settings.BlendMode = ParticleBlendMode::AlphaBlend;
                settings.DepthWrite = false;
                break;
            case SurfaceType::Opaque:
                settings.BlendMode = ParticleBlendMode::Cutout;
                settings.AlphaCutoff = 0.0f; // All fragments pass alpha test
                settings.DepthWrite = true;
                break;
        }

        // 6. Diagnostics for unsupported custom shader or advanced PBR features
        if (mat->GetUseCustomShader()) {
            VFXDiagnostic diag;
            diag.Severity = VFXDiagnosticSeverity::Info;
            diag.Code = "VFX_UNSUPPORTED_FEATURE";
            diag.Message = "Material uses a custom shader override which is unsupported in the particle billboard pass; using particle lighting.";
            settings.Diagnostics.push_back(std::move(diag));
        }

        if (mat->GetClearcoatStrength() > 0.0f || mat->GetRefractionEnabled()) {
            VFXDiagnostic diag;
            diag.Severity = VFXDiagnosticSeverity::Info;
            diag.Code = "VFX_UNSUPPORTED_FEATURE";
            diag.Message = "Material uses advanced PBR features (clearcoat/refraction) unsupported on particles; using base color and emission.";
            settings.Diagnostics.push_back(std::move(diag));
        }

        return settings;
    }

}
