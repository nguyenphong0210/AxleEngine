#include "OcclusionTestSupport.hpp"
#include "RHIReadback.hpp"
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
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace Axle;
using namespace Axle::RHI;

namespace {
    bool ReadTextureRGBA16F(RHITexture& tex, std::vector<glm::vec4>& outPixels) {
        if (!tex.IsValid() || tex.GetFormat() != RHITextureFormat::RGBA16_FLOAT) {
            return false;
        }
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
        desc.DebugName = "DDGIAtlasReadback";
        RHIBuffer staging;
        RHICommandBuffer cmd;
        if (!staging.Init(desc) || !cmd.Init() || !cmd.Begin()) {
            return false;
        }

        nri::TextureRegionDesc region{};
        region.width = static_cast<nri::Dim_t>(width);
        region.height = static_cast<nri::Dim_t>(height);
        region.depth = 1;
        region.layerOffset = 0;
        region.mipOffset = 0;
        region.planes = nri::PlaneBits::ALL;
        cmd.ReadbackTextureToBuffer(*staging.GetNative(), layout, *tex.GetNative(), region);
        cmd.TransitionTexture(tex.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResource);
        if (!cmd.End() || !cmd.Submit() || !device.WaitIdle()) {
            return false;
        }

        const auto* mapped = static_cast<const uint8_t*>(staging.Map());
        if (!mapped) {
            return false;
        }
        outPixels.resize(static_cast<size_t>(width) * height);
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* row = mapped + static_cast<size_t>(y) * layout.rowPitch;
            for (uint32_t x = 0; x < width; ++x) {
                uint64_t packed = 0;
                std::memcpy(&packed, row + static_cast<size_t>(x) * pixelSize, pixelSize);
                outPixels[static_cast<size_t>(y) * width + x] = glm::unpackHalf4x16(packed);
            }
        }
        staging.Unmap();
        return true;
    }

    struct TileStats {
        glm::vec3 MeanIrrad{0.0f};
        glm::vec3 MaxIrrad{0.0f};
        float MinAlpha = 1.0f;
        float MaxAlpha = 0.0f;
        bool BorderMirrorsMatch = true;
    };

    TileStats AnalyzeTile(const std::vector<glm::vec4>& atlas, uint32_t atlasWidth,
                          uint32_t probeIndex, uint32_t atlasCols,
                          uint32_t interior, uint32_t tileSize) {
        TileStats s;
        const glm::ivec2 tile = DDGIContract::ProbeIndexToAtlasTile(probeIndex, atlasCols);
        const uint32_t ox = static_cast<uint32_t>(tile.x) * tileSize;
        const uint32_t oy = static_cast<uint32_t>(tile.y) * tileSize;
        const auto sample = [&](uint32_t lx, uint32_t ly) -> glm::vec4 {
            return atlas[static_cast<size_t>(oy + ly) * atlasWidth + (ox + lx)];
        };

        glm::vec3 sum{0.0f};
        float count = 0.0f;
        for (uint32_t ly = 1u; ly <= interior; ++ly) {
            for (uint32_t lx = 1u; lx <= interior; ++lx) {
                const glm::vec4 v = sample(lx, ly);
                sum += glm::vec3(v);
                s.MaxIrrad = glm::max(s.MaxIrrad, glm::vec3(v));
                s.MinAlpha = std::min(s.MinAlpha, v.a);
                s.MaxAlpha = std::max(s.MaxAlpha, v.a);
                count += 1.0f;
            }
        }
        s.MeanIrrad = count > 0.0f ? (sum / count) : glm::vec3(0.0f);

        const uint32_t last = interior + 1u;
        for (uint32_t ly = 0u; ly <= last; ++ly) {
            for (uint32_t lx = 0u; lx <= last; ++lx) {
                if (lx >= 1u && lx <= interior && ly >= 1u && ly <= interior) {
                    continue;
                }
                uint32_t mx = lx;
                uint32_t my = ly;
                const bool left = lx == 0u, right = lx == last, top = ly == 0u, bottom = ly == last;
                if ((left || right) && (top || bottom)) {
                    mx = left ? interior : 1u;
                    my = top ? interior : 1u;
                } else if (left) {
                    mx = 1u; my = last - ly;
                } else if (right) {
                    mx = interior; my = last - ly;
                } else if (top) {
                    mx = last - lx; my = 1u;
                } else if (bottom) {
                    mx = last - lx; my = interior;
                }
                const glm::vec4 border = sample(lx, ly);
                const glm::vec4 mirrored = sample(mx, my);
                if (glm::length(border - mirrored) > 1e-3f) {
                    s.BorderMirrorsMatch = false;
                }
            }
        }
        return s;
    }

    struct DistStats {
        float MinMean = 1e9f;
        float MaxMean = 0.0f;
        float MeanBackfaceRatio = 0.0f;
        bool MomentsConsistent = true;
        bool BorderMirrorsMatch = true;
    };

    DistStats AnalyzeDistanceTile(const std::vector<glm::vec4>& atlas, uint32_t atlasWidth,
                                    uint32_t probeIndex, uint32_t atlasCols) {
        DistStats s;
        constexpr uint32_t interior = DDGIContract::DistanceInterior;
        constexpr uint32_t tileSize = DDGIContract::DistanceTileSize;
        const glm::ivec2 tile = DDGIContract::ProbeIndexToAtlasTile(probeIndex, atlasCols);
        const uint32_t ox = static_cast<uint32_t>(tile.x) * tileSize;
        const uint32_t oy = static_cast<uint32_t>(tile.y) * tileSize;
        const auto sample = [&](uint32_t lx, uint32_t ly) -> glm::vec4 {
            return atlas[static_cast<size_t>(oy + ly) * atlasWidth + (ox + lx)];
        };
        float bfSum = 0.0f;
        float count = 0.0f;
        for (uint32_t ly = 1u; ly <= interior; ++ly) {
            for (uint32_t lx = 1u; lx <= interior; ++lx) {
                const glm::vec4 v = sample(lx, ly);
                s.MinMean = std::min(s.MinMean, v.x);
                s.MaxMean = std::max(s.MaxMean, v.x);
                bfSum += v.w;
                count += 1.0f;
                // E[r^2] >= E[r]^2 minus small half-float rounding tolerance
                if (!(v.y + 0.05f >= v.x * v.x && std::isfinite(v.x) && std::isfinite(v.y))) {
                    s.MomentsConsistent = false;
                }
            }
        }
        s.MeanBackfaceRatio = count > 0.0f ? (bfSum / count) : 0.0f;
        const TileStats borderCheck = AnalyzeTile(atlas, atlasWidth, probeIndex, atlasCols, interior, tileSize);
        s.BorderMirrorsMatch = borderCheck.BorderMirrorsMatch;
        return s;
    }

    std::shared_ptr<RHIMaterial> MakeSolidMat(const glm::vec3& albedo, const glm::vec3& emissive = glm::vec3(0.0f), float emissiveIntensity = 0.0f) {
        RHIMaterialDesc desc{};
        desc.CullMode = CullMode::Back;
        desc.ShadingModel = RHIShadingModel::MetallicRoughness;
        desc.BaseColor = glm::vec4(albedo, 1.0f);
        desc.EmissiveColor = emissive;
        desc.EmissiveIntensity = emissiveIntensity;
        desc.Roughness = 0.85f;
        desc.Metallic = 0.0f;
        return std::make_shared<RHIMaterial>(desc);
    }
}

