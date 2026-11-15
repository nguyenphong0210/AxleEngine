#include "RenderTestCommon.hpp"

#include <Axle/Components.hpp>
#include <Axle/EnvironmentBake.hpp>
#include <Axle/Scene.hpp>
#include <Axle/SceneExtraction.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace {
using namespace Axle;

EnvironmentBakeSettings SmallBake() {
    EnvironmentBakeSettings settings;
    settings.EnvironmentSize = 16;
    settings.IrradianceSize = 4;
    settings.PrefilterSize = 16;
    settings.PrefilterMips = 5;
    settings.BRDFSize = 16;
    settings.Samples = 128;
    return settings;
}

DecodedImage MakeEquirectangular(bool directional) {
    constexpr uint32_t width = 32;
    constexpr uint32_t height = 16;
    DecodedImage image;
    image.HDR = true;
    image.SourceChannels = 4;
    image.Mips.emplace_back();
    auto& mip = image.Mips.back();
    mip.Width = width;
    mip.Height = height;
    mip.Pixels.resize(size_t(width) * height * 16);
    for (uint32_t y = 0; y < height; ++y) {
        const float theta = (static_cast<float>(y) + 0.5f) / static_cast<float>(height) * glm::pi<float>();
        const float directionY = std::cos(theta);
        for (uint32_t x = 0; x < width; ++x) {
            const glm::vec4 value = directional
                ? glm::vec4(2.0f + directionY, 4.0f + directionY, 8.0f + directionY, 1.0f)
                : glm::vec4(2.0f, 4.0f, 8.0f, 1.0f);
            std::memcpy(mip.Pixels.data() + (size_t(y) * width + x) * 16, &value, 16);
        }
    }
    return image;
}

bool FiniteImage(const DecodedImage& image) {
    for (const auto& mip : image.Mips) {
        for (size_t offset = 0; offset + 4 <= mip.Pixels.size(); offset += 4) {
            float value = 0.0f;
            std::memcpy(&value, mip.Pixels.data() + offset, sizeof(value));
            if (!std::isfinite(value)) return false;
        }
    }
    return true;
}

bool ConstantBakeContract() {
    BakedEnvironment baked;
    std::string error;
    RENDER_CHECK(BakeEnvironment(MakeEquirectangular(false), SmallBake(), baked, &error) && error.empty(),
        "linear HDR source bakes successfully with the frozen settings");
    for (const auto* cube : {&baked.Radiance, &baked.Irradiance, &baked.Prefilter}) {
        for (const auto& face : cube->Faces) {
            for (const auto& mip : face) {
                RENDER_CHECK(mip.Size > 0 && mip.Pixels.size() == size_t(mip.Size) * mip.Size,
                    "every cubemap face mip has the declared square size");
                for (const auto& pixel : mip.Pixels) {
                    RENDER_CHECK(glm::length(glm::vec3(pixel) - glm::vec3(2.0f, 4.0f, 8.0f)) < 1e-5f && pixel.w == 1.0f,
                        "constant linear HDR radiance remains unchanged through bake outputs");
                }
            }
        }
    }
    RENDER_CHECK(baked.Radiance.Faces[0].size() == 5 && baked.Prefilter.Faces[0].size() == 5,
        "radiance and prefilter mip counts follow the frozen settings");
    for (const auto& pixel : baked.BRDF.Pixels) {
        RENDER_CHECK(std::isfinite(pixel.x) && std::isfinite(pixel.y) && pixel.x >= 0.0f && pixel.y >= 0.0f,
            "BRDF bake values remain finite and non-negative");
    }
    return true;
}

bool ProceduralSkyContract() {
    ProceduralSkySettings settings;
    settings.SunDirection = {0.0f, 1.0f, 0.0f};
    settings.SunColor = {0.0f, 0.0f, 0.0f};
    settings.ZenithColor = {0.10f, 0.20f, 0.80f};
    settings.HorizonColor = {0.80f, 0.60f, 0.30f};
    settings.GroundColor = {0.20f, 0.10f, 0.05f};
    settings.Intensity = 2.0f;
    settings.Exposure = 0.5f;

    DecodedImage first;
    DecodedImage second;
    std::string error;
    RENDER_CHECK(GenerateProceduralSkyImage(settings, 32, 16, first, &error) && error.empty(),
        "procedural sky accepts the frozen linear output dimensions");
    RENDER_CHECK(GenerateProceduralSkyImage(settings, 32, 16, second, &error) && first.Mips[0].Pixels == second.Mips[0].Pixels,
        "procedural sky generation is deterministic for identical inputs");
    RENDER_CHECK(first.HDR && first.Mips.size() == 1 && FiniteImage(first),
        "procedural sky produces one finite HDR equirectangular mip");

    auto readPixel = [](const DecodedImage& image, uint32_t x, uint32_t y) {
        glm::vec4 value{};
        std::memcpy(&value, image.Mips[0].Pixels.data() + (size_t(y) * image.Mips[0].Width + x) * 16, 16);
        return value;
    };
    const glm::vec4 zenith = readPixel(first, 16, 0);
    const glm::vec4 ground = readPixel(first, 16, 15);
    RENDER_CHECK(zenith.b > zenith.r && ground.r > ground.b,
        "top equirectangular row maps to +Y zenith and bottom row maps to ground");

    ProceduralSkySettings zeroDirection = settings;
    zeroDirection.SunDirection = {0.0f, 0.0f, 0.0f};
    DecodedImage fallbackDirection;
    RENDER_CHECK(GenerateProceduralSkyImage(zeroDirection, 16, 8, fallbackDirection, &error) && FiniteImage(fallbackDirection),
        "zero sun direction uses the finite +Y fallback");

    ProceduralSkySettings invalid = settings;
    invalid.Intensity = -1.0f;
    RENDER_CHECK(!GenerateProceduralSkyImage(invalid, 32, 16, fallbackDirection, &error) &&
                 error.find("ENV_PROCEDURAL_SETTINGS") != std::string::npos,
        "negative procedural intensity is rejected with an explicit diagnostic");
    invalid = settings;
    invalid.Exposure = std::numeric_limits<float>::quiet_NaN();
    RENDER_CHECK(!GenerateProceduralSkyImage(invalid, 32, 16, fallbackDirection, &error) &&
                 error.find("ENV_PROCEDURAL_SETTINGS") != std::string::npos,
        "non-finite procedural exposure is rejected with an explicit diagnostic");
    return true;
}

