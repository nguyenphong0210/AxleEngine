#pragma once
#include <Axle/DecodedImage.hpp>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <vector>
namespace Axle {
    struct EnvironmentBakeSettings {
        uint32_t EnvironmentSize=256, IrradianceSize=32, PrefilterSize=128;
        uint32_t PrefilterMips=5, BRDFSize=128, Samples=256;
        bool operator==(const EnvironmentBakeSettings&) const = default;
    };
    struct EnvironmentMip {
        uint32_t Size=0;
        std::vector<glm::vec4> Pixels; // linear RGBA32F, row zero at cube v=0
    };
    struct EnvironmentCube {
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
    bool ValidateEnvironmentBakeSettings(const EnvironmentBakeSettings&,std::string* error=nullptr);
    glm::vec3 EnvironmentCubeDirection(uint32_t face,float u,float v);
    glm::vec2 IntegrateEnvironmentBRDF(float nDotV,float roughness,uint32_t samples);
}
