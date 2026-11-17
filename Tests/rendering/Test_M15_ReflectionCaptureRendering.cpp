#include "OcclusionTestSupport.hpp"
#include "RHIReadback.hpp"
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RenderParticleData.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

using namespace Axle;
using namespace Axle::RHI;
using Axle::RenderTests::Read;
using Axle::RenderTests::Upload;

namespace {
    bool ReadCubeFaceRGBA16(RHITexture& cubemap, uint32_t face, std::vector<glm::vec4>& pixels) {
        if (!cubemap.IsValid() || !cubemap.GetDesc().IsCubeMap || face >= 6 ||
            cubemap.GetFormat() != RHITextureFormat::RGBA16_FLOAT) {
            return false;
        }
        auto& device = RHIDevice::Get();
        const auto alignment = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
        const auto align = [](uint32_t value, uint32_t step) { return (value + step - 1u) / step * step; };
        const uint32_t w = cubemap.GetWidth();
        const uint32_t h = cubemap.GetHeight();
        nri::TextureDataLayoutDesc layout{};
        layout.rowPitch = align(w * 8u, alignment.uploadBufferTextureRow);
        layout.slicePitch = align(layout.rowPitch * h, alignment.uploadBufferTextureSlice);

        RHIBufferDesc desc{};
        desc.Size = layout.slicePitch;
        desc.MemoryLocation = RHIMemoryLocation::HostReadback;
        RHIBuffer readback;
        RHICommandBuffer commands;
        if (!readback.Init(desc) || !commands.Init() || !commands.Begin()) {
            return false;
        }

        nri::TextureRegionDesc region{};
        region.width = static_cast<nri::Dim_t>(w);
        region.height = static_cast<nri::Dim_t>(h);
        region.depth = 1;
        region.mipOffset = 0;
        region.layerOffset = static_cast<nri::Dim_t>(face);
        region.planes = nri::PlaneBits::COLOR;

        commands.ReadbackTextureToBuffer(*readback.GetNative(), layout, *cubemap.GetNative(), region);
        commands.TransitionTexture(cubemap.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResource);
        if (!commands.End() || !commands.Submit() || !device.WaitIdle()) {
            return false;
        }

        const auto* data = static_cast<const uint8_t*>(readback.Map());
        if (!data) {
            return false;
        }
        pixels.resize(size_t(w) * h);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                uint32_t packed[2]{};
                std::memcpy(packed, data + size_t(y) * layout.rowPitch + size_t(x) * 8u, 8);
                pixels[size_t(y) * w + x] = glm::vec4(
                    glm::unpackHalf2x16(packed[0]),
                    glm::unpackHalf2x16(packed[1]));
            }
        }
        readback.Unmap();
        return true;
    }

    std::shared_ptr<RHIMaterial> MakeEmissiveMaterial(
        const glm::vec3& emissive,
        float intensity = 1.0f,
        SurfaceType surfaceType = SurfaceType::Opaque,
        CullMode cullMode = CullMode::None) {
        RHIMaterialDesc desc{};
        desc.SurfaceType = surfaceType;
        desc.CullMode = cullMode;
        desc.BaseColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        desc.Metallic = 0.0f;
        desc.Roughness = 1.0f;
        desc.EmissiveColor = emissive;
        desc.EmissiveIntensity = intensity;
        return std::make_shared<RHIMaterial>(desc);
    }

    RenderMeshInstance MakeBoxInstance(
        uint32_t entityID,
        const std::shared_ptr<const RHIMeshView>& meshView,
        const std::shared_ptr<RHIMaterial>& material,
        const glm::vec3& position,
        const glm::vec3& scale,
        bool castsShadows = true) {
        RenderMeshInstance inst{};
        inst.EntityID = entityID;
        inst.NativeView = meshView;
        inst.Material.RHIMaterialAsset = material;
        inst.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), position), scale);
        inst.CastsShadows = castsShadows;
        return inst;
    }

    bool WriteHDRCubeStripPPM(
        const std::filesystem::path& path,
        RHITexture& cubemap,
        uint32_t size) {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        const uint32_t totalWidth = size * 6u;
        file << "P6\n" << totalWidth << ' ' << size << "\n255\n";
        std::vector<std::vector<glm::vec4>> faces(6);
        for (uint32_t f = 0; f < 6; ++f) {
            if (!ReadCubeFaceRGBA16(cubemap, f, faces[f])) return false;
        }
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t f = 0; f < 6; ++f) {
                for (uint32_t x = 0; x < size; ++x) {
                    const glm::vec4& p = faces[f][size_t(y) * size + x];
                    for (int c = 0; c < 3; ++c) {
                        const float v = std::max(0.0f, p[c]);
                        const float mapped = std::clamp((v * (2.51f * v + 0.03f)) / (v * (2.43f * v + 0.59f) + 0.14f), 0.0f, 1.0f);
                        const float srgb = std::pow(mapped, 1.0f / 2.2f);
                        const unsigned char b = static_cast<unsigned char>(std::lround(srgb * 255.0f));
                        file.write(reinterpret_cast<const char*>(&b), 1);
                    }
                }
            }
        }
        return bool(file);
    }

    glm::vec3 AverageRegion(
        const std::vector<glm::vec4>& pixels,
        uint32_t width,
        uint32_t x0, uint32_t y0,
        uint32_t x1, uint32_t y1) {
        glm::vec3 sum(0.0f);
        uint32_t count = 0;
        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = x0; x < x1; ++x) {
                sum += glm::vec3(pixels[size_t(y) * width + x]);
                ++count;
            }
        }
        return count > 0 ? (sum / static_cast<float>(count)) : glm::vec3(0.0f);
    }
} // namespace

