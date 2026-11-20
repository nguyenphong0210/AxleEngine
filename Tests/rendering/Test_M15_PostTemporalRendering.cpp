#include "RHIReadback.hpp"
#include <Axle/MaterialSurfaceShader.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace Axle;
using namespace Axle::RHI;

namespace {
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
        desc.DebugName = "U8PostReadback";
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

    bool ReadExposureValue(RHIViewTargets& targets, glm::vec4& outValue) {
        auto* tex = targets.GetExposure();
        if (!tex || !tex->IsValid()) return false;
        std::vector<glm::vec4> pixels;
        if (!ReadTextureRGBA16F(*tex, pixels) || pixels.empty()) return false;
        outValue = pixels[0];
        return std::isfinite(outValue.r) && std::isfinite(outValue.g) &&
               std::isfinite(outValue.b) && std::isfinite(outValue.a);
    }

    std::shared_ptr<RHIViewTargets> MakeExposureViewTargets(uint32_t w, uint32_t h, bool enableBloom) {
        auto targets = std::make_shared<RHIViewTargets>();
        RHIViewTargetDesc desc;
        desc.Width = w;
        desc.Height = h;
        desc.Samples = 1;
        desc.HDR = true;
        desc.EnableDepth = true;
        desc.EnableScreenStorage = true;
        desc.EnableAutoExposure = true;
        desc.EnableBloom = enableBloom;
        desc.OwnPrimary = false;
        if (!targets->Configure(desc, nullptr, nullptr, FroxelHistoryPolicy::AllowStaticFog)) return {};
        return targets;
    }
}

