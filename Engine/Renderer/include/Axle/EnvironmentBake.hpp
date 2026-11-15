#pragma once
#include <Axle/DecodedImage.hpp>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <vector>
namespace Axle {
    struct EnvironmentBakeSettings {
        // M15-S2.1 contract: all dimensions are power-of-two; input/output
        // maps are linear floating-point data and the bake is deterministic for
        // an identical source image, settings and sample count.
        uint32_t EnvironmentSize=256, IrradianceSize=32, PrefilterSize=128;
        uint32_t PrefilterMips=5, BRDFSize=128, Samples=256;
        bool operator==(const EnvironmentBakeSettings&) const = default;
    };
    struct ProceduralSkySettings {
        // SunDirection is a world-space direction toward the sun. Generated
        // equirectangular images use the top row for +Y (north/zenith), with
        // linear RGBA32F radiance after Intensity * Exposure scaling.
        glm::vec3 SunDirection{0.45f,1.0f,0.65f};
        glm::vec3 SunColor{1.0f,0.98f,0.92f};
        glm::vec3 ZenithColor{0.2f,0.35f,0.6f};
        glm::vec3 HorizonColor{0.6f,0.7f,0.8f};
        glm::vec3 GroundColor{0.15f,0.12f,0.1f};
        float SunSize=0.02f, SunGlow=1.0f, Turbidity=2.0f;
        float Intensity=1.0f, Exposure=1.0f;
    };
    struct EnvironmentMip {
        uint32_t Size=0;
        std::vector<glm::vec4> Pixels; // linear RGBA32F, row zero at cube v=0
    };
    struct EnvironmentCube {
        // Face order is fixed: +X, -X, +Y, -Y, +Z, -Z. Face v=0 follows the
        // canonical EnvironmentCubeDirection mapping and is never vertically
        // flipped during authoring or upload.
        std::array<std::vector<EnvironmentMip>,6> Faces; // +X,-X,+Y,-Y,+Z,-Z; face-major/mip-minor
    };
    struct BakedEnvironment {
        EnvironmentCube Radiance, Irradiance, Prefilter;
        EnvironmentMip BRDF; // x=NdotV, y=roughness; RG=scale/bias, B=0, A=1
    };
    // No GPU/device/window or authored mutations. Input is unflipped linear HDR
    // equirectangular data (top row=north). Irradiance stores E/pi: a constant
    // radiance C remains C, matching the Lambert factor in the PBR receiver.
    bool BakeEnvironment(const DecodedImage&, const EnvironmentBakeSettings&, BakedEnvironment& output, std::string* error=nullptr);
    // Generates one linear floating-point equirectangular procedural sky mip.
    // The horizontal coordinate wraps; the first row points toward +Y.
    // The result is CPU-only and can be passed to BakeEnvironment.
    bool GenerateProceduralSkyImage(const ProceduralSkySettings&, uint32_t width, uint32_t height,
        DecodedImage& output, std::string* error=nullptr);
    bool ValidateEnvironmentBakeSettings(const EnvironmentBakeSettings&,std::string* error=nullptr);
    glm::vec3 EnvironmentCubeDirection(uint32_t face,float u,float v);
    glm::vec2 IntegrateEnvironmentBRDF(float nDotV,float roughness,uint32_t samples);
}
