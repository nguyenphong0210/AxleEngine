#pragma once

#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIMaterialConstants.hpp>
#include <Axle/RHI/RHITransform.hpp>
#include <Axle/Types.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <array>
#include <memory>
#include <string>

namespace Axle {
    struct RenderMaterialData;

    enum class SurfaceEvaluation : uint8_t { BuiltIn, CustomShader, MaterialGraph };
    enum class SurfaceFeature : uint32_t {
        Clearcoat = 1u << 0, Anisotropy = 1u << 1, Sheen = 1u << 2,
        Subsurface = 1u << 3, Absorption = 1u << 4, Refraction = 1u << 5,
        DetailNormal = 1u << 6, NormalStrength = 1u << 7,
        SpecularPolicy = 1u << 8, ReflectionPolicy = 1u << 9, Cutout = 1u << 10,
        Emission = 1u << 11, NonBaseTextures = 1u << 12
    };
    struct ResolvedSurfaceTexture {
        std::string Path;
        std::shared_ptr<const RHI::RHITexture> Native;
    };
    // Canonical material maps are individual, single-sample float Texture2D SRVs.
    bool IsSurfaceTextureBinding(const RHI::RHITexture& texture);

    // Canonical CPU material result shared by built-in PBR, preview, graph
    // validation and screen-space consumers. Units are documented in the M14
    // architecture contract; GPU packing is deterministic and 256-byte aligned.
    struct ResolvedSurface {
        bool AuthoredMaterial = true;
        glm::vec4 PatternSecondary{1.0f}; // Explicit adapter for unmaterialized procedural meshes.
        glm::vec4 BaseColor{1.0f};
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        float AO = 1.0f;
        float NormalStrength = 1.0f;
        glm::vec3 EmissionColor{0.0f};
        float EmissionIntensity = 0.0f;
        glm::vec2 UVScale{1.0f};
        glm::vec2 UVOffset{0.0f};
        glm::vec2 DetailUVScale{1.0f};
        glm::vec2 DetailUVOffset{0.0f};
        float DetailNormalStrength = 1.0f;

        glm::vec3 GeometricNormal{0.0f, 0.0f, 1.0f};
        glm::vec3 ShadingNormal{0.0f, 0.0f, 1.0f};
        glm::vec4 Tangent{1.0f, 0.0f, 0.0f, 1.0f};

        float ClearcoatStrength = 0.0f;
        float ClearcoatRoughness = 0.04f;
        float AnisotropyStrength = 0.0f;
        float AnisotropyRotation = 0.0f;
        bool SheenEnabled = false;
        glm::vec3 SheenColor{1.0f};
        float SheenRoughness = 0.5f;
        bool SubsurfaceEnabled = false;
        glm::vec3 SubsurfaceColor{1.0f, 0.8f, 0.7f};
        float SubsurfaceRadius = 1.0f;
        bool AbsorptionEnabled = false;
        glm::vec3 AbsorptionColor{0.0f};
        float Dispersion = 0.0f;

        bool RefractionEnabled = false;
        float RefractionStrength = 1.0f;
        float RefractionIOR = 1.5f;
        float RefractionThickness = 0.1f;
        glm::vec3 RefractionTint{1.0f};

        SurfaceType Surface = SurfaceType::Opaque;
        CullMode Cull = CullMode::Back;
        bool DepthWrite = true;
        int RenderQueue = 2000;
        RHI::RHIShadingModel ShadingModel = RHI::RHIShadingModel::MetallicRoughness;
        float AlphaCutoff = 0.5f;
        bool EnableSpecularHighlights = true;
        bool EnableEnvironmentReflections = true;
        uint32_t TextureFlags = 0;
        std::array<ResolvedSurfaceTexture, static_cast<size_t>(RHI::RHITextureSlot::Count)> Textures;
        // For shader paths these numeric values are canonical engine inputs,
        // not a CPU evaluation of the per-pixel graph/custom shader output.
        SurfaceEvaluation Evaluation = SurfaceEvaluation::BuiltIn;
        uint64_t ShaderGeneration = 0;
        uint32_t UnsupportedFeatures = 0;
        uint64_t Generation = 0;
        bool Valid = true;
        bool Fallback = false;
        std::string Diagnostic;

        RHI::RHIMaterialConstants ToMaterialConstants() const;
        void ApplyToPushConstants(RHI::RHIObjectPushConstants& constants) const;
    };

    class MaterialResolver {
    public:
        static ResolvedSurface Resolve(const RHI::RHIMaterial& material, uint64_t generation = 0);
        static ResolvedSurface Resolve(const RenderMaterialData& snapshot);
        static uint64_t ComputeGeneration(const RHI::RHIMaterial& material);
    };

}
