#include "RHIReadback.hpp"
#include "RenderTestCommon.hpp"
#include <Axle/MaterialSurfaceShader.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderBackendPolicy.hpp>
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
#include <filesystem>
#include <fstream>
#include <iomanip>
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

    bool ReadTextureRG16F(RHITexture& tex, std::vector<glm::vec2>& outPixels) {
        if (!tex.IsValid() || tex.GetFormat() != RHITextureFormat::RG16_FLOAT) return false;
        auto& device = RHIDevice::Get();
        const auto alignment = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
        const auto align = [](uint32_t value, uint32_t step) { return (value + step - 1u) / step * step; };
        const uint32_t width = tex.GetWidth();
        const uint32_t height = tex.GetHeight();
        constexpr uint32_t pixelSize = 4u;

        nri::TextureDataLayoutDesc layout{};
        layout.rowPitch = align(width * pixelSize, alignment.uploadBufferTextureRow);
        layout.slicePitch = align(layout.rowPitch * height, alignment.uploadBufferTextureSlice);

        RHIBufferDesc desc{};
        desc.Size = layout.slicePitch;
        desc.MemoryLocation = RHIMemoryLocation::HostReadback;
        desc.DebugName = "U8VelocityReadback";
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
                uint32_t packed = 0;
                std::memcpy(&packed, row + x * pixelSize, sizeof(packed));
                outPixels[static_cast<size_t>(y) * width + x] = glm::unpackHalf2x16(packed);
            }
        }
        staging.Unmap();
        return true;
    }

    bool ReadTextureR8(RHITexture& tex, std::vector<uint8_t>& outPixels) {
        if (!tex.IsValid() || tex.GetFormat() != RHITextureFormat::R8_UNORM) return false;
        auto& device = RHIDevice::Get();
        const auto alignment = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
        const auto align = [](uint32_t value, uint32_t step) { return (value + step - 1u) / step * step; };
        const uint32_t width = tex.GetWidth();
        const uint32_t height = tex.GetHeight();

        nri::TextureDataLayoutDesc layout{};
        layout.rowPitch = align(width, alignment.uploadBufferTextureRow);
        layout.slicePitch = align(layout.rowPitch * height, alignment.uploadBufferTextureSlice);

        RHIBufferDesc desc{};
        desc.Size = layout.slicePitch;
        desc.MemoryLocation = RHIMemoryLocation::HostReadback;
        desc.DebugName = "U8ReactiveReadback";
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
            std::memcpy(outPixels.data() + static_cast<size_t>(y) * width, row, width);
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

    // =========================================================================
    // Batch B (M15-U8-10.1..10.4): Physically-Based 6-Level Additive Bloom Tests
    // =========================================================================
    auto makeBloomViewTargets = [](uint32_t w, uint32_t h, bool hdr, bool enableBloom = true,
                                   RHITextureFormat format = RHITextureFormat::RGBA8_UNORM) -> std::shared_ptr<RHIViewTargets> {
        auto targets = std::make_shared<RHIViewTargets>();
        RHIViewTargetDesc desc;
        desc.Width = w;
        desc.Height = h;
        desc.ColorFormat = format;
        desc.Samples = 1;
        desc.HDR = hdr;
        desc.EnableDepth = true;
        desc.EnableAutoExposure = false;
        desc.EnableBloom = enableBloom;
        desc.OwnPrimary = false;
        if (!targets->Configure(desc, nullptr, nullptr, FroxelHistoryPolicy::AllowStaticFog)) return {};
        return targets;
    };

    // -------------------------------------------------------------------------
    // 5. M15-U8-10.1: Small HDR impulse halo retains finer inner core & wider halo
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 256, H = 256;
        RHITexture colorOff, depthOff, colorOn, depthOn;
        if (!colorOff.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depthOff.Init(RHITextureDesc::DepthStencil2D(W, H)) ||
            !colorOn.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depthOn.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targetsOff = makeBloomViewTargets(W, H, true, false);
        auto targetsOn = makeBloomViewTargets(W, H, true, true);
        if (!targetsOff || !targetsOn) return false;

        RenderWorld world;
        RHIMaterialDesc emitterDesc{};
        emitterDesc.BaseColor = {0, 0, 0, 1};
        emitterDesc.Roughness = 1.0f;
        emitterDesc.EmissiveColor = {1.0f, 1.0f, 1.0f};
        emitterDesc.EmissiveIntensity = 18.0f;
        auto emitterMat = std::make_shared<RHIMaterial>(emitterDesc);
        RenderMeshInstance emitter;
        emitter.EntityID = 3001;
        emitter.NativeView = cube->CaptureView();
        emitter.WorldTransform = glm::scale(glm::mat4(1.0f), glm::vec3(0.14f));
        emitter.Material.RHIMaterialAsset = emitterMat;
        world.Meshes.push_back(emitter);

        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0, 0, 4.0f), glm::vec3(0), glm::vec3(0, 1, 0)), glm::vec3(0, 0, 4.0f));

        SceneRenderSettings settings;
        settings.EnableShadows = false;
        settings.EnableSkybox = false;
        settings.EnableParticles = false;
        settings.EnableStudioLightLobes = false;
        settings.EnableAutoExposure = false;
        settings.EnableToneMapping = true;
        settings.ToneMappingMode = 3;
        settings.Gamma = 1.0f;
        settings.Exposure = 1.0f;
        settings.ClearColor = {0.0f, 0.0f, 0.0f, 1.0f};

        settings.EnableBloom = false;
        renderer.RenderWorldView(world, cam, &colorOff, &depthOff, settings, nullptr, targetsOff, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        settings.EnableBloom = true;
        settings.BloomThreshold = 0.5f;
        settings.BloomKnee = 0.1f;
        settings.BloomIntensity = 2.0f;
        settings.BloomRadius = 1.5f;
        renderer.RenderWorldView(world, cam, &colorOn, &depthOn, settings, nullptr, targetsOn, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        std::vector<glm::vec4> bloom0Pixels;
        if (!ReadTextureRGBA16F(*targetsOn->GetBloom(0), bloom0Pixels)) return false;
        for (const auto& p : bloom0Pixels) {
            if (!std::isfinite(p.r) || !std::isfinite(p.g) || !std::isfinite(p.b) || p.r < 0.0f) {
                std::cerr << "[M15U8] FAIL (10.1): Non-finite or negative pixel in Bloom0\n";
                return false;
            }
        }
        const uint32_t bw = targetsOn->GetBloom(0)->GetWidth();
        const uint32_t bh = targetsOn->GetBloom(0)->GetHeight();
        auto sampleRingLuma = [&](uint32_t minR, uint32_t maxR) -> double {
            const float cx = static_cast<float>(bw) * 0.5f - 0.5f;
            const float cy = static_cast<float>(bh) * 0.5f - 0.5f;
            double total = 0.0;
            uint32_t count = 0;
            for (uint32_t y = 0; y < bh; ++y) {
                for (uint32_t x = 0; x < bw; ++x) {
                    const float dx = static_cast<float>(x) - cx;
                    const float dy = static_cast<float>(y) - cy;
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    if (dist >= static_cast<float>(minR) && dist < static_cast<float>(maxR)) {
                        const auto& c = bloom0Pixels[static_cast<size_t>(y) * bw + x];
                        total += static_cast<double>(c.r + c.g + c.b) / 3.0;
                        ++count;
                    }
                }
            }
            return count ? (total / static_cast<double>(count)) : 0.0;
        };
        const double innerCore = sampleRingLuma(0u, 3u);
        const double midRing = sampleRingLuma(6u, 12u);
        const double outerHalo = sampleRingLuma(18u, 30u);
        if (!(innerCore > midRing * 1.4 && midRing > outerHalo && outerHalo > 0.002)) {
            std::cerr << "[M15U8] FAIL (10.1): Impulse radial falloff broken! inner=" << innerCore
                      << " mid=" << midRing << " outer=" << outerHalo << "\n";
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // 6. M15-U8-10.2: Energy gain scales linearly with BloomIntensity & single BloomTint
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 64, H = 64;
        RHITexture cOff, dOff, c1x, d1x, c2x, d2x, cTint, dTint;
        if (!cOff.Init(RHITextureDesc::RenderTarget2D(W, H)) || !dOff.Init(RHITextureDesc::DepthStencil2D(W, H)) ||
            !c1x.Init(RHITextureDesc::RenderTarget2D(W, H)) || !d1x.Init(RHITextureDesc::DepthStencil2D(W, H)) ||
            !c2x.Init(RHITextureDesc::RenderTarget2D(W, H)) || !d2x.Init(RHITextureDesc::DepthStencil2D(W, H)) ||
            !cTint.Init(RHITextureDesc::RenderTarget2D(W, H)) || !dTint.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto tOff = makeBloomViewTargets(W, H, false, false);
        auto t1x = makeBloomViewTargets(W, H, false, true);
        auto t2x = makeBloomViewTargets(W, H, false, true);
        auto tTint = makeBloomViewTargets(W, H, false, true);
        if (!tOff || !t1x || !t2x || !tTint) return false;

        RenderWorld emptyWorld;
        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        SceneRenderSettings s;
        s.EnableShadows = false;
        s.EnableSkybox = false;
        s.EnableParticles = false;
        s.EnableStudioLightLobes = false;
        s.EnableAutoExposure = false;
        s.EnableToneMapping = false; // Linear [0..1] additive readout in SDR target without 8-bit saturation
        s.ClearColor = {0.34f, 0.34f, 0.34f, 1.0f};
        s.BloomThreshold = 0.2f;
        s.BloomKnee = 0.0f;
        s.BloomRadius = 1.0f;

        s.EnableBloom = false;
        renderer.RenderWorldView(emptyWorld, cam, &cOff, &dOff, s, nullptr, tOff, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        s.EnableBloom = true;
        s.BloomIntensity = 0.5f;
        s.BloomTint = {1.0f, 1.0f, 1.0f};
        renderer.RenderWorldView(emptyWorld, cam, &c1x, &d1x, s, nullptr, t1x, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        s.BloomIntensity = 1.0f;
        s.BloomTint = {1.0f, 1.0f, 1.0f};
        renderer.RenderWorldView(emptyWorld, cam, &c2x, &d2x, s, nullptr, t2x, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        s.BloomIntensity = 1.0f;
        s.BloomTint = {1.0f, 0.25f, 0.0f};
        renderer.RenderWorldView(emptyWorld, cam, &cTint, &dTint, s, nullptr, tTint, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        const auto pxOff = RenderTests::ReadTexture(cOff);
        const auto px1x = RenderTests::ReadTexture(c1x);
        const auto px2x = RenderTests::ReadTexture(c2x);
        const auto pxTint = RenderTests::ReadTexture(cTint);
        const size_t centerIdx = (static_cast<size_t>(H / 2) * W + (W / 2)) * 4u;
        const int baseR = pxOff[centerIdx + 0];
        const int delta1x = static_cast<int>(px1x[centerIdx + 0]) - baseR;
        const int delta2x = static_cast<int>(px2x[centerIdx + 0]) - baseR;
        const int tintDeltaR = static_cast<int>(pxTint[centerIdx + 0]) - baseR;
        const int tintDeltaG = static_cast<int>(pxTint[centerIdx + 1]) - static_cast<int>(pxOff[centerIdx + 1]);
        const int tintDeltaB = static_cast<int>(pxTint[centerIdx + 2]) - static_cast<int>(pxOff[centerIdx + 2]);
        const float ratio2x = delta1x > 0 ? static_cast<float>(delta2x) / static_cast<float>(delta1x) : 0.0f;
        const float tintRatioG = tintDeltaR > 0 ? static_cast<float>(tintDeltaG) / static_cast<float>(tintDeltaR) : 0.0f;
        if (delta1x < 14 || std::abs(ratio2x - 2.0f) > 0.15f ||
            tintDeltaB > 2 || std::abs(tintRatioG - 0.25f) > 0.08f) {
            std::cerr << "[M15U8] FAIL (10.2): Linearity or single-tint broken! base=" << baseR
                      << " d1x=" << delta1x << " d2x=" << delta2x << " tint(R,G,B)=("
                      << tintDeltaR << "," << tintDeltaG << "," << tintDeltaB << ")\n";
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // 7. M15-U8-10.3 & 10.4: Odd/small viewports (3x5, 127x63) + HDR/SDR parity
    // -------------------------------------------------------------------------
    struct OddBloomCase {
        uint32_t W = 0u;
        uint32_t H = 0u;
        bool HDR = true;
        RHITextureFormat Format = RHITextureFormat::RGBA8_UNORM;
    };
    for (const auto& cfg : std::vector<OddBloomCase>{
             {3u, 5u, true}, {127u, 63u, true}, {127u, 63u, false},
             {127u, 63u, false, RHITextureFormat::BGRA8_UNORM}}) {
        const uint32_t w = cfg.W;
        const uint32_t h = cfg.H;
        const bool hdr = cfg.HDR;
        RHITexture col, dep;
        if (!col.Init(RHITextureDesc::RenderTarget2D(w, h, cfg.Format)) ||
            !dep.Init(RHITextureDesc::DepthStencil2D(w, h))) return false;
        auto targets = makeBloomViewTargets(w, h, hdr, true, cfg.Format);
        if (!targets || !targets->Validate()) return false;
        for (uint32_t lvl = 0; lvl < 6u; ++lvl) {
            const auto expected = BloomContract::ComputeMipExtent(w, h, lvl);
            auto* bTex = targets->GetBloom(lvl);
            if (!bTex || bTex->GetFormat() != RHITextureFormat::RGBA16_FLOAT ||
                bTex->GetWidth() != expected.Width || bTex->GetHeight() != expected.Height) {
                std::cerr << "[M15U8] FAIL (10.3): Bloom mip extent mismatch\n";
                return false;
            }
        }

        RenderWorld emptyWorld;
        PerspectiveCamera cam(60.0f, static_cast<float>(w) / static_cast<float>(h), 0.1f, 50.0f);
        SceneRenderSettings s;
        s.EnableShadows = false;
        s.EnableSkybox = false;
        s.EnableParticles = false;
        s.EnableStudioLightLobes = false;
        s.EnableAutoExposure = false;
        s.EnableToneMapping = hdr;
        s.ToneMappingMode = 3;
        s.Gamma = 1.0f;
        s.EnableBloom = true;
        s.BloomThreshold = 0.25f;
        s.BloomIntensity = 1.0f;
        s.ClearColor = {1.5f, 1.5f, 1.5f, 0.375f};
        renderer.RenderWorldView(emptyWorld, cam, &col, &dep, s, nullptr, targets, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        const auto px = RenderTests::ReadTexture(col);
        if (px.size() != static_cast<size_t>(w) * h * 4u || px[0] < 100 ||
            std::abs(static_cast<int>(px[3]) - 96) > 1) {
            std::cerr << "[M15U8] FAIL (10.3/10.4): Invalid Bloom output at " << w << "x" << h
                      << " hdr=" << hdr << " px0=" << (px.empty() ? -1 : int(px[0])) << "\n";
            return false;
        }
    }

    // =========================================================================
    // Batch C (M15-U8-13.1..13.5): Motion Vectors, Jitter Cancellation & 5 Resets
    // =========================================================================
    auto makeTemporalViewTargets = [](uint32_t w, uint32_t h, bool hdr,
                                      RHITextureFormat colorFmt = RHITextureFormat::RGBA8_UNORM) -> std::shared_ptr<RHIViewTargets> {
        auto targets = std::make_shared<RHIViewTargets>();
        RHIViewTargetDesc desc;
        desc.Width = w;
        desc.Height = h;
        desc.ColorFormat = colorFmt;
        desc.Samples = 1;
        desc.HDR = hdr;
        desc.HistoryCount = 2;
        desc.EnableVelocity = true;
        desc.EnableReactive = true;
        desc.EnableDepth = true;
        desc.OwnPrimary = false;
        if (!targets->Configure(desc, nullptr, nullptr, FroxelHistoryPolicy::AllowStaticFog)) return {};
        return targets;
    };

    // -------------------------------------------------------------------------
    // 8. M15-U8-13.1, 13.2 & 13.4: Static jitter cancellation + Camera pan
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 128, H = 128;
        RHITexture col, dep;
        if (!col.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !dep.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targets = makeTemporalViewTargets(W, H, true);
        if (!targets || !targets->GetVelocity()) return false;

        RenderWorld world;
        RHIMaterialDesc matDesc{};
        matDesc.BaseColor = {0.8f, 0.8f, 0.8f, 1.0f};
        matDesc.Roughness = 0.6f;
        auto mat = std::make_shared<RHIMaterial>(matDesc);
        RenderMeshInstance obj;
        obj.EntityID = 4001;
        obj.StableUUID = UUID(0x4001u);
        obj.NativeView = cube->CaptureView();
        obj.WorldTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1.2f));
        obj.Material.RHIMaterialAsset = mat;
        world.Meshes.push_back(obj);

        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        const glm::mat4 view0 = glm::lookAtRH(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        cam.SetViewMatrix(view0, glm::vec3(0.0f, 0.0f, 4.0f));

        SceneRenderSettings s;
        s.EnableShadows = false;
        s.EnableSkybox = false;
        s.EnableParticles = false;
        s.EnableStudioLightLobes = false;
        s.EnableAutoExposure = false;
        s.EnableBloom = false;
        s.EnableToneMapping = true;
        s.ToneMappingMode = 3;
        s.Gamma = 1.0f;
        s.EnableTAA = true;
        s.TAAJitterSpread = 1.0f;

        // Frames 0..2 static camera + static object under varying Halton jitter => exact zero velocity
        for (uint32_t f = 0; f < 3u; ++f) {
            renderer.RenderWorldView(world, cam, &col, &dep, s, nullptr, targets, f == 0u, RenderViewKind::Main, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful()) return false;
            std::vector<glm::vec2> vel;
            if (!ReadTextureRG16F(*targets->GetVelocity(), vel) || vel.size() != size_t(W) * H) return false;
            const glm::vec2& centerVel = vel[(H / 2u) * W + (W / 2u)];
            const glm::vec2& cornerVel = vel[8u * W + 8u];
            if (!std::isfinite(centerVel.x) || !std::isfinite(centerVel.y) ||
                glm::length(centerVel) > 1e-4f || glm::length(cornerVel) > 1e-4f) {
                std::cerr << "[M15U8] FAIL (13.1/13.4): Jitter leaked into static velocity on frame " << f
                          << " center=(" << centerVel.x << "," << centerVel.y << ") corner=("
                          << cornerVel.x << "," << cornerVel.y << ")\n";
                return false;
            }
        }

        // Frame 3: Linear camera pan (+X) -> negative horizontal motion vector on both object & background
        const glm::mat4 prevVP = cam.GetProjectionMatrixZO() * view0;
        const glm::mat4 view1 = glm::lookAtRH(glm::vec3(0.8f, 0.0f, 4.0f), glm::vec3(0.8f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        cam.SetViewMatrix(view1, glm::vec3(0.8f, 0.0f, 4.0f));
        const glm::mat4 currVP = cam.GetProjectionMatrixZO() * view1;
        renderer.RenderWorldView(world, cam, &col, &dep, s, nullptr, targets, false, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        std::vector<glm::vec2> velPan;
        if (!ReadTextureRG16F(*targets->GetVelocity(), velPan) || velPan.size() != size_t(W) * H) return false;
        bool ok = false;
        const glm::vec3 facePt(0.0f, 0.0f, 0.6f);
        const glm::vec2 projCurrUV = MotionVectorContract::ProjectWorldPointToUV(currVP, facePt, ok);
        const glm::vec2 expectedObjVel = MotionVectorContract::ComputeWorldVelocityUV(currVP, prevVP, facePt, facePt, ok);
        const uint32_t sx = std::clamp<uint32_t>(static_cast<uint32_t>(projCurrUV.x * float(W)), 0u, W - 1u);
        const uint32_t sy = std::clamp<uint32_t>(static_cast<uint32_t>(projCurrUV.y * float(H)), 0u, H - 1u);
        const glm::vec2 sampledCubeVel = velPan[sy * W + sx];
        const glm::vec2 sampledBgVel = velPan[8u * W + 8u];
        if (!ok || expectedObjVel.x >= -0.02f ||
            glm::length(sampledCubeVel - expectedObjVel) > 0.015f ||
            sampledBgVel.x >= -0.01f) {
            std::cerr << "[M15U8] FAIL (13.2): Camera pan velocity mismatch! sampledCube=("
                      << sampledCubeVel.x << "," << sampledCubeVel.y << ") expected=("
                      << expectedObjVel.x << "," << expectedObjVel.y << ") bg=("
                      << sampledBgVel.x << "," << sampledBgVel.y << ")\n";
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // 9. M15-U8-13.3: Moving object vs static neighbor + StableUUID continuity + first spawn
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 128, H = 128;
        RHITexture col, dep;
        if (!col.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !dep.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targets = makeTemporalViewTargets(W, H, true);
        if (!targets) return false;

        RHIMaterialDesc matDesc{};
        matDesc.BaseColor = {0.75f, 0.75f, 0.75f, 1.0f};
        matDesc.Roughness = 0.7f;
        auto mat = std::make_shared<RHIMaterial>(matDesc);

        const glm::mat4 staticM = glm::translate(glm::mat4(1.0f), glm::vec3(-1.1f, 0.0f, 0.0f)) *
                                  glm::scale(glm::mat4(1.0f), glm::vec3(0.75f));
        const glm::mat4 movingM0 = glm::translate(glm::mat4(1.0f), glm::vec3(0.6f, 0.0f, 0.0f)) *
                                   glm::scale(glm::mat4(1.0f), glm::vec3(0.75f));
        const glm::mat4 movingM1 = glm::translate(glm::mat4(1.0f), glm::vec3(1.1f, 0.0f, 0.0f)) *
                                   glm::scale(glm::mat4(1.0f), glm::vec3(0.75f));
        const glm::mat4 spawnM = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.2f, 0.0f)) *
                                 glm::scale(glm::mat4(1.0f), glm::vec3(0.65f));

        RenderWorld world;
        RenderMeshInstance staticInst;
        staticInst.EntityID = 4101;
        staticInst.StableUUID = UUID(0x4101u);
        staticInst.NativeView = cube->CaptureView();
        staticInst.WorldTransform = staticM;
        staticInst.Material.RHIMaterialAsset = mat;

        RenderMeshInstance movingInst = staticInst;
        movingInst.EntityID = 4102;
        movingInst.StableUUID = UUID(0xB002u);
        movingInst.WorldTransform = movingM0;

        world.Meshes = { staticInst, movingInst };

        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 4.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                          glm::vec3(0.0f, 0.0f, 4.5f));
        const glm::mat4 vp = cam.GetViewProjectionMatrixZO();

        SceneRenderSettings s;
        s.EnableShadows = false;
        s.EnableSkybox = false;
        s.EnableParticles = false;
        s.EnableStudioLightLobes = false;
        s.EnableAutoExposure = false;
        s.EnableBloom = false;
        s.EnableToneMapping = true;
        s.ToneMappingMode = 3;
        s.Gamma = 1.0f;
        s.EnableTAA = true;

        // Frame 0 establishes previous transforms
        renderer.RenderWorldView(world, cam, &col, &dep, s, nullptr, targets, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        // Frame 1: movingInst translates +X and gets a different EntityID (same StableUUID), plus new spawned cube
        movingInst.EntityID = 4999;
        movingInst.WorldTransform = movingM1;
        RenderMeshInstance spawnInst = staticInst;
        spawnInst.EntityID = 4103;
        spawnInst.StableUUID = UUID(0xC003u);
        spawnInst.WorldTransform = spawnM;
        world.Meshes = { staticInst, movingInst, spawnInst };

        renderer.RenderWorldView(world, cam, &col, &dep, s, nullptr, targets, false, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        std::vector<glm::vec2> vel;
        if (!ReadTextureRG16F(*targets->GetVelocity(), vel) || vel.size() != size_t(W) * H) return false;
        bool ok = false;
        auto sampleAtWorld = [&](const glm::vec3& wp) -> glm::vec2 {
            const glm::vec2 uv = MotionVectorContract::ProjectWorldPointToUV(vp, wp, ok);
            const uint32_t x = std::clamp<uint32_t>(static_cast<uint32_t>(uv.x * float(W)), 0u, W - 1u);
            const uint32_t y = std::clamp<uint32_t>(static_cast<uint32_t>(uv.y * float(H)), 0u, H - 1u);
            return vel[y * W + x];
        };
        const glm::vec2 vStatic = sampleAtWorld(glm::vec3(-1.1f, 0.0f, 0.375f));
        const glm::vec2 vMoving = sampleAtWorld(glm::vec3(1.1f, 0.0f, 0.375f));
        const glm::vec2 vSpawn = sampleAtWorld(glm::vec3(0.0f, -1.2f, 0.325f));
        const glm::vec2 expectedMoving = MotionVectorContract::ComputeObjectVelocityUV(
            vp, vp, movingM1, movingM0, glm::vec3(0.0f, 0.0f, 0.5f), ok);
        if (!ok || glm::length(vStatic) > 1e-3f ||
            vMoving.x <= 0.02f || glm::length(vMoving - expectedMoving) > 0.015f ||
            glm::length(vSpawn) > 1e-3f) {
            std::cerr << "[M15U8] FAIL (13.3): Moving/static/spawn object velocities invalid! static=("
                      << vStatic.x << "," << vStatic.y << ") moving=(" << vMoving.x << "," << vMoving.y
                      << ") expected=(" << expectedMoving.x << "," << expectedMoving.y
                      << ") spawn=(" << vSpawn.x << "," << vSpawn.y << ")\n";
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // 10. M15-U8-13.5: All 5 TAA History Reset Triggers Reset GPU History & Velocity
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 128, H = 128;
        RHITexture col, dep, colSmall, depSmall;
        if (!col.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !dep.Init(RHITextureDesc::DepthStencil2D(W, H)) ||
            !colSmall.Init(RHITextureDesc::RenderTarget2D(96u, 96u)) ||
            !depSmall.Init(RHITextureDesc::DepthStencil2D(96u, 96u))) return false;
        auto targets = makeTemporalViewTargets(W, H, true);
        auto targetsSmall = makeTemporalViewTargets(96u, 96u, true);
        if (!targets || !targetsSmall) return false;

        RenderWorld world;
        world.ViewSceneRevision = 10u;
        RHIMaterialDesc matDesc{};
        matDesc.BaseColor = {0.8f, 0.8f, 0.8f, 1.0f};
        auto mat = std::make_shared<RHIMaterial>(matDesc);
        RenderMeshInstance inst;
        inst.EntityID = 4201;
        inst.StableUUID = UUID(0x4201u);
        inst.NativeView = cube->CaptureView();
        inst.WorldTransform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
        inst.Material.RHIMaterialAsset = mat;
        world.Meshes.push_back(inst);

        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        SceneRenderSettings s;
        s.EnableShadows = false;
        s.EnableSkybox = false;
        s.EnableParticles = false;
        s.EnableStudioLightLobes = false;
        s.EnableAutoExposure = false;
        s.EnableBloom = false;
        s.EnableToneMapping = true;
        s.ToneMappingMode = 3;
        s.Gamma = 1.0f;
        s.EnableTAA = true;

        auto warmUpMotion = [&](const std::shared_ptr<RHIViewTargets>& vt, RHITexture& cTex, RHITexture& dTex) -> bool {
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              glm::vec3(0.0f, 0.0f, 4.0f));
            renderer.RenderWorldView(world, cam, &cTex, &dTex, s, nullptr, vt, true, RenderViewKind::Main, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful()) {
                std::cerr << "[M15U8] warmUp frame0 render failed\n";
                return false;
            }
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.5f, 0.0f, 4.0f), glm::vec3(0.5f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              glm::vec3(0.5f, 0.0f, 4.0f));
            renderer.RenderWorldView(world, cam, &cTex, &dTex, s, nullptr, vt, false, RenderViewKind::Main, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful()) {
                std::cerr << "[M15U8] warmUp frame1 render failed\n";
                return false;
            }
            std::vector<glm::vec2> vel;
            if (!ReadTextureRG16F(*vt->GetVelocity(), vel)) {
                std::cerr << "[M15U8] warmUp ReadTextureRG16F failed\n";
                return false;
            }
            const uint32_t w = vt->GetVelocity()->GetWidth();
            const uint32_t h = vt->GetVelocity()->GetHeight();
            const glm::vec2 cVel = vel[(h / 2u) * w + (w / 2u)];
            const uint64_t comm = renderer.GetMainViewContext().Temporal().GetCommittedFrameCount();
            if (!(std::abs(cVel.x) > 0.02f && comm == 2u)) {
                std::cerr << "[M15U8] warmUp check failed: vel=(" << cVel.x << "," << cVel.y << ") committed=" << comm << "\n";
                return false;
            }
            return true;
        };

        auto checkResetProducesZeroVelocity = [&](const char* label,
                                                  const std::shared_ptr<RHIViewTargets>& vt,
                                                  RHITexture& cTex, RHITexture& dTex,
                                                  bool cut, RenderViewKind kind,
                                                  const SceneRenderSettings& renderCfg) -> bool {
            renderer.RenderWorldView(world, cam, &cTex, &dTex, renderCfg, nullptr, vt, cut, kind, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful()) return false;
            std::vector<glm::vec2> vel;
            if (!ReadTextureRG16F(*vt->GetVelocity(), vel)) return false;
            const uint32_t w = vt->GetVelocity()->GetWidth();
            const uint32_t h = vt->GetVelocity()->GetHeight();
            const glm::vec2& centerVel = vel[(h / 2u) * w + (w / 2u)];
            const bool committedRestarted = renderer.GetMainViewContext().Temporal().GetCommittedFrameCount() == 1u;
            if (!committedRestarted || glm::length(centerVel) > 1e-4f) {
                std::cerr << "[M15U8] FAIL (13.5): Reset trigger '" << label << "' failed! committed="
                          << renderer.GetMainViewContext().Temporal().GetCommittedFrameCount()
                          << " vel=(" << centerVel.x << "," << centerVel.y << ")\n";
                return false;
            }
            return true;
        };

        // 1. CameraCut
        if (!warmUpMotion(targets, col, dep) ||
            !checkResetProducesZeroVelocity("CameraCut", targets, col, dep, true, RenderViewKind::Main, s)) return false;
        // 2. ViewSwitch
        if (!warmUpMotion(targets, col, dep) ||
            !checkResetProducesZeroVelocity("ViewSwitch", targets, col, dep, false, RenderViewKind::AssetPreview, s)) return false;
        // 3. Resize
        if (!warmUpMotion(targets, col, dep) ||
            !checkResetProducesZeroVelocity("Resize", targetsSmall, colSmall, depSmall, false, RenderViewKind::Main, s)) return false;
        // 4. Settings / Format change (toggle TAAJitterSpread / EnableTAA / Format)
        if (!warmUpMotion(targets, col, dep)) return false;
        SceneRenderSettings settingsChanged = s;
        settingsChanged.TAAJitterSpread = 0.5f;
        if (!checkResetProducesZeroVelocity("SettingsPolicy", targets, col, dep, false, RenderViewKind::Main, settingsChanged)) return false;
        // 5. SceneRevision change
        if (!warmUpMotion(targets, col, dep)) return false;
        world.ViewSceneRevision = 11u;
        if (!checkResetProducesZeroVelocity("SceneRevision", targets, col, dep, false, RenderViewKind::Main, s)) return false;
    }

    // =========================================================================
    // Batch D (M15-U8-15.1..15.4): TAA Shimmer Reduction, Moving Fidelity,
    // Disocclusion Rejection, Resize/ViewSwitch & TAA/FXAA Mutual Exclusion
    // =========================================================================

    // -------------------------------------------------------------------------
    // 10b. Preserve the FP16 peak and viewport alpha after history starts blending.
    {
        constexpr uint32_t W = 32, H = 32;
        RHITexture color, depth;
        if (!color.Init(RHITextureDesc::RenderTarget2D(W, H, RHITextureFormat::RGBA16_FLOAT)) ||
            !depth.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targets = makeTemporalViewTargets(W, H, false, RHITextureFormat::RGBA16_FLOAT);
        if (!targets) return false;
        SceneRenderSettings s;
        s.EnableSkybox = false;
        s.EnableShadows = false;
        s.EnableParticles = false;
        s.EnableTAA = true;
        s.ClearColor = {24000.0f, 6000.0f, 18000.0f, 0.375f};
        PerspectiveCamera camera(60.0f, 1.0f, 0.1f, 50.0f);
        const RenderWorld empty;
        for (uint32_t frame = 0; frame < 3; ++frame) {
            renderer.RenderWorldView(empty, camera, &color, &depth, s, nullptr, targets, frame == 0);
            if (!renderer.WasLastRenderSuccessful()) return false;
        }
        std::vector<glm::vec4> pixels;
        RENDER_CHECK(ReadTextureRGBA16F(color, pixels) && pixels.size() == size_t(W) * H,
            "M15-U8 HDR temporal readback");
        const auto center = pixels[size_t(W) * (H / 2u) + W / 2u];
        RENDER_CHECK(std::abs(center.r - 24000.0f) < 100.0f && std::abs(center.a - 0.375f) < 0.001f,
            "M15-U8 HDR temporal resolve keeps full peak radiance and does not overwrite viewport alpha");
    }

    // 11. M15-U8-15.1: Static diagonal high-contrast shimmer reduction & AA
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 128, H = 128;
        RHITexture colNoTAA, depNoTAA, colTAA, depTAA;
        if (!colNoTAA.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depNoTAA.Init(RHITextureDesc::DepthStencil2D(W, H)) ||
            !colTAA.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depTAA.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targetsNoTAA = makeTemporalViewTargets(W, H, true);
        auto targetsTAA = makeTemporalViewTargets(W, H, true);
        if (!targetsNoTAA || !targetsTAA) return false;

        RHIMaterialDesc barMatDesc{};
        barMatDesc.BaseColor = {0.0f, 0.0f, 0.0f, 1.0f};
        barMatDesc.EmissiveColor = {0.85f, 0.85f, 0.85f};
        barMatDesc.EmissiveIntensity = 1.0f;
        barMatDesc.Roughness = 1.0f;
        auto barMat = std::make_shared<RHIMaterial>(barMatDesc);

        RenderWorld world;
        RenderMeshInstance bar;
        bar.EntityID = 5101;
        bar.StableUUID = UUID(0x5101u);
        bar.NativeView = cube->CaptureView();
        bar.WorldTransform = glm::rotate(glm::mat4(1.0f), glm::radians(26.0f), glm::vec3(0.0f, 0.0f, 1.0f)) *
                             glm::scale(glm::mat4(1.0f), glm::vec3(0.28f, 3.2f, 0.28f));
        bar.Material.RHIMaterialAsset = barMat;
        world.Meshes.push_back(bar);

        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                          glm::vec3(0.0f, 0.0f, 4.0f));

        SceneRenderSettings s;
        s.EnableShadows = false;
        s.EnableSkybox = false;
        s.EnableParticles = false;
        s.EnableStudioLightLobes = false;
        s.EnableAutoExposure = false;
        s.EnableBloom = false;
        s.EnableToneMapping = true;
        s.ToneMappingMode = 3;
        s.Gamma = 1.0f;
        s.ClearColor = {0.04f, 0.04f, 0.04f, 1.0f};

        s.EnableTAA = false;
        renderer.RenderWorldView(world, cam, &colNoTAA, &depNoTAA, s, nullptr, targetsNoTAA, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;
        const auto pxNoTAA = RenderTests::ReadTexture(colNoTAA);

        s.EnableTAA = true;
        std::vector<uint8_t> pxTAA14, pxTAA15;
        for (uint32_t f = 0; f < 16u; ++f) {
            renderer.RenderWorldView(world, cam, &colTAA, &depTAA, s, nullptr, targetsTAA, f == 0u, RenderViewKind::Main, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful()) return false;
            if (f == 14u) pxTAA14 = RenderTests::ReadTexture(colTAA);
            if (f == 15u) pxTAA15 = RenderTests::ReadTexture(colTAA);
        }

        uint32_t noAaEdgeTransitions = 0, aaEdgeTransitions = 0;
        double diffSq = 0.0;
        const size_t pixelCount = size_t(W) * H;
        for (size_t i = 0; i < pixelCount; ++i) {
            const uint8_t r0 = pxNoTAA[i * 4u];
            const uint8_t r14 = pxTAA14[i * 4u];
            const uint8_t r15 = pxTAA15[i * 4u];
            if (r0 > 20u && r0 < 195u) ++noAaEdgeTransitions;
            if (r15 > 20u && r15 < 195u) ++aaEdgeTransitions;
            const double d = double(r15) - double(r14);
            diffSq += d * d;
        }
        const double rmsFrameDelta = std::sqrt(diffSq / double(pixelCount));
        if (aaEdgeTransitions <= noAaEdgeTransitions || aaEdgeTransitions < 20u || rmsFrameDelta > 4.5) {
            std::cerr << "[M15U8] FAIL (15.1): Diagonal edge TAA shimmer/AA check failed! noAaEdge="
                      << noAaEdgeTransitions << " aaEdge=" << aaEdgeTransitions
                      << " rmsDelta(f14,f15)=" << rmsFrameDelta << "\n";
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // 12. M15-U8-15.2: Moving high-contrast object edge fidelity & streak-free wake
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 128, H = 128;
        RHITexture col, dep;
        if (!col.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !dep.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targets = makeTemporalViewTargets(W, H, true);
        if (!targets) return false;

        RHIMaterialDesc movingMatDesc{};
        movingMatDesc.BaseColor = {0.0f, 0.0f, 0.0f, 1.0f};
        movingMatDesc.EmissiveColor = {0.9f, 0.85f, 0.15f};
        movingMatDesc.EmissiveIntensity = 1.0f;
        movingMatDesc.Roughness = 1.0f;
        auto movingMat = std::make_shared<RHIMaterial>(movingMatDesc);

        RHIMaterialDesc staticMatDesc{};
        staticMatDesc.BaseColor = {0.0f, 0.0f, 0.0f, 1.0f};
        staticMatDesc.EmissiveColor = {0.1f, 0.25f, 0.8f};
        staticMatDesc.EmissiveIntensity = 1.0f;
        auto staticMat = std::make_shared<RHIMaterial>(staticMatDesc);

        RenderMeshInstance staticInst;
        staticInst.EntityID = 5201;
        staticInst.StableUUID = UUID(0x5201u);
        staticInst.NativeView = cube->CaptureView();
        staticInst.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.6f, 1.3f, 0.0f)) *
                                    glm::scale(glm::mat4(1.0f), glm::vec3(0.55f));
        staticInst.Material.RHIMaterialAsset = staticMat;

        RenderMeshInstance movingInst = staticInst;
        movingInst.EntityID = 5202;
        movingInst.StableUUID = UUID(0x5202u);
        movingInst.Material.RHIMaterialAsset = movingMat;

        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 4.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                          glm::vec3(0.0f, 0.0f, 4.5f));

        SceneRenderSettings s;
        s.EnableShadows = false;
        s.EnableSkybox = false;
        s.EnableParticles = false;
        s.EnableStudioLightLobes = false;
        s.EnableAutoExposure = false;
        s.EnableBloom = false;
        s.EnableToneMapping = true;
        s.ToneMappingMode = 3;
        s.Gamma = 1.0f;
        s.ClearColor = {0.05f, 0.05f, 0.07f, 1.0f};
        s.EnableTAA = true;

        RenderWorld world;
        for (uint32_t f = 0; f <= 5u; ++f) {
            const float x = -1.5f + 0.30f * float(f);
            movingInst.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f)) *
                                        glm::scale(glm::mat4(1.0f), glm::vec3(0.85f));
            world.Meshes = { staticInst, movingInst };
            renderer.RenderWorldView(world, cam, &col, &dep, s, nullptr, targets, f == 0u, RenderViewKind::Main, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful()) return false;
        }

        const auto px = RenderTests::ReadTexture(col);
        auto at = [&](uint32_t x, uint32_t y) {
            const size_t idx = (size_t(y) * W + x) * 4u;
            return glm::ivec3(px[idx + 0u], px[idx + 1u], px[idx + 2u]);
        };
        const glm::ivec3 objCenter = at(64u, 64u);
        const glm::ivec3 trailingWake = at(28u, 64u);
        const glm::ivec3 leadingAhead = at(96u, 64u);
        if (objCenter.r < 160 || objCenter.g < 140 || objCenter.b > 80 ||
            trailingWake.r > 45 || trailingWake.g > 45 ||
            leadingAhead.r > 45 || leadingAhead.g > 45) {
            std::cerr << "[M15U8] FAIL (15.2): Moving object edge/wake ghosting check failed! center=("
                      << objCenter.r << "," << objCenter.g << "," << objCenter.b << ") wake=("
                      << trailingWake.r << "," << trailingWake.g << "," << trailingWake.b << ") ahead=("
                      << leadingAhead.r << "," << leadingAhead.g << "," << leadingAhead.b << ")\n";
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // 13. M15-U8-15.3: Disocclusion behind rapidly moving occluder (zero ghost)
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 128, H = 128;
        RHITexture col, dep;
        if (!col.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !dep.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targets = makeTemporalViewTargets(W, H, true);
        if (!targets) return false;

        RHIMaterialDesc occMatDesc{};
        occMatDesc.BaseColor = {0.0f, 0.0f, 0.0f, 1.0f};
        occMatDesc.EmissiveColor = {0.02f, 0.04f, 0.85f};
        occMatDesc.EmissiveIntensity = 1.0f;
        auto occMat = std::make_shared<RHIMaterial>(occMatDesc);

        RenderMeshInstance occ;
        occ.EntityID = 5301;
        occ.StableUUID = UUID(0x5301u);
        occ.NativeView = cube->CaptureView();
        occ.Material.RHIMaterialAsset = occMat;

        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 4.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                          glm::vec3(0.0f, 0.0f, 4.5f));

        SceneRenderSettings s;
        s.EnableShadows = false;
        s.EnableSkybox = false;
        s.EnableParticles = false;
        s.EnableStudioLightLobes = false;
        s.EnableAutoExposure = false;
        s.EnableBloom = false;
        s.EnableToneMapping = true;
        s.ToneMappingMode = 3;
        s.Gamma = 1.0f;
        s.ClearColor = {0.85f, 0.03f, 0.03f, 1.0f};
        s.EnableTAA = true;

        RenderWorld world;
        occ.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 1.5f)) *
                             glm::scale(glm::mat4(1.0f), glm::vec3(1.4f));
        world.Meshes = { occ };
        for (uint32_t f = 0; f < 4u; ++f) {
            renderer.RenderWorldView(world, cam, &col, &dep, s, nullptr, targets, f == 0u, RenderViewKind::Main, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful()) return false;
        }
        const auto pxOccluded = RenderTests::ReadTexture(col);
        const size_t cIdx = (size_t(H / 2u) * W + (W / 2u)) * 4u;
        if (pxOccluded[cIdx + 2u] < 140 || pxOccluded[cIdx + 2u] <= pxOccluded[cIdx + 0u] + 80) {
            std::cerr << "[M15U8] FAIL (15.3): Expected blue occluder over center before move\n";
            return false;
        }

        // Rapidly move occluder to +X uncovering center & trailing margin immediately
        occ.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.6f, 0.0f, 1.5f)) *
                             glm::scale(glm::mat4(1.0f), glm::vec3(1.4f));
        world.Meshes = { occ };
        renderer.RenderWorldView(world, cam, &col, &dep, s, nullptr, targets, false, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;

        const auto pxDisoccluded = RenderTests::ReadTexture(col);
        const size_t nearEdgeIdx = (size_t(H / 2u) * W + 82u) * 4u;
        if (pxDisoccluded[cIdx + 0u] < 140 || pxDisoccluded[cIdx + 2u] > 45 ||
            pxDisoccluded[nearEdgeIdx + 0u] < 140 || pxDisoccluded[nearEdgeIdx + 2u] > 45) {
            std::cerr << "[M15U8] FAIL (15.3): Disoccluded background retained blue occluder ghost! center=("
                      << int(pxDisoccluded[cIdx + 0u]) << "," << int(pxDisoccluded[cIdx + 1u]) << "," << int(pxDisoccluded[cIdx + 2u])
                      << ") edge=(" << int(pxDisoccluded[nearEdgeIdx + 0u]) << "," << int(pxDisoccluded[nearEdgeIdx + 1u]) << "," << int(pxDisoccluded[nearEdgeIdx + 2u]) << ")\n";
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // 13b. Both colors are inside the 3x3 neighborhood range at this moving edge.
    {
        constexpr uint32_t W = 128, H = 128;
        RHITexture color, depth;
        if (!color.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depth.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targets = makeTemporalViewTargets(W, H, true);
        if (!targets) return false;

        RHIMaterialDesc material{};
        material.BaseColor = {0.0f, 0.0f, 0.0f, 1.0f};
        material.EmissiveColor = {0.52f, 0.52f, 0.52f};
        material.EmissiveIntensity = 1.0f;
        RenderMeshInstance occluder;
        occluder.EntityID = 5311;
        occluder.StableUUID = UUID(0x5311u);
        occluder.NativeView = cube->CaptureView();
        occluder.Material.RHIMaterialAsset = std::make_shared<RHIMaterial>(material);
        occluder.WorldTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 0.5f));
        RenderWorld world;
        world.Meshes.push_back(occluder);

        PerspectiveCamera camera;
        camera.SetOrthographic(4.0f, 1.0f, 0.1f, 40.0f);
        const glm::vec3 eye(0.0f, 0.0f, 4.5f);
        camera.SetViewMatrix(glm::lookAtRH(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)), eye);

        SceneRenderSettings s;
        s.EnableSkybox = false;
        s.EnableShadows = false;
        s.EnableParticles = false;
        s.EnableToneMapping = true;
        s.ToneMappingMode = 3;
        s.Gamma = 1.0f;
        s.EnableTAA = true;
        s.TAAJitterSpread = 0.0f;
        s.ClearColor = {0.38f, 0.38f, 0.38f, 1.0f};
        auto render = [&](bool cameraCut) {
            renderer.RenderWorldView(world, camera, &color, &depth, s, nullptr, targets, cameraCut);
            return renderer.WasLastRenderSuccessful() ? RenderTests::ReadTexture(color) : std::vector<uint8_t>{};
        };

        const auto occluded = render(true);
        world.Meshes.front().WorldTransform[3].x = 0.44f;
        const auto accumulated = render(false);
        const auto current = render(true);
        RENDER_CHECK(occluded.size() == size_t(W) * H * 4u && accumulated.size() == occluded.size() &&
                     current.size() == occluded.size(), "M15-U8-15.3: temporal depth fixture readbacks");
        const auto red = [](const std::vector<uint8_t>& image, uint32_t x) {
            return static_cast<int>(image[(size_t(H / 2u) * W + x) * 4u]);
        };
        uint32_t edgeReveals = 0;
        int maxDelta = 0;
        for (uint32_t x = 1; x + 1u < W; ++x) {
            if (red(occluded, x) > 125 && red(current, x) < 105 && red(current, x + 1) > 125) {
                ++edgeReveals;
                maxDelta = std::max(maxDelta, std::abs(red(accumulated, x) - red(current, x)));
            }
        }
        std::cout << "[M15U8Depth] nearby-color reveals=" << edgeReveals << " max_rgb_delta=" << maxDelta << '\n';
        RENDER_CHECK(edgeReveals > 0 && maxDelta <= 2,
            "M15-U8-15.3: previous occluder depth rejects plausible-colored history at a revealed edge");
    }

    // 14. M15-U8-15.4: Resize + ViewSwitch reset cleanly & no silent FXAA-on-TAA
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 128, H = 128, W2 = 192, H2 = 144;
        RHITexture colA, depA, colB, depB, colBig, depBig;
        if (!colA.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depA.Init(RHITextureDesc::DepthStencil2D(W, H)) ||
            !colB.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depB.Init(RHITextureDesc::DepthStencil2D(W, H)) ||
            !colBig.Init(RHITextureDesc::RenderTarget2D(W2, H2)) ||
            !depBig.Init(RHITextureDesc::DepthStencil2D(W2, H2))) return false;
        auto targetsA = makeTemporalViewTargets(W, H, true);
        auto targetsB = makeTemporalViewTargets(W, H, true);
        auto targetsBig = makeTemporalViewTargets(W2, H2, true);
        if (!targetsA || !targetsB || !targetsBig) return false;

        RHIMaterialDesc barMatDesc{};
        barMatDesc.BaseColor = {0.0f, 0.0f, 0.0f, 1.0f};
        barMatDesc.EmissiveColor = {0.85f, 0.85f, 0.85f};
        barMatDesc.EmissiveIntensity = 1.0f;
        auto barMat = std::make_shared<RHIMaterial>(barMatDesc);

        RenderWorld world;
        RenderMeshInstance bar;
        bar.EntityID = 5401;
        bar.StableUUID = UUID(0x5401u);
        bar.NativeView = cube->CaptureView();
        bar.WorldTransform = glm::rotate(glm::mat4(1.0f), glm::radians(26.0f), glm::vec3(0.0f, 0.0f, 1.0f)) *
                             glm::scale(glm::mat4(1.0f), glm::vec3(0.28f, 3.2f, 0.28f));
        bar.Material.RHIMaterialAsset = barMat;
        world.Meshes.push_back(bar);

        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                          glm::vec3(0.0f, 0.0f, 4.0f));

        SceneRenderSettings sTaaOnly;
        sTaaOnly.EnableShadows = false;
        sTaaOnly.EnableSkybox = false;
        sTaaOnly.EnableParticles = false;
        sTaaOnly.EnableStudioLightLobes = false;
        sTaaOnly.EnableAutoExposure = false;
        sTaaOnly.EnableBloom = false;
        sTaaOnly.EnableToneMapping = true;
        sTaaOnly.ToneMappingMode = 3;
        sTaaOnly.Gamma = 1.0f;
        sTaaOnly.EnableTAA = true;
        sTaaOnly.EnableFXAA = false;

        SceneRenderSettings sTaaAndFxaa = sTaaOnly;
        sTaaAndFxaa.EnableFXAA = true;

        const auto policy = RenderBackendPolicy::FromDevice(RHIDevice::Get().GetCapabilities());
        const auto resolved = policy.ResolveSettings(sTaaAndFxaa);
        const auto fallbacks = policy.CollectFallbacks(sTaaAndFxaa);
        bool sawSuppressionReason = false;
        for (const auto& reason : fallbacks) {
            if (reason.find("fxaa_suppressed_by_taa") != std::string::npos) {
                sawSuppressionReason = true;
                break;
            }
        }
        if (resolved.EnableFXAA || !sawSuppressionReason) {
            std::cerr << "[M15U8] FAIL (15.4): RenderBackendPolicy did not suppress FXAA under TAA!\n";
            return false;
        }

        for (uint32_t f = 0; f < 4u; ++f) {
            renderer.RenderWorldView(world, cam, &colA, &depA, sTaaOnly, nullptr, targetsA, f == 0u, RenderViewKind::Main, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful()) return false;
        }
        for (uint32_t f = 0; f < 4u; ++f) {
            renderer.RenderWorldView(world, cam, &colB, &depB, sTaaAndFxaa, nullptr, targetsB, f == 0u, RenderViewKind::Main, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful()) return false;
        }
        const auto pxTaaOnly = RenderTests::ReadTexture(colA);
        const auto pxTaaFxaa = RenderTests::ReadTexture(colB);
        if (pxTaaOnly != pxTaaFxaa) {
            std::cerr << "[M15U8] FAIL (15.4): EnableTAA+EnableFXAA differed from EnableTAA-only (silent FXAA stacking)!\n";
            return false;
        }

        // Resize cleanly
        cam.SetProjection(60.0f, float(W2) / float(H2), 0.1f, 50.0f);
        renderer.RenderWorldView(world, cam, &colBig, &depBig, sTaaOnly, nullptr, targetsBig, false, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful() ||
            renderer.GetMainViewContext().Temporal().GetCommittedFrameCount() != 1u) {
            std::cerr << "[M15U8] FAIL (15.4): Resize did not reset TAA cleanly\n";
            return false;
        }
        // ViewSwitch cleanly
        renderer.RenderWorldView(world, cam, &colBig, &depBig, sTaaOnly, nullptr, targetsBig, false, RenderViewKind::AssetPreview, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful() ||
            renderer.GetMainViewContext().Temporal().GetCommittedFrameCount() != 1u) {
            std::cerr << "[M15U8] FAIL (15.4): ViewSwitch did not reset TAA cleanly\n";
            return false;
        }
    }

    // =========================================================================
    // Batch E (M15-U8-14.1..14.3, 15.5): Transparent/Reactive Mask, Unjittered
    // Debug Overlays, Ghosting Fixture & GPU/VRAM Scorecard
    // =========================================================================

    // -------------------------------------------------------------------------
    // 15. M15-U8-14.1, 14.2 & 15.5: Transparent/Additive reactive mask + ghosting
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 128, H = 128;
        RHITexture col, dep;
        if (!col.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !dep.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targets = makeTemporalViewTargets(W, H, true);
        if (!targets || !targets->GetReactive()) return false;

        RHIMaterialDesc staticMatDesc{};
        staticMatDesc.SurfaceType = SurfaceType::Opaque;
        staticMatDesc.BaseColor = {0.25f, 0.35f, 0.65f, 1.0f};
        staticMatDesc.EmissiveIntensity = 0.0f;
        auto staticMat = std::make_shared<RHIMaterial>(staticMatDesc);

        RHIMaterialDesc glassMatDesc{};
        glassMatDesc.SurfaceType = SurfaceType::Transparent;
        glassMatDesc.DepthWrite = false;
        glassMatDesc.RenderQueue = 3000;
        glassMatDesc.BaseColor = {0.95f, 0.15f, 0.15f, 0.70f};
        glassMatDesc.EmissiveColor = {0.90f, 0.12f, 0.12f};
        glassMatDesc.EmissiveIntensity = 1.0f;
        auto glassMat = std::make_shared<RHIMaterial>(glassMatDesc);

        RenderMeshInstance staticCorner;
        staticCorner.EntityID = 5501;
        staticCorner.StableUUID = UUID(0x5501u);
        staticCorner.NativeView = cube->CaptureView();
        staticCorner.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.55f, 1.45f, 0.0f)) *
                                      glm::scale(glm::mat4(1.0f), glm::vec3(0.55f));
        staticCorner.Material.RHIMaterialAsset = staticMat;

        RenderMeshInstance movingGlass = staticCorner;
        movingGlass.EntityID = 5502;
        movingGlass.StableUUID = UUID(0x5502u);
        movingGlass.Material.RHIMaterialAsset = glassMat;

        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        const glm::mat4 viewMat = glm::lookAtRH(glm::vec3(0.0f, 0.0f, 4.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        cam.SetViewMatrix(viewMat, glm::vec3(0.0f, 0.0f, 4.5f));

        SceneRenderSettings s;
        s.EnableShadows = false;
        s.EnableSkybox = false;
        s.EnableParticles = false;
        s.EnableStudioLightLobes = false;
        s.EnableAutoExposure = false;
        s.EnableBloom = false;
        s.EnableToneMapping = true;
        s.ToneMappingMode = 3;
        s.Gamma = 1.0f;
        s.ClearColor = {0.04f, 0.04f, 0.06f, 1.0f};
        s.EnableTAA = true;

        RenderWorld world;
        for (uint32_t f = 0; f <= 5u; ++f) {
            const float x = -1.75f + 0.35f * float(f);
            movingGlass.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f)) *
                                         glm::scale(glm::mat4(1.0f), glm::vec3(0.9f));
            world.Meshes = { staticCorner, movingGlass };
            renderer.RenderWorldView(world, cam, &col, &dep, s, nullptr, targets, f == 0u, RenderViewKind::Main, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful()) return false;
        }

        std::vector<glm::vec2> vel;
        std::vector<uint8_t> reactive;
        if (!ReadTextureRG16F(*targets->GetVelocity(), vel) ||
            !ReadTextureR8(*targets->GetReactive(), reactive)) return false;

        bool okProj = false;
        const glm::vec2 staticUV = MotionVectorContract::ProjectWorldPointToUV(cam.GetViewProjectionMatrixZO(), glm::vec3(-1.55f, 1.45f, 0.275f), okProj);
        const uint32_t sx = std::clamp<uint32_t>(static_cast<uint32_t>(staticUV.x * float(W)), 0u, W - 1u);
        const uint32_t sy = std::clamp<uint32_t>(static_cast<uint32_t>(staticUV.y * float(H)), 0u, H - 1u);

        const glm::vec2 glassCenterVel = vel[(H / 2u) * W + (W / 2u)];
        const uint8_t glassReactive = reactive[(H / 2u) * W + (W / 2u)];
        const uint8_t staticReactive = reactive[sy * W + sx];
        const auto px = RenderTests::ReadTexture(col);
        auto rgbAt = [&](uint32_t x, uint32_t y) {
            const size_t idx = (size_t(y) * W + x) * 4u;
            return glm::ivec3(px[idx], px[idx + 1u], px[idx + 2u]);
        };
        const glm::ivec3 glassRGB = rgbAt(64u, 64u);
        const glm::ivec3 wakeRGB = rgbAt(28u, 64u);
        if (!okProj || glm::length(glassCenterVel) > 1e-4f ||
            glassReactive < 150u || staticReactive != 0u ||
            glassRGB.r < 120 || glassRGB.r <= glassRGB.b + 50 ||
            wakeRGB.r > 40) {
            std::cerr << "[M15U8] FAIL (14.1/14.2/15.5): Transparent reactive/ghosting check failed! vel=("
                      << glassCenterVel.x << "," << glassCenterVel.y << ") react=" << int(glassReactive)
                      << " staticReact=" << int(staticReactive)
                      << " glassRGB=(" << glassRGB.r << "," << glassRGB.g << "," << glassRGB.b
                      << ") wakeRGB=(" << wakeRGB.r << "," << wakeRGB.g << "," << wakeRGB.b << ")\n";
            return false;
        }

        // Additive snap-off ghosting test (15.5)
        RHIMaterialDesc addMatDesc{};
        addMatDesc.SurfaceType = SurfaceType::Additive;
        addMatDesc.DepthWrite = false;
        addMatDesc.RenderQueue = 3000;
        addMatDesc.BaseColor = {0.0f, 0.9f, 0.9f, 0.9f};
        addMatDesc.EmissiveColor = {0.0f, 0.9f, 0.9f};
        addMatDesc.EmissiveIntensity = 1.0f;
        auto addMat = std::make_shared<RHIMaterial>(addMatDesc);

        RenderMeshInstance flashBox;
        flashBox.EntityID = 5503;
        flashBox.StableUUID = UUID(0x5503u);
        flashBox.NativeView = cube->CaptureView();
        flashBox.WorldTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1.1f));
        flashBox.Material.RHIMaterialAsset = addMat;
        world.Meshes = { flashBox };
        for (uint32_t f = 0; f < 4u; ++f) {
            renderer.RenderWorldView(world, cam, &col, &dep, s, nullptr, targets, f == 0u, RenderViewKind::Main, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful()) return false;
        }
        flashBox.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(3.2f, 0.0f, 0.0f));
        world.Meshes = { flashBox };
        renderer.RenderWorldView(world, cam, &col, &dep, s, nullptr, targets, false, RenderViewKind::Main, 1.0f / 60.0f);
        if (!renderer.WasLastRenderSuccessful()) return false;
        const auto pxSnap = RenderTests::ReadTexture(col);
        const size_t cIdx = (size_t(H / 2u) * W + (W / 2u)) * 4u;
        if (pxSnap[cIdx + 1u] > 40 || pxSnap[cIdx + 2u] > 40) {
            std::cerr << "[M15U8] FAIL (15.5): Additive flash retained ghost after snap-off! center=("
                      << int(pxSnap[cIdx]) << "," << int(pxSnap[cIdx + 1u]) << "," << int(pxSnap[cIdx + 2u]) << ")\n";
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // 16. M15-U8-14.3: Unjittered PhysicsDebug overlay & AutoExposure isolation
    // -------------------------------------------------------------------------
    {
        RHISceneRenderer debugRenderer;
        if (!debugRenderer.Init(RHIDevice::Get(), true)) return false;

        constexpr uint32_t W = 128, H = 128;
        RHITexture colRef, depRef, colTAA, depTAA;
        if (!colRef.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depRef.Init(RHITextureDesc::DepthStencil2D(W, H)) ||
            !colTAA.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !depTAA.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto makeDbgTargets = [&](bool enableTaa, bool enableExp) -> std::shared_ptr<RHIViewTargets> {
            auto t = std::make_shared<RHIViewTargets>();
            RHIViewTargetDesc d{};
            d.Width = W; d.Height = H;
            d.HDR = true; d.EnableDepth = true;
            d.EnableAutoExposure = enableExp;
            d.HistoryCount = enableTaa ? 2 : 0;
            d.EnableVelocity = enableTaa;
            d.EnableReactive = enableTaa;
            d.OwnPrimary = false;
            if (!t->Configure(d, nullptr, nullptr, FroxelHistoryPolicy::AllowStaticFog)) return {};
            return t;
        };
        auto targetsRef = makeDbgTargets(false, false);
        auto expNoDbgTargets = makeDbgTargets(false, true);
        auto expWithDbgTargets = makeDbgTargets(true, true);
        if (!targetsRef || !expNoDbgTargets || !expWithDbgTargets) return false;

        RenderWorld worldEmpty;
        RenderWorld worldWithDebug;
        RenderDebugLine line{};
        line.From = glm::vec3(-0.08f, -1.6f, 0.0f);
        line.To = glm::vec3(-0.08f, 1.6f, 0.0f);
        line.Color = glm::vec4(0.05f, 0.98f, 0.12f, 1.0f);
        worldWithDebug.DebugLines.push_back(line);

        PerspectiveCamera cam(60.0f, 1.0f, 0.1f, 50.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                          glm::vec3(0.0f, 0.0f, 4.0f));

        SceneRenderSettings s;
        s.EnableShadows = false;
        s.EnableSkybox = false;
        s.EnableParticles = false;
        s.EnableStudioLightLobes = false;
        s.EnableBloom = false;
        s.EnableToneMapping = true;
        s.ToneMappingMode = 3;
        s.Gamma = 1.0f;
        s.ClearColor = {0.03f, 0.03f, 0.03f, 1.0f};
        s.EnableAutoExposure = true;
        s.AutoExposureMin = 0.05f;
        s.AutoExposureMax = 8.0f;
        s.AutoExposureSpeed = 2.0f;

        s.EnableTAA = false;
        debugRenderer.RenderWorldView(worldEmpty, cam, &colRef, &depRef, s, nullptr, expNoDbgTargets, true, RenderViewKind::Main, 1.0f / 60.0f);
        std::vector<glm::vec4> noOverlayExposure, overlayExposure;
        RENDER_CHECK(debugRenderer.WasLastRenderSuccessful() &&
            ReadTextureRGBA16F(*expNoDbgTargets->GetExposure(), noOverlayExposure) && !noOverlayExposure.empty(),
            "M15-U8-04: read reference histogram without TAA");
        debugRenderer.RenderWorldView(worldWithDebug, cam, &colRef, &depRef, s, nullptr, expNoDbgTargets, true);
        RENDER_CHECK(debugRenderer.WasLastRenderSuccessful() &&
            ReadTextureRGBA16F(*expNoDbgTargets->GetExposure(), overlayExposure) && !overlayExposure.empty() &&
            glm::length(overlayExposure.front() - noOverlayExposure.front()) < 0.001f,
            "M15-U8-04: debug overlay never meters even with TAA disabled");
        s.EnableAutoExposure = false;
        debugRenderer.RenderWorldView(worldWithDebug, cam, &colRef, &depRef, s, nullptr, targetsRef, true, RenderViewKind::Main, 1.0f / 60.0f);
        if (!debugRenderer.WasLastRenderSuccessful()) return false;
        const auto pxRef = RenderTests::ReadTexture(colRef);
        auto findPeakGreenX = [&](const std::vector<uint8_t>& img, int& peakVal) -> int {
            int bestX = -1;
            peakVal = -1;
            const size_t y = H / 2u;
            for (uint32_t x = 0; x < W; ++x) {
                const int g = int(img[(y * W + x) * 4u + 1u]) - int(img[(y * W + x) * 4u + 0u]);
                if (g > peakVal) { peakVal = g; bestX = int(x); }
            }
            return bestX;
        };
        int refPeakVal = 0;
        const int refPeakX = findPeakGreenX(pxRef, refPeakVal);

        s.EnableAutoExposure = true;
        s.EnableTAA = true;
        s.TAAJitterSpread = 2.0f;
        std::vector<uint8_t> pxTAA6, pxTAA7;
        for (uint32_t f = 0; f < 8u; ++f) {
            debugRenderer.RenderWorldView(worldWithDebug, cam, &colTAA, &depTAA, s, nullptr, expWithDbgTargets, f == 0u, RenderViewKind::Main, 1.0f / 60.0f);
            if (!debugRenderer.WasLastRenderSuccessful()) return false;
            if (f == 6u) pxTAA6 = RenderTests::ReadTexture(colTAA);
            if (f == 7u) pxTAA7 = RenderTests::ReadTexture(colTAA);
        }
        glm::vec4 expNoDbg{}, expWithDbg{};
        if (!ReadExposureValue(*expNoDbgTargets, expNoDbg) ||
            !ReadExposureValue(*expWithDbgTargets, expWithDbg)) return false;

        debugRenderer.Shutdown();

        int pVal6 = 0, pVal7 = 0;
        const int pX6 = findPeakGreenX(pxTAA6, pVal6);
        const int pX7 = findPeakGreenX(pxTAA7, pVal7);
        if (refPeakVal < 150 || pX6 != refPeakX || pX7 != refPeakX || pVal6 < 150 || pVal7 < 150 ||
            std::abs(expWithDbg.r - expNoDbg.r) > 0.005f) {
            std::cerr << "[M15U8] FAIL (14.3): Unjittered PhysicsDebug overlay drifted or polluted AutoExposure! refX="
                      << refPeakX << " pX6=" << pX6 << " pX7=" << pX7
                      << " pVal6=" << pVal6 << " pVal7=" << pVal7
                      << " expNo=" << expNoDbg.r << " expWith=" << expWithDbg.r << "\n";
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // 17. U8 Closure: Post/Temporal GPU Timestamp & VRAM Budget Scorecard (720p)
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t W = 1280, H = 720;
        RHITexture col, dep;
        if (!col.Init(RHITextureDesc::RenderTarget2D(W, H)) ||
            !dep.Init(RHITextureDesc::DepthStencil2D(W, H))) return false;
        auto targets = std::make_shared<RHIViewTargets>();
        RHIViewTargetDesc desc{};
        desc.Width = W;
        desc.Height = H;
        desc.HDR = true;
        desc.EnableDepth = true;
        desc.EnableAutoExposure = true;
        desc.EnableBloom = true;
        desc.HistoryCount = 2;
        desc.EnableVelocity = true;
        desc.EnableReactive = true;
        desc.OwnPrimary = false;
        if (!targets->Configure(desc, nullptr, nullptr, FroxelHistoryPolicy::AllowStaticFog)) return false;

        RHIMaterialDesc opaqueMatDesc{};
        opaqueMatDesc.BaseColor = {0.75f, 0.75f, 0.75f, 1.0f};
        RHIMaterialDesc transMatDesc{};
        transMatDesc.SurfaceType = SurfaceType::Transparent;
        transMatDesc.DepthWrite = false;
        transMatDesc.RenderQueue = 3000;
        transMatDesc.BaseColor = {0.2f, 0.7f, 0.9f, 0.6f};

        RenderWorld world;
        RenderMeshInstance opq;
        opq.EntityID = 5701; opq.StableUUID = UUID(0x5701u); opq.NativeView = cube->CaptureView();
        opq.WorldTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1.2f));
        opq.Material.RHIMaterialAsset = std::make_shared<RHIMaterial>(opaqueMatDesc);
        RenderMeshInstance trn = opq;
        trn.EntityID = 5702; trn.StableUUID = UUID(0x5702u);
        trn.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.4f, 0.0f, 1.2f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.6f));
        trn.Material.RHIMaterialAsset = std::make_shared<RHIMaterial>(transMatDesc);
        world.Meshes = { opq, trn };

        PerspectiveCamera cam(60.0f, float(W) / float(H), 0.1f, 50.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 4.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                          glm::vec3(0.0f, 0.0f, 4.5f));

        SceneRenderSettings s;
        s.EnableShadows = false;
        s.EnableSkybox = false;
        s.EnableParticles = false;
        s.EnableStudioLightLobes = false;
        s.EnableAutoExposure = true;
        s.EnableBloom = true;
        s.BloomThreshold = 0.8f;
        s.BloomIntensity = 0.5f;
        s.EnableTAA = true;
        s.EnableToneMapping = true;

        auto& device = RHIDevice::Get();
        double sumExposure = 0.0, sumBloom = 0.0, sumMotion = 0.0, sumReactive = 0.0, sumTAA = 0.0, sumTone = 0.0, sumTotal = 0.0;
        uint32_t measured = 0;
        constexpr int kWarmup = 4;
        constexpr int kSamples = 6;
        RHICompletionToken priorFrame{};
        for (int f = 0; f <= kWarmup + kSamples; ++f) {
            renderer.RenderWorldView(world, cam, &col, &dep, s, nullptr, targets, f == 0, RenderViewKind::Main, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful() || !device.WaitIdle()) return false;
            const auto& timing = renderer.GetLastGPUTimingFrame();
            auto getPassMs = [&](std::string_view name, double& outMs) -> bool {
                for (const auto& tp : timing.Passes) {
                    if (tp.Name == name && tp.Validity != DiagnosticValidity::Invalid && std::isfinite(tp.Milliseconds)) {
                        outMs = tp.Milliseconds;
                        return true;
                    }
                }
                return false;
            };
            if (f > kWarmup && timing.Validity == DiagnosticValidity::Valid) {
                double eH = 0, eR = 0, b0 = 0, bA = 0, mv = 0, rx = 0, taa = 0, tm = 0;
                if (timing.Completion != priorFrame ||
                    !getPassMs("AutoExposureHistogram", eH) || !getPassMs("AutoExposure", eR) ||
                    !getPassMs("BloomDown0", b0) || !getPassMs("BloomApply", bA) ||
                    !getPassMs("MotionVectors", mv) || !getPassMs("ReactiveMask", rx) ||
                    !getPassMs("TAAResolve", taa) || !getPassMs("ToneMapping", tm)) {
                    std::cerr << "[M15U8] FAIL (Scorecard): Missing or stale GPU timing passes on frame " << f << "\n";
                    return false;
                }
                sumExposure += (eH + eR);
                sumBloom += (b0 + bA);
                sumMotion += mv;
                sumReactive += rx;
                sumTAA += taa;
                sumTone += tm;
                sumTotal += timing.Milliseconds;
                ++measured;
            }
            priorFrame = renderer.GetLastCompletion();
        }
        if (measured != kSamples) {
            std::cerr << "[M15U8] FAIL (Scorecard): Valid GPU timestamps measured=" << measured << " expected=" << kSamples << "\n";
            return false;
        }

        uint64_t totalVramBytes = 0;
        auto addTexBytes = [&](RHITexture* tex) {
            if (!tex || !tex->IsValid()) return;
            nri::MemoryDesc mem{};
            device.GetNRICore().GetTextureMemoryDesc(*tex->GetNative(), nri::MemoryLocation::DEVICE, mem);
            totalVramBytes += mem.size;
        };
        addTexBytes(targets->GetExposure());
        for (uint32_t lvl = 0; lvl < 6u; ++lvl) addTexBytes(targets->GetBloom(lvl));
        addTexBytes(targets->Get(RHIViewTarget::History0));
        addTexBytes(targets->Get(RHIViewTarget::History1));
        addTexBytes(targets->GetVelocity());
        addTexBytes(targets->Get(RHIViewTarget::MotionPreviousDepth));
        addTexBytes(targets->Get(RHIViewTarget::TAAHistoryDepth));
        addTexBytes(targets->GetReactive());

        constexpr uint64_t kMaxPostTemporalBudgetBytes = 64ull * 1024ull * 1024ull;
        const double totalMiB = static_cast<double>(totalVramBytes) / (1024.0 * 1024.0);
        if (totalVramBytes == 0u || totalVramBytes > kMaxPostTemporalBudgetBytes) {
            std::cerr << "[M15U8] FAIL (Scorecard): U8 VRAM out of 64 MiB budget: bytes=" << totalVramBytes << "\n";
            return false;
        }
        const char* backendTag = (device.GetBackend() == RHIType::Vulkan) ? "Vulkan" : "D3D12";
        std::cout << std::fixed << std::setprecision(4)
                  << "[M15U8Scorecard] backend=" << backendTag
                  << " res=1280x720"
                  << " post_temporal_vram_bytes=" << totalVramBytes
                  << " post_temporal_vram_mib=" << totalMiB
                  << " auto_exposure_ms=" << (sumExposure / measured)
                  << " bloom_key_ms=" << (sumBloom / measured)
                  << " motion_vectors_ms=" << (sumMotion / measured)
                  << " reactive_mask_ms=" << (sumReactive / measured)
                  << " taa_resolve_ms=" << (sumTAA / measured)
                  << " tonemapping_ms=" << (sumTone / measured)
                  << " frame_total_ms=" << (sumTotal / measured) << "\n";
    }

    std::cout << "[M15U8] PASS: Batch A + B + C + D + E (AutoExposure, Bloom, Velocity, TAA 3x3, Transparent/Reactive, Unjittered Overlays, Ghosting & Scorecard) exp30="
              << exp30 << " exp60=" << exp60 << " exp120=" << exp120 << "\n";
    return true;
}
