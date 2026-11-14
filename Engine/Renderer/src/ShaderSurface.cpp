#include <Axle/ShaderSurface.hpp>
#include <cmath>
#include <type_traits>

namespace Axle {
namespace {
    uint32_t Unsupported(const ResolvedSurface& surface) {
        uint32_t mask = 0;
        auto add = [&](bool requested, SurfaceFeature feature) { if (requested) mask |= static_cast<uint32_t>(feature); };
        add(surface.ClearcoatStrength > 0, SurfaceFeature::Clearcoat);
        add(surface.AnisotropyStrength != 0, SurfaceFeature::Anisotropy);
        add(surface.SheenEnabled, SurfaceFeature::Sheen);
        add(surface.SubsurfaceEnabled, SurfaceFeature::Subsurface);
        add(surface.AbsorptionEnabled, SurfaceFeature::Absorption);
        add(surface.RefractionEnabled, SurfaceFeature::Refraction);
        add(surface.TextureFlags & ((1u << static_cast<uint32_t>(RHI::RHITextureSlot::DetailNormal)) |
                                   (1u << static_cast<uint32_t>(RHI::RHITextureSlot::DetailMask))), SurfaceFeature::DetailNormal);
        add(surface.NormalStrength != 1, SurfaceFeature::NormalStrength);
        add(!surface.EnableSpecularHighlights, SurfaceFeature::SpecularPolicy);
        add(!surface.EnableEnvironmentReflections, SurfaceFeature::ReflectionPolicy);
        add(surface.Surface == SurfaceType::Cutout, SurfaceFeature::Cutout);
        add(surface.EmissionIntensity != 0 && surface.EmissionColor != glm::vec3(0), SurfaceFeature::Emission);
        add(surface.TextureFlags & ~1u, SurfaceFeature::NonBaseTextures);
        return mask;
    }
    bool Finite(const ResolvedMaterialProperty& property) {
        return std::visit([](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, float>) return std::isfinite(value);
            else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>) {
                for (glm::length_t i = 0; i < value.length(); ++i) if (!std::isfinite(value[i])) return false;
            }
            return true;
        }, property.Value);
    }
}
    ShaderSurfaceResult ResolveShaderSurface(const ResolvedSurface& base,
        SurfaceEvaluation evaluation, uint64_t shaderGeneration,
        const MaterialBindingPlan& plan, const EngineMaterialValues& view,
        std::span<const ShaderPropertyInstance> savedValues) {
        if (!base.Valid || !base.Generation || base.Evaluation != SurfaceEvaluation::BuiltIn)
            return MaterialBindingError{"SurfaceInvalid", {}, {}, "Shader material requires a valid canonical base surface"};
        if (!shaderGeneration || (evaluation != SurfaceEvaluation::CustomShader && evaluation != SurfaceEvaluation::MaterialGraph))
            return MaterialBindingError{"SurfaceProgram", {}, {}, "Shader material requires an immutable shader generation and evaluation domain"};
        auto properties = ResolveMaterialProperties(plan, savedValues);
        if (const auto* error = std::get_if<MaterialBindingError>(&properties)) return *error;
        const auto& resolved = std::get<ResolvedMaterialProperties>(properties);
        for (const auto& property : resolved.Properties) if (!Finite(property))
            return MaterialBindingError{"SurfacePropertyFinite", property.Key.BufferName, property.Key.VariableName,
                "Shader material property must be finite"};

        EngineMaterialValues engine = view;
        engine.BaseColor = base.BaseColor;
        engine.BaseUVTransform = glm::vec4(base.UVScale, base.UVOffset);
        engine.LegacySurfaceParams = {base.Roughness, base.Metallic, base.AO, base.UVScale.x};
        auto constants = PackMaterialUniforms(plan, engine, resolved);
        if (const auto* error = std::get_if<MaterialBindingError>(&constants)) return *error;
        EffectiveMaterialValues values;
        values.BaseMapPath = base.Textures[0].Path;
        values.UseBaseMap = (base.TextureFlags & 1u) != 0;
        values.Properties.assign(savedValues.begin(), savedValues.end());
        auto textures = ResolveMaterialTextureRequests(plan, values);
        if (const auto* error = std::get_if<MaterialBindingError>(&textures)) return *error;

        ResolvedShaderSurface result;
        result.Surface = base;
        result.Surface.Evaluation = evaluation;
        result.Surface.ShaderGeneration = shaderGeneration;
        result.Surface.UnsupportedFeatures = Unsupported(base);
        result.Surface.Fallback = result.Surface.UnsupportedFeatures != 0;
        if (result.Surface.UnsupportedFeatures)
            result.Surface.Diagnostic = "SURFACE_UNSUPPORTED_FEATURES: canonical material controls are not exposed by this shader interface (mask=" +
                std::to_string(result.Surface.UnsupportedFeatures) + ")";
        result.Constants = std::move(std::get<PackedMaterialResultData>(constants));
        result.Textures = std::move(std::get<TextureRequestResultData>(textures).Requests);
        for (auto& texture : result.Textures) {
            // The reserved base-map binding accepts the canonical native binding
            // as well as a path. Other textures remain shader-schema properties.
            auto name = texture.ResourceName;
            for (auto& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if ((name == "u_basemap" || name == "basemap" || name == "albedomap") && values.UseBaseMap)
                texture.Native = base.Textures[0].Native;
        }
        uint64_t hash = base.Generation;
        auto mix = [&](const auto& value) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            for (size_t i = 0; i < sizeof(value); ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
        };
        auto text = [&](const std::string& value) { mix(value.size()); for (auto c : value) mix(c); };
        mix(evaluation); mix(shaderGeneration); mix(plan.InterfaceRevision);
        for (const auto& property : resolved.Properties) {
            text(property.Key.BufferName); text(property.Key.VariableName); mix(property.Kind);
            std::visit([&](const auto& value) {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>) text(value);
                else mix(value);
            }, property.Value);
        }
        for (const auto& texture : result.Textures) {
            text(texture.ResourceName); text(texture.ResolvedPath);
            mix(texture.Address.RegisterSpace); mix(texture.Address.RegisterIndex); mix(texture.Role);
            const auto handle = texture.Native ? texture.Native->GetHandle() : RHI::RHIResourceHandle{};
            mix(handle.Epoch); mix(handle.Slot); mix(handle.Generation);
        }
        result.Surface.Generation = hash ? hash : 1;
        return result;
    }
    bool FinalizeSurfaceTextureBindings(ResolvedSurface& surface,
        std::span<const std::shared_ptr<const RHI::RHITexture>> textures, uint32_t effectiveTextureFlags) {
        uint64_t hash = surface.Generation;
        auto mix = [&](const auto& value) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            for (size_t i = 0; i < sizeof(value); ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
        };
        mix(effectiveTextureFlags); mix(textures.size());
        for (const auto& texture : textures) {
            if (!texture || !IsSurfaceTextureBinding(*texture)) {
                surface.Valid = false;
                surface.Diagnostic = "SURFACE_TEXTURE_INVALID: effective binding requires a live float Texture2D SRV";
                return false;
            }
            const auto handle = texture->GetHandle();
            mix(handle.Epoch); mix(handle.Slot); mix(handle.Generation);
        }
        surface.TextureFlags = effectiveTextureFlags;
        surface.Generation = hash ? hash : 1;
        return surface.Valid;
    }
}
