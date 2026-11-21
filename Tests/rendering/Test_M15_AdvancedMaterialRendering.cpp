#include "RHIReadback.hpp"
#include "RenderTestCommon.hpp"
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/MaterialGraph.hpp>
#include <Axle/MaterialGraphProgram.hpp>
#include <Axle/Material.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Package.hpp>
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

bool RunM15AdvancedMaterialTests(RHISceneRenderer& /*sharedRenderer*/, const std::filesystem::path& output) {
    auto& device = RHIDevice::Get();
    RHISceneRenderer renderer;
    if (!renderer.Init(device, false)) {
        std::cerr << "[M15U9] FAIL: Could not initialize isolated RHISceneRenderer\n";
        return false;
    }
    struct RendererShutdownGuard {
        RHISceneRenderer& Target;
        ~RendererShutdownGuard() { Target.Shutdown(); }
    } shutdownGuard{renderer};
    const char* backendTag = device.GetBackend() == RHIType::Vulkan ? "Vulkan" : "D3D12";
    std::cout << "[M15U9] Running Milestone 15 Unit 9 GPU tests (" << backendTag << ")...\n";
    auto didCompositeSSS = [&] {
        const auto& passes = renderer.GetDiagnostics().GetFrame().Passes;
        return std::any_of(passes.begin(), passes.end(), [](const auto& pass) {
            return pass.Name == "SSSBlurV" && pass.Status == DiagnosticStatus::Success;
        });
    };

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

    // Subpixel tiles should converge to the mean of the two colors, including
    // when a grazing view samples many tiles on one axis in a single pixel.
    {
        auto plane = RHIMesh::CreatePlane(1.0f, 1.0f, 1, 1);
        RENDER_CHECK(plane && plane->IsValid(), "procedural checker plane is available");
        RenderWorld world;
        auto& floor = world.Meshes.emplace_back();
        floor.NativeView = plane->CaptureView();
        floor.WorldTransform = glm::scale(glm::mat4(1.0f), glm::vec3(150.0f, 1.0f, 150.0f));
        floor.Material.PatternMode = 0;
        floor.Material.TileScale = glm::vec2(400.0f);
        floor.Material.AlbedoColor = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
        floor.Material.SecondaryColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
        auto& light = world.DirectionalLight.emplace();
        light.Direction = glm::normalize(glm::vec3(0.4f, 1.0f, 0.5f));
        light.Color = glm::vec3(1.0f);

        auto floorSettings = settings;
        floorSettings.EnableSkybox = false;
        PerspectiveCamera floorCamera(45.0f, 1.0f, 0.1f, 100.0f);
        const glm::vec3 eye(0.16f, 0.45f, 3.3f);
        floorCamera.SetViewMatrix(glm::lookAtRH(eye, glm::vec3(-0.7f, 0.0f, 0.0f), glm::vec3(0, 1, 0)), eye);
        auto renderFloor = [&](std::vector<glm::vec4>& pixels) {
            renderer.RenderWorldView(world, floorCamera, &color, &depth, floorSettings, nullptr, targets);
            return renderer.WasLastRenderSuccessful() && ReadTextureRGBA16F(*targets->Get(RHIViewTarget::HDR), pixels);
        };
        std::vector<glm::vec4> checker, reference;
        RENDER_CHECK(renderFloor(checker), "render distant procedural checker at a grazing angle");
        floor.Material.AlbedoColor = floor.Material.SecondaryColor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
        RENDER_CHECK(renderFloor(reference), "render reference with the mean checker color");
        float maxRelativeDelta = 0.0f;
        for (uint32_t y = 70; y < 96; ++y) {
            for (uint32_t x = 36; x < 92; ++x) {
                const size_t index = static_cast<size_t>(y) * width + x;
                const float gray = Luminance(reference[index]);
                RENDER_CHECK(gray > 0.05f, "checker comparison patch covers the lit plane");
                maxRelativeDelta = std::max(maxRelativeDelta, std::abs(Luminance(checker[index]) - gray) / gray);
            }
        }
        std::cout << "  [procedural checker] maxRelativeDelta=" << maxRelativeDelta << '\n';
        RENDER_CHECK(maxRelativeDelta < 0.08f, "subpixel checker tiles converge to their mean radiance");
    }

    // =========================================================================
    // Batch B (M15-U9-02, M15-U9-03, M15-U9-08.2): Beer-Lambert Absorption,
    // Cauchy Wavelength Dispersion & Opaque Fallback Parity
    // =========================================================================
    {
        auto cube = RHIMesh::CreateCube(1.0f);
        if (!cube) {
            std::cerr << "[M15U9] FAIL (08.2): Could not create cube backdrop mesh\n";
            return false;
        }

        // 1. Exponential Beer-Lambert absorption curve & color shift through flat glass slab (IOR=1.0 -> Fresnel=0)
        RHIMaterialDesc whiteWallDesc;
        whiteWallDesc.SurfaceType = SurfaceType::Opaque;
        whiteWallDesc.BaseColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        whiteWallDesc.Roughness = 1.0f;
        whiteWallDesc.EmissiveColor = glm::vec3(1.0f, 1.0f, 1.0f);
        whiteWallDesc.EmissiveIntensity = 1.0f;
        auto whiteWallMat = std::make_shared<RHIMaterial>(whiteWallDesc);

        RHIMaterialDesc slabDesc;
        slabDesc.SurfaceType = SurfaceType::Transparent;
        slabDesc.BaseColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.05f);
        slabDesc.Roughness = 0.05f;
        slabDesc.Metallic = 0.0f;
        slabDesc.RefractionEnabled = true;
        slabDesc.RefractionStrength = 1.0f;
        slabDesc.RefractionIOR = 1.0f;
        slabDesc.RefractionThickness = 0.25f;
        slabDesc.AbsorptionEnabled = true;
        slabDesc.AbsorptionColor = glm::vec3(3.0f, 0.5f, 1.5f);
        slabDesc.Dispersion = 0.0f;
        auto slabMat = std::make_shared<RHIMaterial>(slabDesc);

        RenderWorld absWorld;
        absWorld.Skybox.Enabled = false;
        absWorld.Skybox.EnvironmentLighting = false;

        RenderMeshInstance wallInst;
        wallInst.EntityID = 201;
        wallInst.NativeView = cube->CaptureView();
        wallInst.Material.RHIMaterialAsset = whiteWallMat;
        wallInst.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.5f)),
                                             glm::vec3(8.0f, 8.0f, 0.2f));

        RenderMeshInstance slabInst;
        slabInst.EntityID = 202;
        slabInst.NativeView = cube->CaptureView();
        slabInst.Material.RHIMaterialAsset = slabMat;
        slabInst.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
                                             glm::vec3(1.8f, 1.8f, 0.2f));
        absWorld.Meshes = {wallInst, slabInst};

        auto sampleCenterRGB = [&](const std::vector<glm::vec4>& pixels) {
            glm::vec3 accum(0.0f);
            for (uint32_t y = 62; y <= 65; ++y) {
                for (uint32_t x = 62; x <= 65; ++x) {
                    accum += glm::vec3(pixels[static_cast<size_t>(y) * width + x]);
                }
            }
            return accum / 16.0f;
        };

        std::vector<glm::vec4> absPx;
        slabMat->GetDesc().AbsorptionEnabled = false;
        if (!renderLinear(absWorld, absPx)) return false;
        const glm::vec3 unabsorbed = sampleCenterRGB(absPx);
        if (glm::length(unabsorbed - glm::vec3(1.0f)) > 0.03f) {
            std::cerr << "[M15U9] FAIL (08.2): Unabsorbed transmission did not preserve white backdrop! got=("
                      << unabsorbed.r << "," << unabsorbed.g << "," << unabsorbed.b << ")\n";
            return false;
        }

        slabMat->GetDesc().AbsorptionEnabled = true;
        const glm::vec3 sigma = slabMat->GetDesc().AbsorptionColor;
        for (float thick : {0.25f, 0.65f, 1.25f}) {
            slabMat->GetDesc().RefractionThickness = thick;
            if (!renderLinear(absWorld, absPx)) return false;
            const glm::vec3 measured = sampleCenterRGB(absPx);
            const glm::vec3 expected = glm::exp(-sigma * thick);
            if (glm::length(measured - expected) > 0.03f || !(measured.g > measured.b && measured.b > measured.r)) {
                std::cerr << "[M15U9] FAIL (08.2): Beer-Lambert transmittance mismatch at d=" << thick
                          << " measured=(" << measured.r << "," << measured.g << "," << measured.b
                          << ") expected=(" << expected.r << "," << expected.g << "," << expected.b << ")\n";
                return false;
            }
        }

        // Negative absorption clamp check (<= 0 never amplifies > 1.0)
        slabMat->GetDesc().AbsorptionColor = glm::vec3(-4.0f, 0.0f, -1.0f);
        slabMat->GetDesc().RefractionThickness = 1.0f;
        if (!renderLinear(absWorld, absPx)) return false;
        const glm::vec3 negClamped = sampleCenterRGB(absPx);
        if (glm::length(negClamped - glm::vec3(1.0f)) > 0.03f) {
            std::cerr << "[M15U9] FAIL (08.2): Negative absorption coefficient amplified/altered radiance! ("
                      << negClamped.r << "," << negClamped.g << "," << negClamped.b << ")\n";
            return false;
        }

        // 2. Cauchy chromatic dispersion across crisp black/white step backdrop behind refracting sphere
        RHIMaterialDesc darkWallDesc = whiteWallDesc;
        darkWallDesc.EmissiveColor = glm::vec3(0.02f, 0.02f, 0.02f);
        auto darkWallMat = std::make_shared<RHIMaterial>(darkWallDesc);

        RenderMeshInstance leftWall = wallInst;
        leftWall.EntityID = 203;
        leftWall.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, -2.5f)),
                                             glm::vec3(4.0f, 8.0f, 0.2f));
        RenderMeshInstance rightWall = wallInst;
        rightWall.EntityID = 204;
        rightWall.Material.RHIMaterialAsset = darkWallMat;
        rightWall.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, -2.5f)),
                                              glm::vec3(4.0f, 8.0f, 0.2f));

        slabMat->GetDesc().AbsorptionEnabled = false;
        slabMat->GetDesc().RefractionIOR = 1.65f;
        slabMat->GetDesc().RefractionThickness = 1.20f;
        slabMat->GetDesc().RefractionStrength = 1.0f;
        RenderMeshInstance dispPrism;
        dispPrism.EntityID = 205;
        dispPrism.NativeView = cube->CaptureView();
        dispPrism.Material.RHIMaterialAsset = slabMat;
        dispPrism.WorldTransform = glm::scale(
            glm::rotate(glm::mat4(1.0f), glm::radians(35.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::vec3(1.6f, 1.6f, 0.2f));

        RenderWorld dispWorld;
        dispWorld.Skybox.Enabled = false;
        dispWorld.Skybox.EnvironmentLighting = false;
        dispWorld.Meshes = {leftWall, rightWall, dispPrism};

        slabMat->GetDesc().Dispersion = 0.0f;
        std::vector<glm::vec4> noDispPx;
        if (!renderLinear(dispWorld, noDispPx)) return false;

        slabMat->GetDesc().Dispersion = 0.60f;
        std::vector<glm::vec4> dispPx;
        if (!renderLinear(dispWorld, dispPx)) return false;

        auto analyzeChromaAcrossPrism = [&](const std::vector<glm::vec4>& pixels, float& outMaxRB, float& outMinRB) {
            outMaxRB = -10.0f;
            outMinRB = 10.0f;
            for (uint32_t y = 44; y <= 84; ++y) {
                for (uint32_t x = 36; x <= 92; ++x) {
                    const glm::vec4& px = pixels[static_cast<size_t>(y) * width + x];
                    const float diff = px.r - px.b;
                    outMaxRB = std::max(outMaxRB, diff);
                    outMinRB = std::min(outMinRB, diff);
                }
            }
        };
        float noDispMax = 0.0f, noDispMin = 0.0f, dispMax = 0.0f, dispMin = 0.0f;
        analyzeChromaAcrossPrism(noDispPx, noDispMax, noDispMin);
        analyzeChromaAcrossPrism(dispPx, dispMax, dispMin);
        if (!(std::max(std::abs(noDispMax), std::abs(noDispMin)) < 0.04f &&
              (dispMax - dispMin > 0.25f) && (dispMax > 0.20f || dispMin < -0.20f))) {
            std::cerr << "[M15U9] FAIL (08.2): Cauchy chromatic dispersion did not separate RGB channels cleanly! noDisp=("
                      << noDispMin << "," << noDispMax << ") disp=(" << dispMin << "," << dispMax << ")\n";
            return false;
        }

        // 3. Opaque fallback unaffected by Absorption / Dispersion / Refraction flags
        slabMat->GetDesc().SurfaceType = SurfaceType::Opaque;
        slabMat->GetDesc().BaseColor = glm::vec4(0.35f, 0.45f, 0.55f, 1.0f);
        slabMat->GetDesc().AbsorptionEnabled = true;
        slabMat->GetDesc().AbsorptionColor = glm::vec3(4.0f, 1.0f, 0.2f);
        slabMat->GetDesc().Dispersion = 0.6f;
        slabMat->GetDesc().RefractionEnabled = true;
        std::vector<glm::vec4> opaqueWithAdv;
        if (!renderLinear(dispWorld, opaqueWithAdv)) return false;

        slabMat->GetDesc().AbsorptionEnabled = false;
        slabMat->GetDesc().Dispersion = 0.0f;
        slabMat->GetDesc().RefractionEnabled = false;
        std::vector<glm::vec4> opaqueClean;
        if (!renderLinear(dispWorld, opaqueClean)) return false;

        float maxOpaqueDiff = 0.0f;
        for (size_t i = 0; i < opaqueWithAdv.size(); ++i) {
            maxOpaqueDiff = std::max(maxOpaqueDiff, glm::length(glm::vec3(opaqueWithAdv[i] - opaqueClean[i])));
        }
        if (maxOpaqueDiff > 1e-4f) {
            std::cerr << "[M15U9] FAIL (08.2): Absorption/Dispersion leaked into opaque fallback! diff=" << maxOpaqueDiff << "\n";
            return false;
        }
        std::cout << "  [08.2 " << backendTag << "] PASS Beer-Lambert & Cauchy: sigma(3,0.5,1.5) unabs=("
                  << unabsorbed.r << "," << unabsorbed.g << "," << unabsorbed.b << ") dispFringe=("
                  << dispMin << "," << dispMax << ") opaqueDiff=" << maxOpaqueDiff << "\n";
    }

    // =========================================================================
    // Batch C (M15-U9-04.1..04.3, M15-U9-05, M15-U9-08.3):
    // Screen-Space Separable SSS Diffusion, Silhouette & Boundary Isolation,
    // and Crisp Specular Recombination
    // =========================================================================
    {
        auto sssTargets = std::make_shared<RHIViewTargets>();
        {
            RHIViewTargetDesc sssTargetDesc;
            sssTargetDesc.Width = width;
            sssTargetDesc.Height = height;
            sssTargetDesc.Samples = 1;
            sssTargetDesc.HDR = true;
            sssTargetDesc.EnableDepth = true;
            sssTargetDesc.EnableNormal = true;
            sssTargetDesc.EnableSubsurface = true;
            sssTargetDesc.OwnPrimary = false;
            if (!sssTargets->Configure(sssTargetDesc, nullptr, nullptr, FroxelHistoryPolicy::AllowStaticFog)) {
                std::cerr << "[M15U9] FAIL (08.3): Could not configure SSS view targets\n";
                return false;
            }
        }

        auto cube = RHIMesh::CreateCube(1.0f);
        if (!cube) {
            std::cerr << "[M15U9] FAIL (08.3): Could not create cube mesh\n";
            return false;
        }

        auto renderSSSLinear = [&](const RenderWorld& world, bool enableSSS, std::vector<glm::vec4>& outPixels) -> bool {
            SceneRenderSettings sssSettings = settings;
            sssSettings.EnableSubsurfaceScattering = enableSSS;
            renderer.RenderWorldView(world, camera, &color, &depth, sssSettings, nullptr, sssTargets);
            if (!renderer.WasLastRenderSuccessful()) {
                std::cerr << "[M15U9] FAIL (08.3): RenderWorldView(SSS) failed ("
                          << renderer.GetDiagnostics().GetFrame().Fallbacks.size() << " fallbacks)\n";
                return false;
            }
            auto* hdr = sssTargets->Get(RHIViewTarget::HDR);
            return hdr && ReadTextureRGBA16F(*hdr, outPixels);
        };

        auto worldToPixelX = [&](float wx, float wz) -> uint32_t {
            const glm::mat4 vp = camera.GetProjectionMatrixZO() * camera.GetViewMatrix();
            const glm::vec4 clip = vp * glm::vec4(wx, 0.0f, wz, 1.0f);
            const float u = 0.5f * (clip.x / clip.w) + 0.5f;
            return static_cast<uint32_t>(std::clamp(std::lround(u * static_cast<float>(width - 1u)), 0l, static_cast<long>(width - 1u)));
        };

        // 1. Terminator diffuse scatter across continuous SSS surface
        RHIMaterialDesc skinDesc;
        skinDesc.SurfaceType = SurfaceType::Opaque;
        skinDesc.BaseColor = glm::vec4(0.75f, 0.72f, 0.70f, 1.0f);
        skinDesc.Roughness = 0.90f;
        skinDesc.Metallic = 0.0f;
        skinDesc.EnableSpecularHighlights = false;
        skinDesc.SubsurfaceEnabled = true;
        skinDesc.SubsurfaceColor = glm::vec3(1.0f, 0.25f, 0.05f);
        skinDesc.SubsurfaceRadius = 6.0f;
        auto skinMat = std::make_shared<RHIMaterial>(skinDesc);

        // A side light puts the geometric terminator between the center columns.
        // Sample the wrapped lobe before diffusion can hide a jump in the forward shader.
        auto terminatorMaterial = skinDesc;
        terminatorMaterial.BaseColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        RenderMeshInstance terminatorSphere;
        terminatorSphere.NativeView = sphere->CaptureView();
        terminatorSphere.Material.RHIMaterialAsset = std::make_shared<RHIMaterial>(terminatorMaterial);
        RenderWorld terminatorWorld;
        terminatorWorld.Meshes = {terminatorSphere};
        auto& sideLight = terminatorWorld.DirectionalLight.emplace();
        sideLight.Direction = glm::vec3(1.0f, 0.0f, 0.0f);
        sideLight.Color = glm::vec3(1.0f);
        sideLight.AmbientIntensity = 0.0f;
        std::vector<glm::vec4> terminatorPixels;
        if (!renderSSSLinear(terminatorWorld, false, terminatorPixels)) return false;
        const size_t center = static_cast<size_t>(height / 2) * width + width / 2;
        const float shadowSide = terminatorPixels[center - 1u].r;
        const float litSide = terminatorPixels[center].r;
        std::cout << "  [U9 smooth terminator] shadow=" << shadowSide << " lit=" << litSide << '\n';
        RENDER_CHECK(shadowSide > 0.05f && litSide > shadowSide && (litSide - shadowSide) < 0.03f,
            "M15-U9-04.3: wrapped SSS stays continuous across the diffuse light terminator");

        RenderMeshInstance contSlab;
        contSlab.EntityID = 301;
        contSlab.NativeView = cube->CaptureView();
        contSlab.Material.RHIMaterialAsset = skinMat;
        contSlab.WorldTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1.8f, 1.8f, 0.2f));

        RenderSpotLight termSpot;
        termSpot.EntityID = 302;
        termSpot.Position = glm::vec3(-0.45f, 0.0f, 2.0f);
        termSpot.Direction = glm::vec3(0.0f, 0.0f, -1.0f);
        termSpot.Color = glm::vec3(1.0f, 1.0f, 1.0f);
        termSpot.Intensity = 45.0f;
        termSpot.Range = 20.0f;
        termSpot.InnerAngle = 5.0f;
        termSpot.OuterAngle = 10.0f;

        RenderWorld sssWorld;
        sssWorld.Skybox.Enabled = false;
        sssWorld.Skybox.EnvironmentLighting = false;
        sssWorld.Meshes = {contSlab};
        sssWorld.SpotLights = {termSpot};

        std::vector<glm::vec4> pxOff, pxOn;
        if (!renderSSSLinear(sssWorld, false, pxOff) || !renderSSSLinear(sssWorld, true, pxOn)) return false;

        // Just past the spot cutoff edge (x ~ -0.06..+0.02 -> ~px 61..65; test well past cutoff near x = -0.03..+0.04)
        const uint32_t xStart = worldToPixelX(-0.07f, 0.1f);
        const uint32_t xEnd = worldToPixelX(0.03f, 0.1f);
        glm::vec3 meanOff(0.0f), meanOn(0.0f);
        uint32_t tapCount = 0u;
        for (uint32_t y = 60u; y <= 67u; ++y) {
            for (uint32_t x = xStart; x <= xEnd; ++x) {
                meanOff += glm::vec3(pxOff[static_cast<size_t>(y) * width + x]);
                meanOn += glm::vec3(pxOn[static_cast<size_t>(y) * width + x]);
                ++tapCount;
            }
        }
        meanOff /= static_cast<float>(std::max(tapCount, 1u));
        meanOn /= static_cast<float>(std::max(tapCount, 1u));
        const float bleedGainR = meanOn.r - meanOff.r;
        if (!(bleedGainR > 0.025f && meanOn.r > meanOn.g * 1.15f && meanOn.g > meanOn.b)) {
            std::cerr << "[M15U9] FAIL (08.3): Expected warm subsurface scatter past light terminator! off=("
                      << meanOff.r << "," << meanOff.g << "," << meanOff.b << ") on=("
                      << meanOn.r << "," << meanOn.g << "," << meanOn.b << ")\n";
            return false;
        }

        // 2a. Depth-jump silhouette isolation: bright SSS left slab vs distant dark non-SSS backdrop on right
        RHIMaterialDesc darkDesc;
        darkDesc.SurfaceType = SurfaceType::Opaque;
        darkDesc.BaseColor = glm::vec4(0.02f, 0.02f, 0.02f, 1.0f);
        darkDesc.Roughness = 0.90f;
        darkDesc.SubsurfaceEnabled = false;
        auto darkMat = std::make_shared<RHIMaterial>(darkDesc);

        RenderMeshInstance leftHalf;
        leftHalf.EntityID = 303;
        leftHalf.NativeView = cube->CaptureView();
        leftHalf.Material.RHIMaterialAsset = skinMat;
        leftHalf.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-0.48f, 0.0f, 0.0f)),
                                             glm::vec3(0.90f, 1.8f, 0.2f)); // Right edge x = -0.03

        RenderMeshInstance rightDeep;
        rightDeep.EntityID = 304;
        rightDeep.NativeView = cube->CaptureView();
        rightDeep.Material.RHIMaterialAsset = darkMat;
        rightDeep.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.45f, 0.0f, -2.5f)),
                                              glm::vec3(1.8f, 3.6f, 0.2f));

        termSpot.InnerAngle = 15.0f;
        termSpot.OuterAngle = 20.0f; // Illuminate right up to leftHalf edge x = -0.03!
        sssWorld.Meshes = {leftHalf, rightDeep};
        sssWorld.SpotLights = {termSpot};

        if (!renderSSSLinear(sssWorld, false, pxOff) || !renderSSSLinear(sssWorld, true, pxOn)) return false;
        const uint32_t isoStart = worldToPixelX(0.02f, 0.1f);
        const uint32_t isoEnd = std::min(width - 2u, isoStart + 12u);
        float maxDepthJumpBleed = 0.0f;
        for (uint32_t y = 56u; y <= 71u; ++y) {
            for (uint32_t x = isoStart; x <= isoEnd; ++x) {
                maxDepthJumpBleed = std::max(maxDepthJumpBleed,
                    glm::length(glm::vec3(pxOn[static_cast<size_t>(y) * width + x] - pxOff[static_cast<size_t>(y) * width + x])));
            }
        }
        if (maxDepthJumpBleed > 1e-3f) {
            std::cerr << "[M15U9] FAIL (08.3): Subsurface scatter bled across depth silhouette jump! diff="
                      << maxDepthJumpBleed << "\n";
            return false;
        }

        // 2b. Coplanar dissimilar-SSS touching seam isolation (zero depth delta, same normal, different scatter color)
        RHIMaterialDesc blueSSSDesc = skinDesc;
        blueSSSDesc.BaseColor = glm::vec4(0.02f, 0.02f, 0.02f, 1.0f);
        blueSSSDesc.SubsurfaceColor = glm::vec3(0.0f, 0.15f, 1.0f);
        auto blueSSSMat = std::make_shared<RHIMaterial>(blueSSSDesc);

        RenderMeshInstance rightTouch = leftHalf;
        rightTouch.EntityID = 305;
        rightTouch.Material.RHIMaterialAsset = blueSSSMat;
        rightTouch.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.42f, 0.0f, 0.0f)),
                                               glm::vec3(0.90f, 1.8f, 0.2f)); // Left edge x = -0.03 (coplanar z=+0.1!)

        // Keep spot cone strictly inside leftHalf (x <= -0.045) while illuminating right up to ~1px of the seam.
        termSpot.Position = glm::vec3(-0.40f, 0.0f, 2.0f);
        termSpot.InnerAngle = 6.0f;
        termSpot.OuterAngle = 10.5f;
        sssWorld.Meshes = {leftHalf, rightTouch};
        sssWorld.SpotLights = {termSpot};

        if (!renderSSSLinear(sssWorld, false, pxOff) || !renderSSSLinear(sssWorld, true, pxOn)) return false;
        float maxSeamBleed = 0.0f;
        for (uint32_t y = 56u; y <= 71u; ++y) {
            for (uint32_t x = isoStart; x <= isoEnd; ++x) {
                const glm::vec3 d = glm::vec3(pxOn[static_cast<size_t>(y) * width + x] - pxOff[static_cast<size_t>(y) * width + x]);
                maxSeamBleed = std::max(maxSeamBleed, std::abs(d.r));
            }
        }
        if (maxSeamBleed > 2e-3f) {
            std::cerr << "[M15U9] FAIL (08.3): Subsurface scatter bled across coplanar dissimilar-material boundary! diffR="
                      << maxSeamBleed << "\n";
            return false;
        }

        // Adjacent objects can have identical scatter settings and still need an isolated seam.
        auto matchingProfile = blueSSSDesc;
        matchingProfile.SubsurfaceColor = skinDesc.SubsurfaceColor;
        rightTouch.Material.RHIMaterialAsset = std::make_shared<RHIMaterial>(matchingProfile);
        sssWorld.Meshes = {leftHalf, rightTouch};
        if (!renderSSSLinear(sssWorld, false, pxOff) || !renderSSSLinear(sssWorld, true, pxOn)) return false;
        float matchingProfileBleed = 0.0f;
        for (uint32_t y = 56u; y <= 71u; ++y) {
            for (uint32_t x = isoStart; x <= isoEnd; ++x) {
                const size_t index = static_cast<size_t>(y) * width + x;
                matchingProfileBleed = std::max(matchingProfileBleed, std::abs(pxOn[index].r - pxOff[index].r));
            }
        }
        std::cout << "  [U9 same-profile seam] deltaR=" << matchingProfileBleed << '\n';
        RENDER_CHECK(matchingProfileBleed <= 2e-3f, "M15-U9-04.3: no diffusion across adjacent objects with matching SSS profiles");

        // 3. Crisp Specular Peak Preservation (Specular separated before separable H/V diffusion, then recombined)
        RHIMaterialDesc glossySSSDesc;
        glossySSSDesc.SurfaceType = SurfaceType::Opaque;
        glossySSSDesc.BaseColor = glm::vec4(0.03f, 0.03f, 0.03f, 1.0f);
        glossySSSDesc.Roughness = 0.07f;
        glossySSSDesc.Metallic = 0.0f;
        glossySSSDesc.EnableSpecularHighlights = true;
        glossySSSDesc.SubsurfaceEnabled = true;
        glossySSSDesc.SubsurfaceColor = glm::vec3(1.0f, 0.35f, 0.15f);
        glossySSSDesc.SubsurfaceRadius = 12.0f;
        auto glossySSSMat = std::make_shared<RHIMaterial>(glossySSSDesc);

        RenderMeshInstance glossySphere;
        glossySphere.EntityID = 306;
        glossySphere.NativeView = sphere->CaptureView();
        glossySphere.Material.RHIMaterialAsset = glossySSSMat;

        RenderPointLight glintLight;
        glintLight.EntityID = 307;
        glintLight.Position = glm::vec3(0.0f, 0.0f, 2.5f);
        glintLight.Color = glm::vec3(1.0f, 1.0f, 1.0f);
        glintLight.Intensity = 18.0f;
        glintLight.Radius = 20.0f;

        RenderWorld specWorld;
        specWorld.Skybox.Enabled = false;
        specWorld.Skybox.EnvironmentLighting = false;
        specWorld.Meshes = {glossySphere};
        specWorld.PointLights = {glintLight};

        if (!renderSSSLinear(specWorld, false, pxOff) || !renderSSSLinear(specWorld, true, pxOn)) return false;
        float specPeakOff = 0.0f, specPeakOn = 0.0f;
        for (uint32_t y = 58u; y <= 69u; ++y) {
            for (uint32_t x = 58u; x <= 69u; ++x) {
                specPeakOff = std::max(specPeakOff, Luminance(pxOff[static_cast<size_t>(y) * width + x]));
                specPeakOn = std::max(specPeakOn, Luminance(pxOn[static_cast<size_t>(y) * width + x]));
            }
        }
        if (!(specPeakOff > 1.5f && specPeakOn >= specPeakOff * 0.90f)) {
            std::cerr << "[M15U9] FAIL (08.3): Crisp specular peak was attenuated/blurred by SSS! off="
                      << specPeakOff << " on=" << specPeakOn << "\n";
            return false;
        }

        std::cout << "  [08.3 " << backendTag << "] PASS Separable SSS: termBleedR=" << bleedGainR
                  << " silhouetteBleed=" << maxDepthJumpBleed << " seamBleed=" << maxSeamBleed
                  << " specPeak(off=" << specPeakOff << ", on=" << specPeakOn << ")\n";
    }

    // =========================================================================
    // Batch D (M15-U9-07.1, M15-U9-08.4): Built-in vs Material Graph Parity
    // on Charlie Sheen & Separable Subsurface Scattering (Vulkan & D3D12)
    // =========================================================================
    {
        auto buildGraphParityShader = [&](const glm::vec3& baseColor,
                                          float metallic,
                                          float roughness,
                                          const glm::vec3& sheenColor,
                                          float sheenRoughness,
                                          const glm::vec3& sssColor,
                                          float sssRadius,
                                          const char* filename,
                                          std::vector<ShaderPropertyInstance>& outProps) -> bool {
            MaterialGraphDocument doc;
            doc.Identity = 94001ULL;
            doc.Name = "M_U9ParityGraph";
            doc.CullMode = "None";
            doc.Parameters = {
                {"u_BaseColor", {MaterialValueType::Float3, {baseColor.r, baseColor.g, baseColor.b}}, {}},
                {"u_Metallic", {MaterialValueType::Float, {metallic}}, {}},
                {"u_Roughness", {MaterialValueType::Float, {roughness}}, {}},
                {"u_SheenColor", {MaterialValueType::Float3, {sheenColor.r, sheenColor.g, sheenColor.b}}, {}},
                {"u_SheenRoughness", {MaterialValueType::Float, {sheenRoughness}}, {}},
                {"u_SSSColor", {MaterialValueType::Float3, {sssColor.r, sssColor.g, sssColor.b}}, {}},
                {"u_SSSRadius", {MaterialValueType::Float, {sssRadius}}, {}}
            };
            const auto* masterDef = MaterialNodeRegistry::Builtins().Find("PBRMasterOutput");
            if (!masterDef || masterDef->Signatures.size() < 2) return false;
            auto makeParam = [&](uint64_t id, const char* name, MaterialValueType type) {
                MaterialGraphNode n;
                n.ID = id;
                n.Type = "ParameterRef";
                n.Attributes = {{"parameter", std::string(name), {}}};
                n.Outputs = {{"value", type, {}, {}}};
                return n;
            };
            doc.Nodes = {
                makeParam(10, "u_BaseColor", MaterialValueType::Float3),
                makeParam(11, "u_Metallic", MaterialValueType::Float),
                makeParam(12, "u_Roughness", MaterialValueType::Float),
                makeParam(13, "u_SheenColor", MaterialValueType::Float3),
                makeParam(14, "u_SheenRoughness", MaterialValueType::Float),
                makeParam(15, "u_SSSColor", MaterialValueType::Float3),
                makeParam(16, "u_SSSRadius", MaterialValueType::Float)
            };
            MaterialGraphNode master;
            master.ID = 1;
            master.Type = "PBRMasterOutput";
            for (const auto& pin : masterDef->Signatures[1].Inputs) {
                master.Inputs.push_back({pin.Name, *pin.Type, pin.Default, {}});
            }
            doc.Nodes.push_back(std::move(master));
            doc.Connections = {
                {{10, "value"}, {1, "BaseColor"}, {}},
                {{11, "value"}, {1, "Metallic"}, {}},
                {{12, "value"}, {1, "Roughness"}, {}},
                {{13, "value"}, {1, "SheenColor"}, {}},
                {{14, "value"}, {1, "SheenRoughness"}, {}},
                {{15, "value"}, {1, "SubsurfaceColor"}, {}},
                {{16, "value"}, {1, "SubsurfaceRadius"}, {}}
            };

            const auto fmt = (device.GetBackend() == RHIType::Vulkan)
                ? RHIShaderFormat::SPIRV
                : RHIShaderFormat::DXIL;
            CompiledMaterialGraphProgram prog;
            MaterialGraphDiagnostic diag;
            if (!MaterialGraphProgram::Compile(doc, fmt, prog, &diag)) {
                std::cerr << "[M15U9] FAIL (08.4): MaterialGraphProgram::Compile failed: " << diag.Message << "\n";
                return false;
            }
            if (!FileSystem::WriteTextFileAtomically(output / filename, prog.HLSL)) {
                std::cerr << "[M15U9] FAIL (08.4): Failed writing parity shader to " << (output / filename) << "\n";
                return false;
            }
            outProps = std::move(prog.DefaultProperties);
            return true;
        };

        auto checkCustomDiag = [&](uint32_t entityID, const char* label) -> bool {
            const auto& mats = renderer.GetDiagnostics().GetFrame().Materials;
            for (const auto& entry : mats) {
                if (entry.EntityID == entityID) {
                    if (entry.Evaluation == SurfaceEvaluation::CustomShader && !entry.Fallback &&
                        entry.UnsupportedFeatures == 0u && entry.ShaderGeneration != 0u) {
                        return true;
                    }
                    std::cerr << "[M15U9] FAIL (08.4 " << label << "): unexpected graph eval state eval="
                              << static_cast<int>(entry.Evaluation) << " fallback=" << entry.Fallback
                              << " unsup=" << entry.UnsupportedFeatures << " reason='" << entry.Reason << "'\n";
                    return false;
                }
            }
            std::cerr << "[M15U9] FAIL (08.4 " << label << "): entity missing from frame diagnostics\n";
            return false;
        };

        // 1. 08.4a: Built-in vs Material Graph Charlie Sheen Parity (Point + LTC Area + IBL)
        RHIMaterialDesc sheenMatDesc;
        sheenMatDesc.SurfaceType = SurfaceType::Opaque;
        sheenMatDesc.BaseColor = glm::vec4(0.08f, 0.03f, 0.03f, 1.0f);
        sheenMatDesc.Metallic = 0.0f;
        sheenMatDesc.Roughness = 0.85f;
        sheenMatDesc.CullMode = CullMode::None;
        sheenMatDesc.SheenEnabled = true;
        sheenMatDesc.SheenColor = glm::vec3(1.0f, 0.85f, 0.70f);
        sheenMatDesc.SheenRoughness = 0.30f;
        auto sheenMat = std::make_shared<RHIMaterial>(sheenMatDesc);

        RenderMeshInstance sheenInst;
        sheenInst.EntityID = 401;
        sheenInst.NativeView = sphere->CaptureView();
        sheenInst.Material.RHIMaterialAsset = sheenMat;

        RenderPointLight pLight;
        pLight.Position = glm::vec3(-2.2f, 0.0f, 0.8f);
        pLight.Radius = 15.0f;
        pLight.Color = glm::vec3(1.0f);
        pLight.Intensity = 30.0f;

        RenderRectLight rLight;
        rLight.Position = glm::vec3(2.2f, 0.0f, 0.8f);
        rLight.Direction = glm::normalize(glm::vec3(-0.9f, 0.0f, -0.4f));
        rLight.Up = glm::vec3(0.0f, 1.0f, 0.0f);
        rLight.Width = 1.6f;
        rLight.Height = 1.6f;
        rLight.Range = 15.0f;
        rLight.Color = glm::vec3(1.0f);
        rLight.Intensity = 45.0f;
        rLight.TwoSided = true;

        RenderWorld parityWorld;
        parityWorld.Meshes = {sheenInst};
        parityWorld.PointLights = {pLight};
        parityWorld.RectLights = {rLight};
        parityWorld.Skybox.Authored = true;
        parityWorld.Skybox.Enabled = true;
        parityWorld.Skybox.EnvironmentLighting = true;
        parityWorld.Skybox.Type = 1;
        parityWorld.Skybox.HDRPath = "u9_white_furnace.hdr";
        parityWorld.Skybox.Intensity = 1.0f;

        std::vector<glm::vec4> builtinSheenPx, graphSheenPx;
        if (!renderLinear(parityWorld, builtinSheenPx)) return false;

        std::vector<ShaderPropertyInstance> sheenProps;
        if (!buildGraphParityShader(glm::vec3(0.08f, 0.03f, 0.03f), 0.0f, 0.85f,
                                    sheenMatDesc.SheenColor, sheenMatDesc.SheenRoughness,
                                    glm::vec3(0.0f), 0.0f, "u9d_sheen_parity.hlsl", sheenProps)) {
            return false;
        }
        parityWorld.Meshes[0].Material.UseCustomShader = true;
        parityWorld.Meshes[0].Material.CustomShaderPath = "u9d_sheen_parity.hlsl";
        parityWorld.Meshes[0].Material.CustomProperties = sheenProps;
        if (!renderLinear(parityWorld, graphSheenPx) || !checkCustomDiag(401, "Sheen")) return false;

        const float biCenter = AverageRingLuminance(builtinSheenPx, width, height, 0.0f, 12.0f);
        const float mgCenter = AverageRingLuminance(graphSheenPx, width, height, 0.0f, 12.0f);
        const float biPointRim = AverageRingLuminance(builtinSheenPx, width, height, 42.0f, 50.0f, -1);
        const float mgPointRim = AverageRingLuminance(graphSheenPx, width, height, 42.0f, 50.0f, -1);
        const float biAreaRim = AverageRingLuminance(builtinSheenPx, width, height, 42.0f, 50.0f, +1);
        const float mgAreaRim = AverageRingLuminance(graphSheenPx, width, height, 42.0f, 50.0f, +1);

        auto relDiff = [](float a, float b) { return std::abs(a - b) / std::max(std::max(std::abs(a), std::abs(b)), 1e-4f); };
        const float sheenCenterErr = relDiff(biCenter, mgCenter);
        const float sheenPointErr = relDiff(biPointRim, mgPointRim);
        const float sheenAreaErr = relDiff(biAreaRim, mgAreaRim);
        if (sheenCenterErr > 0.05f || sheenPointErr > 0.05f || sheenAreaErr > 0.05f) {
            std::cerr << "[M15U9] FAIL (08.4a): Built-in vs Graph Sheen parity diverged! center("
                      << biCenter << " vs " << mgCenter << ") pointRim(" << biPointRim << " vs " << mgPointRim
                      << ") areaRim(" << biAreaRim << " vs " << mgAreaRim << ")\n";
            return false;
        }

        // 2. 08.4b: Built-in vs Material Graph Separable SSS + Glossy Specular Parity
        auto sssTargets = std::make_shared<RHIViewTargets>();
        {
            RHIViewTargetDesc sssTargetDesc;
            sssTargetDesc.Width = width;
            sssTargetDesc.Height = height;
            sssTargetDesc.Samples = 1;
            sssTargetDesc.HDR = true;
            sssTargetDesc.EnableDepth = true;
            sssTargetDesc.EnableNormal = true;
            sssTargetDesc.EnableSubsurface = true;
            sssTargetDesc.OwnPrimary = false;
            if (!sssTargets->Configure(sssTargetDesc, nullptr, nullptr, FroxelHistoryPolicy::AllowStaticFog)) {
                return false;
            }
        }
        auto cube = RHIMesh::CreateCube(1.0f);
        if (!cube) return false;

        RHIMaterialDesc sssMatDesc;
        sssMatDesc.SurfaceType = SurfaceType::Opaque;
        sssMatDesc.BaseColor = glm::vec4(0.65f, 0.62f, 0.60f, 1.0f);
        sssMatDesc.Metallic = 0.0f;
        sssMatDesc.Roughness = 0.25f;
        sssMatDesc.CullMode = CullMode::None;
        sssMatDesc.SubsurfaceEnabled = true;
        sssMatDesc.SubsurfaceColor = glm::vec3(1.0f, 0.25f, 0.05f);
        sssMatDesc.SubsurfaceRadius = 6.0f;
        auto sssMat = std::make_shared<RHIMaterial>(sssMatDesc);

        RenderMeshInstance slabInst;
        slabInst.EntityID = 402;
        slabInst.NativeView = cube->CaptureView();
        slabInst.Material.RHIMaterialAsset = sssMat;
        slabInst.WorldTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1.8f, 1.8f, 0.2f));

        RenderSpotLight sssSpot;
        sssSpot.EntityID = 403;
        sssSpot.Position = glm::vec3(-0.45f, 0.0f, 2.0f);
        sssSpot.Direction = glm::vec3(0.0f, 0.0f, -1.0f);
        sssSpot.Color = glm::vec3(1.0f);
        sssSpot.Intensity = 40.0f;
        sssSpot.Range = 20.0f;
        sssSpot.InnerAngle = 5.0f;
        sssSpot.OuterAngle = 10.0f;

        RenderWorld sssParityWorld;
        sssParityWorld.Skybox.Enabled = false;
        sssParityWorld.Skybox.EnvironmentLighting = false;
        sssParityWorld.Meshes = {slabInst};
        sssParityWorld.SpotLights = {sssSpot};

        auto renderSSS = [&](const RenderWorld& w, std::vector<glm::vec4>& outPx) -> bool {
            SceneRenderSettings sssSettings = settings;
            sssSettings.EnableSubsurfaceScattering = true;
            renderer.RenderWorldView(w, camera, &color, &depth, sssSettings, nullptr, sssTargets);
            if (!renderer.WasLastRenderSuccessful()) return false;
            auto* hdr = sssTargets->Get(RHIViewTarget::HDR);
            return hdr && ReadTextureRGBA16F(*hdr, outPx);
        };

        std::vector<glm::vec4> builtinSSSPx, graphSSSPx;
        if (!renderSSS(sssParityWorld, builtinSSSPx)) return false;

        std::vector<ShaderPropertyInstance> sssProps;
        if (!buildGraphParityShader(glm::vec3(0.65f, 0.62f, 0.60f), 0.0f, 0.25f,
                                    glm::vec3(0.0f), -1.0f,
                                    sssMatDesc.SubsurfaceColor, sssMatDesc.SubsurfaceRadius,
                                    "u9d_sss_parity.hlsl", sssProps)) {
            return false;
        }
        sssParityWorld.Meshes[0].Material.UseCustomShader = true;
        sssParityWorld.Meshes[0].Material.CustomShaderPath = "u9d_sss_parity.hlsl";
        sssParityWorld.Meshes[0].Material.CustomProperties = sssProps;
        auto graphFallback = sssMatDesc;
        graphFallback.SubsurfaceEnabled = false;
        sssParityWorld.Meshes[0].Material.RHIMaterialAsset = std::make_shared<RHIMaterial>(graphFallback);
        if (!renderSSS(sssParityWorld, graphSSSPx) || !checkCustomDiag(402, "SSS")) return false;

        const auto explicitPixels = RenderTests::ReadTexture(color);
        RENDER_CHECK(!explicitPixels.empty(), "M15-U9-07.1: read explicit graph SSS view");
        SceneRenderSettings graphSettings = settings;
        graphSettings.EnableSubsurfaceScattering = false;
        renderer.RenderWorldView(sssParityWorld, camera, &color, &depth, graphSettings);
        const auto unscattered = RenderTests::ReadTexture(color);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && unscattered.size() == explicitPixels.size() &&
                     unscattered != explicitPixels, "M15-U9-07.1: graph-authored diffusion changes the lit surface");
        graphSettings.EnableSubsurfaceScattering = true;
        renderer.RenderWorldView(sssParityWorld, camera, &color, &depth, graphSettings);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && didCompositeSSS() &&
                     RenderTests::ReadTexture(color) == explicitPixels,
            "M15-U9-07.1: graph-only SSS allocates implicit targets and matches the explicit SSS view");

        // A cooked product can remove the loose shader after writing its package.
        const auto packagePath = output / "u9_material_graph.axlepak";
        const std::string packagedShader = "u9_packaged_sss.hlsl";
        PackageWriter writer;
        RENDER_CHECK(writer.Begin(packagePath) &&
                     writer.AddFile(packagedShader, output / "u9d_sss_parity.hlsl") &&
                     writer.Finish() && FileSystem::MountPackage(packagePath),
            "M15-U9-06.6: mount generated graph from a cooked package");
        {
            struct UnmountGuard { ~UnmountGuard() { FileSystem::UnmountPackage(); } } unmount;
            RENDER_CHECK(!std::filesystem::exists(output / packagedShader),
                "M15-U9-06.6: cooked shader has no loose copy");
            sssParityWorld.Meshes[0].Material.CustomShaderPath = packagedShader;
            renderer.RenderWorldView(sssParityWorld, camera, &color, &depth, graphSettings);
            RENDER_CHECK(renderer.WasLastRenderSuccessful() && didCompositeSSS() && checkCustomDiag(402, "cooked SSS") &&
                         RenderTests::ReadTexture(color) == explicitPixels,
                "M15-U9-06.6: package-only graph SSS matches loose shader pixels");
        }

        Scene graphScene;
        auto entity = graphScene.CreateEntity("Graph-only SSS");
        entity.AddComponent<MeshInstance3D>().AssetPath = "builtin://sphere";
        auto& component = entity.AddComponent<MaterialComponent>();
        component.UseCustomShader = true;
        component.CustomShaderPath = "u9d_sss_parity.hlsl";
        RENDER_CHECK(SceneRequiresSubsurfacePass(graphScene, graphSettings, 1u) &&
                     !SceneRequiresSubsurfacePass(graphScene, graphSettings, 4u),
            "M15-U9-06.5: preview opts in to single-sample diffusion for shader-authored SSS");

        float maxSSSPatchErr = 0.0f;
        for (uint32_t y = 52u; y <= 75u; ++y) {
            for (uint32_t x = 32u; x <= 76u; ++x) {
                const glm::vec3 a = glm::vec3(builtinSSSPx[static_cast<size_t>(y) * width + x]);
                const glm::vec3 b = glm::vec3(graphSSSPx[static_cast<size_t>(y) * width + x]);
                const float denom = std::max({1.0f, a.r, a.g, a.b, b.r, b.g, b.b});
                maxSSSPatchErr = std::max(maxSSSPatchErr, glm::length(a - b) / denom);
            }
        }
        if (maxSSSPatchErr > 0.05f) {
            std::cerr << "[M15U9] FAIL (08.4b): Built-in vs Graph SSS parity exceeded tolerance! maxRelErr="
                      << maxSSSPatchErr << "\n";
            return false;
        }

        std::cout << "  [08.4 " << backendTag << "] PASS Built-in vs MaterialGraph Parity: sheenRelErr=("
                  << sheenCenterErr << "," << sheenPointErr << "," << sheenAreaErr
                  << ") sssMaxRelErr=" << maxSSSPatchErr << "\n";
    }

    // =========================================================================
    // SDR Runtime uses the legacy forward display transform. Preserve emissive radiance in SSS.
    {
        RHIMaterialDesc material{};
        material.BaseColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        material.EmissiveColor = glm::vec3(1.0f, 0.62f, 0.17f);
        material.EmissiveIntensity = 8.0f;
        material.SubsurfaceEnabled = true;
        material.SubsurfaceRadius = 6.0f;
        RenderMeshInstance emitter;
        emitter.EntityID = 420;
        emitter.NativeView = sphere->CaptureView();
        emitter.Material.RHIMaterialAsset = std::make_shared<RHIMaterial>(material);
        RenderWorld world;
        world.Meshes.push_back(emitter);
        SceneRenderSettings s;
        s.EnableSkybox = false;
        s.EnableShadows = false;
        s.EnableParticles = false;
        s.EnableToneMapping = false;
        s.EnableSubsurfaceScattering = false;
        RHITexture bgraColor;
        RENDER_CHECK(bgraColor.Init(RHITextureDesc::RenderTarget2D(width, height, RHITextureFormat::BGRA8_UNORM)),
            "M15-U9-05: initialize native BGRA SDR view");
        renderer.RenderWorldView(world, camera, &bgraColor, &depth, s);
        RENDER_CHECK(renderer.WasLastRenderSuccessful(), "M15-U9-05: render SDR emissive reference");
        const auto reference = RenderTests::ReadTexture(bgraColor);
        s.EnableSubsurfaceScattering = true;
        renderer.RenderWorldView(world, camera, &bgraColor, &depth, s);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && didCompositeSSS(),
            "M15-U9-05: render SDR subsurface composite with native BGRA attachments");
        const auto composite = RenderTests::ReadTexture(bgraColor);
        const size_t center = (static_cast<size_t>(height / 2) * width + width / 2) * 4u;
        RENDER_CHECK(reference.size() > center + 3u && composite.size() == reference.size() && reference[center + 1] >= 250 &&
            std::abs(int(reference[center]) - int(composite[center])) <= 1 &&
            std::abs(int(reference[center + 1]) - int(composite[center + 1])) <= 1 &&
            std::abs(int(reference[center + 2]) - int(composite[center + 2])) <= 1,
            "M15-U9-05: SDR diffusion preserves crisp radiance above one before tone mapping");
    }

    // Batch E (M15-U9-06.2, 06.5, 06.6): Scene Save/Reopen -> ExtractRenderWorld -> GPU Render
    // =========================================================================
    {
        auto rtTargets = std::make_shared<RHIViewTargets>();
        {
            RHIViewTargetDesc rtDesc;
            rtDesc.Width = width;
            rtDesc.Height = height;
            rtDesc.Samples = 1;
            rtDesc.HDR = true;
            rtDesc.EnableDepth = true;
            rtDesc.EnableNormal = true;
            rtDesc.EnableSubsurface = true;
            rtDesc.OwnPrimary = false;
            if (!rtTargets->Configure(rtDesc, nullptr, nullptr, FroxelHistoryPolicy::AllowStaticFog)) {
                std::cerr << "[M15U9] FAIL (06.5/06.6): Could not configure runtime/preview SSS view targets\n";
                return false;
            }
        }

        Scene saveScene;
        {
            auto lightEnt = saveScene.CreateEntity("U9KeyLight");
            auto& dlc = lightEnt.AddComponent<DirectionalLightComponent>();
            dlc.Elevation = 15.0f;
            dlc.Azimuth = -70.0f;
            dlc.Color = glm::vec3(1.0f);
            dlc.Intensity = 4.0f;
            dlc.AmbientIntensity = 0.02f;
            dlc.CastShadows = false;

            auto matEnt = saveScene.CreateEntity("U9PersistedSphere");
            auto& meshComp = matEnt.AddComponent<MeshInstance3D>();
            meshComp.AssetPath = "builtin://sphere";
            meshComp.RHIMeshAsset = sphere;
            auto& mc = matEnt.AddComponent<MaterialComponent>();
            mc.SurfaceType = SurfaceType::Opaque;
            mc.AlbedoColor = glm::vec4(0.18f, 0.14f, 0.14f, 1.0f);
            mc.Roughness = 0.85f;
            mc.Metallic = 0.0f;
            mc.EnableSpecularHighlights = false;
            mc.SheenEnabled = true;
            mc.SheenColor = glm::vec3(1.0f, 0.90f, 0.82f);
            mc.SheenRoughness = 0.25f;
            mc.SubsurfaceEnabled = true;
            mc.SubsurfaceColor = glm::vec3(1.0f, 0.20f, 0.10f);
            mc.SubsurfaceRadius = 8.0f;
        }

        const std::filesystem::path sceneFile = output / "u9e_runtime_scene.axle";
        if (!SceneSerializer(&saveScene).Serialize(sceneFile)) {
            std::cerr << "[M15U9] FAIL (06.2): SceneSerializer::Serialize failed\n";
            return false;
        }

        Scene runtimeScene;
        if (!SceneSerializer(&runtimeScene).Deserialize(sceneFile)) {
            std::cerr << "[M15U9] FAIL (06.2/06.6): SceneSerializer::Deserialize failed\n";
            return false;
        }

        SceneRenderSettings rtSettings = settings;
        rtSettings.EnableSubsurfaceScattering = true;
        if (!SceneRequiresSubsurfacePass(runtimeScene, rtSettings, 1u) ||
            SceneRequiresSubsurfacePass(runtimeScene, rtSettings, 4u)) {
            std::cerr << "[M15U9] FAIL (06.5): SceneRequiresSubsurfacePass mismatch on deserialized runtime scene\n";
            return false;
        }

        SceneExtractionOptions extOpts;
        extOpts.CameraOverride = &camera;
        RenderWorld rtWorld = ExtractRenderWorld(runtimeScene, extOpts);
        if (rtWorld.Meshes.size() != 1u || !rtWorld.Meshes[0].Material.RHIMaterialAsset) {
            std::cerr << "[M15U9] FAIL (06.3/06.6): ExtractRenderWorld missing persisted mesh or RHIMaterialAsset\n";
            return false;
        }
        // Re-attach procedural sphere RHI mesh handle if builtin:// lookup did not bind RHI mesh offline
        if (!rtWorld.Meshes[0].NativeView || !rtWorld.Meshes[0].NativeView->IsValid()) {
            rtWorld.Meshes[0].NativeMesh = sphere;
            rtWorld.Meshes[0].NativeView = sphere->CaptureView();
        }

        std::vector<glm::vec4> runtimePixels;
        renderer.RenderWorldView(rtWorld, camera, &color, &depth, rtSettings, nullptr, rtTargets);
        if (!renderer.WasLastRenderSuccessful() ||
            !rtTargets->Get(RHIViewTarget::HDR) ||
            !ReadTextureRGBA16F(*rtTargets->Get(RHIViewTarget::HDR), runtimePixels)) {
            std::cerr << "[M15U9] FAIL (06.6): Deserialized scene GPU render failed\n";
            return false;
        }

        // Disable U9 terms on an identical baseline pass and confirm non-zero Sheen + SSS response
        auto baseMat = std::make_shared<RHIMaterial>(*rtWorld.Meshes[0].Material.RHIMaterialAsset);
        baseMat->SetSheenEnabled(false);
        baseMat->SetSubsurfaceEnabled(false);
        rtWorld.Meshes[0].Material.RHIMaterialAsset = baseMat;
        rtWorld.Meshes[0].Material.SheenEnabled = false;
        rtWorld.Meshes[0].Material.SubsurfaceEnabled = false;
        std::vector<glm::vec4> basePixels;
        SceneRenderSettings offSettings = settings;
        offSettings.EnableSubsurfaceScattering = false;
        renderer.RenderWorldView(rtWorld, camera, &color, &depth, offSettings, nullptr, rtTargets);
        if (!renderer.WasLastRenderSuccessful() ||
            !rtTargets->Get(RHIViewTarget::HDR) ||
            !ReadTextureRGBA16F(*rtTargets->Get(RHIViewTarget::HDR), basePixels)) {
            std::cerr << "[M15U9] FAIL (06.6): Baseline GPU render failed\n";
            return false;
        }

        const float rimRuntime = AverageRingLuminance(runtimePixels, width, height, 42.0f, 51.0f);
        const float rimBase = AverageRingLuminance(basePixels, width, height, 42.0f, 51.0f);
        double termRedRuntime = 0.0, termRedBase = 0.0;
        uint32_t termCount = 0u;
        for (uint32_t y = 52u; y <= 76u; ++y) {
            for (uint32_t x = 58u; x <= 70u; ++x) {
                termRedRuntime += static_cast<double>(runtimePixels[static_cast<size_t>(y) * width + x].r);
                termRedBase += static_cast<double>(basePixels[static_cast<size_t>(y) * width + x].r);
                ++termCount;
            }
        }
        const float rLift = termCount > 0u ? static_cast<float>((termRedRuntime - termRedBase) / termCount) : 0.0f;
        const float rimLift = rimRuntime - rimBase;
        if (rimLift <= 0.01f || rLift <= 0.005f) {
            std::cerr << "[M15U9] FAIL (06.5/06.6): Persisted U9 Sheen/SSS effects did not propagate to GPU! rimLift="
                      << rimLift << " termRedLift=" << rLift << "\n";
            return false;
        }

        std::cout << "  [06.5/06.6 " << backendTag << "] PASS Runtime Scene Persistence -> GPU: rimLift="
                  << rimLift << " termRedLift=" << rLift << "\n";
    }

    std::cout << "[M15U9] M15 Unit 9 Batch A, B, C, D & E GPU tests (" << backendTag << ") passed.\n";
    return true;
}