bool RunM15ReflectionCaptureTests(RHISceneRenderer& renderer, const std::filesystem::path& output) {
    std::cout << "[M15ReflectionCaptureGPU] Running native D3D12/Vulkan U5-B dynamic capture tests...\n";
    auto& device = RHIDevice::Get();

    auto cubeMesh = RHIMesh::CreateCube(1.0f);
    auto sphereMesh = RHIMesh::CreateSphere(1.0f, 24, 32);
    RENDER_CHECK(cubeMesh && cubeMesh->IsValid() && sphereMesh && sphereMesh->IsValid(),
        "U5-B fixture meshes");
    auto cubeView = cubeMesh->CaptureView();
    auto sphereView = sphereMesh->CaptureView();

    // -------------------------------------------------------------------------
    // 1. Six-face linear HDR orientation + off-axis quadrant oracle + isolation
    //    (08.1, 08.2, 09.1, 09.2, 10.1, 10.2, 10.3, 10.4, 11.1, 12.1, 12.2, 13.1)
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t captureRes = 32;
        RenderWorld world;
        world.SceneRevision = 100;
        world.Skybox.Authored = true;
        world.Skybox.Enabled = false;

        // Distinct emissive walls along all 6 axes (+X includes HDR > 1.0 radiance = 3.0)
        world.Meshes.push_back(MakeBoxInstance(10, cubeView, MakeEmissiveMaterial({1.0f, 0.0f, 0.0f}, 3.0f), { 6.0f, 0.0f, 0.0f}, {0.5f, 8.0f, 8.0f})); // +X: Red HDR
        world.Meshes.push_back(MakeBoxInstance(11, cubeView, MakeEmissiveMaterial({0.0f, 1.0f, 0.0f}, 1.0f), {-6.0f, 0.0f, 0.0f}, {0.5f, 8.0f, 8.0f})); // -X: Green
        world.Meshes.push_back(MakeBoxInstance(12, cubeView, MakeEmissiveMaterial({0.0f, 0.0f, 1.0f}, 1.0f), { 0.0f, 6.0f, 0.0f}, {8.0f, 0.5f, 8.0f})); // +Y: Blue
        world.Meshes.push_back(MakeBoxInstance(13, cubeView, MakeEmissiveMaterial({1.0f, 1.0f, 0.0f}, 1.0f), { 0.0f,-6.0f, 0.0f}, {8.0f, 0.5f, 8.0f})); // -Y: Yellow
        world.Meshes.push_back(MakeBoxInstance(14, cubeView, MakeEmissiveMaterial({0.0f, 1.0f, 1.0f}, 1.0f), { 0.0f, 0.0f, 6.0f}, {8.0f, 8.0f, 0.5f})); // +Z: Cyan
        world.Meshes.push_back(MakeBoxInstance(15, cubeView, MakeEmissiveMaterial({1.0f, 0.0f, 1.0f}, 1.0f), { 0.0f, 0.0f,-6.0f}, {8.0f, 8.0f, 0.5f})); // -Z: Magenta

        // Cutout mesh (must be captured!) in -Z's BOTTOM-LEFT quadrant (+X, -Y)
        auto cutoutMat = MakeEmissiveMaterial({1.0f, 0.6f, 0.0f}, 2.0f, SurfaceType::Cutout);
        world.Meshes.push_back(MakeBoxInstance(16, cubeView, cutoutMat, {1.8f, -1.8f, -4.0f}, {1.6f, 1.6f, 0.4f}));
        // Transparent blocker covering +X -> MUST be excluded so +X center stays pure HDR Red!
        auto transparentMat = MakeEmissiveMaterial({0.0f, 0.0f, 1.0f}, 5.0f, SurfaceType::Transparent);
        world.Meshes.push_back(MakeBoxInstance(17, cubeView, transparentMat, {3.0f, 0.0f, 0.0f}, {0.3f, 6.0f, 6.0f}));

        // Off-axis markers proving U/V orientation after horizontal flip (zero mirror flips!):
        // Placed at (+-2.5, +-2.5) at distance 5.0 with size 2.0 to cleanly fill the expected quadrant.
        // +X face (Forward=+X, Right=-Z, Up=+Y): (+5, +2.5, -2.5) => TOP-RIGHT quadrant!
        auto markerMat = MakeEmissiveMaterial({0.0f, 1.0f, 1.0f}, 4.0f); // Cyan marker on Red +X
        world.Meshes.push_back(MakeBoxInstance(18, cubeView, markerMat, {5.0f, 2.5f, -2.5f}, {0.5f, 2.0f, 2.0f}));
        // +Y face (Forward=+Y, Right=+X, Up=-Z): (+2.5, +5, -2.5) => TOP-RIGHT quadrant!
        auto markerMatY = MakeEmissiveMaterial({1.0f, 1.0f, 0.0f}, 4.0f); // Yellow marker on Blue +Y
        world.Meshes.push_back(MakeBoxInstance(19, cubeView, markerMatY, {2.5f, 5.0f, -2.5f}, {2.0f, 0.5f, 2.0f}));
        // +Z face (Forward=+Z, Right=+X, Up=+Y): (+2.5, -2.5, +5) => BOTTOM-RIGHT quadrant!
        auto markerMatZ = MakeEmissiveMaterial({1.0f, 0.0f, 0.0f}, 4.0f); // Red marker on Cyan +Z
        world.Meshes.push_back(MakeBoxInstance(20, cubeView, markerMatZ, {2.5f, -2.5f, 5.0f}, {2.0f, 2.0f, 0.5f}));

        // Self/probe entity wrapping origin (EntityID = 777) -> MUST be excluded!
        world.Meshes.push_back(MakeBoxInstance(777, sphereView, MakeEmissiveMaterial({1.0f, 1.0f, 1.0f}, 10.0f), {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}));

        // Debug overlay line crossing the probe view -> MUST be excluded!
        RenderDebugLine debugLine{};
        debugLine.From = {2.0f, -2.0f, -2.0f};
        debugLine.To = {2.0f, 2.0f, 2.0f};
        debugLine.Color = {1.0f, 1.0f, 1.0f, 1.0f};
        world.DebugLines.push_back(debugLine);

        RenderReflectionProbe probe{};
        probe.EntityID = 777;
        probe.Position = {0.0f, 0.0f, 0.0f};
        probe.BoxExtents = {16.0f, 16.0f, 16.0f};
        probe.DynamicCapture = true;
        probe.CaptureResolution = captureRes;
        probe.CaptureShadows = false;
        probe.CaptureNearPlane = 0.1f;
        probe.CaptureFarPlane = 50.0f;
        world.ReflectionProbes.push_back(probe);

        // First render 2 main-view frames with TAA + SSR + ToneMapping + Debug enabled to verify zero TAA/state contamination!
        RHITexture mainColor, mainDepth;
        RENDER_CHECK(mainColor.Init(RHITextureDesc::RenderTarget2D(64, 64)) &&
                     mainDepth.Init(RHITextureDesc::DepthStencil2D(64, 64)),
            "U5-B main view targets");
        PerspectiveCamera mainCam(55.0f, 1.0f, 0.1f, 100.0f);
        mainCam.SetViewMatrix(glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0)), {0, 0, 3});
        SceneRenderSettings noisySettings;
        noisySettings.EnableTAA = true;
        noisySettings.EnableSSR = true;
        noisySettings.EnableToneMapping = true;
        noisySettings.EnableBloom = true;
        noisySettings.EnableGTAO = true;
        noisySettings.ShowPhysicsDebug = true;
        noisySettings.EnableParticles = false;
        noisySettings.EnableShadows = false;

        // Avoid auto-capturing the incomplete test world twice: pre-capture explicitly or run main view on a probe-free world
        RenderWorld mainWorld = world;
        mainWorld.ReflectionProbes.clear();
        renderer.RenderWorldView(mainWorld, mainCam, &mainColor, &mainDepth, noisySettings);
        RENDER_CHECK(renderer.WasLastRenderSuccessful(), "U5-B main view frame 0");
        renderer.RenderWorldView(mainWorld, mainCam, &mainColor, &mainDepth, noisySettings);
        RENDER_CHECK(renderer.WasLastRenderSuccessful(), "U5-B main view frame 1");
        const uint64_t frameIndexBeforeCapture = renderer.GetDiagnostics().GetFrame().FrameIndex;
        const RHICompletionToken mainCompletionBefore = renderer.GetLastCompletion();

        // Execute dynamic six-face capture with noisy settings (must isolate all post/SSR/TAA/debug/self/transparent)
        RenderSkyboxData preparedSky;
        preparedSky.Authored = preparedSky.Enabled = preparedSky.EnvironmentLighting = true;
        preparedSky.Type = 1; preparedSky.HDRPath = "red.hdr";
        RENDER_CHECK(renderer.GetEnvironment().Prepare(preparedSky) && renderer.GetEnvironment().Ready(), "prepare main HDR environment sentinel");
        const auto environmentBeforeCapture = renderer.GetEnvironment().Ready();
        const DynamicReflectionCaptureResult captureResInfo =
            renderer.CaptureDynamicReflectionProbe(world, probe, noisySettings);
        RENDER_CHECK(renderer.GetEnvironment().Ready() == environmentBeforeCapture, "capture preserves main environment publication");
        RENDER_CHECK(captureResInfo.Success && captureResInfo.CapturedFaces == 6 &&
                     captureResInfo.PublishedGenerationID > 0,
            "U5-B dynamic capture publishes candidate after all 6 faces");
        // Verify main view FrameIndex / completion were untouched by capture!
        RENDER_CHECK(renderer.GetDiagnostics().GetFrame().FrameIndex == frameIndexBeforeCapture &&
                     renderer.GetLastCompletion().Value == mainCompletionBefore.Value,
            "M15-U5-10.2: dynamic capture isolates main-view ViewContext, FrameIndex, and completion");

        // Next main-view frame must continue smoothly from frameIndexBeforeCapture -> +1 without TAA reset!
        renderer.RenderWorldView(mainWorld, mainCam, &mainColor, &mainDepth, noisySettings);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() &&
                     renderer.GetDiagnostics().GetFrame().FrameIndex == frameIndexBeforeCapture + 1,
            "M15-U5-10.2: main-view TAA history progression stays intact across reflection capture");

        auto gen = renderer.GetDynamicReflectionProbe(probe);
        RENDER_CHECK(gen && gen->Cubemap && gen->Cubemap->IsValid() &&
                     gen->Completion && gen->Cubemap->GetDesc().IsCubeMap &&
                     gen->Cubemap->GetWidth() == captureRes &&
                     gen->Cubemap->GetFormat() == RHITextureFormat::RGBA16_FLOAT,
            "M15-U5-08.1/13.1: published cubemap retains linear HDR RGBA16_FLOAT storage & completion token");
        RENDER_CHECK(device.WaitForCompletion(gen->Completion), "M15-U5-13.1: wait on dynamic probe completion");

        std::vector<std::vector<glm::vec4>> faces(6);
        for (uint32_t f = 0; f < 6; ++f) {
            RENDER_CHECK(ReadCubeFaceRGBA16(*gen->Cubemap, f, faces[f]) &&
                         faces[f].size() == size_t(captureRes) * captureRes,
                "M15-U5-12.2: cubemap face layer readback succeeds");
            for (const glm::vec4& p : faces[f]) {
                RENDER_CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.w) &&
                             p.x >= 0.0f && p.y >= 0.0f && p.z >= 0.0f,
                    "M15-U5-08.2: all captured face texels are finite non-negative linear HDR");
            }
        }

        const uint32_t half = captureRes / 2u;
        // Center averages must match each directional face color and exclude transparent & self sphere!
        const glm::vec3 cPosX = AverageRegion(faces[0], captureRes, half - 2, half - 2, half + 2, half + 2);
        const glm::vec3 cNegX = AverageRegion(faces[1], captureRes, half - 2, half - 2, half + 2, half + 2);
        const glm::vec3 cPosY = AverageRegion(faces[2], captureRes, half - 2, half - 2, half + 2, half + 2);
        const glm::vec3 cNegY = AverageRegion(faces[3], captureRes, half - 2, half - 2, half + 2, half + 2);
        const glm::vec3 cPosZ = AverageRegion(faces[4], captureRes, half - 2, half - 2, half + 2, half + 2);
        const glm::vec3 cNegZ = AverageRegion(faces[5], captureRes, half - 2, half - 2, half + 2, half + 2);

        // +X has emissive intensity 3.0 -> linear HDR R > 2.0 (not tone-mapped/clamped to [0, 1]!), and low G/B (transparent blue & white self sphere excluded!)
        RENDER_CHECK(cPosX.r > 2.0f && cPosX.g < 0.25f && cPosX.b < 0.25f,
            "M15-U5-08.2/09.1/10.4/11.1: +X face captures linear HDR Red > 2.0 and excludes transparent/self-probe blockers");
        RENDER_CHECK(cNegX.g > 0.7f && cNegX.r < 0.2f && cNegX.b < 0.2f, "M15-U5-09.1: -X face captures Green");
        RENDER_CHECK(cPosY.b > 0.7f && cPosY.r < 0.2f && cPosY.g < 0.2f, "M15-U5-09.1: +Y face captures Blue");
        RENDER_CHECK(cNegY.r > 0.7f && cNegY.g > 0.7f && cNegY.b < 0.2f, "M15-U5-09.1: -Y face captures Yellow");
        RENDER_CHECK(cPosZ.g > 0.7f && cPosZ.b > 0.7f && cPosZ.r < 0.2f, "M15-U5-09.1: +Z face captures Cyan");
        RENDER_CHECK(cNegZ.r > 0.7f && cNegZ.b > 0.7f && cNegZ.g < 0.2f, "M15-U5-09.1: -Z face captures Magenta");

        // Off-axis quadrant orientation checks (proves zero X or Y mirror flip across X, Y, Z faces!)
        const glm::vec3 posX_TopRight = AverageRegion(faces[0], captureRes, half + 3, 3, captureRes - 3, half - 3);
        const glm::vec3 posX_TopLeft  = AverageRegion(faces[0], captureRes, 3, 3, half - 3, half - 3);
        const glm::vec3 posX_BotRight = AverageRegion(faces[0], captureRes, half + 3, half + 3, captureRes - 3, captureRes - 3);
        std::cout << "  [U5-B +X] TR=(" << posX_TopRight.r << "," << posX_TopRight.g << "," << posX_TopRight.b
                  << ") TL=(" << posX_TopLeft.r << "," << posX_TopLeft.g << "," << posX_TopLeft.b
                  << ") BR=(" << posX_BotRight.r << "," << posX_BotRight.g << "," << posX_BotRight.b << ")\n";
        RENDER_CHECK(posX_TopRight.g > posX_TopLeft.g + 1.0f && posX_TopRight.b > posX_BotRight.b + 1.0f,
            "M15-U5-09.2/12.1: +X off-axis marker (+5,+2.5,-2.5) lands cleanly in TOP-RIGHT quadrant after X orientation flip");

        const glm::vec3 posY_TopRight = AverageRegion(faces[2], captureRes, half + 3, 3, captureRes - 3, half - 3);
        const glm::vec3 posY_TopLeft  = AverageRegion(faces[2], captureRes, 3, 3, half - 3, half - 3);
        const glm::vec3 posY_BotRight = AverageRegion(faces[2], captureRes, half + 3, half + 3, captureRes - 3, captureRes - 3);
        RENDER_CHECK(posY_TopRight.r > posY_TopLeft.r + 1.0f && posY_TopRight.g > posY_BotRight.g + 1.0f,
            "M15-U5-09.2/12.1: +Y off-axis marker (+2.5,+5,-2.5) lands cleanly in TOP-RIGHT quadrant");

        const glm::vec3 posZ_BotRight = AverageRegion(faces[4], captureRes, half + 3, half + 3, captureRes - 3, captureRes - 3);
        const glm::vec3 posZ_BotLeft  = AverageRegion(faces[4], captureRes, 3, half + 3, half - 3, captureRes - 3);
        const glm::vec3 posZ_TopRight = AverageRegion(faces[4], captureRes, half + 3, 3, captureRes - 3, half - 3);
        RENDER_CHECK(posZ_BotRight.r > posZ_BotLeft.r + 1.0f && posZ_BotRight.r > posZ_TopRight.r + 1.0f,
            "M15-U5-09.2/12.1: +Z off-axis marker (+2.5,-2.5,+5) lands cleanly in BOTTOM-RIGHT quadrant");

        // Cutout check on -Z (-Z Right = -X, Up = +Y; (+1.8, -1.8, -4.0) is in BOTTOM-LEFT quadrant of -Z!)
        const glm::vec3 negZ_BotLeft = AverageRegion(faces[5], captureRes, 3, half + 3, half - 3, captureRes - 3);
        const glm::vec3 negZ_TopRight = AverageRegion(faces[5], captureRes, half + 3, 3, captureRes - 3, half - 3);
        RENDER_CHECK(negZ_BotLeft.g > negZ_TopRight.g + 0.4f,
            "M15-U5-11.1: Cutout orange mesh is captured in -Z BOTTOM-LEFT quadrant");

        RENDER_CHECK(WriteHDRCubeStripPPM(output / "M15_U5B_faces_strip.ppm", *gen->Cubemap, captureRes),
            "U5-B write cube face strip artifact");

        // ---------------------------------------------------------------------
        // 2. M15-U5-14: Mirror reflection fallback samples published dynamic cubemap
        // ---------------------------------------------------------------------
        RenderWorld mirrorWorld = world;
        std::erase_if(mirrorWorld.Meshes, [](const auto& mesh) { return mesh.EntityID < 10 || mesh.EntityID > 15; });
        // The mirror carries the probe entity identity so capture excludes it.
        // Real emissive walls stay in the world; no stale capture is substituted.
        RHIMaterialDesc mirrorDesc{};
        mirrorDesc.CullMode = CullMode::None;
        mirrorDesc.BaseColor = glm::vec4(0.95f, 0.95f, 0.95f, 1.0f);
        mirrorDesc.Metallic = 1.0f;
        mirrorDesc.Roughness = 0.05f;
        mirrorWorld.Meshes.push_back(MakeBoxInstance(probe.EntityID, sphereView, std::make_shared<RHIMaterial>(mirrorDesc), {0, 0, 0}, {1.0f, 1.0f, 1.0f}));
        mirrorWorld.ReflectionProbes = {probe};

        PerspectiveCamera mirrorCam(55.0f, 1.0f, 0.1f, 100.0f);
        mirrorCam.SetViewMatrix(glm::lookAtRH(glm::vec3(3.5f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0)), {3.5f, 0.0f, 0.0f});
        SceneRenderSettings ssrSettings;
        ssrSettings.EnableShadows = false;
        ssrSettings.EnableParticles = false;
        ssrSettings.EnableSkybox = false;
        ssrSettings.EnableToneMapping = true;
        ssrSettings.EnableSSR = true;
        ssrSettings.SSRStrength = 1.0f;
        ssrSettings.SSRLengthPixels = 16.0f;

        renderer.RenderWorldView(mirrorWorld, mirrorCam, &mainColor, &mainDepth, ssrSettings);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() &&
                     renderer.GetReflectionFrame().State == ReflectionState::DynamicReady &&
                     renderer.GetReflectionFrame().PrefilterReady,
            "M15-U5-14: dynamic probe publishes DynamicReady reflection state for mirror sphere");
        const auto reflectedWithProbe = Axle::RenderTests::ReadTexture(mainColor);
        const size_t mirrorCenter = (32u * 64u + 32u) * 4u;
        mirrorWorld.ReflectionProbes.clear();
        renderer.RenderWorldView(mirrorWorld, mirrorCam, &mainColor, &mainDepth, ssrSettings);
        const auto reflectedWithoutProbe = Axle::RenderTests::ReadTexture(mainColor);
        std::cout << "  [U5-B SSR Fallback] withProbe=("
                  << int(reflectedWithProbe[mirrorCenter + 0]) << ","
                  << int(reflectedWithProbe[mirrorCenter + 1]) << ","
                  << int(reflectedWithProbe[mirrorCenter + 2]) << ") withoutProbe=("
                  << int(reflectedWithoutProbe[mirrorCenter + 0]) << ","
                  << int(reflectedWithoutProbe[mirrorCenter + 1]) << ","
                  << int(reflectedWithoutProbe[mirrorCenter + 2]) << ")\n";
        RENDER_CHECK(reflectedWithProbe.size() == 64u * 64u * 4u &&
                     reflectedWithProbe[mirrorCenter + 0] > reflectedWithProbe[mirrorCenter + 1] + 20 &&
                     reflectedWithProbe[mirrorCenter + 0] > reflectedWithProbe[mirrorCenter + 2] + 20,
            "M15-U5-14: mirror sphere SSR probe fallback reflects dynamically captured +X HDR Red wall");
        RENDER_CHECK(renderer.WasLastRenderSuccessful() &&
                     reflectedWithProbe[mirrorCenter + 0] > reflectedWithoutProbe[mirrorCenter + 0] + 20,
            "M15-U5-14: mirror reflection delta comes directly from the captured dynamic probe cubemap");
    }

    // -------------------------------------------------------------------------
    // 3. M15-U5-11.2: Selected shadows inside dynamic capture
    // -------------------------------------------------------------------------
    {
        RenderWorld shadowWorld;
        shadowWorld.SceneRevision = 200;
        shadowWorld.Skybox.Enabled = false;
        RenderDirectionalLight sun{};
        sun.Direction = glm::normalize(glm::vec3(-1.0f, 0.5f, 0.0f));
        sun.Color = {1.0f, 1.0f, 1.0f};
        sun.Intensity = 2.0f;
        sun.AmbientIntensity = 0.0f;
        sun.CastsShadows = true;
        shadowWorld.DirectionalLight = sun;

        RHIMaterialDesc whiteWall{};
        whiteWall.CullMode = CullMode::None;
        whiteWall.BaseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        whiteWall.Metallic = 0.0f;
        whiteWall.Roughness = 0.9f;
        auto wallMat = std::make_shared<RHIMaterial>(whiteWall);

        // Receiver wall at +X = 5 (face at x = 4.75); blocker at x = 2.5, y in [0.45, 2.05]
        // sits strictly above the center camera rays (|y| <= 0.31) while casting full umbra across [y = -0.35..+0.35] at x = 4.75.
        shadowWorld.Meshes.push_back(MakeBoxInstance(101, cubeView, wallMat, {5.0f, 0.0f, 0.0f}, {0.5f, 6.0f, 6.0f}, true));
        shadowWorld.Meshes.push_back(MakeBoxInstance(102, cubeView, wallMat, {2.5f, 1.25f, 0.0f}, {0.8f, 1.6f, 4.0f}, true));

        SceneRenderSettings shadowCapSettings;
        shadowCapSettings.EnableSkybox = false;
        shadowCapSettings.EnableShadows = true;

        RenderReflectionProbe unshadowedProbe{};
        unshadowedProbe.EntityID = 501;
        unshadowedProbe.DynamicCapture = true;
        unshadowedProbe.CaptureResolution = 32;
        unshadowedProbe.CaptureShadows = false;
        const auto noShadowRes = renderer.CaptureDynamicReflectionProbe(shadowWorld, unshadowedProbe, shadowCapSettings);
        RENDER_CHECK(noShadowRes.Success, "M15-U5-11.2: unshadowed capture succeeds");
        auto noShadowGen = renderer.GetDynamicReflectionProbe(unshadowedProbe);
        RENDER_CHECK(noShadowGen && device.WaitForCompletion(noShadowGen->Completion), "U5-11.2 unshadowed wait");
        std::vector<glm::vec4> unshadowedFace0;
        RENDER_CHECK(ReadCubeFaceRGBA16(*noShadowGen->Cubemap, 0, unshadowedFace0), "U5-11.2 unshadowed read");

        RenderReflectionProbe shadowedProbe = unshadowedProbe;
        shadowedProbe.EntityID = 502;
        shadowedProbe.CaptureShadows = true;
        const auto shadowRes = renderer.CaptureDynamicReflectionProbe(shadowWorld, shadowedProbe, shadowCapSettings);
        RENDER_CHECK(shadowRes.Success, "M15-U5-11.2: shadowed capture succeeds");
        auto shadowGen = renderer.GetDynamicReflectionProbe(shadowedProbe);
        RENDER_CHECK(shadowGen && device.WaitForCompletion(shadowGen->Completion), "U5-11.2 shadowed wait");
        std::vector<glm::vec4> shadowedFace0;
        RENDER_CHECK(ReadCubeFaceRGBA16(*shadowGen->Cubemap, 0, shadowedFace0), "U5-11.2 shadowed read");

        const glm::vec3 unlitCenter = AverageRegion(unshadowedFace0, 32, 14, 14, 18, 18);
        const glm::vec3 litCenter = AverageRegion(shadowedFace0, 32, 14, 14, 18, 18);
        std::cout << "  [U5-B Shadows] unlit=(" << unlitCenter.r << "," << unlitCenter.g << "," << unlitCenter.b
                  << ") shadowed=(" << litCenter.r << "," << litCenter.g << "," << litCenter.b << ")\n";
        RENDER_CHECK(unlitCenter.r > 0.3f && litCenter.r < unlitCenter.r * 0.4f,
            "M15-U5-11.2: CaptureShadows=true casts directional blocker shadow inside captured +X face");
    }

    // -------------------------------------------------------------------------
    // 4. M15-U5-13.2 & M15-U5-15: Transactional publication, last-good retention on mid-capture fault, and recovery
    // -------------------------------------------------------------------------
    {
        RenderReflectionProbe txnProbe{};
        txnProbe.EntityID = 900;
        txnProbe.DynamicCapture = true;
        txnProbe.CaptureResolution = 32;
        txnProbe.CaptureShadows = false;

        RenderWorld greenWorld;
        greenWorld.SceneRevision = 301;
        greenWorld.Skybox.Enabled = false;
        greenWorld.Meshes.push_back(MakeBoxInstance(201, cubeView, MakeEmissiveMaterial({0.0f, 1.0f, 0.0f}, 2.0f), {4.0f, 0.0f, 0.0f}, {0.5f, 8.0f, 8.0f}));

        bool nestedRejected = true, allocationAttempted = false, faceAttempted = false;
        DynamicReflectionCaptureResult greenRes;
        {
            ScopedRHIFault reentry([&](std::string_view op) -> std::optional<nri::Result> {
                if ((op == "CreateTexture" && !allocationAttempted) || (op == "BeginCommandBuffer" && !faceAttempted)) {
                    if (op == "CreateTexture") allocationAttempted = true; else faceAttempted = true;
                    const auto nested = renderer.CaptureDynamicReflectionProbe(greenWorld, txnProbe);
                    nestedRejected &= !nested.Success && nested.Diagnostic.find("RECURSION") != std::string::npos;
                }
                return {};
            });
            greenRes = renderer.CaptureDynamicReflectionProbe(greenWorld, txnProbe);
        }
        RENDER_CHECK(nestedRejected && allocationAttempted && faceAttempted, "nested capture during allocation and recording preserves the outer session");
        RENDER_CHECK(greenRes.Success && greenRes.CapturedFaces == 6, "U5-15 initial valid green generation");
        auto greenGen = renderer.GetDynamicReflectionProbe(txnProbe);
        RENDER_CHECK(greenGen && device.WaitForCompletion(greenGen->Completion), "U5-15 green wait");
        const uint64_t greenGenId = greenGen->GenerationID;
        const RHIResourceHandle greenHandle = greenGen->Cubemap->GetHandle();

        // Inject fault mid-capture during the 3rd face command acquisition/submit so Faces 0..1 run and Face 2 fails!
        RenderWorld blueWorld;
        blueWorld.SceneRevision = 302;
        blueWorld.Skybox.Enabled = false;
        blueWorld.Meshes.push_back(MakeBoxInstance(202, cubeView, MakeEmissiveMaterial({0.0f, 0.0f, 1.0f}, 2.0f), {4.0f, 0.0f, 0.0f}, {0.5f, 8.0f, 8.0f}));

        uint32_t beginCmdCount = 0;
        bool injectedMidCapture = false;
        {
            ScopedRHIFault fault([&](std::string_view op) -> std::optional<nri::Result> {
                if (op == "BeginCommandBuffer") {
                    ++beginCmdCount;
                    if (beginCmdCount == 3 && !injectedMidCapture) {
                        injectedMidCapture = true;
                        return nri::Result::OUT_OF_MEMORY;
                    }
                }
                return {};
            });
            const auto failedRes = renderer.CaptureDynamicReflectionProbe(blueWorld, txnProbe);
            RENDER_CHECK(injectedMidCapture &&
                         !failedRes.Success &&
                         failedRes.CapturedFaces == 2 &&
                         failedRes.PreservedPreviousGeneration &&
                         failedRes.PublishedGenerationID == greenGenId &&
                         !failedRes.Diagnostic.empty(),
                "M15-U5-13.2/15: mid-capture face failure aborts transaction without publishing partial cubemap");
        }

        // Verify previous green generation survived completely uncorrupted!
        auto retainedGen = renderer.GetDynamicReflectionProbe(txnProbe);
        RENDER_CHECK(retainedGen && retainedGen->GenerationID == greenGenId &&
                     retainedGen->Cubemap && retainedGen->Cubemap->IsValid() &&
                     retainedGen->Cubemap->GetHandle() == greenHandle,
            "M15-U5-15: last-good cubemap handle & generation remain active after failed recapture");
        std::vector<glm::vec4> preservedFace0;
        RENDER_CHECK(ReadCubeFaceRGBA16(*retainedGen->Cubemap, 0, preservedFace0), "U5-15 preserved face read");
        const glm::vec3 preservedCenter = AverageRegion(preservedFace0, 32, 14, 14, 18, 18);
        RENDER_CHECK(preservedCenter.g > 1.0f && preservedCenter.b < 0.2f,
            "M15-U5-15: preserved last-good cubemap texels are uncorrupted green");

        // Verify invalid settings also retain previous generation safely
        txnProbe.CaptureResolution = 127;
        const auto badRes = renderer.CaptureDynamicReflectionProbe(blueWorld, txnProbe);
        RENDER_CHECK(!badRes.Success && badRes.PreservedPreviousGeneration &&
                     badRes.Diagnostic.find("DYNAMIC_PROBE_SETTINGS") != std::string::npos,
            "M15-U5-15: invalid capture resolution fails explicitly while retaining last-good generation");

        // Recover cleanly with valid blue capture
        txnProbe.CaptureResolution = 32;
        const auto recoveredRes = renderer.CaptureDynamicReflectionProbe(blueWorld, txnProbe);
        RENDER_CHECK(recoveredRes.Success && recoveredRes.CapturedFaces == 6 &&
                     recoveredRes.PublishedGenerationID > greenGenId,
            "M15-U5-15: capture recovers cleanly once fault clears");
        auto recoveredGen = renderer.GetDynamicReflectionProbe(txnProbe);
        RENDER_CHECK(recoveredGen && device.WaitForCompletion(recoveredGen->Completion) &&
                     recoveredGen->Cubemap->GetHandle() != greenHandle,
            "U5-15 recovered cubemap handle updated");
        std::vector<glm::vec4> recoveredFace0;
        RENDER_CHECK(ReadCubeFaceRGBA16(*recoveredGen->Cubemap, 0, recoveredFace0), "U5-15 recovered face read");
        const glm::vec3 recoveredCenter = AverageRegion(recoveredFace0, 32, 14, 14, 18, 18);
        RENDER_CHECK(recoveredCenter.b > 1.0f && recoveredCenter.g < 0.2f,
            "M15-U5-15: recovered generation contains new blue scene radiance");

        // Even a superseded cubemap retained by a caller must not feed back
        // into later captures through the manual IBL input path.
        const auto savedIrr = renderer.GetIrradianceMap(), savedPref = renderer.GetPrefilterMap(), savedLut = renderer.GetBRDFLUT();
        RenderWorld feedbackWorld;
        feedbackWorld.Skybox.Authored = feedbackWorld.Skybox.Enabled = feedbackWorld.Skybox.EnvironmentLighting = true;
        feedbackWorld.Skybox.Type = 1;
        feedbackWorld.DirectionalLight = RenderDirectionalLight{};
        feedbackWorld.DirectionalLight->Intensity = 0; feedbackWorld.DirectionalLight->AmbientIntensity = 1;
        auto receiver = std::make_shared<RHIMaterial>();
        feedbackWorld.Meshes.push_back(MakeBoxInstance(1410, cubeView, receiver, {-4,0,0}, {.5f,8,8}));
        RenderReflectionProbe feedbackProbe;
        feedbackProbe.EntityID = 1401; feedbackProbe.CaptureResolution = 32; feedbackProbe.CaptureShadows = false;
        const std::weak_ptr<RHITexture> originalWrapper = greenGen->Cubemap;
        auto retainedAlias = std::make_shared<RHITexture>(greenGen->Cubemap->RetainGeneration());
        greenGen.reset(); retainedGen.reset();
        RENDER_CHECK(originalWrapper.expired(), "feedback alias outlives the original publication wrapper");
        renderer.SetIBLMaps(retainedAlias, retainedAlias, RHIMaterial::GetDefaultBRDFLUT());
        RENDER_CHECK(renderer.CaptureDynamicReflectionProbe(feedbackWorld, feedbackProbe).Success, "capture suppresses superseded manual IBL feedback");
        const auto blocked = renderer.GetDynamicReflectionProbe(feedbackProbe);
        std::vector<glm::vec4> blockedPixels, neutralPixels;
        RENDER_CHECK(ReadCubeFaceRGBA16(*blocked->Cubemap, 1, blockedPixels), "read feedback suppression capture");
        renderer.SetIBLMaps(RHIMaterial::GetDefaultIrradianceCubemap(), RHIMaterial::GetDefaultPrefilterCubemap(), RHIMaterial::GetDefaultBRDFLUT());
        feedbackProbe.EntityID = 1402;
        RENDER_CHECK(renderer.CaptureDynamicReflectionProbe(feedbackWorld, feedbackProbe).Success, "neutral manual IBL reference capture");
        RENDER_CHECK(ReadCubeFaceRGBA16(*renderer.GetDynamicReflectionProbe(feedbackProbe)->Cubemap, 1, neutralPixels), "read neutral feedback oracle");
        const auto reference = AverageRegion(neutralPixels,32,14,14,18,18);
        RENDER_CHECK(reference.r > .1f && glm::distance(reference, AverageRegion(blockedPixels,32,14,14,18,18)) < .003f,
            "published-cubemap IBL feedback matches nonzero neutral reference");
        renderer.SetIBLMaps(savedIrr, savedPref, savedLut);
    }

    // Automatic capture invalidation must observe settings/material edits even
    // when the scene's transform revision has not changed.
    {
        RenderWorld world;
        world.SceneRevision = 450;
        auto material = MakeEmissiveMaterial({1,0,0}, 2);
        world.Meshes.push_back(MakeBoxInstance(1301, cubeView, material, {4,0,0}, {.5f,8,8}));
        RenderReflectionProbe probe;
        probe.EntityID = 1300; probe.DynamicCapture = true; probe.CaptureResolution = 32;
        probe.BoxExtents = glm::vec3(100); probe.CaptureShadows = false;
        world.ReflectionProbes.push_back(probe);
        RHITexture color, depth;
        RENDER_CHECK(color.Init(RHITextureDesc::RenderTarget2D(32,32)) && depth.Init(RHITextureDesc::DepthStencil2D(32,32)), "automatic capture targets");
        PerspectiveCamera camera(55,1,.1f,100);
        camera.SetViewMatrix(glm::lookAtRH(glm::vec3(0,0,3), glm::vec3(0), glm::vec3(0,1,0)), {0,0,3});
        SceneRenderSettings settings; settings.EnableParticles = settings.EnableShadows = settings.EnableSkybox = false;
        auto render = [&] {
            renderer.RenderWorldView(world, camera, &color, &depth, settings);
            return renderer.WasLastRenderSuccessful();
        };
        RENDER_CHECK(render(), "initial automatic capture");
        auto generation = renderer.GetDynamicReflectionProbe(probe);
        RENDER_CHECK(generation && render() && renderer.GetDynamicReflectionProbe(probe)->GenerationID == generation->GenerationID, "unchanged request reuses capture");
        material->GetDesc().EmissiveColor = {0,1,0};
        RENDER_CHECK(render() && renderer.GetDynamicReflectionProbe(probe)->GenerationID > generation->GenerationID, "material edit recaptures at unchanged transform revision");
        generation = renderer.GetDynamicReflectionProbe(probe);
        std::vector<glm::vec4> pixels;
        RENDER_CHECK(ReadCubeFaceRGBA16(*generation->Cubemap,0,pixels) && AverageRegion(pixels,32,14,14,18,18).g > 1, "automatic replacement contains new material radiance");
        world.ReflectionProbes[0].CaptureFarPlane = 2;
        RENDER_CHECK(render() && renderer.GetDynamicReflectionProbe(probe)->GenerationID > generation->GenerationID, "clip edit triggers automatic capture");
        generation = renderer.GetDynamicReflectionProbe(probe);
        RENDER_CHECK(ReadCubeFaceRGBA16(*generation->Cubemap,0,pixels) && AverageRegion(pixels,32,14,14,18,18).g < .1f, "short far plane removes distant wall");
        world.ReflectionProbes[0].CaptureResolution = 127;
        RENDER_CHECK(render() && renderer.GetDynamicReflectionProbe(probe)->GenerationID == generation->GenerationID, "invalid automatic request retains last-good");
        world.ReflectionProbes[0].CaptureResolution = 32;
        world.ReflectionProbes[0].CaptureFarPlane = 50;
        RENDER_CHECK(render() && renderer.GetDynamicReflectionProbe(probe)->GenerationID > generation->GenerationID, "corrected settings retry at unchanged revision");
        generation = renderer.GetDynamicReflectionProbe(probe);
        world.ReflectionProbes[0].CaptureNearPlane = .12f;
        world.ReflectionProbes[0].CaptureShadows = true;
        settings.EnableShadows = true;
        RENDER_CHECK(render() && renderer.GetDynamicReflectionProbe(probe)->GenerationID > generation->GenerationID, "near plane and shadow policy invalidate the capture request");
        generation = renderer.GetDynamicReflectionProbe(probe);
        material->GetDesc().EmissiveColor = {0,0,1};
        bool transientFault = false;
        {
            ScopedRHIFault fault([&](std::string_view op) -> std::optional<nri::Result> {
                if (op == "BeginCommandBuffer" && !transientFault) { transientFault = true; return nri::Result::OUT_OF_MEMORY; }
                return {};
            });
            RENDER_CHECK(render() && transientFault && renderer.GetDynamicReflectionProbe(probe)->GenerationID == generation->GenerationID, "automatic transient failure keeps last-good capture");
        }
        RENDER_CHECK(render() && renderer.GetDynamicReflectionProbe(probe)->GenerationID > generation->GenerationID, "automatic capture retries transient failure without a revision change");
    }
    renderer.ClearDynamicReflectionProbes();
    RENDER_CHECK(device.WaitIdle(), "U5-B wait idle at cleanup");
    RENDER_CHECK(Axle::RenderTests::GetRHIValidationErrorCount() == 0, "U5-B zero RHI validation errors");
    std::cout << "[M15ReflectionCaptureGPU] All U5-B native D3D12/Vulkan checks passed!\n";
    return true;
}
