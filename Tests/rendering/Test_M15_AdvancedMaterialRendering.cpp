#include "RHIReadback.hpp"
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace Axle;
using namespace Axle::RHI;

namespace {
    bool WriteSolidHDR(const std::filesystem::path& path, const glm::vec3& radiance) {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        constexpr uint32_t w = 8u, h = 4u;
        file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " << h << " +X " << w << "\n";
        for (uint32_t i = 0; i < w * h; ++i) {
            const float maximum = std::max({radiance.r, radiance.g, radiance.b});
            unsigned char pixel[4]{0, 0, 0, 0};
            if (maximum > 1e-32f) {
                int exponent = 0;
                const float scale = std::frexp(maximum, &exponent) * 256.0f / maximum;
                pixel[0] = static_cast<unsigned char>(std::clamp(radiance.r * scale, 0.0f, 255.0f));
                pixel[1] = static_cast<unsigned char>(std::clamp(radiance.g * scale, 0.0f, 255.0f));
                pixel[2] = static_cast<unsigned char>(std::clamp(radiance.b * scale, 0.0f, 255.0f));
                pixel[3] = static_cast<unsigned char>(exponent + 128);
            }
            file.write(reinterpret_cast<const char*>(pixel), 4);
        }
        return static_cast<bool>(file);
    }

    bool ReadTextureRGBA16F(RHITexture& tex, std::vector<glm::vec4>& outPixels) {
        if (!tex.IsValid() || tex.GetFormat() != RHITextureFormat::RGBA16_FLOAT) return false;
        auto& device = RHIDevice::Get();
        const auto alignment = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
        const auto align = [](uint32_t value, uint32_t step) { return (value + step - 1u) / step * step; };
        const uint32_t width = tex.GetWidth();
        const uint32_t height = tex.GetHeight();
        constexpr uint32_t pixelSize = 8u;

        nri::TextureDataLayoutDesc layout{};
        layout.rowPitch = align(width * pixelSize, alignment.uploadBufferTextureRow);
        layout.slicePitch = align(layout.rowPitch * height, alignment.uploadBufferTextureSlice);

        RHIBufferDesc desc{};
        desc.Size = layout.slicePitch;
        desc.MemoryLocation = RHIMemoryLocation::HostReadback;
        desc.DebugName = "U9AdvancedMatReadback";
        RHIBuffer staging;
        RHICommandBuffer cmd;
        if (!staging.Init(desc) || !cmd.Init() || !cmd.Begin()) return false;

        cmd.TransitionTexture(tex.GetNative(), RHIResourceLayout::ShaderResource, RHIResourceLayout::CopySource);
        nri::TextureRegionDesc region{};
        region.width = static_cast<nri::Dim_t>(width);
        region.height = static_cast<nri::Dim_t>(height);
        region.depth = 1;
        cmd.ReadbackTextureToBuffer(*staging.GetNative(), layout, *tex.GetNative(), region);
        cmd.TransitionTexture(tex.GetNative(), RHIResourceLayout::CopySource, RHIResourceLayout::ShaderResource);
        if (!cmd.End() || !cmd.Submit() || !device.WaitIdle()) return false;

        const auto* bytes = static_cast<const uint8_t*>(staging.Map());
        if (!bytes) return false;
        outPixels.resize(static_cast<size_t>(width) * height);
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* row = bytes + y * layout.rowPitch;
            for (uint32_t x = 0; x < width; ++x) {
                uint64_t packed = 0;
                std::memcpy(&packed, row + x * pixelSize, sizeof(packed));
                outPixels[static_cast<size_t>(y) * width + x] = glm::unpackHalf4x16(packed);
            }
        }
        staging.Unmap();
        return true;
    }

    float Luminance(const glm::vec4& rgba) {
        return 0.2126f * rgba.r + 0.7152f * rgba.g + 0.0722f * rgba.b;
    }

    float AverageRingLuminance(const std::vector<glm::vec4>& pixels,
                               uint32_t width,
                               uint32_t height,
                               float minRadiusPx,
                               float maxRadiusPx,
                               int sideFilter = 0) {
        const float cx = (width - 1u) * 0.5f;
        const float cy = (height - 1u) * 0.5f;
        double sum = 0.0;
        uint32_t count = 0u;
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const float dx = static_cast<float>(x) - cx;
                const float dy = static_cast<float>(y) - cy;
                if (sideFilter < 0 && dx >= -10.0f) continue;
                if (sideFilter > 0 && dx <= 10.0f) continue;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist >= minRadiusPx && dist <= maxRadiusPx) {
                    sum += static_cast<double>(Luminance(pixels[static_cast<size_t>(y) * width + x]));
                    ++count;
                }
            }
        }
        return count > 0u ? static_cast<float>(sum / count) : 0.0f;
    }
} // namespace