bool RunM15PostTemporalTests(RHISceneRenderer& renderer, const std::filesystem::path& output) {
    std::cout << "[M15U8] Running Milestone 15 Unit 8 Batch A (AutoExposure & Pipeline Order) GPU Tests...\n";
    const auto cube = RHIMesh::CreateCube(1.0f);
    if (!cube) return false;

    // -------------------------------------------------------------------------
    // 1. M15-U8-01.1, 01.2, 05.1: Full-viewport 512x256 mixed bright/dark tiles
    // (> 65,536 px so any stride skip would distort global geometric mean)
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 512, H = 256;
        RHITexture color, depth;
        if (!color.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depth.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targets = MakeExposureViewTargets(W, H, false);
        if (!targets) return false;

        // Tile shader alternating checkerboard of luminance 4.0 and 0.25 (mean = 1.0, target = 0.18)
        std::filesystem::path customShaderPath = std::filesystem::absolute(output / "u8a-mixed-tiles-5050.hlsl");
        {
            std::ofstream out(customShaderPath, std::ios::binary);
            out << AssembleMaterialSurfaceShader("",
                "MaterialSurface AxleEvaluateMaterial(float2 uv) { MaterialSurface s = (MaterialSurface)0; "
                "uint gx = (uint)floor(uv.x * 128.0); uint gy = (uint)floor(uv.y * 128.0); "
                "float val = (((gx + gy) & 1u) == 0u) ? 4.0 : 0.25; "
                "s.BaseColor = float3(0.0, 0.0, 0.0); s.Roughness = 1.0; s.Metallic = 0.0; "
                "s.Normal = float3(0.0, 0.0, 1.0); s.AO = 1.0; s.IOR = 1.5; s.ClearcoatRoughness = 0.04; "
                "s.Emission = float3(val, val, val); return s; }");
        }
        RHIMaterialDesc tileMatDesc{};
        tileMatDesc.BaseColor = {0, 0, 0, 1};
        tileMatDesc.Metallic = 0.0f;
        tileMatDesc.Roughness = 1.0f;
        tileMatDesc.CullMode = CullMode::None;
        auto tileMat = std::make_shared<RHIMaterial>(tileMatDesc);

        RenderWorld tileWorld;
        RenderMeshInstance wall;
        wall.EntityID = 1001;
        wall.NativeView = cube->CaptureView();
        wall.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, 0.0f}), {20.0f, 20.0f, 0.2f});
        wall.Material.RHIMaterialAsset = tileMat;
        wall.Material.UseCustomShader = true;
        wall.Material.CustomShaderPath = customShaderPath.string();
        tileWorld.Meshes.push_back(wall);

        PerspectiveCamera cam(60.0f, static_cast<float>(W) / H, 0.1f, 50.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0, 0, 2.0f), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0)), glm::vec3(0, 0, 2.0f));

        SceneRenderSettings settings;
        settings.EnableShadows = false;
        settings.EnableSkybox = false;
        settings.EnableParticles = false;
        settings.EnableStudioLightLobes = false;
        settings.EnableToneMapping = true;
        settings.ToneMappingMode = 3; // Linear bypass after exposure so output = hdr * exp
        settings.Gamma = 1.0f;
        settings.EnableAutoExposure = true;
        settings.AutoExposureMin = 0.05f;
        settings.AutoExposureMax = 10.0f;
        settings.AutoExposureKeyValue = 0.18f;
        settings.AutoExposureSpeed = 2.0f;

        renderer.RenderWorldView(tileWorld, cam, &color, &depth, settings, nullptr, targets, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;
        glm::vec4 exp5050{};
        if (!ReadExposureValue(*targets, exp5050)) return false;
        if (std::abs(exp5050.g - 1.0f) > 0.08f || std::abs(exp5050.r - 0.18f) > 0.02f || exp5050.a < 0.5f) {
            std::cerr << "[M15U8] FAIL (05.1): 50/50 mixed 512x256 tiles expected avgLuma~1.0 exp~0.18, got luma="
                      << exp5050.g << " exp=" << exp5050.r << " valid=" << exp5050.a << "\n";
            return false;
        }

        // Now asymmetric 25% bright (16.0) + 75% dark (0.25) -> avgLuma ~ 0.7071, target ~ 0.2546
        std::filesystem::path asymShaderPath = std::filesystem::absolute(output / "u8a-mixed-tiles-2575.hlsl");
        {
            std::ofstream out(asymShaderPath, std::ios::binary);
            out << AssembleMaterialSurfaceShader("",
                "MaterialSurface AxleEvaluateMaterial(float2 uv) { MaterialSurface s = (MaterialSurface)0; "
                "uint gx = (uint)floor(uv.x * 128.0) & 3u; "
                "float val = (gx == 0u) ? 16.0 : 0.25; "
                "s.BaseColor = float3(0.0, 0.0, 0.0); s.Roughness = 1.0; s.Metallic = 0.0; "
                "s.Normal = float3(0.0, 0.0, 1.0); s.AO = 1.0; s.IOR = 1.5; s.ClearcoatRoughness = 0.04; "
                "s.Emission = float3(val, val, val); return s; }");
        }
        tileWorld.Meshes[0].Material.CustomShaderPath = asymShaderPath.string();
        ++tileWorld.SceneRevision;
        renderer.RenderWorldView(tileWorld, cam, &color, &depth, settings, nullptr, targets, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;
        glm::vec4 exp2575{};
        if (!ReadExposureValue(*targets, exp2575)) return false;
        const float expectedTarget2575 = 0.18f / 0.70710678f;
        if (std::abs(exp2575.g - 0.7071f) > 0.08f || std::abs(exp2575.r - expectedTarget2575) > 0.03f) {
            std::cerr << "[M15U8] FAIL (05.1): 25/75 asymmetric tiles expected avgLuma~0.7071 exp~" << expectedTarget2575
                      << ", got luma=" << exp2575.g << " exp=" << exp2575.r << "\n";
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // 2. M15-U8-02.1, 02.2, 05.2, 05.3, 05.4: 30 Hz / 60 Hz / 120 Hz invariance
    // -------------------------------------------------------------------------
    auto runAdaptationAtRate = [&](uint32_t steps, float dt, float& outExposure) -> bool {
        constexpr uint32_t W = 64, H = 64;
        RHITexture color, depth;
        if (!color.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depth.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targets = MakeExposureViewTargets(W, H, false);
        if (!targets) return false;

        RenderWorld world;
        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)), glm::vec3(0, 0, 3));

        SceneRenderSettings settings;
        settings.EnableShadows = false;
        settings.EnableSkybox = false;
        settings.EnableParticles = false;
        settings.EnableStudioLightLobes = false;
        settings.EnableToneMapping = true;
        settings.ToneMappingMode = 3;
        settings.Gamma = 1.0f;
        settings.EnableAutoExposure = true;
        settings.AutoExposureMin = 0.05f;
        settings.AutoExposureMax = 10.0f;
        settings.AutoExposureKeyValue = 0.18f;
        settings.AutoExposureSpeed = 2.0f;
        settings.ClearColor = {0.9f, 0.9f, 0.9f, 1.0f}; // target = 0.20 on frame 0

        renderer.RenderWorldView(world, cam, &color, &depth, settings, nullptr, targets, true, RenderViewKind::Main, dt);
        if (!renderer.WasLastRenderSuccessful()) return false;

        settings.ClearColor = {0.045f, 0.045f, 0.045f, 1.0f}; // target = 4.00
        for (uint32_t i = 0; i < steps; ++i) {
            renderer.RenderWorldView(world, cam, &color, &depth, settings, nullptr, targets, false, RenderViewKind::Main, dt);
            if (!renderer.WasLastRenderSuccessful()) return false;
        }
        glm::vec4 expVal{};
        if (!ReadExposureValue(*targets, expVal)) return false;
        outExposure = expVal.r;
        return true;
    };

    float exp30 = 0.0f, exp60 = 0.0f, exp120 = 0.0f;
    if (!runAdaptationAtRate(15u, 1.0f / 30.0f, exp30) ||
        !runAdaptationAtRate(30u, 1.0f / 60.0f, exp60) ||
        !runAdaptationAtRate(60u, 1.0f / 120.0f, exp120)) return false;
    const float expectedHalfSecond = 0.2f + (4.0f - 0.2f) * (1.0f - std::exp(-2.0f * 0.5f));
    if (std::abs(exp30 - expectedHalfSecond) > 0.06f ||
        std::abs(exp60 - expectedHalfSecond) > 0.06f ||
        std::abs(exp120 - expectedHalfSecond) > 0.06f ||
        std::abs(exp30 - exp120) > 0.04f) {
        std::cerr << "[M15U8] FAIL (05.2-05.4): 30/60/120 Hz divergence! exact=" << expectedHalfSecond
                  << " 30Hz=" << exp30 << " 60Hz=" << exp60 << " 120Hz=" << exp120 << "\n";
        return false;
    }

    // -------------------------------------------------------------------------
    // 3. M15-U8-02.3, 03.1, 03.2, 03.3, 03.4: Per-view isolation, ToneMapping,
    // First-frame, Black-frame, Camera-cut
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 64, H = 64;
        RHITexture colorA, depthA, colorB, depthB;
        if (!colorA.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depthA.Init(RHITextureDesc::DepthStencil2D(W, H)) ||
            !colorB.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depthB.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targetsA = MakeExposureViewTargets(W, H, false);
        auto targetsB = MakeExposureViewTargets(W, H, false);
        if (!targetsA || !targetsB) return false;

        RenderWorld world;
        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0)), glm::vec3(0, 0, 3));

        SceneRenderSettings settings;
        settings.EnableShadows = false;
        settings.EnableSkybox = false;
        settings.EnableParticles = false;
        settings.EnableStudioLightLobes = false;
        settings.EnableToneMapping = true;
        settings.ToneMappingMode = 3;
        settings.Gamma = 1.0f;
        settings.EnableAutoExposure = true;
        settings.AutoExposureMin = 0.1f;
        settings.AutoExposureMax = 10.0f;
        settings.AutoExposureKeyValue = 0.18f;
        settings.AutoExposureSpeed = 2.0f;
        settings.Exposure = 1.5f;

        // First-frame snap on A (ClearColor=1.8 -> exp=0.1) and B (ClearColor=0.045 -> exp=4.0)
        settings.ClearColor = {1.8f, 1.8f, 1.8f, 1.0f};
        renderer.RenderWorldView(world, cam, &colorA, &depthA, settings, nullptr, targetsA, false, RenderViewKind::Main, 1.0f / 60.0f);
        settings.ClearColor = {0.045f, 0.045f, 0.045f, 1.0f};
        renderer.RenderWorldView(world, cam, &colorB, &depthB, settings, nullptr, targetsB, false, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        glm::vec4 eA0{}, eB0{};
        if (!ReadExposureValue(*targetsA, eA0) || !ReadExposureValue(*targetsB, eB0)) return false;
        if (std::abs(eA0.r - 0.1f) > 0.01f || std::abs(eB0.r - 4.0f) > 0.1f) {
            std::cerr << "[M15U8] FAIL (03.2): First frame snap mismatch A=" << eA0.r << " B=" << eB0.r << "\n";
            return false;
        }

        // Switch A smoothly to medium (0.18 -> target=1.0) while interleaving B; A must adapt smoothly (~0.1 + delta, NOT jump to B's 4.0 or snap to 1.0!)
        SceneRenderSettings settingsA = settings;
        settingsA.ClearColor = {0.18f, 0.18f, 0.18f, 1.0f};
        renderer.RenderWorldView(world, cam, &colorA, &depthA, settingsA, nullptr, targetsA, false, RenderViewKind::Main, 0.1f);
        renderer.RenderWorldView(world, cam, &colorB, &depthB, settings, nullptr, targetsB, false, RenderViewKind::Main, 0.1f);
        if (!renderer.WasLastRenderSuccessful()) return false;
        glm::vec4 eA1{}, eB1{};
        if (!ReadExposureValue(*targetsA, eA1) || !ReadExposureValue(*targetsB, eB1)) return false;
        const float expectedA1 = 0.1f + (1.0f - 0.1f) * (1.0f - std::exp(-2.0f * 0.1f));
        if (std::abs(eA1.r - expectedA1) > 0.04f || std::abs(eB1.r - 4.0f) > 0.1f) {
            std::cerr << "[M15U8] FAIL (02.3): Per-view isolation/adaptation failed A=" << eA1.r
                      << " expectedA=" << expectedA1 << " B=" << eB1.r << "\n";
            return false;
        }

        // Verify ToneMapping feeds adapted exposure (03.1): linear-bypass pixel on A (hdr=0.18 * exp=eA1.r)
        const auto pixelsA1 = RenderTests::ReadTexture(colorA);
        if (pixelsA1.size() < 4u) return false;
        const float outNorm = static_cast<float>(pixelsA1[0]) / 255.0f;
        const float expectedOut = std::clamp(0.18f * eA1.r, 0.0f, 1.0f);
        if (std::abs(outNorm - expectedOut) > 0.03f) {
            std::cerr << "[M15U8] FAIL (03.1): ToneMapping output did not reflect GPU adapted exposure (got="
                      << outNorm << ", expected=" << expectedOut << ")\n";
            return false;
        }

        // Black frame continuous on A (03.3): holds prior exposure ~eA1.r cleanly, blackFrame flag a==0
        settingsA.ClearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        renderer.RenderWorldView(world, cam, &colorA, &depthA, settingsA, nullptr, targetsA, false, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;
        glm::vec4 eABlack{};
        if (!ReadExposureValue(*targetsA, eABlack)) return false;
        if (eABlack.a > 0.5f || eABlack.g > 1e-4f || std::abs(eABlack.r - eA1.r) > 0.03f) {
            std::cerr << "[M15U8] FAIL (03.3): Continuous black frame spiked or corrupted exposure: ("
                      << eABlack.r << ", " << eABlack.g << ", " << eABlack.b << ", " << eABlack.a << ")\n";
            return false;
        }

        // Camera cut on black frame falls back deterministically to clamped settings.Exposure (1.5)
        renderer.RenderWorldView(world, cam, &colorA, &depthA, settingsA, nullptr, targetsA, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;
        glm::vec4 eABlackCut{};
        if (!ReadExposureValue(*targetsA, eABlackCut)) return false;
        if (eABlackCut.a > 0.5f || std::abs(eABlackCut.r - 1.5f) > 0.03f) {
            std::cerr << "[M15U8] FAIL (03.3/03.4): Black frame on camera cut did not use fallback exposure 1.5 (got="
                      << eABlackCut.r << ")\n";
            return false;
        }

        // Camera cut from bright (1.8 -> 0.1) to dark (0.045 -> 4.0) snaps immediately at slow Speed=0.5, dt=1/60
        settingsA.AutoExposureSpeed = 0.5f;
        settingsA.ClearColor = {1.8f, 1.8f, 1.8f, 1.0f};
        renderer.RenderWorldView(world, cam, &colorA, &depthA, settingsA, nullptr, targetsA, true, RenderViewKind::Main, 1.0f / 60.0f);
        settingsA.ClearColor = {0.045f, 0.045f, 0.045f, 1.0f};
        renderer.RenderWorldView(world, cam, &colorA, &depthA, settingsA, nullptr, targetsA, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;
        glm::vec4 eACut{};
        if (!ReadExposureValue(*targetsA, eACut)) return false;
        if (std::abs(eACut.r - 4.0f) > 0.1f) {
            std::cerr << "[M15U8] FAIL (03.4): Camera cut did not reset/snap exposure to 4.0 (got=" << eACut.r << ")\n";
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // 4. M15-U8-04: Bloom must be excluded from AutoExposure metering
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 128, H = 128;
        RHITexture colorOff, depthOff, colorOn, depthOn;
        if (!colorOff.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depthOff.Init(RHITextureDesc::DepthStencil2D(W, H)) ||
            !colorOn.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depthOn.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targetsOff = MakeExposureViewTargets(W, H, false);
        auto targetsOn = MakeExposureViewTargets(W, H, true);
        if (!targetsOff || !targetsOn) return false;

        RenderWorld world;
        RHIMaterialDesc brightDesc{};
        brightDesc.BaseColor = {0, 0, 0, 1};
        brightDesc.Roughness = 1.0f;
        brightDesc.EmissiveColor = {1.0f, 1.0f, 1.0f};
        brightDesc.EmissiveIntensity = 12.0f;
        auto brightMat = std::make_shared<RHIMaterial>(brightDesc);
        RenderMeshInstance core;
        core.EntityID = 2001;
        core.NativeView = cube->CaptureView();
        core.WorldTransform = glm::scale(glm::mat4(1.0f), glm::vec3(0.4f));
        core.Material.RHIMaterialAsset = brightMat;
        world.Meshes.push_back(core);

        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0, 0, 4.0f), glm::vec3(0), glm::vec3(0, 1, 0)), glm::vec3(0, 0, 4.0f));

        SceneRenderSettings settings;
        settings.EnableShadows = false;
        settings.EnableSkybox = false;
        settings.EnableParticles = false;
        settings.EnableStudioLightLobes = false;
        settings.EnableToneMapping = true;
        settings.ToneMappingMode = 3;
        settings.Gamma = 1.0f;
        settings.ClearColor = {0.09f, 0.09f, 0.09f, 1.0f};
        settings.EnableAutoExposure = true;
        settings.AutoExposureMin = 0.05f;
        settings.AutoExposureMax = 10.0f;
        settings.AutoExposureKeyValue = 0.18f;
        settings.AutoExposureSpeed = 2.0f;

        settings.EnableBloom = false;
        renderer.RenderWorldView(world, cam, &colorOff, &depthOff, settings, nullptr, targetsOff, true, RenderViewKind::Main, 1.0f / 60.0f);

        settings.EnableBloom = true;
        settings.BloomThreshold = 0.2f;
        settings.BloomIntensity = 15.0f;
        settings.BloomRadius = 3.0f;
        renderer.RenderWorldView(world, cam, &colorOn, &depthOn, settings, nullptr, targetsOn, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        glm::vec4 expOff{}, expOn{};
        if (!ReadExposureValue(*targetsOff, expOff) || !ReadExposureValue(*targetsOn, expOn)) return false;
        const auto pxOff = RenderTests::ReadTexture(colorOff);
        const auto pxOn = RenderTests::ReadTexture(colorOn);
        uint64_t sumOff = 0, sumOn = 0;
        for (size_t i = 0; i < pxOff.size(); i += 4) {
            sumOff += pxOff[i];
            sumOn += pxOn[i];
        }
        if (std::abs(expOn.r - expOff.r) > 0.005f || std::abs(expOn.g - expOff.g) > 0.005f || sumOn <= sumOff) {
            std::cerr << "[M15U8] FAIL (04): Bloom polluted AutoExposure! expOff=" << expOff.r
                      << " expOn=" << expOn.r << " sumOff=" << sumOff << " sumOn=" << sumOn << "\n";
            return false;
        }
    }

    std::cout << "[M15U8] PASS: full-viewport log histogram, 30/60/120Hz dt invariance, per-view isolation, edge resets & Bloom exclusion (exp30="
              << exp30 << ", exp60=" << exp60 << ", exp120=" << exp120 << ")\n";
    return true;
}
