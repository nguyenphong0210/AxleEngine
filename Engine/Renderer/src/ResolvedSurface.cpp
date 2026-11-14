#include <Axle/ResolvedSurface.hpp>
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace Axle {

    RHI::RHIMaterialConstants ResolvedSurface::ToMaterialConstants() const {
        RHI::RHIMaterialConstants data;
        data.UVTransform = glm::vec4(UVScale, UVOffset);
        data.DetailUVTransform = glm::vec4(DetailUVScale, DetailUVOffset);
        data.DetailParameters.x = std::max(0.0f, DetailNormalStrength);
        data.Surface = glm::vec4(std::clamp(NormalStrength, 0.0f, 4.0f), std::clamp(AO, 0.0f, 1.0f),
            EnableSpecularHighlights ? 1.0f : 0.0f, EnableEnvironmentReflections ? 1.0f : 0.0f);
        data.CoatAnisotropy = glm::vec4(std::clamp(ClearcoatStrength, 0.0f, 1.0f),
            std::clamp(ClearcoatRoughness, 0.001f, 1.0f), std::clamp(AnisotropyStrength, -1.0f, 1.0f),
            std::clamp(AnisotropyRotation, 0.0f, 1.0f));
        data.Flags = glm::uvec4(TextureFlags, 1u, 0u, 0u);
        data.Refraction = glm::vec4(std::clamp(RefractionStrength, 0.0f, 1.0f),
            std::clamp(RefractionIOR, 1.0f, 3.0f), std::max(0.0f, RefractionThickness), RefractionEnabled ? 1.0f : 0.0f);
        data.RefractionTint = glm::vec4(glm::clamp(RefractionTint, glm::vec3(0.0f), glm::vec3(1.0f)), 1.0f);
        data.Sheen = SheenEnabled ? glm::vec4(glm::clamp(SheenColor, glm::vec3(0.0f), glm::vec3(1.0f)), std::clamp(SheenRoughness, 0.01f, 1.0f))
                                  : glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
        data.Subsurface = SubsurfaceEnabled ? glm::vec4(glm::max(SubsurfaceColor, glm::vec3(0.0f)), std::max(0.0f, SubsurfaceRadius))
                                            : glm::vec4(0.0f);
        data.Absorption = AbsorptionEnabled ? glm::vec4(glm::max(AbsorptionColor, glm::vec3(0.0f)), std::max(0.0f, Dispersion))
                                            : glm::vec4(0.0f);
        return data;
    }

    void ResolvedSurface::ApplyToPushConstants(RHI::RHIObjectPushConstants& constants) const {
        constants.ColorPrimary = BaseColor;
        constants.ColorSecondary = glm::vec4(EmissionColor, EmissionIntensity);
        constants.Params = glm::vec4(UVScale.x, static_cast<float>(ShadingModel), Roughness, Metallic);
        constants.MaterialExtra = glm::vec4(UVOffset, AlphaCutoff, static_cast<float>(Surface));
    }

    uint64_t MaterialResolver::ComputeGeneration(const RHI::RHIMaterial& material) {
        const auto& desc = material.GetDesc();
        uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash](const auto& value) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            for (size_t i = 0; i < sizeof(value); ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
        };
        mix(desc.BaseColor); mix(desc.Roughness); mix(desc.Metallic); mix(desc.AO); mix(desc.NormalStrength);
        mix(desc.EmissiveColor); mix(desc.EmissiveIntensity); mix(desc.UVScale); mix(desc.UVOffset);
        mix(desc.ClearcoatStrength); mix(desc.ClearcoatRoughness); mix(desc.AnisotropyStrength); mix(desc.AnisotropyRotation);
        mix(desc.RefractionEnabled); mix(desc.RefractionStrength); mix(desc.RefractionIOR); mix(desc.RefractionThickness);
        mix(desc.SheenEnabled); mix(desc.SheenColor); mix(desc.SheenRoughness);
        mix(desc.SubsurfaceEnabled); mix(desc.SubsurfaceColor); mix(desc.SubsurfaceRadius);
        mix(desc.AbsorptionEnabled); mix(desc.AbsorptionColor); mix(desc.Dispersion); mix(desc.TextureEnableMask);
        mix(desc.SurfaceType); mix(desc.CullMode); mix(desc.DepthWrite); mix(desc.RenderQueue);
        mix(desc.ShadingModel); mix(desc.AlphaCutoff); mix(desc.TextureMode); mix(desc.UseORMMap);
        mix(desc.DetailUVScale); mix(desc.DetailUVOffset); mix(desc.DetailNormalStrength);
        mix(desc.RefractionTint); mix(desc.EnableSpecularHighlights); mix(desc.EnableEnvironmentReflections);
        const auto mixPath = [&](const std::string& path) {
            const auto size = path.size();
            mix(size);
            for (const auto character : path) mix(character);
        };
        mixPath(desc.ORMPath);
        for (uint32_t i = 0; i < static_cast<uint32_t>(RHI::RHITextureSlot::Count); ++i) {
            const auto slot = static_cast<RHI::RHITextureSlot>(i);
            mixPath(material.GetTexturePath(slot));
            // GetTexture creates a fallback for unassigned slots. Generation
            // hashing must remain CPU-only and reflect authored bindings only.
            const auto texture = material.HasTexture(slot) ? material.GetTexture(slot) : nullptr;
            const auto handle = texture ? texture->GetHandle() : RHI::RHIResourceHandle{};
            mix(handle.Epoch); mix(handle.Slot); mix(handle.Generation);
        }
        return hash;
    }

    ResolvedSurface MaterialResolver::Resolve(const RHI::RHIMaterial& material, uint64_t generation) {
        const auto& desc = material.GetDesc();
        ResolvedSurface result;
        result.Generation = generation ? generation : ComputeGeneration(material);
        auto finite = [](const auto& value) {
            if constexpr (std::is_floating_point_v<std::decay_t<decltype(value)>>) return std::isfinite(value);
            else { for (glm::length_t i = 0; i < value.length(); ++i) if (!std::isfinite(value[i])) return false; }
            return true;
        };
        if (!finite(desc.BaseColor) || !finite(desc.Roughness) || !finite(desc.Metallic) || !finite(desc.AO) ||
            !finite(desc.NormalStrength) || !finite(desc.EmissiveColor) || !finite(desc.EmissiveIntensity) ||
            !finite(desc.UVScale) || !finite(desc.UVOffset) || !finite(desc.DetailUVScale) || !finite(desc.DetailUVOffset) ||
            !finite(desc.DetailNormalStrength) || !finite(desc.ClearcoatStrength) || !finite(desc.ClearcoatRoughness) ||
            !finite(desc.AnisotropyStrength) || !finite(desc.AnisotropyRotation) || !finite(desc.AlphaCutoff) ||
            !finite(desc.SheenColor) || !finite(desc.SheenRoughness) || !finite(desc.SubsurfaceColor) ||
            !finite(desc.SubsurfaceRadius) || !finite(desc.AbsorptionColor) || !finite(desc.Dispersion) ||
            !finite(desc.RefractionStrength) || !finite(desc.RefractionIOR) || !finite(desc.RefractionThickness) || !finite(desc.RefractionTint)) {
            result.Valid = false;
            result.Diagnostic = "SURFACE_NONFINITE_INPUT: canonical material values must be finite";
            return result;
        }
        if (static_cast<uint32_t>(desc.SurfaceType) > static_cast<uint32_t>(SurfaceType::Additive) ||
            static_cast<uint32_t>(desc.CullMode) > static_cast<uint32_t>(CullMode::None) ||
            static_cast<uint32_t>(desc.TextureMode) > static_cast<uint32_t>(TextureMode::PackedORM) ||
            static_cast<uint32_t>(desc.ShadingModel) > static_cast<uint32_t>(RHI::RHIShadingModel::PackedORMTexturedPBR)) {
            result.Valid = false;
            result.Diagnostic = "SURFACE_INVALID_POLICY: invalid surface, cull or shading policy";
            return result;
        }
        result.BaseColor = desc.BaseColor;
        result.Metallic = std::clamp(desc.Metallic, 0.0f, 1.0f);
        result.Roughness = std::clamp(desc.Roughness, 0.04f, 1.0f);
        result.AO = std::clamp(desc.AO, 0.0f, 1.0f);
        result.NormalStrength = std::max(0.0f, desc.NormalStrength);
        result.EmissionColor = glm::max(desc.EmissiveColor, glm::vec3(0.0f));
        result.EmissionIntensity = std::max(0.0f, desc.EmissiveIntensity);
        result.UVScale = desc.UVScale; result.UVOffset = desc.UVOffset;
        result.DetailUVScale = desc.DetailUVScale; result.DetailUVOffset = desc.DetailUVOffset;
        result.DetailNormalStrength = std::max(0.0f, desc.DetailNormalStrength);
        result.ClearcoatStrength = std::clamp(desc.ClearcoatStrength, 0.0f, 1.0f);
        result.ClearcoatRoughness = std::clamp(desc.ClearcoatRoughness, 0.001f, 1.0f);
        result.AnisotropyStrength = std::clamp(desc.AnisotropyStrength, -1.0f, 1.0f);
        result.AnisotropyRotation = std::clamp(desc.AnisotropyRotation, 0.0f, 1.0f);
        result.SheenEnabled = desc.SheenEnabled; result.SheenColor = glm::max(desc.SheenColor, glm::vec3(0.0f)); result.SheenRoughness = desc.SheenRoughness;
        result.SubsurfaceEnabled = desc.SubsurfaceEnabled; result.SubsurfaceColor = glm::max(desc.SubsurfaceColor, glm::vec3(0.0f)); result.SubsurfaceRadius = desc.SubsurfaceRadius;
        result.AbsorptionEnabled = desc.AbsorptionEnabled; result.AbsorptionColor = glm::max(desc.AbsorptionColor, glm::vec3(0.0f)); result.Dispersion = desc.Dispersion;
        result.RefractionEnabled = desc.RefractionEnabled; result.RefractionStrength = desc.RefractionStrength; result.RefractionIOR = desc.RefractionIOR;
        result.RefractionThickness = desc.RefractionThickness; result.RefractionTint = glm::clamp(desc.RefractionTint, glm::vec3(0.0f), glm::vec3(1.0f));
        result.Surface = desc.SurfaceType; result.Cull = desc.CullMode; result.DepthWrite = desc.DepthWrite;
        result.RenderQueue = desc.RenderQueue;
        result.ShadingModel = desc.TextureMode == TextureMode::PackedORM
            ? RHI::RHIShadingModel::PackedORMTexturedPBR : desc.ShadingModel;
        result.AlphaCutoff = std::clamp(desc.AlphaCutoff, 0.0f, 1.0f);
        result.EnableSpecularHighlights = desc.EnableSpecularHighlights; result.EnableEnvironmentReflections = desc.EnableEnvironmentReflections;
        result.TextureFlags = material.BuildConstants().Flags.x;
        for (size_t i = 0; i < result.Textures.size(); ++i) {
            const auto slot = static_cast<RHI::RHITextureSlot>(i);
            result.Textures[i].Path = (i == 2 && desc.TextureMode == TextureMode::PackedORM)
                ? desc.ORMPath : material.GetTexturePath(slot);
            if (material.HasTexture(slot)) result.Textures[i].Native = material.GetTexture(slot);
        }
        result.Valid = true;
        return result;
    }

}