bool DirectionAndSourceContract() {
    const glm::vec3 axes[]{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (uint32_t face = 0; face < 6; ++face) {
        RENDER_CHECK(glm::length(EnvironmentCubeDirection(face, 0.5f, 0.5f) - axes[face]) < 1e-6f,
            "cubemap face centers use the frozen +X,-X,+Y,-Y,+Z,-Z order");
    }

    BakedEnvironment baked;
    std::string error;
    RENDER_CHECK(BakeEnvironment(MakeEquirectangular(true), SmallBake(), baked, &error),
        "directional linear HDR source bakes successfully");
    const auto top = baked.Radiance.Faces[2][0].Pixels[8 * 16 + 8];
    const auto bottom = baked.Radiance.Faces[3][0].Pixels[8 * 16 + 8];
    RENDER_CHECK(top.x > 2.9f && bottom.x < 1.1f,
        "the +Y and -Y faces preserve equirectangular vertical orientation");

    BakedEnvironment retained;
    retained.BRDF.Size = 123;
    auto invalid = MakeEquirectangular(false);
    invalid.HDR = false;
    RENDER_CHECK(!BakeEnvironment(invalid, SmallBake(), retained, &error) && retained.BRDF.Size == 123,
        "non-HDR authored input is rejected without mutating the prior result");
    invalid = MakeEquirectangular(false);
    invalid.Options.SRGB = true;
    RENDER_CHECK(!BakeEnvironment(invalid, SmallBake(), retained, &error) && retained.BRDF.Size == 123,
        "sRGB authored input is rejected without mutating the prior result");
    invalid = MakeEquirectangular(false);
    invalid.Options.FlipVertically = true;
    RENDER_CHECK(!BakeEnvironment(invalid, SmallBake(), retained, &error) && retained.BRDF.Size == 123,
        "vertically flipped authored input is rejected without mutating the prior result");
    return true;
}

bool AuthoredSkyPolicyContract() {
    SkyboxComponent defaults;
    RENDER_CHECK(defaults.Enabled && !defaults.EnvironmentLighting &&
                 defaults.Type == SkyboxType::ProceduralAtmospheric && defaults.HDRPath.empty() &&
                 defaults.Intensity == 1.0f && defaults.Exposure == 1.0f,
        "new sky components default to background-only procedural sky policy");

    Scene scene;
    auto entity = scene.CreateEntity("Authored environment");
    auto& authored = entity.AddComponent<SkyboxComponent>();
    authored.Enabled = true;
    authored.EnvironmentLighting = true;
    authored.Type = SkyboxType::HDRCubemap;
    authored.HDRPath = "assets/textures/studio_cubemap.hdr";
    authored.Intensity = 1.75f;
    authored.Exposure = 1.25f;
    authored.SyncWithSun = false;
    authored.CustomAzimuth = 120.0f;
    authored.CustomElevation = 35.0f;

    const auto world = ExtractRenderWorld(scene);
    RENDER_CHECK(world.Skybox.Authored && world.Skybox.Enabled && world.Skybox.EnvironmentLighting &&
                 world.Skybox.Type == static_cast<int>(SkyboxType::HDRCubemap) &&
                 world.Skybox.HDRPath == authored.HDRPath &&
                 world.Skybox.Intensity == authored.Intensity && world.Skybox.Exposure == authored.Exposure,
        "authored cubemap identity and linear intensity/exposure survive extraction");
    RENDER_CHECK(!world.Skybox.SyncWithSun && world.Skybox.CustomAzimuth == authored.CustomAzimuth &&
                 world.Skybox.CustomElevation == authored.CustomElevation,
        "procedural sun controls remain separate authored values");

    authored.EnvironmentLighting = false;
    const auto backgroundOnly = ExtractRenderWorld(scene);
    RENDER_CHECK(backgroundOnly.Skybox.Enabled && !backgroundOnly.Skybox.EnvironmentLighting &&
                 backgroundOnly.Skybox.HDRPath == authored.HDRPath,
        "background visibility and environment-lighting opt-in remain independent");
    return true;
}
}

bool RunM15EnvironmentContractReferenceUnitTests() {
    std::cout << "  [M15EnvironmentContractReference] Running S2.1 environment contract checks...\n";
    RENDER_CHECK(ProceduralSkyContract(), "procedural sky contract");
    RENDER_CHECK(ConstantBakeContract(), "environment bake contract");
    RENDER_CHECK(DirectionAndSourceContract(), "cubemap direction and source contract");
    RENDER_CHECK(AuthoredSkyPolicyContract(), "authored sky policy contract");
    std::cout << "  [M15EnvironmentContractReference] S2.1 checks passed.\n";
    return true;
}
