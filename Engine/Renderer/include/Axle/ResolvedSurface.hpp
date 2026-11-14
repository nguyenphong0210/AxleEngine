#pragma once

#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIMaterialConstants.hpp>
#include <Axle/RHI/RHITransform.hpp>
#include <Axle/Types.hpp>
#include <glm/glm.hpp>
#include <cstdint>

namespace Axle {

    // Canonical CPU material result shared by built-in PBR, preview, graph
    // validation and screen-space consumers. Units are documented in the M14
    // architecture contract; GPU packing is deterministic and 256-byte aligned.
    struct ResolvedSurface {
        glm::vec4 BaseColor{1.0f};
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        float AO = 1.0f;
        float NormalStrength = 1.0f;
        glm::vec3 EmissionColor{0.0f};
        float EmissionIntensity = 0.0f;
        glm::vec2 UVScale{1.0f};
        glm::vec2 UVOffset{0.0f};

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
        bool EnableSpecularHighlights = true;
        bool EnableEnvironmentReflections = true;
        uint32_t TextureFlags = 0;
        uint64_t Generation = 0;
        bool Valid = true;

        RHI::RHIMaterialConstants ToMaterialConstants() const;
        void ApplyToPushConstants(RHI::RHIObjectPushConstants& constants) const;
    };

    class MaterialResolver {
    public:
        static ResolvedSurface Resolve(const RHI::RHIMaterial& material, uint64_t generation = 0);
        static uint64_t ComputeGeneration(const RHI::RHIMaterial& material);
    };

}