bool RunM15AdvancedMaterialTests(RHISceneRenderer& renderer, const std::filesystem::path& output) {
    auto& device = RHIDevice::Get();
    const char* backendTag = device.GetBackend() == RHIType::Vulkan ? "Vulkan" : "D3D12";
    std::cout << "[M15U9] Running Milestone 15 Unit 9 GPU tests (" << backendTag << ")...\n";

    constexpr uint32_t width = 128u;
    constexpr uint32_t height = 128u;
    // Sphere radius=1 at origin, camera at z=3, fov=45 deg => projected radius ~ 54.6 px on 128x128 target
    RHITexture color, depth;
    if (!color.Init(RHITextureDesc::RenderTarget2D(width, height)) ||
        !depth.Init(RHITextureDesc::DepthStencil2D(width, height))) {
        std::cerr << "[M15U9] FAIL: Could not initialize color/depth targets\n";
        return false;
    }
    auto targets = std::make_shared<RHIViewTargets>();
    {
        RHIViewTargetDesc targetDesc;
        targetDesc.Width = width;
        targetDesc.Height = height;
        targetDesc.Samples = 1;
        targetDesc.HDR = true;
        targetDesc.EnableDepth = true;
        targetDesc.OwnPrimary = false;
        if (!targets->Configure(targetDesc, nullptr, nullptr, FroxelHistoryPolicy::AllowStaticFog)) {
            std::cerr << "[M15U9] FAIL: Could not configure HDR view targets\n";
            return false;
        }
    }

    auto sphere = RHIMesh::CreateSphere(1.0f);
    if (!sphere) {
        std::cerr << "[M15U9] FAIL: Could not create sphere mesh\n";
        return false;
    }

    PerspectiveCamera camera(45.0f, 1.0f, 0.1f, 50.0f);
    camera.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                         glm::vec3(0.0f, 0.0f, 3.0f));

    SceneRenderSettings settings;
    settings.EnableShadows = false;
    settings.EnableParticles = false;
    settings.EnableToneMapping = true;
    settings.ToneMappingMode = 3;
    settings.Gamma = 1.0f;
    settings.EnableAutoExposure = false;
    settings.EnableBloom = false;
    settings.EnableTAA = false;
    settings.EnableFXAA = false;
    settings.EnableGTAO = false;
    settings.EnableSSR = false;
    settings.EnableVolumetricFog = false;
    settings.EnableDDGI = false;
    settings.EnableStudioLightLobes = false;

    const std::filesystem::path whiteHdrPath = output / "u9_white_furnace.hdr";
    if (!WriteSolidHDR(whiteHdrPath, glm::vec3(1.0f, 1.0f, 1.0f))) {
        std::cerr << "[M15U9] FAIL: Could not write white furnace HDR\n";
        return false;
    }

    EnvironmentBakeSettings fastBake;
    fastBake.EnvironmentSize = 16;
    fastBake.IrradianceSize = 4;
    fastBake.PrefilterSize = 16;
    fastBake.PrefilterMips = 5;
    fastBake.BRDFSize = 16;
    fastBake.Samples = 64;
    if (!renderer.GetEnvironment().SetBakeSettings(fastBake)) {
        std::cerr << "[M15U9] FAIL: Could not configure environment bake settings\n";
        return false;
    }

    auto renderLinear = [&](const RenderWorld& world, std::vector<glm::vec4>& outPixels) -> bool {
        renderer.RenderWorldView(world, camera, &color, &depth, settings, nullptr, targets);
        if (!renderer.WasLastRenderSuccessful()) {
            std::cerr << "[M15U9] FAIL: RenderWorldView did not succeed (" << renderer.GetDiagnostics().GetFrame().Fallbacks.size() << " fallbacks)\n";
            return false;
        }
        auto* hdr = targets->Get(RHIViewTarget::HDR);
        if (!hdr) {
            std::cerr << "[M15U9] FAIL: RHIViewTarget::HDR is null\n";
            return false;
        }
        if (!ReadTextureRGBA16F(*hdr, outPixels)) {
            std::cerr << "[M15U9] FAIL: ReadTextureRGBA16F(HDR) failed (fmt=" << static_cast<int>(hdr->GetFormat()) << ")\n";
            return false;
        }
        return true;
    };

    // =========================================================================
    // Batch A (M15-U9-01, M15-U9-08.1): Charlie Sheen Grazing Rim, Roughness Sweep,
    // Area/Direct Toggle & White Furnace Layering <= 1.0
    // =========================================================================
    {
        RHIMaterialDesc matDesc;
        matDesc.SurfaceType = SurfaceType::Opaque;
        matDesc.BaseColor = glm::vec4(0.06f, 0.02f, 0.02f, 1.0f); // Dark velvet diffuse
        matDesc.Roughness = 0.85f;
        matDesc.Metallic = 0.0f;
        matDesc.SheenEnabled = true;
        matDesc.SheenColor = glm::vec3(1.0f, 0.92f, 0.92f);
        matDesc.SheenRoughness = 0.35f;
        auto mat = std::make_shared<RHIMaterial>(matDesc);

        RenderMeshInstance mesh;
        mesh.EntityID = 101;
        mesh.NativeView = sphere->CaptureView();
        mesh.Material.RHIMaterialAsset = mat;

        RenderWorld world;
        world.Meshes.push_back(mesh);
        settings.EnableSkybox = false;
        world.Skybox.Authored = true;
        world.Skybox.Enabled = true;
        world.Skybox.EnvironmentLighting = true;
        world.Skybox.Type = 1;
        world.Skybox.HDRPath = "u9_white_furnace.hdr";
        world.Skybox.Intensity = 1.0f;

        // 1. IBL Rim vs Center contrast (uniform white dome over dark velvet base)
        std::vector<glm::vec4> pixelsRim;
        if (!renderLinear(world, pixelsRim)) return false;
        const float centerLuma = AverageRingLuminance(pixelsRim, width, height, 0.0f, 8.0f);
        const float midLuma = AverageRingLuminance(pixelsRim, width, height, 22.0f, 28.0f);
        const float grazingLuma = AverageRingLuminance(pixelsRim, width, height, 48.0f, 52.0f);
        if (!(grazingLuma > centerLuma * 2.5f && grazingLuma > midLuma && centerLuma > 0.01f)) {
            std::cerr << "[M15U9] FAIL (08.1): Expected grazing rim > center diffuse! center="
                      << centerLuma << " mid=" << midLuma << " grazing=" << grazingLuma << "\n";
            return false;
        }

        // 2. Roughness sweep narrows grazing peak as SheenRoughness drops (0.08 vs 0.85)
        mat->GetDesc().SheenRoughness = 0.08f;
        std::vector<glm::vec4> pixelsTight;
        if (!renderLinear(world, pixelsTight)) return false;
        mat->GetDesc().SheenRoughness = 0.85f;
        std::vector<glm::vec4> pixelsWide;
        if (!renderLinear(world, pixelsWide)) return false;

        const float tightGrazing = AverageRingLuminance(pixelsTight, width, height, 51.0f, 53.8f);
        const float tightMid = AverageRingLuminance(pixelsTight, width, height, 14.0f, 20.0f);
        const float wideGrazing = AverageRingLuminance(pixelsWide, width, height, 51.0f, 53.8f);
        const float wideMid = AverageRingLuminance(pixelsWide, width, height, 14.0f, 20.0f);
        const float tightRatio = tightGrazing / std::max(tightMid, 1e-4f);
        const float wideRatio = wideGrazing / std::max(wideMid, 1e-4f);
        if (!(tightRatio > wideRatio * 1.8f && wideMid > tightMid * 2.0f)) {
            std::cerr << "[M15U9] FAIL (08.1): Sheen roughness sweep did not narrow grazing peak! tightRatio="
                      << tightRatio << " wideRatio=" << wideRatio << " tightMid=" << tightMid << " wideMid=" << wideMid << "\n";
            return false;
        }

        // 3. White-furnace conservation <= 1.0 under full white IBL with pure white albedo + white sheen
        mat->GetDesc().BaseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        mat->GetDesc().Roughness = 0.50f;
        mat->GetDesc().Metallic = 0.0f;
        mat->GetDesc().SheenEnabled = true;
        mat->GetDesc().SheenColor = glm::vec3(1.0f, 1.0f, 1.0f);
        for (float testR : {0.06f, 0.25f, 0.70f}) {
            mat->GetDesc().SheenRoughness = testR;
            std::vector<glm::vec4> furnacePixels;
            if (!renderLinear(world, furnacePixels)) return false;
            float maxRGB = 0.0f;
            for (const auto& px : furnacePixels) {
                if (!std::isfinite(px.r) || !std::isfinite(px.g) || !std::isfinite(px.b) ||
                    px.r < -1e-4f || px.g < -1e-4f || px.b < -1e-4f) {
                    std::cerr << "[M15U9] FAIL (08.1): Non-finite or negative pixel in furnace pass\n";
                    return false;
                }
                maxRGB = std::max({maxRGB, px.r, px.g, px.b});
            }
            if (maxRGB > 1.06f) {
                std::cerr << "[M15U9] FAIL (08.1): White-furnace exceeded unity! r=" << testR
                          << " maxRGB=" << maxRGB << "\n";
                return false;
            }
        }

        // 4. Direct Point (left rim) + LTC Area (right rim) Sheen & Specular Toggle + SheenEnabled=false fallback
        world.Skybox.Enabled = false;
        world.Skybox.EnvironmentLighting = false;
        settings.EnableSkybox = false;

        mat->GetDesc().BaseColor = glm::vec4(0.02f, 0.02f, 0.02f, 1.0f);
        mat->GetDesc().Roughness = 0.92f;
        mat->GetDesc().SheenRoughness = 0.45f;
        mat->GetDesc().SheenColor = glm::vec3(1.0f, 0.85f, 0.70f);

        RenderPointLight rimPoint;
        rimPoint.Position = glm::vec3(-2.2f, 0.0f, 0.8f);
        rimPoint.Radius = 15.0f;
        rimPoint.Color = glm::vec3(1.0f, 1.0f, 1.0f);
        rimPoint.Intensity = 40.0f;

        RenderRectLight rimRect;
        rimRect.Position = glm::vec3(2.2f, 0.0f, 0.8f);
        rimRect.Direction = glm::normalize(glm::vec3(-0.9f, 0.0f, -0.4f));
        rimRect.Up = glm::vec3(0.0f, 1.0f, 0.0f);
        rimRect.Width = 1.6f;
        rimRect.Height = 1.6f;
        rimRect.Range = 15.0f;
        rimRect.Color = glm::vec3(1.0f, 1.0f, 1.0f);
        rimRect.Intensity = 60.0f;
        rimRect.TwoSided = true;

        world.PointLights = {rimPoint};
        world.RectLights = {rimRect};

        mat->GetDesc().SheenEnabled = false;
        std::vector<glm::vec4> directSheenOff;
        if (!renderLinear(world, directSheenOff)) return false;

        mat->GetDesc().SheenEnabled = true;
        mat->GetDesc().EnableSpecularHighlights = false;
        std::vector<glm::vec4> directSpecDisabled;
        if (!renderLinear(world, directSpecDisabled)) return false;

        mat->GetDesc().EnableSpecularHighlights = true;
        std::vector<glm::vec4> directSheenOn;
        if (!renderLinear(world, directSheenOn)) return false;

        const float offPointRim = AverageRingLuminance(directSheenOff, width, height, 45.0f, 52.0f, -1);
        const float specDisPointRim = AverageRingLuminance(directSpecDisabled, width, height, 45.0f, 52.0f, -1);
        const float onPointRim = AverageRingLuminance(directSheenOn, width, height, 45.0f, 52.0f, -1);
        const float offRectRim = AverageRingLuminance(directSheenOff, width, height, 45.0f, 52.0f, +1);
        const float specDisRectRim = AverageRingLuminance(directSpecDisabled, width, height, 45.0f, 52.0f, +1);
        const float onRectRim = AverageRingLuminance(directSheenOn, width, height, 45.0f, 52.0f, +1);
        if (!(onPointRim > offPointRim * 1.25f && onPointRim > specDisPointRim * 2.0f &&
              onRectRim > offRectRim * 1.25f && onRectRim > specDisRectRim * 2.0f)) {
            std::cerr << "[M15U9] FAIL (08.1): Direct Point/LTC Area Sheen response invalid! point(off="
                      << offPointRim << ", specDis=" << specDisPointRim << ", on=" << onPointRim
                      << ") rect(off=" << offRectRim << ", specDis=" << specDisRectRim << ", on=" << onRectRim << ")\n";
            return false;
        }

        // Verify disabled-sheen fallback parity on white base
        mat->GetDesc().BaseColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        mat->GetDesc().SheenEnabled = false;
        std::vector<glm::vec4> sheenOffBaseA, sheenOffBaseB;
        if (!renderLinear(world, sheenOffBaseA)) return false;
        mat->GetDesc().SheenColor = glm::vec3(0.2f, 0.9f, 0.4f);
        mat->GetDesc().SheenRoughness = 0.05f;
        if (!renderLinear(world, sheenOffBaseB)) return false;
        float maxFallbackDiff = 0.0f;
        for (size_t i = 0; i < sheenOffBaseA.size(); ++i) {
            maxFallbackDiff = std::max(maxFallbackDiff, glm::length(glm::vec3(sheenOffBaseA[i] - sheenOffBaseB[i])));
        }
        if (maxFallbackDiff > 1e-4f) {
            std::cerr << "[M15U9] FAIL (08.1): Disabled sheen parameters leaked into output! diff=" << maxFallbackDiff << "\n";
            return false;
        }
        std::cout << "  [08.1 " << backendTag << "] PASS Charlie Sheen: center=" << centerLuma
                  << " grazing=" << grazingLuma << " tightRatio=" << tightRatio << " wideRatio=" << wideRatio
                  << " pointOn=" << onPointRim << " rectOn=" << onRectRim << "\n";
    }

    std::cout << "[M15U9] M15 Unit 9 Batch A GPU tests (" << backendTag << ") passed.\n";
    return true;
}