bool RunM15DDGITests(RHISceneRenderer& renderer, const std::filesystem::path&) {
    std::cout << "[M15DDGI] Running Milestone 15 Unit 7 (Experimental DDGI GPU Acquisition & Moments) Tests...\n";

    auto& device = RHIDevice::Get();
    auto cube = RHIMesh::CreateCube();
    if (!cube || !cube->CaptureView() || !cube->CaptureView()->IsValid()) {
        std::cerr << "[M15DDGI] FAIL: failed to create builtin cube mesh\n";
        return false;
    }

    constexpr uint32_t width = 64u, height = 64u;
    RHITexture color, depth;
    if (!color.Init(RHITextureDesc::RenderTarget2D(width, height, RHITextureFormat::RGBA8_UNORM, "DDGITestColor")) ||
        !depth.Init(RHITextureDesc::DepthStencil2D(width, height, RHITextureFormat::D32_FLOAT, "DDGITestDepth"))) {
        return false;
    }

    // 4x2x4 = 32 probes: X in [0..6], Y in [1..3], Z in [0..6]
    RenderDDGIProbeGrid grid{};
    grid.Enabled = true;
    grid.GridDimensions = glm::ivec3(4, 2, 4);
    grid.GridOrigin = glm::vec3(0.0f, 1.0f, 0.0f);
    grid.GridSpacing = glm::vec3(2.0f, 2.0f, 2.0f);
    grid.EnergyMultiplier = 1.0f;
    grid.UpdateFraction = 0.25f;
    grid.MaxProbesPerFrame = 8;
    grid.Hysteresis = 0.90f;

    const auto extent = DDGIContract::ComputeAtlasExtent(grid.GridDimensions);
    auto targets = std::make_shared<RHIViewTargets>();
    RHIViewTargetDesc targetDesc{};
    targetDesc.Width = width;
    targetDesc.Height = height;
    targetDesc.ColorFormat = RHITextureFormat::RGBA8_UNORM;
    targetDesc.DepthFormat = RHITextureFormat::D32_FLOAT;
    targetDesc.EnableDepth = true;
    targetDesc.OwnPrimary = false;
    targetDesc.EnableDDGI = true;
    targetDesc.DDGIGridX = static_cast<uint32_t>(grid.GridDimensions.x);
    targetDesc.DDGIGridY = static_cast<uint32_t>(grid.GridDimensions.y);
    targetDesc.DDGIGridZ = static_cast<uint32_t>(grid.GridDimensions.z);
    if (!targets->Configure(targetDesc)) {
        std::cerr << "[M15DDGI] FAIL: failed to configure dual DDGI view targets\n";
        return false;
    }

    RenderWorld world{};
    world.DDGIProbeGrid = grid;
    world.SceneRevision = 1;
    // Bright emissive red wall right next to probe 0..7 along X = -1.2 (within X=0 probes' hemisphere)
    RenderMeshInstance emissiveWall{};
    emissiveWall.EntityID = 101;
    emissiveWall.NativeView = cube->CaptureView();
    emissiveWall.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.2f, 2.0f, 3.0f)) *
                                  glm::scale(glm::mat4(1.0f), glm::vec3(0.4f, 6.0f, 10.0f));
    emissiveWall.Material.RHIMaterialAsset = MakeSolidMat(glm::vec3(0.9f, 0.05f, 0.05f), glm::vec3(1.0f, 0.04f, 0.04f), 8.0f);
    world.Meshes.push_back(emissiveWall);

    // Enclose probe index 1 (coord (1,0,0) = world (2.0, 1.0, 0.0)) tightly inside a solid cube
    // to verify SV_IsFrontFace inside-geometry backface detection!
    const uint32_t enclosedProbeIdx = DDGIContract::ProbeCoordToIndex({1, 0, 0}, grid.GridDimensions);
    const glm::vec3 enclosedProbePos = DDGIContract::ProbeIndexToWorldPos(enclosedProbeIdx, grid);
    RenderMeshInstance trapCube{};
    trapCube.EntityID = 102;
    trapCube.NativeView = cube->CaptureView();
    trapCube.WorldTransform = glm::translate(glm::mat4(1.0f), enclosedProbePos) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(1.2f, 1.2f, 1.2f));
    trapCube.Material.RHIMaterialAsset = MakeSolidMat(glm::vec3(0.15f, 0.15f, 0.15f));
    world.Meshes.push_back(trapCube);

    // Camera looks FAR AWAY from the emissive red wall & trap cube (negative Z empty void)
    // to verify DDGI probe captures world geometry independently of main camera frustum!
    PerspectiveCamera camAway(60.0f, 1.0f, 0.1f, 200.0f);
    camAway.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 3.0f, -40.0f), glm::vec3(0.0f, 3.0f, -80.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                          glm::vec3(0.0f, 3.0f, -40.0f));

    SceneRenderSettings settings{};
    settings.EnableSkybox = false;
    settings.EnableShadows = false;
    settings.EnableParticles = false;
    settings.EnableToneMapping = false;
    settings.EnableStudioLightLobes = false;
    settings.EnableDDGI = true;

    auto renderViewWith = [&](const PerspectiveCamera& cam, std::shared_ptr<RHIViewTargets> viewTargets) -> bool {
        renderer.RenderWorldView(world, cam, &color, &depth, settings, nullptr, std::move(viewTargets));
        return renderer.WasLastRenderSuccessful() && device.WaitIdle();
    };
    auto renderView = [&](const PerspectiveCamera& cam) -> bool {
        return renderViewWith(cam, targets);
    };

    // Frame 0: Seed reset clears atlas & updates probes [0..7]
    if (!renderView(camAway)) {
        std::cerr << "[M15DDGI] FAIL: Frame 0 render failed\n";
        return false;
    }
    const auto f0GI = renderer.GetGIFrame();
    if (!f0GI.UpdateScheduled || !f0GI.IrradianceAvailable || !f0GI.VisibilityAvailable ||
        !f0GI.SamplingEnabled || f0GI.ProbeCursor != 0u || f0GI.UpdatedProbeCount != 8u) {
        std::cerr << "[M15DDGI] FAIL: Frame 0 GI telemetry mismatch (cursor=" << f0GI.ProbeCursor
                  << ", updated=" << f0GI.UpdatedProbeCount << ", sampling=" << f0GI.SamplingEnabled << ")\n";
        return false;
    }

    std::vector<glm::vec4> irrad0, dist0;
    if (!ReadTextureRGBA16F(*targets->GetDDGI(), irrad0) ||
        !ReadTextureRGBA16F(*targets->GetDDGIDistance(), dist0)) {
        std::cerr << "[M15DDGI] FAIL: Atlas readback failed on Frame 0\n";
        return false;
    }

    // Air probe (0,0,0) = index 0 sits at (0, 1, 0), directly facing red wall at x=-1.2
    const auto p0Irrad = AnalyzeTile(irrad0, extent.IrradianceWidth, 0u, extent.AtlasCols,
                                     DDGIContract::IrradianceInterior, DDGIContract::IrradianceTileSize);
    const auto p0Dist = AnalyzeDistanceTile(dist0, extent.DistanceWidth, 0u, extent.AtlasCols);
    // Enclosed probe index 1 sits inside solid cube -> high backface ratio
    const auto p1Dist = AnalyzeDistanceTile(dist0, extent.DistanceWidth, enclosedProbeIdx, extent.AtlasCols);
    // Probe 8 (outside [0..7] batch) was cleared by CSSeedClear -> alpha remains 0 on Frame 0
    const auto p8Unseeded = AnalyzeTile(irrad0, extent.IrradianceWidth, 8u, extent.AtlasCols,
                                        DDGIContract::IrradianceInterior, DDGIContract::IrradianceTileSize);

    if (!p0Irrad.BorderMirrorsMatch || !p0Dist.BorderMirrorsMatch || !p0Dist.MomentsConsistent || !p1Dist.MomentsConsistent) {
        std::cerr << "[M15DDGI] FAIL: Octahedral border mirroring or Chebyshev moment invariant failed\n";
        return false;
    }
    if (!(p0Irrad.MeanIrrad.r > 0.2f && p0Irrad.MeanIrrad.r > p0Irrad.MeanIrrad.g * 3.0f && p0Irrad.MinAlpha > 0.5f)) {
        std::cerr << "[M15DDGI] FAIL: Probe 0 did not capture world-space red bounce away from camera! mean=("
                  << p0Irrad.MeanIrrad.r << "," << p0Irrad.MeanIrrad.g << "," << p0Irrad.MeanIrrad.b << ") a=" << p0Irrad.MinAlpha << "\n";
        return false;
    }
    if (!(p0Dist.MinMean < 1.2f && p0Dist.MeanBackfaceRatio < 0.25f)) {
        std::cerr << "[M15DDGI] FAIL: Air probe 0 hit distance / low backface check failed (minMean="
                  << p0Dist.MinMean << ", back=" << p0Dist.MeanBackfaceRatio << ")\n";
        return false;
    }
    if (!(p1Dist.MeanBackfaceRatio > 0.7f)) {
        std::cerr << "[M15DDGI] FAIL: Enclosed probe inside solid cube did not trigger backface detection (back="
                  << p1Dist.MeanBackfaceRatio << ")\n";
        return false;
    }
    if (!(p8Unseeded.MaxAlpha == 0.0f)) {
        std::cerr << "[M15DDGI] FAIL: Staggered batch leaked beyond [0..7] on Frame 0 (p8 alpha=" << p8Unseeded.MaxAlpha << ")\n";
        return false;
    }

    // Frames 1..4: Move main camera arbitrarily; verify round-robin cursor [8, 16, 24, 0]
    PerspectiveCamera camOther(60.0f, 1.0f, 0.1f, 200.0f);
    camOther.SetViewMatrix(glm::lookAtRH(glm::vec3(30.0f, 10.0f, 30.0f), glm::vec3(3.0f, 2.0f, 3.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                           glm::vec3(30.0f, 10.0f, 30.0f));
    const uint32_t expectedCursors[4] = { 8u, 16u, 24u, 0u };
    for (uint32_t step = 0; step < 4u; ++step) {
        if (!renderView(camOther)) {
            std::cerr << "[M15DDGI] FAIL: Staggered rotation render step " << step << " failed\n";
            return false;
        }
        const auto gi = renderer.GetGIFrame();
        if (gi.ProbeCursor != expectedCursors[step] || gi.UpdatedProbeCount != 8u) {
            std::cerr << "[M15DDGI] FAIL: Staggered cursor expected " << expectedCursors[step]
                      << " got " << gi.ProbeCursor << "\n";
            return false;
        }
    }

    std::vector<glm::vec4> irradSteady;
    if (!ReadTextureRGBA16F(*targets->GetDDGI(), irradSteady)) {
        return false;
    }
    const auto p8Seeded = AnalyzeTile(irradSteady, extent.IrradianceWidth, 8u, extent.AtlasCols,
                                      DDGIContract::IrradianceInterior, DDGIContract::IrradianceTileSize);
    if (!(p8Seeded.MinAlpha > 0.5f)) {
        std::cerr << "[M15DDGI] FAIL: Staggered rotation did not warm probe 8 cleanly\n";
        return false;
    }

    // Scene edit invalidation test: replace red emissive wall with bright GREEN wall -> must reset cursor to 0, clear old red [8..31], and capture GREEN on [0..7]!
    world.Meshes[0].Material.RHIMaterialAsset = MakeSolidMat(glm::vec3(0.05f, 0.9f, 0.05f), glm::vec3(0.04f, 1.0f, 0.04f), 8.0f);
    world.SceneRevision = 2;
    if (!renderView(camOther)) {
        return false;
    }
    const auto fInvalidGI = renderer.GetGIFrame();
    std::vector<glm::vec4> irradInvalid;
    if (!ReadTextureRGBA16F(*targets->GetDDGI(), irradInvalid)) {
        return false;
    }
    const auto p0Green = AnalyzeTile(irradInvalid, extent.IrradianceWidth, 0u, extent.AtlasCols,
                                     DDGIContract::IrradianceInterior, DDGIContract::IrradianceTileSize);
    const auto p8Cleared = AnalyzeTile(irradInvalid, extent.IrradianceWidth, 8u, extent.AtlasCols,
                                       DDGIContract::IrradianceInterior, DDGIContract::IrradianceTileSize);
    if (fInvalidGI.ProbeCursor != 0u ||
        !fInvalidGI.SamplingEnabled ||
        !(p0Green.MeanIrrad.g > 0.2f && p0Green.MeanIrrad.g > p0Green.MeanIrrad.r * 3.0f) ||
        !(p8Cleared.MaxAlpha == 0.0f)) {
        std::cerr << "[M15DDGI] FAIL: Scene revision invalidation left stale red bounce or failed reset\n";
        return false;
    }

    // U7-C Live Forward Diffuse GI & Fallback Check (M15-U7-06 / 07.2):
    // Moderate green emitter keeps indirect HDR bounce inside non-clipped RGBA8 SDR range.
    world.Meshes[0].Material.RHIMaterialAsset = MakeSolidMat(glm::vec3(0.02f, 0.6f, 0.02f), glm::vec3(0.01f, 0.8f, 0.01f), 2.0f);
    // Place an unlit matte white receiver wall at X=1.2 facing the green emitter wall at X=-1.2
    // inside the air probes at X=0. Main camera looks straight at the receiver wall (emitter behind camera).
    RenderMeshInstance receiverWall{};
    receiverWall.EntityID = 103;
    receiverWall.NativeView = cube->CaptureView();
    receiverWall.WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.2f, 2.0f, 3.0f)) *
                                  glm::scale(glm::mat4(1.0f), glm::vec3(0.4f, 6.0f, 10.0f));
    receiverWall.Material.RHIMaterialAsset = MakeSolidMat(glm::vec3(0.9f, 0.9f, 0.9f));
    world.Meshes.push_back(receiverWall);
    world.SceneRevision = 3;

    PerspectiveCamera camReceiver(60.0f, 1.0f, 0.1f, 200.0f);
    camReceiver.SetViewMatrix(
        glm::lookAtRH(glm::vec3(-0.3f, 2.0f, 3.0f), glm::vec3(1.2f, 2.0f, 3.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
        glm::vec3(-0.3f, 2.0f, 3.0f));

    auto readCenterRGB = [&]() -> glm::ivec3 {
        const auto px = Axle::RenderTests::ReadTexture(color);
        if (px.size() != static_cast<size_t>(width) * height * 4u) {
            return glm::ivec3(-1);
        }
        const size_t idx = (static_cast<size_t>(height / 2u) * width + (width / 2u)) * 4u;
        return glm::ivec3(px[idx + 0], px[idx + 1], px[idx + 2]);
    };

    auto offTargets = std::make_shared<RHIViewTargets>();
    RHIViewTargetDesc offDesc = targetDesc;
    offDesc.EnableDDGI = false;
    offDesc.DDGIGridX = 0u;
    offDesc.DDGIGridY = 0u;
    offDesc.DDGIGridZ = 0u;
    if (!offTargets->Configure(offDesc)) {
        std::cerr << "[M15DDGI] FAIL: failed to configure off baseline targets\n";
        return false;
    }

    settings.EnableDDGI = false;
    const bool offOk = renderViewWith(camReceiver, offTargets);
    if (!offOk || renderer.GetGIFrame().SamplingEnabled) {
        std::cerr << "[M15DDGI] FAIL: Disabled DDGI baseline render failed or reported SamplingEnabled"
                  << " (offOk=" << offOk
                  << ", lastRender=" << renderer.WasLastRenderSuccessful()
                  << ", sampling=" << renderer.GetGIFrame().SamplingEnabled
                  << ", status=" << static_cast<int>(renderer.GetGIFrame().Status)
                  << ", updateSched=" << renderer.GetGIFrame().UpdateScheduled
                  << ", diag=" << renderer.GetGIFrame().Diagnostic << ")\n";
        return false;
    }
    const glm::ivec3 offRGB = readCenterRGB();
    if (offRGB.r < 0 || offRGB.r > 2 || offRGB.g > 2 || offRGB.b > 2) {
        std::cerr << "[M15DDGI] FAIL: Disabled DDGI unlit receiver baseline not dark ("
                  << offRGB.r << "," << offRGB.g << "," << offRGB.b << ")\n";
        return false;
    }

    settings.EnableDDGI = true;
    if (!renderView(camReceiver) || !renderer.GetGIFrame().SamplingEnabled) {
        std::cerr << "[M15DDGI] FAIL: Active DDGI forward receiver render failed\n";
        return false;
    }
    const glm::ivec3 bounceFrame0 = readCenterRGB();
    for (int f = 0; f < 4; ++f) {
        if (!renderView(camReceiver) || !renderer.GetGIFrame().SamplingEnabled) {
            std::cerr << "[M15DDGI] FAIL: Multi-bounce warm frame " << f << " failed\n";
            return false;
        }
    }
    const glm::ivec3 bounceSteady = readCenterRGB();
    if (!(bounceFrame0.g > 30 && bounceFrame0.g > bounceFrame0.r * 2 &&
          bounceSteady.g >= bounceFrame0.g && bounceSteady.g > bounceSteady.r * 2)) {
        std::cerr << "[M15DDGI] FAIL: White receiver wall did not receive indirect green bounce ("
                  << "f0=(" << bounceFrame0.r << "," << bounceFrame0.g << "," << bounceFrame0.b << "), "
                  << "steady=(" << bounceSteady.r << "," << bounceSteady.g << "," << bounceSteady.b << "))\n";
        return false;
    }

    settings.EnableDDGI = false;
    if (!renderViewWith(camReceiver, nullptr) || renderer.GetGIFrame().SamplingEnabled || readCenterRGB() != offRGB) {
        std::cerr << "[M15DDGI] FAIL: Turning DDGI back off did not restore exact baseline\n";
        return false;
    }

    std::cout << "[M15DDGI] PASS: world-space probe capture ("
              << "p0_red=(" << p0Irrad.MeanIrrad.r << "," << p0Irrad.MeanIrrad.g << "), "
              << "p0_dist_min=" << p0Dist.MinMean << ", "
              << "air_backface=" << p0Dist.MeanBackfaceRatio << ", "
              << "trapped_backface=" << p1Dist.MeanBackfaceRatio << ", "
              << "p0_green=(" << p0Green.MeanIrrad.r << "," << p0Green.MeanIrrad.g << "), "
              << "fwd_bounce=(" << bounceSteady.r << "," << bounceSteady.g << "," << bounceSteady.b << "))\n";
    return true;
}
