#include "OcclusionTestSupport.hpp"
#include "RHIReadback.hpp"
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RenderParticleData.hpp>
#include <glm/gtc/constants.hpp>
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
    bool ReadCubeFaceRGBA16(RHITexture& cubemap, uint32_t face, std::vector<glm::vec4>& pixels, uint32_t mip = 0) {
        if (!cubemap.IsValid() || !cubemap.GetDesc().IsCubeMap || face >= 6 ||
            mip >= cubemap.GetMipCount() || cubemap.GetFormat() != RHITextureFormat::RGBA16_FLOAT) {
            return false;
        }
        auto& device = RHIDevice::Get();
        const auto alignment = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
        const auto align = [](uint32_t value, uint32_t step) { return (value + step - 1u) / step * step; };
        const uint32_t w = std::max(1u, cubemap.GetWidth() >> mip);
        const uint32_t h = std::max(1u, cubemap.GetHeight() >> mip);
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
        region.mipOffset = static_cast<nri::Dim_t>(mip);
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

    bool ReadTexture2DRGBA16(RHITexture& texture, std::vector<glm::vec4>& pixels) {
        if (!texture.IsValid() || texture.GetFormat() != RHITextureFormat::RGBA16_FLOAT) {
            return false;
        }
        auto& device = RHIDevice::Get();
        const auto alignment = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
        const auto align = [](uint32_t value, uint32_t step) { return (value + step - 1u) / step * step; };
        const uint32_t w = texture.GetWidth();
        const uint32_t h = texture.GetHeight();
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
        region.planes = nri::PlaneBits::COLOR;

        commands.ReadbackTextureToBuffer(*readback.GetNative(), layout, *texture.GetNative(), region);
        commands.TransitionTexture(texture.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResource);
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

    bool WriteSolidHDRFile(const std::filesystem::path& path, const glm::vec3& color) {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 16 +X 32\n";
        unsigned char pixel[4]{0, 0, 0, 0};
        const float maximum = std::max({color.x, color.y, color.z});
        if (maximum > 1e-6f) {
            int exponent = 0;
            const float scale = std::frexp(maximum, &exponent) * 256.0f / maximum;
            pixel[0] = static_cast<unsigned char>(std::clamp(color.x * scale, 0.0f, 255.0f));
            pixel[1] = static_cast<unsigned char>(std::clamp(color.y * scale, 0.0f, 255.0f));
            pixel[2] = static_cast<unsigned char>(std::clamp(color.z * scale, 0.0f, 255.0f));
            pixel[3] = static_cast<unsigned char>(exponent + 128);
        }
        for (int i = 0; i < 16 * 32; ++i) {
            file.write(reinterpret_cast<const char*>(pixel), 4);
        }
        return bool(file);
    }

    bool SaveCubemapAsset(
        const std::filesystem::path& output,
        const char* hdrName,
        const glm::vec3& color) {
        return WriteSolidHDRFile(output / hdrName, color);
    }

    std::shared_ptr<RHIMaterial> MakeChromeMaterial(float roughness = 0.04f, float metallic = 1.0f) {
        RHIMaterialDesc desc{};
        desc.CullMode = CullMode::None;
        desc.BaseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        desc.Metallic = metallic;
        desc.Roughness = roughness;
        return std::make_shared<RHIMaterial>(desc);
    }

    bool ProjectWorldToPixel(
        const PerspectiveCamera& camera,
        uint32_t width,
        uint32_t height,
        const glm::vec3& worldPos,
        uint32_t& outX,
        uint32_t& outY) {
        const glm::vec4 clip = camera.GetViewProjectionMatrix() * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 1e-4f) return false;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        const float u = ndc.x * 0.5f + 0.5f;
        const float v = 0.5f - ndc.y * 0.5f;
        if (u <= 0.02f || u >= 0.98f || v <= 0.02f || v >= 0.98f) return false;
        outX = static_cast<uint32_t>(std::clamp(u * static_cast<float>(width), 0.0f, static_cast<float>(width - 1u)));
        outY = static_cast<uint32_t>(std::clamp(v * static_cast<float>(height), 0.0f, static_cast<float>(height - 1u)));
        return true;
    }

    glm::vec3 SampleWindowAroundPixel(
        const std::vector<glm::vec4>& pixels,
        uint32_t width,
        uint32_t height,
        uint32_t cx,
        uint32_t cy,
        uint32_t halfExtent = 2u) {
        const uint32_t x0 = cx > halfExtent ? cx - halfExtent : 0u;
        const uint32_t y0 = cy > halfExtent ? cy - halfExtent : 0u;
        const uint32_t x1 = std::min(width, cx + halfExtent + 1u);
        const uint32_t y1 = std::min(height, cy + halfExtent + 1u);
        return AverageRegion(pixels, width, x0, y0, x1, y1);
    }

    bool AllFiniteNonNegative(const std::vector<glm::vec4>& pixels) {
        for (const auto& p : pixels) {
            if (!std::isfinite(p.r) || !std::isfinite(p.g) || !std::isfinite(p.b) || !std::isfinite(p.a) ||
                p.r < -1e-4f || p.g < -1e-4f || p.b < -1e-4f) {
                return false;
            }
        }
        return true;
    }

    bool RunM15MultiProbeBlendTests(RHISceneRenderer& renderer, const std::filesystem::path& output) {
        std::cout << "[M15ReflectionCaptureGPU] Running U5-E multi-probe blend, parallax, energy policy & P5 exit checks...\n";
        auto& device = RHIDevice::Get();
        renderer.ClearDynamicReflectionProbes();
        RENDER_CHECK(device.WaitIdle(), "U5-E idle before suite");

        RENDER_CHECK(
            SaveCubemapAsset(output, "u5e_red.hdr",   {2.0f, 0.04f, 0.04f}) &&
            SaveCubemapAsset(output, "u5e_green.hdr", {0.04f, 2.0f, 0.04f}) &&
            SaveCubemapAsset(output, "u5e_blue.hdr",  {0.04f, 0.04f, 2.0f}) &&
            SaveCubemapAsset(output, "u5e_white.hdr", {1.0f, 1.0f, 1.0f}),
            "U5-E authored HDR cubemap assets created");

        auto cubeMesh = RHIMesh::CreateCube(1.0f);
        auto sphereMesh = RHIMesh::CreateSphere(1.0f, 24, 32);
        RENDER_CHECK(cubeMesh && cubeMesh->IsValid() && sphereMesh && sphereMesh->IsValid(), "U5-E fixture meshes");
        auto cubeView = cubeMesh->CaptureView();
        auto sphereView = sphereMesh->CaptureView();

        constexpr uint32_t renderW = 256u;
        constexpr uint32_t renderH = 128u;
        RHITexture hdrColor, depthTex;
        RENDER_CHECK(hdrColor.Init(RHITextureDesc::RenderTarget2D(renderW, renderH, RHITextureFormat::RGBA16_FLOAT)) &&
                     depthTex.Init(RHITextureDesc::DepthStencil2D(renderW, renderH)),
            "U5-E linear HDR render target init");

        auto renderHDR = [&](const RenderWorld& world, const PerspectiveCamera& cam, const SceneRenderSettings& inSettings,
                             std::vector<glm::vec4>& outPixels) -> bool {
            SceneRenderSettings s = inSettings;
            s.EnableToneMapping = false;
            s.EnableShadows = false;
            s.EnableParticles = false;
            s.EnableStudioLightLobes = false;
            renderer.RenderWorldView(world, cam, &hdrColor, &depthTex, s);
            if (!renderer.WasLastRenderSuccessful() || renderer.GetDiagnostics().HasErrors() || !device.WaitIdle()) {
                return false;
            }
            if (!ReadTexture2DRGBA16(hdrColor, outPixels)) return false;
            return outPixels.size() == size_t(renderW) * renderH && AllFiniteNonNegative(outPixels);
        };

        auto chromeBarMat = MakeChromeMaterial(0.04f, 1.0f);

        // -----------------------------------------------------------------
        // 1. M15-U5-27.1 / 28 / 31.1: Overlapping Box probes + 4-probe cap
        // -----------------------------------------------------------------
        {
            PerspectiveCamera cam(45.0f, float(renderW) / float(renderH), 0.1f, 100.0f);
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 12.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              glm::vec3(0.0f, 0.0f, 12.0f));

            RenderWorld world;
            world.SceneRevision = 9101;
            world.Skybox.Authored = true;
            world.Skybox.Enabled = false;
            world.Meshes.push_back(MakeBoxInstance(101, cubeView, chromeBarMat, {0.0f, 0.0f, 0.0f}, {16.0f, 2.5f, 0.5f}, false));

            RenderReflectionProbe leftBox{};
            leftBox.EntityID = 5101;
            leftBox.Shape = 0;
            leftBox.Position = {-6.0f, 0.0f, 0.0f};
            leftBox.BoxExtents = {4.0f, 12.0f, 12.0f}; // halfExtents.x = 2 -> [-8..-4], BlendDistance=8 -> reaches +4
            leftBox.BlendDistance = 8.0f;
            leftBox.Intensity = 1.0f;
            leftBox.ParallaxCorrected = false;
            leftBox.CubemapPath = "u5e_red.hdr";

            RenderReflectionProbe rightBox{};
            rightBox.EntityID = 5102;
            rightBox.Shape = 0;
            rightBox.Position = {6.0f, 0.0f, 0.0f};
            rightBox.BoxExtents = {4.0f, 12.0f, 12.0f}; // core [+4..+8], BlendDistance=8 -> reaches -4
            rightBox.BlendDistance = 8.0f;
            rightBox.Intensity = 1.0f;
            rightBox.ParallaxCorrected = false;
            rightBox.CubemapPath = "u5e_green.hdr";

            world.ReflectionProbes.push_back(leftBox);
            world.ReflectionProbes.push_back(rightBox);

            SceneRenderSettings s{};
            s.EnableSkybox = false;
            s.EnableSSR = false;
            std::vector<glm::vec4> pixels;
            RENDER_CHECK(renderHDR(world, cam, s, pixels), "M15-U5-27.1/28 box overlap render");
            RENDER_CHECK(renderer.GetLastReflectionFrame().ActiveProbeCount == 2u,
                "M15-U5-28: both overlapping box probes remain active simultaneously");

            const float xs[7]{-6.0f, -3.5f, -1.75f, 0.0f, 1.75f, 3.5f, 6.0f};
            float greenFrac[7]{};
            glm::vec3 samples[7]{};
            for (int i = 0; i < 7; ++i) {
                uint32_t px = 0, py = 0;
                RENDER_CHECK(ProjectWorldToPixel(cam, renderW, renderH, {xs[i], 0.0f, 0.25f}, px, py), "U5-E box sample projection");
                samples[i] = SampleWindowAroundPixel(pixels, renderW, renderH, px, py);
                greenFrac[i] = samples[i].g / std::max(samples[i].r + samples[i].g, 1e-4f);
            }
            std::cout << "  [U5-E BoxOverlap] left=(" << samples[0].r << "," << samples[0].g
                      << ") mid=(" << samples[3].r << "," << samples[3].g
                      << ") right=(" << samples[6].r << "," << samples[6].g << ")\n";
            RENDER_CHECK(samples[0].r > 0.7f && samples[0].r > samples[0].g * 5.0f,
                "M15-U5-27.1: left box interior samples Red probe cleanly");
            RENDER_CHECK(samples[6].g > 0.7f && samples[6].g > samples[6].r * 5.0f,
                "M15-U5-27.1: right box interior samples Green probe cleanly");
            RENDER_CHECK(std::abs(samples[3].r - samples[3].g) < 0.25f * std::max(samples[3].r, samples[3].g) &&
                         samples[3].r > 0.35f && samples[3].g > 0.35f,
                "M15-U5-28: midpoint smoothly blends 50/50 Red + Green without darkening seam");
            for (int i = 1; i < 7; ++i) {
                RENDER_CHECK(greenFrac[i] + 0.01f >= greenFrac[i - 1],
                    "M15-U5-28/31.1: overlap transition is monotonically smooth");
                RENDER_CHECK((greenFrac[i] - greenFrac[i - 1]) < 0.45f,
                    "M15-U5-28: no hard binary step seam across probe overlap");
            }

            // Also verify 5 probes cap to MaxActiveReflectionProbes (4) on GPU
            RenderWorld capWorld = world;
            for (uint32_t extra = 0; extra < 3; ++extra) {
                RenderReflectionProbe p = leftBox;
                p.EntityID = 5200u + extra;
                p.Position = {0.0f, 0.0f, 0.0f};
                p.BoxExtents = {24.0f, 24.0f, 24.0f};
                capWorld.ReflectionProbes.push_back(p);
            }
            std::vector<glm::vec4> capPixels;
            RENDER_CHECK(renderHDR(capWorld, cam, s, capPixels), "U5-E 5-probe cap render");
            RENDER_CHECK(renderer.GetLastReflectionFrame().ActiveProbeCount == MaxActiveReflectionProbes,
                "M15-U5-28: active GPU probe slots cap cleanly at 4");
        }

        // -----------------------------------------------------------------
        // 2. M15-U5-27.2: Overlapping Sphere probes (radial smoothstep)
        // -----------------------------------------------------------------
        {
            PerspectiveCamera cam(45.0f, float(renderW) / float(renderH), 0.1f, 100.0f);
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 12.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              glm::vec3(0.0f, 0.0f, 12.0f));

            RenderWorld world;
            world.SceneRevision = 9102;
            world.Skybox.Authored = true;
            world.Skybox.Enabled = false;
            world.Meshes.push_back(MakeBoxInstance(102, cubeView, chromeBarMat, {0.0f, 0.0f, 0.0f}, {16.0f, 2.5f, 0.5f}, false));

            RenderReflectionProbe leftSphere{};
            leftSphere.EntityID = 5111;
            leftSphere.Shape = 1;
            leftSphere.Position = {-6.0f, 0.0f, 0.0f};
            leftSphere.Radius = 2.0f;
            leftSphere.BlendDistance = 8.0f;
            leftSphere.Intensity = 1.0f;
            leftSphere.ParallaxCorrected = false;
            leftSphere.CubemapPath = "u5e_red.hdr";

            RenderReflectionProbe rightSphere{};
            rightSphere.EntityID = 5112;
            rightSphere.Shape = 1;
            rightSphere.Position = {6.0f, 0.0f, 0.0f};
            rightSphere.Radius = 2.0f;
            rightSphere.BlendDistance = 8.0f;
            rightSphere.Intensity = 1.0f;
            rightSphere.ParallaxCorrected = false;
            rightSphere.CubemapPath = "u5e_green.hdr";

            world.ReflectionProbes.push_back(leftSphere);
            world.ReflectionProbes.push_back(rightSphere);

            SceneRenderSettings s{};
            s.EnableSkybox = false;
            s.EnableSSR = false;
            std::vector<glm::vec4> pixels;
            RENDER_CHECK(renderHDR(world, cam, s, pixels), "M15-U5-27.2 sphere overlap render");
            RENDER_CHECK(renderer.GetLastReflectionFrame().ActiveProbeCount == 2u,
                "M15-U5-27.2: both overlapping sphere probes active");

            const float xs[7]{-6.0f, -3.5f, -1.75f, 0.0f, 1.75f, 3.5f, 6.0f};
            float greenFrac[7]{};
            glm::vec3 samples[7]{};
            for (int i = 0; i < 7; ++i) {
                uint32_t px = 0, py = 0;
                RENDER_CHECK(ProjectWorldToPixel(cam, renderW, renderH, {xs[i], 0.0f, 0.25f}, px, py), "U5-E sphere sample projection");
                samples[i] = SampleWindowAroundPixel(pixels, renderW, renderH, px, py);
                greenFrac[i] = samples[i].g / std::max(samples[i].r + samples[i].g, 1e-4f);
            }
            std::cout << "  [U5-E SphereOverlap] left=(" << samples[0].r << "," << samples[0].g
                      << ") mid=(" << samples[3].r << "," << samples[3].g
                      << ") right=(" << samples[6].r << "," << samples[6].g << ")\n";
            RENDER_CHECK(samples[0].r > 0.7f && samples[0].r > samples[0].g * 5.0f &&
                         samples[6].g > 0.7f && samples[6].g > samples[6].r * 5.0f &&
                         std::abs(samples[3].r - samples[3].g) < 0.25f * std::max(samples[3].r, samples[3].g),
                "M15-U5-27.2: sphere radial falloff transitions Red -> balanced -> Green");
            for (int i = 1; i < 7; ++i) {
                RENDER_CHECK(greenFrac[i] + 0.01f >= greenFrac[i - 1] && (greenFrac[i] - greenFrac[i - 1]) < 0.45f,
                    "M15-U5-27.2: sphere overlap gradient is monotonic and free of hard seams");
            }
        }

        // -----------------------------------------------------------------
        // 3. M15-U5-31.2 / 30.2: Probe boundary movement -> smooth Sky fallback
        // -----------------------------------------------------------------
        {
            PerspectiveCamera cam(45.0f, float(renderW) / float(renderH), 0.1f, 100.0f);
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 12.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              glm::vec3(0.0f, 0.0f, 12.0f));

            RenderWorld world;
            world.SceneRevision = 9103;
            world.Skybox.Authored = world.Skybox.Enabled = world.Skybox.EnvironmentLighting = true;
            world.Skybox.Type = 1;
            world.Skybox.HDRPath = "u5e_blue.hdr";
            world.Meshes.push_back(MakeBoxInstance(103, cubeView, chromeBarMat, {0.0f, 0.0f, 0.0f}, {16.0f, 2.5f, 0.5f}, false));

            RenderReflectionProbe edgeProbe{};
            edgeProbe.EntityID = 5121;
            edgeProbe.Shape = 0;
            edgeProbe.Position = {-5.0f, 0.0f, 0.0f};
            edgeProbe.BoxExtents = {4.0f, 12.0f, 12.0f}; // core [-7..-3], outer fade to +3
            edgeProbe.BlendDistance = 6.0f;
            edgeProbe.Intensity = 1.0f;
            edgeProbe.ParallaxCorrected = false;
            edgeProbe.CubemapPath = "u5e_red.hdr";
            world.ReflectionProbes.push_back(edgeProbe);

            SceneRenderSettings s{};
            s.EnableSkybox = true;
            s.EnableSSR = false;
            std::vector<glm::vec4> pixels;
            RENDER_CHECK(renderHDR(world, cam, s, pixels), "M15-U5-31.2/30.2 boundary sky fallback render");

            const float xs[5]{-5.0f, -1.5f, 0.0f, 1.5f, 5.0f};
            float blueFrac[5]{};
            glm::vec3 samples[5]{};
            for (int i = 0; i < 5; ++i) {
                uint32_t px = 0, py = 0;
                RENDER_CHECK(ProjectWorldToPixel(cam, renderW, renderH, {xs[i], 0.0f, 0.25f}, px, py), "U5-E sky boundary proj");
                samples[i] = SampleWindowAroundPixel(pixels, renderW, renderH, px, py);
                blueFrac[i] = samples[i].b / std::max(samples[i].r + samples[i].b, 1e-4f);
            }
            std::cout << "  [U5-E BoundarySky] inside=(" << samples[0].r << "," << samples[0].b
                      << ") edge=(" << samples[2].r << "," << samples[2].b
                      << ") outside=(" << samples[4].r << "," << samples[4].b << ")\n";
            RENDER_CHECK(samples[0].r > samples[0].b * 4.0f && samples[0].r > 0.7f,
                "M15-U5-31.2: inside probe boundary reflects local Red probe");
            RENDER_CHECK(samples[2].r > 0.35f && samples[2].b > 0.35f,
                "M15-U5-30.2: probe boundary smoothly mixes into sky without black energy dip");
            RENDER_CHECK(samples[4].b > samples[4].r * 4.0f && samples[4].b > 0.7f,
                "M15-U5-30.2 / 31.2: outside probe boundary falls back cleanly to Blue sky IBL");
            for (int i = 1; i < 5; ++i) {
                RENDER_CHECK(blueFrac[i] + 0.01f >= blueFrac[i - 1],
                    "M15-U5-31.2: boundary fade toward sky is strictly monotonic");
            }
        }

        // -----------------------------------------------------------------
        // 4. M15-U5-29: Per-probe box parallax correction before blending
        // -----------------------------------------------------------------
        {
            renderer.ClearDynamicReflectionProbes();
            RenderWorld pWorld;
            pWorld.SceneRevision = 9104;
            pWorld.Skybox.Authored = true;
            pWorld.Skybox.Enabled = false;

            // Off-center reflective plate at (+3.5, 0, 0) viewed straight along -Z -> raw R = (0, 0, +1)
            pWorld.Meshes.push_back(MakeBoxInstance(201, cubeView, chromeBarMat, {3.5f, 0.0f, 0.0f}, {2.0f, 2.0f, 0.2f}, false));
            // Behind main camera (+Z = +8): Green panel aligned with probe center (-2, 0, +8),
            // Red panel aligned with parallax-corrected hit (+3.5, 0, +8)
            pWorld.Meshes.push_back(MakeBoxInstance(202, cubeView, MakeEmissiveMaterial({0.0f, 1.0f, 0.0f}, 3.0f), {-2.0f, 0.0f, 8.0f}, {4.0f, 8.0f, 0.5f}, false));
            pWorld.Meshes.push_back(MakeBoxInstance(203, cubeView, MakeEmissiveMaterial({1.0f, 0.0f, 0.0f}, 3.0f), {3.5f, 0.0f, 8.0f}, {4.5f, 8.0f, 0.5f}, false));

            RenderReflectionProbe dynProbe{};
            dynProbe.EntityID = 5131;
            dynProbe.Shape = 0;
            dynProbe.Position = {-2.0f, 0.0f, 0.0f};
            dynProbe.BoxExtents = {16.0f, 16.0f, 16.0f}; // halfExtents = (8, 8, 8)
            dynProbe.BlendDistance = 4.0f;
            dynProbe.Intensity = 1.0f;
            dynProbe.DynamicCapture = true;
            dynProbe.CaptureResolution = 64u;
            dynProbe.CaptureShadows = false;
            dynProbe.CaptureMode = ReflectionProbeCaptureMode::Manual;
            dynProbe.ParallaxCorrected = false;

            const auto cap = renderer.CaptureDynamicReflectionProbe(pWorld, dynProbe);
            RENDER_CHECK(cap.Success && cap.CapturedFaces == 6u, "M15-U5-29: parallax test dynamic capture");

            PerspectiveCamera cam(45.0f, float(renderW) / float(renderH), 0.1f, 100.0f);
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(3.5f, 0.0f, 5.0f), glm::vec3(3.5f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              glm::vec3(3.5f, 0.0f, 5.0f));
            SceneRenderSettings s{};
            s.EnableSkybox = false;
            s.EnableSSR = false;

            // Case A: ParallaxCorrected = false -> raw R=(0,0,+1) hits probe-center +Z wall (-2, 0, +8) = Green
            pWorld.ReflectionProbes = {dynProbe};
            std::vector<glm::vec4> rawPixels;
            RENDER_CHECK(renderHDR(pWorld, cam, s, rawPixels), "M15-U5-29 uncorrected render");

            // Case B: ParallaxCorrected = true (plus overlapping second parallax probe -> Blue/Red composite on shifted wall)
            dynProbe.ParallaxCorrected = true;
            RenderReflectionProbe secondParallax = dynProbe;
            secondParallax.EntityID = 5132;
            secondParallax.DynamicCapture = false;
            secondParallax.CubemapPath = "u5e_red.hdr";
            secondParallax.Intensity = 0.25f;
            pWorld.ReflectionProbes = {dynProbe, secondParallax};
            std::vector<glm::vec4> parallaxPixels;
            RENDER_CHECK(renderHDR(pWorld, cam, s, parallaxPixels), "M15-U5-29 parallax-corrected render");

            uint32_t px = 0, py = 0;
            RENDER_CHECK(ProjectWorldToPixel(cam, renderW, renderH, {3.5f, 0.0f, 0.1f}, px, py), "U5-E parallax plate proj");
            const glm::vec3 rawColor = SampleWindowAroundPixel(rawPixels, renderW, renderH, px, py);
            const glm::vec3 correctedColor = SampleWindowAroundPixel(parallaxPixels, renderW, renderH, px, py);
            std::cout << "  [U5-E Parallax] raw=(" << rawColor.r << "," << rawColor.g
                      << ") corrected=(" << correctedColor.r << "," << correctedColor.g << ")\n";
            RENDER_CHECK(rawColor.g > rawColor.r * 2.5f && rawColor.g > 0.5f,
                "M15-U5-29: uncorrected ray reflects probe-center +Z Green wall");
            RENDER_CHECK(correctedColor.r > correctedColor.g * 2.5f && correctedColor.r > 0.5f,
                "M15-U5-29: per-probe box parallax redirects ray to actual off-axis Red wall before blend");
            renderer.ClearDynamicReflectionProbes();
        }

        // -----------------------------------------------------------------
        // 5. M15-U5-31.3: Missing/unready probe graceful omission & renormalization
        // -----------------------------------------------------------------
        {
            PerspectiveCamera cam(45.0f, float(renderW) / float(renderH), 0.1f, 100.0f);
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 4.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              glm::vec3(0.0f, 0.0f, 4.5f));

            RenderWorld world;
            world.SceneRevision = 9105;
            world.Skybox.Authored = true;
            world.Skybox.Enabled = false;
            world.Meshes.push_back(MakeBoxInstance(301, sphereView, chromeBarMat, {0.0f, 0.0f, 0.0f}, {1.2f, 1.2f, 1.2f}, false));

            RenderReflectionProbe validRed{};
            validRed.EntityID = 5141;
            validRed.BoxExtents = {10.0f, 10.0f, 10.0f};
            validRed.BlendDistance = 2.0f;
            validRed.Intensity = 1.0f;
            validRed.CubemapPath = "u5e_red.hdr";
            world.ReflectionProbes.push_back(validRed);

            SceneRenderSettings s{};
            s.EnableSkybox = false;
            s.EnableSSR = false;
            std::vector<glm::vec4> refPixels;
            RENDER_CHECK(renderHDR(world, cam, s, refPixels), "M15-U5-31.3 reference valid probe render");

            // Add cold uncaptured Manual dynamic probe overlapping the exact same sphere
            RenderReflectionProbe missingProbe{};
            missingProbe.EntityID = 5142;
            missingProbe.DynamicCapture = true;
            missingProbe.CaptureMode = ReflectionProbeCaptureMode::Manual;
            missingProbe.CaptureResolution = 64u;
            missingProbe.BoxExtents = {10.0f, 10.0f, 10.0f};
            missingProbe.BlendDistance = 2.0f;
            world.ReflectionProbes.push_back(missingProbe);

            std::vector<glm::vec4> withMissingPixels;
            RENDER_CHECK(renderHDR(world, cam, s, withMissingPixels), "M15-U5-31.3 with missing probe render");
            const auto& rf = renderer.GetLastReflectionFrame();
            RENDER_CHECK(rf.ActiveProbeCount == 1u && rf.MissingProbeCount == 1u,
                "M15-U5-31.3: missing unready manual probe is excluded while valid Red probe remains active");

            const glm::vec3 refCenter = SampleWindowAroundPixel(refPixels, renderW, renderH, renderW / 2u, renderH / 2u);
            const glm::vec3 missCenter = SampleWindowAroundPixel(withMissingPixels, renderW, renderH, renderW / 2u, renderH / 2u);
            std::cout << "  [U5-E MissingProbe] ref=(" << refCenter.r << "," << refCenter.g
                      << ") withMissing=(" << missCenter.r << "," << missCenter.g << ")\n";
            RENDER_CHECK(std::abs(missCenter.r - refCenter.r) < 0.05f && missCenter.r > 0.7f,
                "M15-U5-31.3: remaining probe renormalizes without darkening when overlapping missing probe");
        }

        // -----------------------------------------------------------------
        // 6. M15-U5-31.4: Stale Manual probe retains last ready generation until recapture
        // -----------------------------------------------------------------
        {
            renderer.ClearDynamicReflectionProbes();
            PerspectiveCamera cam(45.0f, float(renderW) / float(renderH), 0.1f, 100.0f);
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              glm::vec3(0.0f, 0.0f, 5.0f));

            RenderWorld world;
            world.SceneRevision = 9106;
            world.Skybox.Authored = true;
            world.Skybox.Enabled = false;
            world.Meshes.push_back(MakeBoxInstance(400, sphereView, chromeBarMat, {0.0f, 0.0f, 0.0f}, {1.2f, 1.2f, 1.2f}, false));
            world.Meshes.push_back(MakeBoxInstance(401, cubeView, MakeEmissiveMaterial({1.0f, 0.0f, 0.0f}, 3.0f), {0.0f, 0.0f, 8.0f}, {12.0f, 12.0f, 0.5f}, false));

            RenderReflectionProbe manualProbe{};
            manualProbe.EntityID = 400; // excludes center sphere during capture
            manualProbe.BoxExtents = {16.0f, 16.0f, 16.0f};
            manualProbe.BlendDistance = 4.0f;
            manualProbe.DynamicCapture = true;
            manualProbe.CaptureResolution = 64u;
            manualProbe.CaptureShadows = false;
            manualProbe.CaptureMode = ReflectionProbeCaptureMode::Manual;
            world.ReflectionProbes.push_back(manualProbe);

            SceneRenderSettings s{};
            s.EnableSkybox = false;
            s.EnableSSR = false;
            renderer.RequestDynamicReflectionCapture(manualProbe);
            std::vector<glm::vec4> redPixels;
            RENDER_CHECK(renderHDR(world, cam, s, redPixels), "M15-U5-31.4 initial manual Red capture render");
            const uint64_t initialGen = renderer.GetDynamicReflectionProbe(manualProbe)
                ? renderer.GetDynamicReflectionProbe(manualProbe)->GenerationID : 0u;
            RENDER_CHECK(initialGen > 0u, "M15-U5-31.4 manual capture published generation");

            // Edit emissive wall from Red -> Green and bump SceneRevision without triggering manual capture
            world.Meshes[1].Material.RHIMaterialAsset = MakeEmissiveMaterial({0.0f, 1.0f, 0.0f}, 3.0f);
            world.SceneRevision = 9107;
            std::vector<glm::vec4> stalePixels;
            RENDER_CHECK(renderHDR(world, cam, s, stalePixels), "M15-U5-31.4 stale retained render");
            const auto& staleFrame = renderer.GetLastReflectionFrame();
            const glm::vec3 staleCenter = SampleWindowAroundPixel(stalePixels, renderW, renderH, renderW / 2u, renderH / 2u);
            std::cout << "  [U5-E StaleManual] staleCenter=(" << staleCenter.r << "," << staleCenter.g
                      << ") staleCount=" << staleFrame.StaleProbeCount << "\n";
            RENDER_CHECK(staleFrame.ActiveProbeCount == 1u && staleFrame.StaleProbeCount == 1u && staleFrame.CaptureStale &&
                         staleCenter.r > staleCenter.g * 3.0f,
                "M15-U5-31.4: stale manual probe retains valid prior Red generation seamlessly");

            // Explicit manual recapture updates cleanly to Green
            renderer.RequestDynamicReflectionCapture(manualProbe);
            std::vector<glm::vec4> updatedPixels;
            RENDER_CHECK(renderHDR(world, cam, s, updatedPixels), "M15-U5-31.4 explicit recapture render");
            const glm::vec3 freshCenter = SampleWindowAroundPixel(updatedPixels, renderW, renderH, renderW / 2u, renderH / 2u);
            RENDER_CHECK(!renderer.GetLastReflectionFrame().CaptureStale &&
                         renderer.GetLastReflectionFrame().StaleProbeCount == 0u &&
                         freshCenter.g > freshCenter.r * 3.0f,
                "M15-U5-31.4: explicit recapture transitions cleanly to updated Green generation");
            renderer.ClearDynamicReflectionProbes();
        }

        // -----------------------------------------------------------------
        // 7. M15-U5-30.1 / 30.3 / 31.5: Unified energy policy & SSR hit/miss fallback
        // -----------------------------------------------------------------
        {
            PerspectiveCamera cam(45.0f, float(renderW) / float(renderH), 0.1f, 100.0f);
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 1.6f, 7.5f), glm::vec3(0.0f, 0.2f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              glm::vec3(0.0f, 1.6f, 7.5f));

            // 7a. M15-U5-30.3 Energy Conservation: White wall + White probe + White sky
            // Total reflected radiance must stay ~1.0 under SSR (no double counting).
            {
                RenderWorld energyWorld;
                energyWorld.SceneRevision = 9108;
                energyWorld.Skybox.Authored = energyWorld.Skybox.Enabled = energyWorld.Skybox.EnvironmentLighting = true;
                energyWorld.Skybox.Type = 1;
                energyWorld.Skybox.HDRPath = "u5e_white.hdr";
                energyWorld.Meshes.push_back(MakeBoxInstance(501, cubeView, MakeChromeMaterial(0.02f, 1.0f),
                    {0.0f, 0.0f, 0.0f}, {11.0f, 0.2f, 3.5f}, false));
                energyWorld.Meshes.push_back(MakeBoxInstance(502, cubeView, MakeEmissiveMaterial({1.0f, 1.0f, 1.0f}, 1.0f),
                    {-3.2f, 1.5f, -2.5f}, {4.5f, 4.0f, 1.0f}, false));

                RenderReflectionProbe whiteProbe{};
                whiteProbe.EntityID = 5151;
                whiteProbe.BoxExtents = {14.0f, 14.0f, 14.0f};
                whiteProbe.BlendDistance = 4.0f;
                whiteProbe.Intensity = 1.0f;
                whiteProbe.ParallaxCorrected = false;
                whiteProbe.CubemapPath = "u5e_white.hdr";
                energyWorld.ReflectionProbes.push_back(whiteProbe);

                SceneRenderSettings noSSR{};
                noSSR.EnableSkybox = true;
                noSSR.EnableSSR = false;
                std::vector<glm::vec4> basePixels;
                RENDER_CHECK(renderHDR(energyWorld, cam, noSSR, basePixels), "M15-U5-30.3 no-SSR energy baseline");

                SceneRenderSettings withSSR = noSSR;
                withSSR.EnableSSR = true;
                withSSR.SSRStrength = 1.0f;
                withSSR.SSRLengthPixels = 14.0f;
                std::vector<glm::vec4> ssrPixels;
                RENDER_CHECK(renderHDR(energyWorld, cam, withSSR, ssrPixels), "M15-U5-30.3 SSR energy render");
                const auto& passes = renderer.GetDiagnostics().GetFrame().Passes;
                const bool ssrPassRan =
                    std::any_of(passes.begin(), passes.end(), [](const auto& p) { return p.Name == "SSRCompute"; }) &&
                    std::any_of(passes.begin(), passes.end(), [](const auto& p) { return p.Name == "SSRApply"; });
                RENDER_CHECK(ssrPassRan && renderer.GetLastReflectionFrame().State == ReflectionState::AuthoredReady &&
                             renderer.GetLastReflectionFrame().ActiveProbeCount == 1u,
                    "M15-U5-30.1: SSRCompute + SSRApply execute alongside active AuthoredReady probe hierarchy");

                uint32_t lx = 0, ly = 0;
                RENDER_CHECK(ProjectWorldToPixel(cam, renderW, renderH, {-2.5f, 0.1f, 0.5f}, lx, ly), "U5-E energy floor proj");
                const glm::vec3 baseVal = SampleWindowAroundPixel(basePixels, renderW, renderH, lx, ly, 3u);
                const glm::vec3 ssrVal = SampleWindowAroundPixel(ssrPixels, renderW, renderH, lx, ly, 3u);
                std::cout << "  [U5-E Energy] base=(" << baseVal.r << "," << baseVal.g << "," << baseVal.b
                          << ") withSSR=(" << ssrVal.r << "," << ssrVal.g << "," << ssrVal.b << ")\n";
                RENDER_CHECK(ssrVal.r <= 1.15f && ssrVal.g <= 1.15f && ssrVal.b <= 1.15f &&
                             std::abs(ssrVal.r - baseVal.r) < 0.18f,
                    "M15-U5-30.3: unified energy policy prevents additive SSR double-counting");
            }

            // 7b. M15-U5-30.1 / 31.5: SSR hit on left bright Green wall vs SSR miss on right falling back to Red probe / Blue sky
            {
                RenderWorld hitMissWorld;
                hitMissWorld.SceneRevision = 9109;
                hitMissWorld.Skybox.Authored = hitMissWorld.Skybox.Enabled = hitMissWorld.Skybox.EnvironmentLighting = true;
                hitMissWorld.Skybox.Type = 1;
                hitMissWorld.Skybox.HDRPath = "u5e_blue.hdr";
                hitMissWorld.Meshes.push_back(MakeBoxInstance(511, cubeView, MakeChromeMaterial(0.02f, 1.0f),
                    {0.0f, 0.0f, 0.0f}, {11.0f, 0.2f, 3.5f}, false));
                // Emissive bright Green wall ONLY on the left half (right half misses into sky)
                hitMissWorld.Meshes.push_back(MakeBoxInstance(512, cubeView, MakeEmissiveMaterial({0.0f, 6.0f, 0.0f}, 1.0f),
                    {-3.2f, 1.5f, -2.5f}, {4.5f, 4.0f, 1.0f}, false));

                // Red probe covering left/mid of floor, blending into Blue sky on far right
                RenderReflectionProbe redProbe{};
                redProbe.EntityID = 5161;
                redProbe.Position = {-1.5f, 0.0f, 0.0f};
                redProbe.BoxExtents = {6.0f, 8.0f, 8.0f}; // core [-4.5..+1.5], fades out across BlendDistance=2.0 -> +3.5
                redProbe.BlendDistance = 2.0f;
                redProbe.Intensity = 1.0f;
                redProbe.ParallaxCorrected = false;
                redProbe.CubemapPath = "u5e_red.hdr";
                hitMissWorld.ReflectionProbes.push_back(redProbe);

                SceneRenderSettings noSSR{};
                noSSR.EnableSkybox = true;
                noSSR.EnableSSR = false;
                std::vector<glm::vec4> noSSRPixels;
                RENDER_CHECK(renderHDR(hitMissWorld, cam, noSSR, noSSRPixels), "M15-U5-31.5 no-SSR baseline");

                SceneRenderSettings withSSR = noSSR;
                withSSR.EnableSSR = true;
                withSSR.SSRStrength = 1.0f;
                withSSR.SSRLengthPixels = 14.0f;
                std::vector<glm::vec4> ssrPixels;
                RENDER_CHECK(renderHDR(hitMissWorld, cam, withSSR, ssrPixels), "M15-U5-31.5 SSR hit/miss render");

                uint32_t hlx = 0, hly = 0, mrpx = 0, mrpy = 0, skx = 0, sky = 0;
                RENDER_CHECK(ProjectWorldToPixel(cam, renderW, renderH, {-2.5f, 0.1f, 0.5f}, hlx, hly) &&
                             ProjectWorldToPixel(cam, renderW, renderH, {0.5f, 0.1f, 0.5f}, mrpx, mrpy) &&
                             ProjectWorldToPixel(cam, renderW, renderH, {4.5f, 0.1f, 0.5f}, skx, sky),
                    "U5-E SSR hit/miss sample projections");
                const glm::vec3 noSSRL = SampleWindowAroundPixel(noSSRPixels, renderW, renderH, hlx, hly, 3u);
                const glm::vec3 withSSRL = SampleWindowAroundPixel(ssrPixels, renderW, renderH, hlx, hly, 3u);
                const glm::vec3 missProbe = SampleWindowAroundPixel(ssrPixels, renderW, renderH, mrpx, mrpy, 3u);
                const glm::vec3 noSSRMissP = SampleWindowAroundPixel(noSSRPixels, renderW, renderH, mrpx, mrpy, 3u);
                const glm::vec3 missSky = SampleWindowAroundPixel(ssrPixels, renderW, renderH, skx, sky, 3u);
                std::cout << "  [U5-E SSR] hitL_no=(" << noSSRL.r << "," << noSSRL.g << ") hitL_on=("
                          << withSSRL.r << "," << withSSRL.g << ") missProbe=("
                          << missProbe.r << "," << missProbe.g << "," << missProbe.b << ") missSky=("
                          << missSky.r << "," << missSky.b << ")\n";
                RENDER_CHECK(withSSRL.g > noSSRL.g * 2.0f && withSSRL.g > withSSRL.r &&
                             withSSRL.r < noSSRL.r * 0.65f,
                    "M15-U5-30.1: SSR hit injects Green reflection proportionally and replaces rather than adds probe Red");
                RENDER_CHECK(std::abs(missProbe.r - noSSRMissP.r) < 0.08f && missProbe.r > 0.7f,
                    "M15-U5-31.5: SSR miss inside probe falls back cleanly to Red local probe");
                RENDER_CHECK(missSky.b > missSky.r * 3.0f && missSky.b > 0.7f,
                    "M15-U5-31.5: SSR miss outside probe falls back cleanly to Blue sky IBL");
            }
        }

        // -----------------------------------------------------------------
        // 8. P5 exit: With SSR DISABLED, dynamic captured cubemap contains Red object,
        //    sphere samples captured cubemap, and roughness changes sharpness.
        // -----------------------------------------------------------------
        {
            renderer.ClearDynamicReflectionProbes();
            RenderWorld p5World;
            p5World.SceneRevision = 9110;
            p5World.Skybox.Authored = true;
            p5World.Skybox.Enabled = false;

            // Emissive bright Red beacon behind camera at -Z = -9.0 (captured in face -Z, never occludes sphere at origin)
            p5World.Meshes.push_back(MakeBoxInstance(901, cubeView, MakeEmissiveMaterial({1.0f, 0.04f, 0.04f}, 4.0f),
                {0.0f, 0.0f, -9.0f}, {2.8f, 2.8f, 0.8f}, false));

            auto sphereMeshIdx = p5World.Meshes.size();
            p5World.Meshes.push_back(MakeBoxInstance(900, sphereView, MakeChromeMaterial(0.05f, 1.0f),
                {0.0f, 0.0f, 0.0f}, {1.2f, 1.2f, 1.2f}, false));

            RenderReflectionProbe p5Probe{};
            p5Probe.EntityID = 900; // self-excludes sphere during probe capture
            p5Probe.BoxExtents = {24.0f, 24.0f, 24.0f};
            p5Probe.BlendDistance = 4.0f;
            p5Probe.Intensity = 1.0f;
            p5Probe.ParallaxCorrected = false;
            p5Probe.DynamicCapture = true;
            p5Probe.CaptureResolution = 64u;
            p5Probe.CaptureShadows = false;
            p5Probe.CaptureMode = ReflectionProbeCaptureMode::Manual;
            p5World.ReflectionProbes.push_back(p5Probe);

            const auto capRes = renderer.CaptureDynamicReflectionProbe(p5World, p5Probe);
            const auto gen = renderer.GetDynamicReflectionProbe(p5Probe);
            RENDER_CHECK(capRes.Success && gen && device.WaitForCompletion(gen->Completion) && gen->RawCubemap,
                "P5 exit: dynamic reflection probe captures scene cubemap");

            std::vector<glm::vec4> negZPixels;
            RENDER_CHECK(ReadCubeFaceRGBA16(*gen->RawCubemap, 5u, negZPixels, 0u), "P5 exit: read captured -Z face");
            const glm::vec3 negZCenter = SampleWindowAroundPixel(negZPixels, 64u, 64u, 32u, 32u, 2u);
            std::cout << "  [P5 Exit Cube -Z] center=(" << negZCenter.r << "," << negZCenter.g << "," << negZCenter.b << ")\n";
            RENDER_CHECK(negZCenter.r > 1.5f && negZCenter.r > negZCenter.g * 5.0f,
                "P5 exit: captured cubemap -Z face contains the emissive Red object");

            PerspectiveCamera cam(45.0f, float(renderW) / float(renderH), 0.1f, 100.0f);
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, -6.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              glm::vec3(0.0f, 0.0f, -6.0f));
            RHITexture p5Color;
            RENDER_CHECK(p5Color.Init(RHITextureDesc::RenderTarget2D(renderW, renderH)), "P5 exit LDR display target");
            SceneRenderSettings p5Settings{};
            p5Settings.EnableSSR = false;
            p5Settings.EnableSkybox = false;
            p5Settings.EnableShadows = false;
            p5Settings.EnableParticles = false;
            p5Settings.EnableStudioLightLobes = false;
            p5Settings.EnableToneMapping = true;

            float redByRoughness[3]{};
            const float roughnessLevels[3]{0.05f, 0.40f, 0.85f};
            for (int r = 0; r < 3; ++r) {
                p5World.Meshes[sphereMeshIdx].Material.RHIMaterialAsset = MakeChromeMaterial(roughnessLevels[r], 1.0f);
                renderer.RenderWorldView(p5World, cam, &p5Color, &depthTex, p5Settings);
                RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors() && device.WaitIdle(),
                    "P5 exit sphere render with SSR disabled");
                const auto& p5Passes = renderer.GetDiagnostics().GetFrame().Passes;
                const bool ssrRan = std::any_of(p5Passes.begin(), p5Passes.end(), [](const auto& p) {
                    return p.Name == "SSRCompute" || p.Name == "SSRApply";
                });
                RENDER_CHECK(!ssrRan && renderer.GetLastReflectionFrame().State == ReflectionState::DynamicReady &&
                             renderer.GetLastReflectionFrame().PrefilterReady &&
                             renderer.GetLastReflectionFrame().Fallback == ReflectionFallback::None,
                    "P5 exit: SSR disabled skips SSRCompute/SSRApply and resolves through DynamicReady local probe");
                std::vector<glm::vec4> outPx;
                RENDER_CHECK(renderer.GetIntermediateHDRTarget() &&
                             ReadTexture2DRGBA16(*renderer.GetIntermediateHDRTarget(), outPx) &&
                             outPx.size() == size_t(renderW) * renderH && AllFiniteNonNegative(outPx),
                    "P5 exit scene-linear HDR readback before tone mapping");
                const glm::vec3 center = SampleWindowAroundPixel(outPx, renderW, renderH, renderW / 2u, renderH / 2u, 2u);
                redByRoughness[r] = center.r;
            }
            std::cout << "  [P5 Exit Roughness SSR=OFF] R(0.05)=" << redByRoughness[0]
                      << " R(0.40)=" << redByRoughness[1]
                      << " R(0.85)=" << redByRoughness[2] << "\n";
            RENDER_CHECK(redByRoughness[0] > 1.5f,
                "P5 exit: with SSR disabled, mirror sphere samples captured Red object brightly in linear HDR");
            RENDER_CHECK(redByRoughness[0] > redByRoughness[1] * 1.25f &&
                         redByRoughness[1] > redByRoughness[2] * 1.25f,
                "P5 exit: roughness sweep (0.05 -> 0.40 -> 0.85) monotonically changes reflection sharpness");
            renderer.ClearDynamicReflectionProbes();
        }

        RENDER_CHECK(device.WaitIdle() && Axle::RenderTests::GetRHIValidationErrorCount() == 0,
            "U5-E zero RHI validation errors across multi-probe & SSR blend tests");
        std::cout << "[M15ReflectionCaptureGPU] All U5-E multi-probe blend & P5 exit checks passed!\n";
        return true;
    }
} // namespace

bool RunM15ReflectionCaptureTests(RHISceneRenderer& renderer, const std::filesystem::path& output) {
    std::cout << "[M15ReflectionCaptureGPU] Running native D3D12/Vulkan U5-B/U5-C/U5-D dynamic capture tests...\n";
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
        // Keep the captured probe in the live world. Manual mode prevents another
        // capture here; removing it would intentionally retire its cached generation.
        mainWorld.ReflectionProbes = {probe};
        mainWorld.ReflectionProbes[0].CaptureMode = ReflectionProbeCaptureMode::Manual;
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

    // -------------------------------------------------------------------------
    // 5. M15-U5-C (S5.3): Multi-mip GGX roughness prefilter, cross-face seams,
    //    pole isotropy, CPU parity, roughness sweep, & mid-prefilter fault rollback
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t prefilterRes = 64u;
        constexpr uint32_t expectedMips = 5u;
        RenderWorld prefilterWorld;
        prefilterWorld.SceneRevision = 500;
        prefilterWorld.Skybox.Authored = true;
        prefilterWorld.Skybox.Enabled = false;

        // Compact bright HDR emitter on +Z (center blur & sharpness fixture)
        prefilterWorld.Meshes.push_back(MakeBoxInstance(
            2001, cubeView, MakeEmissiveMaterial({12.0f, 6.0f, 2.0f}, 1.0f), {0.0f, 0.0f, 5.0f}, {1.6f, 1.6f, 0.4f}));
        // Bright HDR seam pillar straddling the +X / +Z boundary (u_Z=1 / u_X=0)
        prefilterWorld.Meshes.push_back(MakeBoxInstance(
            2002, cubeView, MakeEmissiveMaterial({0.0f, 8.0f, 8.0f}, 1.0f), {4.0f, 0.0f, 4.0f}, {1.6f, 2.5f, 1.6f}));
        // Symmetric square HDR beacon on the +Y pole to test azimuthal isotropy (zero equirectangular pinch)
        prefilterWorld.Meshes.push_back(MakeBoxInstance(
            2003, cubeView, MakeEmissiveMaterial({8.0f, 4.0f, 10.0f}, 1.0f), {0.0f, 5.0f, 0.0f}, {1.8f, 0.4f, 1.8f}));

        RenderReflectionProbe prefilterProbe{};
        prefilterProbe.EntityID = 2000;
        prefilterProbe.Position = {0.0f, 0.0f, 0.0f};
        prefilterProbe.BoxExtents = {20.0f, 20.0f, 20.0f};
        prefilterProbe.DynamicCapture = true;
        prefilterProbe.CaptureResolution = prefilterRes;
        prefilterProbe.CaptureShadows = false;

        const auto captureRes = renderer.CaptureDynamicReflectionProbe(prefilterWorld, prefilterProbe);
        RENDER_CHECK(captureRes.Success && captureRes.CapturedFaces == 6u,
            "M15-U5-16.1/17.1: 64px dynamic capture with multi-mip GGX prefilter succeeds");
        const auto gen = renderer.GetDynamicReflectionProbe(prefilterProbe);
        RENDER_CHECK(gen && device.WaitForCompletion(gen->Completion) &&
                     gen->MipCount == expectedMips &&
                     gen->Cubemap && gen->Cubemap->IsValid() &&
                     gen->Cubemap->GetMipCount() == expectedMips &&
                     gen->Cubemap->GetDesc().ArrayLayers == 6 &&
                     gen->Cubemap->GetDesc().IsCubeMap &&
                     gen->Cubemap->GetFormat() == RHITextureFormat::RGBA16_FLOAT &&
                     gen->RawCubemap && gen->RawCubemap->IsValid() &&
                     gen->RawCubemap->GetMipCount() == expectedMips,
            "M15-U5-16.1/16.2: dynamic probe publishes 5-mip RGBA16_FLOAT prefiltered and raw cubemaps");
        for (uint32_t mip = 0u; mip < expectedMips; ++mip) {
            RENDER_CHECK(gen->Cubemap->GetMipSRV(mip) != nullptr && gen->Cubemap->GetMipUAV(mip) != nullptr,
                "M15-U5-16.2: every prefilter mip exposes valid per-mip SRV and storage UAV");
        }

        // Read every face & mip, check finiteness/non-negativity, peak drop, skirt spread, seam continuity, pole isotropy
        std::array<std::vector<glm::vec3>, 6> rawMip0;
        float plusZCenterPeak[expectedMips]{};
        float plusZSkirt[expectedMips]{};
        for (uint32_t f = 0u; f < 6u; ++f) {
            std::vector<glm::vec4> rawFace;
            RENDER_CHECK(ReadCubeFaceRGBA16(*gen->RawCubemap, f, rawFace, 0u), "M15-U5-C raw mip0 readback");
            rawMip0[f].resize(rawFace.size());
            for (size_t i = 0; i < rawFace.size(); ++i) {
                rawMip0[f][i] = glm::vec3(rawFace[i]);
            }
        }

        for (uint32_t mip = 0u; mip < expectedMips; ++mip) {
            const uint32_t mSize = std::max(1u, prefilterRes >> mip);
            std::array<std::vector<glm::vec4>, 6> mFaces;
            for (uint32_t f = 0u; f < 6u; ++f) {
                RENDER_CHECK(ReadCubeFaceRGBA16(*gen->Cubemap, f, mFaces[f], mip),
                    "M15-U5-C prefilter mip readback");
                for (const auto& p : mFaces[f]) {
                    RENDER_CHECK(std::isfinite(p.r) && std::isfinite(p.g) && std::isfinite(p.b) && std::isfinite(p.a) &&
                                 p.r >= 0.0f && p.g >= 0.0f && p.b >= 0.0f,
                        "M15-U5-17.1: prefilter output stays strictly finite and non-negative across all faces/mips");
                }
            }

            const uint32_t c0 = (mSize > 2u) ? (mSize / 2u - 1u) : 0u;
            const uint32_t c1 = (mSize > 2u) ? (mSize / 2u + 1u) : mSize;
            plusZCenterPeak[mip] = AverageRegion(mFaces[4], mSize, c0, c0, c1, c1).r;
            const uint32_t sx0 = mSize / 4u;
            const uint32_t sx1 = std::max(sx0 + 1u, mSize / 4u + std::max(1u, mSize / 8u));
            plusZSkirt[mip] = AverageRegion(mFaces[4], mSize, sx0, c0, sx1, c1).r;

            // M15-U5-17.3: Cross-face seam continuity along the +X (u=0, left col) / +Z (u=1, right col) shared edge
            if (mip >= 1u && mip <= 3u) {
                const glm::vec3 zRightEdge = AverageRegion(mFaces[4], mSize, mSize - 1u, c0, mSize, c1);
                const glm::vec3 xLeftEdge = AverageRegion(mFaces[0], mSize, 0u, c0, 1u, c1);
                const float seamMean = 0.5f * (zRightEdge.g + xLeftEdge.g);
                const float seamDiff = std::abs(zRightEdge.g - xLeftEdge.g);
                RENDER_CHECK(seamMean > 0.5f && seamDiff < seamMean * 0.35f,
                    "M15-U5-17.3: +X/+Z seam continuity preserves bright edge radiance without dark border cutoff");
            }

            // M15-U5-20: +Y pole isotropy (horizontal vs vertical spread on face 2 matches cleanly)
            if (mip == 2u) {
                const uint32_t off = mSize / 4u;
                const glm::vec3 horizSample = AverageRegion(mFaces[2], mSize, mSize / 2u + off, c0, mSize / 2u + off + 1u, c1);
                const glm::vec3 vertSample = AverageRegion(mFaces[2], mSize, c0, mSize / 2u + off, c1, mSize / 2u + off + 1u);
                const glm::vec3 poleCenter = AverageRegion(mFaces[2], mSize, c0, c0, c1, c1);
                const float maxSpread = std::max({horizSample.b, vertSample.b, 1e-4f});
                RENDER_CHECK(poleCenter.b > 0.5f && std::abs(horizSample.b - vertSample.b) <= maxSpread * 0.35f,
                    "M15-U5-20: +Y pole filtering stays isotropic without equirectangular pinch distortion");
            }
        }

        RENDER_CHECK(plusZCenterPeak[0] > plusZCenterPeak[1] &&
                     plusZCenterPeak[1] > plusZCenterPeak[2] &&
                     plusZCenterPeak[2] > plusZCenterPeak[3] &&
                     plusZCenterPeak[3] > plusZCenterPeak[4] &&
                     plusZSkirt[2] > plusZSkirt[0] + 0.05f,
            "M15-U5-17.1/18.1: mip peaks decrease monotonically and GGX skirt broadens with mip level");

        // M15-U5-19.1: Controlled parity check against CPU 3D cubemap GGX reference
        for (uint32_t checkMip : {1u, 2u}) {
            const uint32_t mSize = prefilterRes >> checkMip;
            const float rough = ReflectionCaptureContract::ComputePrefilterRoughness(checkMip, expectedMips);
            std::vector<glm::vec4> gpuFaceZ;
            RENDER_CHECK(ReadCubeFaceRGBA16(*gen->Cubemap, 4u, gpuFaceZ, checkMip), "M15-U5-19.1 read GPU face");
            const uint32_t px = mSize / 2u;
            const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(mSize);
            const glm::vec3 gpuVal = glm::vec3(gpuFaceZ[size_t(px) * mSize + px]);
            const glm::vec3 dir = ReflectionCaptureContract::ComputeCubeTexelDirection(4u, u, u);
            const glm::vec3 cpuRef = ReflectionCaptureContract::EvaluatePrefilterDirectionCPU(
                rawMip0, prefilterRes, dir, rough, ReflectionCaptureContract::DefaultPrefilterSampleCount);
            const float denom = std::max(glm::length(cpuRef), 0.1f);
            const float relErr = glm::distance(gpuVal, cpuRef) / denom;
            std::cout << "  [U5-C Parity] mip=" << checkMip << " r=" << rough
                      << " gpu=(" << gpuVal.r << "," << gpuVal.g << "," << gpuVal.b << ")"
                      << " cpu=(" << cpuRef.r << "," << cpuRef.g << "," << cpuRef.b << ") relErr=" << relErr << "\n";
            RENDER_CHECK(relErr < 0.25f,
                "M15-U5-19.1: GPU cubemap GGX prefilter matches pure-3D CPU reference closely");
        }

        // Prefilter rollback at begin and after all mip dispatches are recorded (command #7, after six faces).
        {
            const uint64_t retainedGenId = gen->GenerationID;
            const auto retainedHandle = gen->Cubemap->GetHandle();
            RenderWorld updatedWorld = prefilterWorld;
            updatedWorld.SceneRevision = 520;
            updatedWorld.Meshes[0] = MakeBoxInstance(
                2001, cubeView, MakeEmissiveMaterial({0.0f, 15.0f, 0.0f}, 1.0f), {0.0f, 0.0f, 5.0f}, {1.6f, 1.6f, 0.4f});

            for (const auto boundary : {"BeginCommandBuffer", "EndCommandBuffer"}) {
                uint32_t commandCount = 0;
                bool prefilterFaultHit = false;
                {
                    ScopedRHIFault fault([&](std::string_view op) -> std::optional<nri::Result> {
                        if (op == boundary) {
                            ++commandCount;
                            if (commandCount == 7u && !prefilterFaultHit) {
                                prefilterFaultHit = true;
                                return nri::Result::OUT_OF_MEMORY;
                            }
                        }
                        return {};
                    });
                    const auto faultRes = renderer.CaptureDynamicReflectionProbe(updatedWorld, prefilterProbe);
                    RENDER_CHECK(prefilterFaultHit &&
                                 !faultRes.Success &&
                                 faultRes.PreservedPreviousGeneration &&
                                 faultRes.PublishedGenerationID == retainedGenId &&
                                 faultRes.Diagnostic.find("DYNAMIC_PROBE_PREFILTER_FAILED") != std::string::npos,
                        "M15-U5-C rollback: prefilter command failure aborts publication cleanly while keeping previous generation");
                }
                const auto afterFaultGen = renderer.GetDynamicReflectionProbe(prefilterProbe);
                RENDER_CHECK(afterFaultGen && afterFaultGen->GenerationID == retainedGenId &&
                             afterFaultGen->Cubemap->GetHandle() == retainedHandle,
                    "M15-U5-C rollback: retained prefiltered cubemap handle remains valid after prefilter fault");
                std::vector<glm::vec4> intactMip2;
                RENDER_CHECK(ReadCubeFaceRGBA16(*afterFaultGen->Cubemap, 4u, intactMip2, 2u), "M15-U5-C intact mip2");
                RENDER_CHECK(AverageRegion(intactMip2, 16u, 7u, 7u, 9u, 9u).r > AverageRegion(intactMip2, 16u, 7u, 7u, 9u, 9u).g,
                    "M15-U5-C rollback: preserved prefilter mip chain still holds original orange/red radiance");

            }

            const auto recoverRes = renderer.CaptureDynamicReflectionProbe(updatedWorld, prefilterProbe);
            RENDER_CHECK(recoverRes.Success && recoverRes.PublishedGenerationID > retainedGenId,
                "M15-U5-C recovery: recapture succeeds once prefilter fault clears");
        }
        // M15-U5-18.1 / 18.2 / 19.2: Roughness sweep sphere fixture sampling dynamic prefiltered cubemap via ForwardPBR
        {
            RHITexture sweepColor, sweepDepth;
            RENDER_CHECK(sweepColor.Init(RHITextureDesc::RenderTarget2D(64, 64, RHITextureFormat::RGBA16_FLOAT, "U5CSweepColor")) &&
                         sweepDepth.Init(RHITextureDesc::DepthStencil2D(64, 64, RHITextureFormat::D32_FLOAT, "U5CSweepDepth")),
                "M15-U5-19.2 sweep render targets");
            const auto savedIrr = renderer.GetIrradianceMap();
            const auto savedPref = renderer.GetPrefilterMap();
            const auto savedLut = renderer.GetBRDFLUT();
            renderer.SetIBLMaps(RHIMaterial::GetDefaultIrradianceCubemap(), gen->Cubemap, RHIMaterial::GetDefaultBRDFLUT());

            RHIMaterialDesc matDesc{};
            matDesc.BaseColor = {1.0f, 1.0f, 1.0f, 1.0f};
            matDesc.Metallic = 1.0f;
            matDesc.Roughness = 0.05f;
            auto sweepMat = std::make_shared<RHIMaterial>(matDesc);

            RenderWorld sweepWorld;
            sweepWorld.SceneRevision = 510;
            sweepWorld.Skybox.Authored = true;
            sweepWorld.Skybox.Enabled = true;
            sweepWorld.Skybox.EnvironmentLighting = true;
            sweepWorld.Skybox.Type = 1;
            sweepWorld.Meshes.push_back(MakeBoxInstance(2100, sphereView, sweepMat, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, false));

            PerspectiveCamera sweepCam(45.0f, 1.0f, 0.1f, 50.0f);
            sweepCam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 2.8f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)), {0.0f, 0.0f, 2.8f});
            SceneRenderSettings sweepSettings;
            sweepSettings.EnableShadows = false;
            sweepSettings.EnableParticles = false;
            sweepSettings.EnableSSR = false;
            sweepSettings.EnableGTAO = false;
            sweepSettings.EnableTAA = false;
            sweepSettings.EnableBloom = false;
            sweepSettings.EnableFXAA = false;
            sweepSettings.EnableToneMapping = true;
            sweepSettings.ClearColor = {0.0f, 0.0f, 0.0f, 1.0f};

            const float roughnessSteps[4] = {0.05f, 0.35f, 0.70f, 0.95f};
            float peakLuma[4]{};
            float offCenterFraction[4]{};
            for (int step = 0; step < 4; ++step) {
                sweepMat->SetRoughness(roughnessSteps[step]);
                renderer.RenderWorldView(sweepWorld, sweepCam, &sweepColor, &sweepDepth, sweepSettings);
                RENDER_CHECK(renderer.WasLastRenderSuccessful(), "M15-U5-19.2 roughness sweep render");
                std::vector<glm::vec4> pixels;
                RENDER_CHECK(renderer.GetIntermediateHDRTarget() && ReadTexture2DRGBA16(*renderer.GetIntermediateHDRTarget(), pixels),
                    "M15-U5-19.2 scene-linear sweep readback before tone mapping");

                float maxVal = 0.0f;
                float centerVal = 0.0f;
                float ringSum = 0.0f;
                uint32_t ringCount = 0;
                for (uint32_t y = 0; y < 64u; ++y) {
                    for (uint32_t x = 0; x < 64u; ++x) {
                        const glm::vec4& p = pixels[size_t(y) * 64u + x];
                        RENDER_CHECK(std::isfinite(p.r) && std::isfinite(p.g) && std::isfinite(p.b) && std::isfinite(p.a) && p.r >= 0.0f && p.g >= 0.0f && p.b >= 0.0f,
                            "M15-U5-19.2 sphere render outputs remain finite and non-negative");
                        const float luma = p.r * 0.2126f + p.g * 0.7152f + p.b * 0.0722f;
                        maxVal = std::max(maxVal, luma);
                        const float dx = static_cast<float>(x) - 31.5f;
                        const float dy = static_cast<float>(y) - 31.5f;
                        const float dist = std::sqrt(dx * dx + dy * dy);
                        if (dist <= 3.0f) centerVal = std::max(centerVal, luma);
                        if (dist >= 6.0f && dist <= 16.0f) {
                            ringSum += luma;
                            ++ringCount;
                        }
                    }
                }
                peakLuma[step] = std::max(maxVal, centerVal);
                if (step == 0) RENDER_CHECK(peakLuma[step] > 1, "sharp sweep is genuinely HDR, not display-mapped color in FP16 storage");
                offCenterFraction[step] = (ringCount > 0 && peakLuma[step] > 1e-4f)
                    ? (ringSum / static_cast<float>(ringCount)) / peakLuma[step]
                    : 0.0f;
                std::cout << "  [U5-C Sweep] r=" << roughnessSteps[step]
                          << " peakLuma=" << peakLuma[step]
                          << " ringFraction=" << offCenterFraction[step] << "\n";
            }
            RENDER_CHECK(peakLuma[0] > peakLuma[1] &&
                         peakLuma[1] > peakLuma[2] &&
                         peakLuma[2] > peakLuma[3] &&
                         offCenterFraction[3] > offCenterFraction[1] &&
                         offCenterFraction[2] > offCenterFraction[0],
                "M15-U5-18.1/18.2/19.2: roughness sweep reduces specular peak monotonically and broadens highlight skirt");
            renderer.SetIBLMaps(savedIrr, savedPref, savedLut);
        }
    }

    // -------------------------------------------------------------------------
    // 6. M15-U5-D (S5.4): Per-domain invalidation, Manual/Automatic modes,
    //    bounded per-frame scheduling, Pending/Stale/Failed retention,
    //    cache memory budget + LRU eviction, viewport/probe resize, and scene replacement.
    // -------------------------------------------------------------------------
    {
        renderer.ClearDynamicReflectionProbes();
        renderer.ClearDynamicCaptureBudgetOverride();

        RHITexture color, depth;
        RENDER_CHECK(color.Init(RHITextureDesc::RenderTarget2D(64, 64)) &&
                     depth.Init(RHITextureDesc::DepthStencil2D(64, 64)),
            "U5-D viewport targets");
        PerspectiveCamera cam(55.0f, 1.0f, 0.1f, 100.0f);
        cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 2.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                          {0.0f, 0.0f, 2.5f});

        SceneRenderSettings schedSettings;
        schedSettings.EnableParticles = false;
        schedSettings.EnableShadows = false;
        schedSettings.EnableSkybox = false;

        auto renderView = [&](const RenderWorld& w, RHITexture& c, RHITexture& d, const SceneRenderSettings& s) {
            renderer.RenderWorldView(w, cam, &c, &d, s);
            const auto& completion = renderer.GetLastCompletion();
            if (completion) {
                device.WaitForCompletion(completion);
            }
            return renderer.WasLastRenderSuccessful();
        };

        // 6A. Isolated per-domain triggers (21.1..21.5) & Manual vs Automatic + Incremental faces (22, 23.1, 23.2, 23.3, 23.4, 24)
        {
            RenderWorld u5dWorld;
            u5dWorld.SceneRevision = 700;
            u5dWorld.Skybox.Authored = true;
            u5dWorld.Skybox.Enabled = false;

            auto beaconMat = MakeEmissiveMaterial({10.0f, 0.0f, 0.0f}, 1.0f);
            u5dWorld.Meshes.push_back(MakeBoxInstance(
                4001, cubeView, beaconMat, {5.0f, 0.0f, 0.0f}, {0.5f, 8.0f, 8.0f}));

            RenderReflectionProbe manualProbe{};
            manualProbe.EntityID = 4000;
            manualProbe.Position = {0.0f, 0.0f, 0.0f};
            manualProbe.BoxExtents = {20.0f, 20.0f, 20.0f};
            manualProbe.DynamicCapture = true;
            manualProbe.CaptureResolution = 32u;
            manualProbe.CaptureShadows = false;
            manualProbe.CaptureMode = ReflectionProbeCaptureMode::Manual;
            u5dWorld.ReflectionProbes.push_back(manualProbe);

            // M15-U5-22: Manual mode stays Idle until explicit capture or manual request.
            RENDER_CHECK(renderView(u5dWorld, color, depth, schedSettings), "U5-D manual cold render");
            RENDER_CHECK(!renderer.GetDynamicReflectionProbe(manualProbe),
                "M15-U5-22: manual probe does not auto-capture on initial render");
            const auto coldStatus = renderer.GetDynamicReflectionProbeStatus(manualProbe);
            RENDER_CHECK(coldStatus && coldStatus->State == DynamicCaptureLifecycleState::Idle &&
                         coldStatus->ManualRefreshRequired && !coldStatus->HasGeneration,
                "M15-U5-22: cold manual probe reports Idle and ManualRefreshRequired");

            // Explicit synchronous capture creates Generation 1 (red radiance).
            const auto manualCap = renderer.CaptureDynamicReflectionProbe(u5dWorld, manualProbe, schedSettings);
            RENDER_CHECK(manualCap.Success && manualCap.CapturedFaces == 6u,
                "M15-U5-22: explicit CaptureDynamicReflectionProbe populates Manual probe");
            const auto gen1 = renderer.GetDynamicReflectionProbe(manualProbe);
            RENDER_CHECK(gen1 && gen1->DeviceEpoch == device.GetEpoch() && gen1->IsValidForDevice(device.GetEpoch()) &&
                         !gen1->IsValidForDevice(device.GetEpoch() + 1u),
                "M15-U5-26.3: dynamic probe generation tracks DeviceEpoch and rejects mismatched epochs");

            // Domain stamp check on live world: each edit isolates its domain flag cleanly.
            const auto stamps1 = gen1->DomainStamps;
            ReflectionService reflSvc;
            {
                auto pMoved = manualProbe; pMoved.Position.y += 1.0f;
                const auto inv = reflSvc.EvaluateInvalidation(stamps1, renderer.ComputeDynamicCaptureDomainStamps(u5dWorld, pMoved, schedSettings));
                RENDER_CHECK(inv.TransformChanged && !inv.GeometryChanged && !inv.MaterialChanged && !inv.LightingChanged,
                    "M15-U5-21.1: GPU renderer DomainStamps isolate probe TransformChanged");
            }
            {
                RenderWorld altMeshWorld = u5dWorld;
                auto altCube = RHIMesh::CreateCube(1.0f);
                RENDER_CHECK(altCube && altCube->IsValid(), "U5-D alternate cube asset");
                altMeshWorld.Meshes[0].NativeMesh = altCube;
                altMeshWorld.Meshes[0].NativeView = altCube->CaptureView();
                const auto inv = reflSvc.EvaluateInvalidation(stamps1, renderer.ComputeDynamicCaptureDomainStamps(altMeshWorld, manualProbe, schedSettings));
                RENDER_CHECK(inv.AssetChanged && !inv.TransformChanged && !inv.GeometryChanged && !inv.MaterialChanged,
                    "M15-U5-21.5: mesh NativeView asset swap triggers AssetChanged");
            }
            {
                RenderWorld revWorld = u5dWorld;
                revWorld.SceneRevision = 701;
                const auto inv = reflSvc.EvaluateInvalidation(stamps1, renderer.ComputeDynamicCaptureDomainStamps(revWorld, manualProbe, schedSettings));
                RENDER_CHECK(inv.SceneRevisionChanged && !inv.GeometryChanged && !inv.MaterialChanged && !inv.LightingChanged,
                    "M15-U5-21.2: GPU renderer DomainStamps isolate SceneRevisionChanged");
            }
            {
                RenderWorld geomWorld = u5dWorld;
                geomWorld.Meshes[0].WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), {4.5f, 0.0f, 0.0f}), {0.5f, 8.0f, 8.0f});
                const auto inv = reflSvc.EvaluateInvalidation(stamps1, renderer.ComputeDynamicCaptureDomainStamps(geomWorld, manualProbe, schedSettings));
                RENDER_CHECK(inv.GeometryChanged && !inv.TransformChanged && !inv.MaterialChanged && !inv.LightingChanged,
                    "M15-U5-21.3: GPU renderer DomainStamps isolate mesh transform GeometryChanged");
            }
            {
                RenderWorld lightWorld = u5dWorld;
                RenderPointLight pt{};
                pt.EntityID = 4999; pt.Position = {2.0f, 2.0f, 0.0f}; pt.Intensity = 15.0f;
                lightWorld.PointLights.push_back(pt);
                const auto inv = reflSvc.EvaluateInvalidation(stamps1, renderer.ComputeDynamicCaptureDomainStamps(lightWorld, manualProbe, schedSettings));
                RENDER_CHECK(inv.LightingChanged && !inv.TransformChanged && !inv.GeometryChanged && !inv.MaterialChanged,
                    "M15-U5-21.4: GPU renderer DomainStamps isolate point-light LightingChanged");
            }

            // Edit material (Red -> Green) on Manual probe -> retains gen1 + flags Stale without auto-capturing.
            beaconMat->GetDesc().EmissiveColor = {0.0f, 10.0f, 0.0f};
            RENDER_CHECK(renderView(u5dWorld, color, depth, schedSettings), "U5-D manual edit render");
            const auto afterManualEdit = renderer.GetDynamicReflectionProbe(manualProbe);
            const auto staleStatus = renderer.GetDynamicReflectionProbeStatus(manualProbe);
            const auto& staleFrame = renderer.GetLastReflectionFrame();
            RENDER_CHECK(afterManualEdit && afterManualEdit->GenerationID == gen1->GenerationID &&
                         staleStatus && staleStatus->State == DynamicCaptureLifecycleState::Stale &&
                         staleStatus->Stale && staleStatus->HasGeneration && staleStatus->ManualRefreshRequired &&
                         staleStatus->PendingInvalidation.MaterialChanged &&
                         staleFrame.PrefilterReady && staleFrame.CaptureStale && staleFrame.ActiveGenerationID == gen1->GenerationID,
                "M15-U5-22 / 23.3: edited Manual probe keeps gen1 alive while exposing Stale diagnostics");

            // Request incremental capture at MaxFacesPerFrame = 2 -> 3 frames (2/6 -> 4/6 -> Ready 6/6)
            SceneRenderSettings slicedSettings = schedSettings;
            slicedSettings.ReflectionCaptureMaxFacesPerFrame = 2u;
            slicedSettings.ReflectionCaptureMaxProbesPerFrame = 1u;
            renderer.RequestDynamicReflectionCapture(manualProbe);

            RENDER_CHECK(renderView(u5dWorld, color, depth, slicedSettings), "U5-D sliced frame 1");
            const auto pend1 = renderer.GetDynamicReflectionProbeStatus(manualProbe);
            const auto& frame1 = renderer.GetLastReflectionFrame();
            RENDER_CHECK(pend1 && pend1->State == DynamicCaptureLifecycleState::Pending && pend1->Pending &&
                         pend1->CompletedFaces == 2u && pend1->HasGeneration &&
                         renderer.GetDynamicReflectionProbe(manualProbe)->GenerationID == gen1->GenerationID &&
                         frame1.PrefilterReady && frame1.CapturePending && frame1.CaptureCompletedFaces == 2u &&
                         renderer.GetDynamicReflectionCaptureCacheStats().LastScheduledFaces == 2u,
                "M15-U5-23.1 / 23.2: MaxFacesPerFrame=2 runs 2/6 faces on frame 1 while retaining last-good gen1");

            RENDER_CHECK(renderView(u5dWorld, color, depth, slicedSettings), "U5-D sliced frame 2");
            const auto pend2 = renderer.GetDynamicReflectionProbeStatus(manualProbe);
            RENDER_CHECK(pend2 && pend2->State == DynamicCaptureLifecycleState::Pending && pend2->Pending &&
                         pend2->CompletedFaces == 4u &&
                         renderer.GetDynamicReflectionProbe(manualProbe)->GenerationID == gen1->GenerationID,
                "M15-U5-23.1 / 23.2: frame 2 advances pending capture to 4/6 faces without publishing partial cubemap");

            // Inject transient failure mid-job on face 4 (while CompletedFaces == 4) -> aborts job cleanly, retains gen1!
            {
                bool hitFault = false;
                {
                    ScopedRHIFault fault([&](std::string_view op) -> std::optional<nri::Result> {
                        if (op == "BeginCommandBuffer" && !hitFault) {
                            hitFault = true;
                            return nri::Result::OUT_OF_MEMORY;
                        }
                        return {};
                    });
                    RENDER_CHECK(renderView(u5dWorld, color, depth, slicedSettings) && hitFault,
                        "U5-D mid-job fault frame executes main pass cleanly after face-4 failure");
                }
                const auto retainedAfterFail = renderer.GetDynamicReflectionProbe(manualProbe);
                const auto failStatus = renderer.GetDynamicReflectionProbeStatus(manualProbe);
                const auto& failFrame = renderer.GetLastReflectionFrame();
                RENDER_CHECK(retainedAfterFail && retainedAfterFail->GenerationID == gen1->GenerationID &&
                             failStatus && failStatus->State == DynamicCaptureLifecycleState::Failed &&
                             failStatus->Failed && failStatus->HasGeneration && failStatus->Stale &&
                             failStatus->CompletedFaces == 4u &&
                             failStatus->FailureCount >= 1u &&
                             renderer.GetDynamicReflectionCaptureCacheStats().PendingJobCount == 0u &&
                             failFrame.PrefilterReady && failFrame.CaptureFailed && failFrame.CaptureStale &&
                             failFrame.ProbeGeneration == gen1->Cubemap->GetHandle(),
                    "M15-U5-23.4 / 24: mid-capture RHI failure at face 4 aborts pending job, retains gen1, and reports Failed+Stale");

                std::vector<glm::vec4> intactPixels;
                RENDER_CHECK(ReadCubeFaceRGBA16(*retainedAfterFail->Cubemap, 0u, intactPixels) &&
                             AverageRegion(intactPixels, 32u, 14u, 14u, 18u, 18u).r > 2.0f,
                    "M15-U5-24: retained gen1 still holds original HDR red radiance after mid-job abort");
            }

            // Finish recovery with new manual request across 3 frames -> completes Blue radiance (gen2 > gen1)
            beaconMat->GetDesc().EmissiveColor = {0.0f, 0.0f, 10.0f};
            renderer.RequestDynamicReflectionCapture(manualProbe);
            RENDER_CHECK(renderView(u5dWorld, color, depth, slicedSettings) &&
                         renderView(u5dWorld, color, depth, slicedSettings) &&
                         renderView(u5dWorld, color, depth, slicedSettings),
                "U5-D recover incremental 3 frames");
            const auto gen2 = renderer.GetDynamicReflectionProbe(manualProbe);
            const auto readyStatus = renderer.GetDynamicReflectionProbeStatus(manualProbe);
            RENDER_CHECK(gen2 && gen2->GenerationID > gen1->GenerationID &&
                         readyStatus && readyStatus->State == DynamicCaptureLifecycleState::Ready &&
                         !readyStatus->Pending && !readyStatus->Stale && !readyStatus->Failed &&
                         readyStatus->CompletedFaces == 6u,
                "M15-U5-22 / 23.1: explicit recapture completes cleanly to Ready after mid-job failure");
            std::vector<glm::vec4> recoveredPixels;
            RENDER_CHECK(ReadCubeFaceRGBA16(*gen2->Cubemap, 0u, recoveredPixels) &&
                         AverageRegion(recoveredPixels, 32u, 14u, 14u, 18u, 18u).b > 2.0f &&
                         AverageRegion(recoveredPixels, 32u, 14u, 14u, 18u, 18u).r < 0.2f,
                "M15-U5-21.3 / 22: published gen2 reflects updated Blue emissive radiance");

            // 6B. Main viewport resize MUST NOT invalidate world-space capture; probe resolution resize MUST reallocate (26.2)
            RHITexture resizedColor, resizedDepth;
            RENDER_CHECK(resizedColor.Init(RHITextureDesc::RenderTarget2D(96, 48)) &&
                         resizedDepth.Init(RHITextureDesc::DepthStencil2D(96, 48)),
                "U5-D resized main viewport targets");
            RENDER_CHECK(renderView(u5dWorld, resizedColor, resizedDepth, schedSettings), "U5-D main viewport resize render");
            RENDER_CHECK(renderer.GetDynamicReflectionProbe(manualProbe)->GenerationID == gen2->GenerationID,
                "M15-U5-26.2: resizing main viewport does NOT invalidate cached dynamic probe");

            const uint64_t bytes32 = ReflectionCaptureContract::EstimateDynamicProbeCacheBytes(32u, 5u);
            const uint64_t bytes64 = ReflectionCaptureContract::EstimateDynamicProbeCacheBytes(64u, 5u);
            RENDER_CHECK(gen2->CachedBytes == bytes32 && bytes64 > bytes32,
                "M15-U5-25 / 26.2: dynamic probe tracks exact cache footprint");

            u5dWorld.ReflectionProbes[0].CaptureMode = ReflectionProbeCaptureMode::Automatic;
            u5dWorld.ReflectionProbes[0].CaptureResolution = 64u;
            RENDER_CHECK(renderView(u5dWorld, resizedColor, resizedDepth, schedSettings), "U5-D probe resolution resize 32->64");
            const auto gen3 = renderer.GetDynamicReflectionProbe(u5dWorld.ReflectionProbes[0]);
            RENDER_CHECK(gen3 && gen3->GenerationID > gen2->GenerationID && gen3->Resolution == 64u &&
                         gen3->CachedBytes == bytes64 && gen3->Cubemap->GetWidth() == 64u,
                "M15-U5-26.2: increasing CaptureResolution reallocates cubemap and updates CachedBytes");
        }

        // 6C. Multi-probe frame bound (23.1) + Cache Memory Budget & LRU Eviction (25)
        {
            renderer.ClearDynamicReflectionProbes();
            const uint64_t perProbeBytes = ReflectionCaptureContract::EstimateDynamicProbeCacheBytes(32u, 5u);

            RenderWorld cacheWorld;
            cacheWorld.SceneRevision = 800;
            cacheWorld.Skybox.Authored = true;
            cacheWorld.Skybox.Enabled = false;
            cacheWorld.Meshes.push_back(MakeBoxInstance(
                5000, cubeView, MakeEmissiveMaterial({4.0f, 4.0f, 4.0f}, 1.0f), {6.0f, 0.0f, 0.0f}, {0.5f, 6.0f, 6.0f}));

            RenderReflectionProbe probe1{};
            probe1.EntityID = 5001; probe1.Position = {0.0f, 0.0f, 0.0f}; probe1.BoxExtents = {8.0f, 8.0f, 8.0f};
            probe1.DynamicCapture = true; probe1.CaptureResolution = 32u; probe1.CaptureShadows = false;

            RenderReflectionProbe probe2{};
            probe2.EntityID = 5002; probe2.Position = {0.0f, 0.0f, 40.0f}; probe2.BoxExtents = {8.0f, 8.0f, 8.0f};
            probe2.DynamicCapture = true; probe2.CaptureResolution = 32u; probe2.CaptureShadows = false;

            RenderReflectionProbe probe3{};
            probe3.EntityID = 5003; probe3.Position = {0.0f, 0.0f, 80.0f}; probe3.BoxExtents = {8.0f, 8.0f, 8.0f};
            probe3.DynamicCapture = true; probe3.CaptureResolution = 32u; probe3.CaptureShadows = false;

            cacheWorld.ReflectionProbes = {probe1, probe2};
            SceneRenderSettings multiSettings = schedSettings;
            multiSettings.ReflectionCaptureMaxProbesPerFrame = 1u;
            multiSettings.ReflectionCaptureMaxFacesPerFrame = 6u;
            multiSettings.ReflectionCaptureCacheBudgetBytes = 2ull * perProbeBytes;

            // Camera at origin: selects probe1 first (MaxProbesPerFrame=1 -> ONLY probe1 captures on frame 1!)
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              {0.0f, 0.0f, 1.0f});
            RENDER_CHECK(renderView(cacheWorld, color, depth, multiSettings), "U5-D multi-probe frame 1");
            RENDER_CHECK(renderer.GetDynamicReflectionProbe(probe1) != nullptr &&
                         renderer.GetDynamicReflectionProbe(probe2) == nullptr &&
                         renderer.GetDynamicReflectionCaptureCacheStats().LastScheduledProbes == 1u,
                "M15-U5-23.1: MaxProbesPerFrame=1 captures selected probe1 and defers probe2 to next frame");

            // Frame 2: probe1 ready, now probe2 captures!
            RENDER_CHECK(renderView(cacheWorld, color, depth, multiSettings), "U5-D multi-probe frame 2");
            RENDER_CHECK(renderer.GetDynamicReflectionProbe(probe1) != nullptr &&
                         renderer.GetDynamicReflectionProbe(probe2) != nullptr &&
                         renderer.GetDynamicReflectionCaptureCacheStats().CachedProbeCount == 2u &&
                         renderer.GetDynamicReflectionCaptureCacheStats().UsedBytes == 2ull * perProbeBytes,
                "M15-U5-23.1 / 25: both probes fit within 2-probe budget on subsequent frame");

            // Move camera near probe2 so probe2's LastUsedSequence becomes newer than probe1, then add probe3!
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 41.0f), glm::vec3(0.0f, 0.0f, 40.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              {0.0f, 0.0f, 41.0f});
            RENDER_CHECK(renderView(cacheWorld, color, depth, multiSettings), "U5-D touch probe2 LRU");
            cacheWorld.ReflectionProbes.push_back(probe3);
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 81.0f), glm::vec3(0.0f, 0.0f, 80.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              {0.0f, 0.0f, 81.0f});
            RENDER_CHECK(renderView(cacheWorld, color, depth, multiSettings), "U5-D probe3 LRU eviction");
            const auto stats = renderer.GetDynamicReflectionCaptureCacheStats();
            RENDER_CHECK(stats.CachedProbeCount == 2u &&
                         stats.UsedBytes == 2ull * perProbeBytes &&
                         stats.EvictionCount >= 1u &&
                         renderer.GetDynamicReflectionProbe(probe1) == nullptr &&
                         renderer.GetDynamicReflectionProbe(probe2) != nullptr &&
                         renderer.GetDynamicReflectionProbe(probe3) != nullptr,
                "M15-U5-25: exceeding cache budget evicts least-recently-used probe1 while retaining probe2 and probe3");

            // Single capture exceeding tight budget fails safely without disturbing cached probe2 & probe3
            renderer.SetDynamicReflectionCaptureCacheBudget(perProbeBytes / 2ull);
            RenderReflectionProbe probe4{};
            probe4.EntityID = 5004; probe4.Position = {0.0f, 0.0f, 120.0f}; probe4.DynamicCapture = true; probe4.CaptureResolution = 32u;
            const auto overBudgetRes = renderer.CaptureDynamicReflectionProbe(cacheWorld, probe4, multiSettings);
            RENDER_CHECK(!overBudgetRes.Success &&
                         overBudgetRes.Diagnostic.find("DYNAMIC_PROBE_BUDGET") != std::string::npos &&
                         renderer.GetDynamicReflectionProbe(probe2) != nullptr &&
                         renderer.GetDynamicReflectionProbe(probe3) != nullptr &&
                         renderer.GetDynamicReflectionProbe(probe4) == nullptr,
                "M15-U5-25: single probe exceeding cache budget fails cleanly without evicting resident cache");
            renderer.ClearDynamicCaptureBudgetOverride();
        }

        // 6D. Scene Replacement with ECS Scene instances & Clear() lifetime epoch (26.4)
        {
            renderer.ClearDynamicReflectionProbes();
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0.0f, 0.0f, 2.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                              {0.0f, 0.0f, 2.5f});

            auto makeBeaconScene = [&](UUID probeUuid, const glm::vec3& colorVal) {
                auto sc = std::make_unique<Scene>();
                auto pEnt = sc->CreateEntityWithUUID(probeUuid, "SceneProbe");
                auto& rpc = pEnt.AddComponent<ReflectionProbeComponent>();
                rpc.DynamicCapture = true;
                rpc.CaptureResolution = 32u;
                rpc.CaptureShadows = false;
                rpc.BoxExtents = {20.0f, 20.0f, 20.0f};

                auto mEnt = sc->CreateEntity("BeaconWall");
                mEnt.GetComponent<TransformComponent>().Translation = {5.0f, 0.0f, 0.0f};
                mEnt.GetComponent<TransformComponent>().Scale = {0.5f, 8.0f, 8.0f};
                auto& mc = mEnt.AddComponent<MeshInstance3D>();
                mc.RHIMeshAsset = cubeMesh;
                mc.RHIMaterialAsset = MakeEmissiveMaterial(colorVal, 1.0f);
                return sc;
            };

            auto sceneA = makeBeaconScene(700001ULL, {10.0f, 0.0f, 0.0f});
            RenderWorld worldA = ExtractRenderWorld(*sceneA);
            worldA.Skybox.Authored = true;
            worldA.Skybox.Enabled = false;
            RENDER_CHECK(worldA.ReflectionProbes.size() == 1 && renderView(worldA, color, depth, schedSettings),
                "U5-D sceneA initial render");
            const auto genA = renderer.GetDynamicReflectionProbe(worldA.ReflectionProbes[0]);
            RENDER_CHECK(genA && genA->SceneInstanceID == sceneA->GetInstanceId(),
                "M15-U5-26.4: Scene A probe captured with SceneInstanceID");

            // Scene B reuses ECS entity index 0 but has distinct SceneInstanceID + StableUUID + Green beacon
            auto sceneB = makeBeaconScene(700002ULL, {0.0f, 10.0f, 0.0f});
            RenderWorld worldB = ExtractRenderWorld(*sceneB);
            worldB.Skybox.Authored = true;
            worldB.Skybox.Enabled = false;
            RENDER_CHECK(worldB.ReflectionProbes.size() == 1 && renderView(worldB, color, depth, schedSettings),
                "U5-D sceneB replacement render");
            const auto genB = renderer.GetDynamicReflectionProbe(worldB.ReflectionProbes[0]);
            const auto statsAfterRep = renderer.GetDynamicReflectionCaptureCacheStats();
            RENDER_CHECK(genB && genB->SceneInstanceID == sceneB->GetInstanceId() &&
                         !renderer.GetDynamicReflectionProbe(worldA.ReflectionProbes[0]) &&
                         statsAfterRep.CachedProbeCount == 1u && statsAfterRep.EvictionCount >= 1u,
                "M15-U5-26.4: replacing Scene A with Scene B evicts retired scene entries without entity-ID aliasing");
            std::vector<glm::vec4> sceneBPixels;
            RENDER_CHECK(ReadCubeFaceRGBA16(*genB->Cubemap, 0u, sceneBPixels) &&
                         AverageRegion(sceneBPixels, 32u, 14u, 14u, 18u, 18u).g > 2.0f &&
                         AverageRegion(sceneBPixels, 32u, 14u, 14u, 18u, 18u).r < 0.2f,
                "M15-U5-26.4: replaced Scene B capture contains Green radiance with zero Scene A leakage");

            // Clearing sceneB advances SceneLifetimeEpoch -> purges epoch 1 capture and recaptures epoch 2 cleanly
            sceneB->Clear();
            auto pEnt2 = sceneB->CreateEntityWithUUID(700002ULL, "SceneProbeAfterClear");
            auto& rpc2 = pEnt2.AddComponent<ReflectionProbeComponent>();
            rpc2.DynamicCapture = true;
            rpc2.CaptureResolution = 32u;
            rpc2.CaptureShadows = false;
            rpc2.BoxExtents = {20.0f, 20.0f, 20.0f};
            auto mEnt2 = sceneB->CreateEntity("BlueBeaconAfterClear");
            mEnt2.GetComponent<TransformComponent>().Translation = {5.0f, 0.0f, 0.0f};
            mEnt2.GetComponent<TransformComponent>().Scale = {0.5f, 8.0f, 8.0f};
            auto& mc2 = mEnt2.AddComponent<MeshInstance3D>();
            mc2.RHIMeshAsset = cubeMesh;
            mc2.RHIMaterialAsset = MakeEmissiveMaterial({0.0f, 0.0f, 10.0f}, 1.0f);

            RenderWorld worldB2 = ExtractRenderWorld(*sceneB);
            worldB2.Skybox.Authored = true;
            worldB2.Skybox.Enabled = false;
            RENDER_CHECK(worldB2.SceneLifetimeEpoch > worldB.SceneLifetimeEpoch &&
                         renderView(worldB2, color, depth, schedSettings),
                "U5-D cleared sceneB epoch 2 render");
            const auto genB2 = renderer.GetDynamicReflectionProbe(worldB2.ReflectionProbes[0]);
            std::vector<glm::vec4> clearedPixels;
            RENDER_CHECK(genB2 && genB2->SceneLifetimeEpoch == worldB2.SceneLifetimeEpoch &&
                         genB2->GenerationID > genB->GenerationID &&
                         ReadCubeFaceRGBA16(*genB2->Cubemap, 0u, clearedPixels) &&
                         AverageRegion(clearedPixels, 32u, 14u, 14u, 18u, 18u).b > 2.0f,
                "M15-U5-26.4: Scene::Clear() lifetime epoch increment invalidates prior scene capture and recaptures Blue radiance");
        }
    }

    // U5D-R1/R2: exercise budget recovery, staging footprint, and removed jobs through the real scheduler.
    {
        renderer.ClearDynamicReflectionProbes();
        renderer.ClearDynamicCaptureBudgetOverride();
        const uint64_t bytes = ReflectionCaptureContract::EstimateDynamicProbeCacheBytes(32u, 5u);
        RenderWorld w;
        w.SceneInstanceID = 91001;
        w.SceneLifetimeEpoch = 1;
        w.Skybox.Authored = true;
        w.Skybox.Enabled = false;
        RenderReflectionProbe p;
        p.EntityID = 91001;
        p.DynamicCapture = true;
        p.CaptureResolution = 32;
        p.CaptureShadows = false;
        p.BoxExtents = glm::vec3(20.0f);
        w.ReflectionProbes = {p};
        RHITexture c, d;
        RENDER_CHECK(c.Init(RHITextureDesc::RenderTarget2D(32, 32)) &&
                     d.Init(RHITextureDesc::DepthStencil2D(32, 32)), "U5D regression targets");
        PerspectiveCamera camera(55.0f, 1.0f, 0.1f, 100.0f);
        SceneRenderSettings s;
        s.EnableSkybox = false;
        s.EnableShadows = false;
        s.EnableParticles = false;
        auto draw = [&]() {
            renderer.RenderWorldView(w, camera, &c, &d, s);
            return renderer.WasLastRenderSuccessful() && device.WaitForCompletion(renderer.GetLastCompletion());
        };

        s.ReflectionCaptureCacheBudgetBytes = bytes / 2;
        RENDER_CHECK(draw() && !renderer.GetDynamicReflectionProbe(p), "U5D-R1 cold over-budget failure");
        s.ReflectionCaptureCacheBudgetBytes = bytes;
        RENDER_CHECK(draw() && renderer.GetDynamicReflectionProbe(p),
            "U5D-R1 increasing budget retries an unchanged failed probe automatically");
        RENDER_CHECK(renderer.GetDynamicReflectionCaptureCacheStats().BudgetBytes == bytes,
            "U5D-R1 diagnostics report the actual scene-settings budget");

        const auto lastGood = renderer.GetDynamicReflectionProbe(p);
        ++w.SceneRevision;
        s.ReflectionCaptureMaxFacesPerFrame = 2;
        s.ReflectionCaptureCacheBudgetBytes = bytes + bytes / 2;
        RENDER_CHECK(draw(), "U5D-R1 tight-budget rebuild frame");
        auto stats = renderer.GetDynamicReflectionCaptureCacheStats();
        RENDER_CHECK(stats.PendingJobCount == 0 && stats.UsedBytes == bytes &&
                     renderer.GetDynamicReflectionProbe(p)->GenerationID == lastGood->GenerationID,
            "U5D-R1 staging must include the retained generation in its budget and preserve last-good on rejection");

        s.ReflectionCaptureCacheBudgetBytes = 2 * bytes;
        RENDER_CHECK(draw(), "U5D-R2 begin partial rebuild");
        stats = renderer.GetDynamicReflectionCaptureCacheStats();
        RENDER_CHECK(stats.PendingJobCount == 1 && stats.UsedBytes == 2 * bytes,
            "U5D-R1 a permitted partial rebuild accounts for resident and candidate cubemaps");
        w.ReflectionProbes.clear();
        RENDER_CHECK(draw(), "U5D-R2 remove probe while capture is pending");
        stats = renderer.GetDynamicReflectionCaptureCacheStats();
        RENDER_CHECK(stats.PendingJobCount == 0 && stats.CachedProbeCount == 0 && stats.UsedBytes == 0 &&
                     !renderer.GetDynamicReflectionProbeStatus(p),
            "U5D-R2 removing a probe retires its job, cache, and diagnostics");

        s.ReflectionCaptureCacheBudgetBytes = bytes;
        s.ReflectionCaptureMaxFacesPerFrame = 6;
        auto background = p;
        background.EntityID += 1;
        background.Position = {0.0f, 0.0f, 80.0f};
        w.ReflectionProbes = {p, background};
        RENDER_CHECK(draw() && renderer.GetDynamicReflectionProbe(p), "U5D-R3 selected probe admission");
        const auto selectedGeneration = renderer.GetDynamicReflectionProbe(p)->GenerationID;
        RENDER_CHECK(draw() && draw() && renderer.GetDynamicReflectionProbe(p) &&
                     renderer.GetDynamicReflectionProbe(p)->GenerationID == selectedGeneration &&
                     renderer.GetLastReflectionFrame().PrefilterReady &&
                     renderer.GetDynamicReflectionCaptureCacheStats().LastScheduledFaces == 0,
            "U5D-R3 background probes cannot evict the selected reflection and cause recapture thrashing");

        // Retire cold jobs too, when there has never been a published generation.
        renderer.ClearDynamicReflectionProbes();
        s.ReflectionCaptureMaxFacesPerFrame = 2;
        w.ReflectionProbes = {p};
        RENDER_CHECK(draw() && renderer.GetDynamicReflectionCaptureCacheStats().PendingJobCount == 1,
            "U5D-R2 cold partial capture");
        w.ReflectionProbes[0].DynamicCapture = false;
        RENDER_CHECK(draw() && renderer.GetDynamicReflectionCaptureCacheStats().UsedBytes == 0 &&
                     !renderer.GetDynamicReflectionProbeStatus(p),
            "U5D-R2 switching away from dynamic capture retires a cold pending job");

        // A one-off policy must not masquerade as a ready default-policy capture.
        w.ReflectionProbes = {p};
        s.ReflectionCaptureMaxFacesPerFrame = 6;
        s.ReflectionCaptureCacheBudgetBytes = 2 * bytes;
        ReflectionCaptureContract::ReflectionCapturePolicy overridePolicy;
        overridePolicy.Resolution = p.CaptureResolution;
        overridePolicy.NearPlane = 0.5f;
        const auto overridden = renderer.CaptureDynamicReflectionProbe(w, p, s, &overridePolicy);
        RENDER_CHECK(overridden.Success && draw() &&
                     renderer.GetDynamicReflectionProbe(p)->GenerationID > overridden.PublishedGenerationID,
            "U5D-R4 an automatic probe replaces a one-off policy override with the default capture policy");

        // Leave a main submission outstanding: capture must synchronize it before
        // touching its shared buffers, including when that first wait fails.
        renderer.RenderWorldView(w, camera, &c, &d, s);
        RENDER_CHECK(renderer.WasLastRenderSuccessful(), "U5D-R5 main render without caller wait");
        const auto beforeWaitFailure = renderer.GetDynamicReflectionProbe(p)->GenerationID;
        ++w.SceneRevision;
        bool failedWait = false;
        bool allocatedBeforeWait = false;
        {
            ScopedRHIFault fault([&](std::string_view op) -> std::optional<nri::Result> {
                if (op == "CreateTexture" && !failedWait) allocatedBeforeWait = true;
                if (op == "WaitFence" && !failedWait) {
                    failedWait = true;
                    return nri::Result::FAILURE;
                }
                return {};
            });
            RENDER_CHECK(draw(), "U5D-R5 main rendering recovers after a single capture synchronization failure");
        }
        RENDER_CHECK(failedWait && !allocatedBeforeWait &&
                     renderer.GetDynamicReflectionCaptureCacheStats().LastScheduledFaces == 0 &&
                     renderer.GetDynamicReflectionProbe(p)->GenerationID == beforeWaitFailure,
            "U5D-R5 failed prior-main synchronization defers capture before any allocation or publication");
        RENDER_CHECK(draw() && renderer.GetDynamicReflectionProbe(p)->GenerationID > beforeWaitFailure,
            "U5D-R5 deferred capture retries on the next render after synchronization recovers");
    }

    RENDER_CHECK(RunM15MultiProbeBlendTests(renderer, output),
        "M15-U5-E: multi-probe blend, per-probe parallax, energy policy & P5 exit checks pass");

    renderer.ClearDynamicReflectionProbes();
    RENDER_CHECK(device.WaitIdle(), "U5-B wait idle at cleanup");
    RENDER_CHECK(Axle::RenderTests::GetRHIValidationErrorCount() == 0, "U5-B zero RHI validation errors");
    std::cout << "[M15ReflectionCaptureGPU] All U5-B/U5-C/U5-D/U5-E native D3D12/Vulkan checks passed!\n";
    return true;
}

bool RunM15ReflectionDeviceResetTests(const RHIDeviceDesc& initDesc) {
    auto& device = RHIDevice::Get();
    const uint64_t epochBefore = device.GetEpoch();

    auto buildSceneWorld = [](const glm::vec3& emissive) {
        RenderWorld w;
        w.SceneRevision = 900;
        w.Skybox.Authored = true;
        w.Skybox.Enabled = false;
        auto cube = RHIMesh::CreateCube(1.0f);
        if (cube && cube->IsValid()) {
            w.Meshes.push_back(MakeBoxInstance(
                6001, cube->CaptureView(), MakeEmissiveMaterial(emissive, 1.0f), {5.0f, 0.0f, 0.0f}, {0.5f, 8.0f, 8.0f}));
        }
        return w;
    };

    RenderReflectionProbe resetProbe{};
    resetProbe.EntityID = 6000;
    resetProbe.DynamicCapture = true;
    resetProbe.CaptureResolution = 32u;
    resetProbe.CaptureShadows = false;
    resetProbe.BoxExtents = {20.0f, 20.0f, 20.0f};

    std::shared_ptr<const DynamicReflectionProbeGeneration> staleGeneration;
    {
        RHISceneRenderer resetRenderer;
        RENDER_CHECK(resetRenderer.Init(device, false), "M15-U5-26.3: init renderer on epochBefore");
        RenderWorld world1 = buildSceneWorld({10.0f, 0.0f, 0.0f});
        RENDER_CHECK(!world1.Meshes.empty(), "M15-U5-26.3: epochBefore cube created");
        const auto cap1 = resetRenderer.CaptureDynamicReflectionProbe(world1, resetProbe);
        RENDER_CHECK(cap1.Success, "M15-U5-26.3: initial capture before device reset succeeds");
        staleGeneration = resetRenderer.GetDynamicReflectionProbe(resetProbe);
        RENDER_CHECK(staleGeneration &&
                     staleGeneration->DeviceEpoch == epochBefore &&
                     staleGeneration->IsValidForDevice(epochBefore),
            "M15-U5-26.3: initial capture binds to epochBefore");
        resetRenderer.Shutdown();
    }

    RENDER_CHECK(device.WaitIdle(), "M15-U5-26.3 wait idle before shutdown");
    device.Shutdown();
    RENDER_CHECK(!staleGeneration->Cubemap->IsValid() &&
                 !staleGeneration->IsValidForDevice(epochBefore),
        "M15-U5-26.3: shutting down RHI device invalidates prior dynamic probe generation");

    RENDER_CHECK(device.Init(initDesc), "M15-U5-26.3: re-initialize RHI device");
    const uint64_t epochAfter = device.GetEpoch();
    RENDER_CHECK(epochAfter > epochBefore && !staleGeneration->IsValidForDevice(epochAfter),
        "M15-U5-26.3: new device epoch rejects stale generation from prior epoch");

    {
        RHISceneRenderer resetRenderer;
        RENDER_CHECK(resetRenderer.Init(device, false), "M15-U5-26.3: init renderer on epochAfter");
        RenderWorld world2 = buildSceneWorld({0.0f, 10.0f, 0.0f});
        RENDER_CHECK(!world2.Meshes.empty(), "M15-U5-26.3: epochAfter cube created");
        const auto cap2 = resetRenderer.CaptureDynamicReflectionProbe(world2, resetProbe);
        RENDER_CHECK(cap2.Success, "M15-U5-26.3: capture succeeds after device reset");
        const auto newGeneration = resetRenderer.GetDynamicReflectionProbe(resetProbe);
        std::vector<glm::vec4> pixels;
        RENDER_CHECK(newGeneration &&
                     newGeneration->DeviceEpoch == epochAfter &&
                     newGeneration->IsValidForDevice(epochAfter) &&
                     newGeneration->Cubemap->GetHandle().Epoch == epochAfter &&
                     ReadCubeFaceRGBA16(*newGeneration->Cubemap, 0u, pixels) &&
                     AverageRegion(pixels, 32u, 14u, 14u, 18u, 18u).g > 2.0f,
            "M15-U5-26.3: post-reset capture runs cleanly on new epoch with zero validation errors");
        resetRenderer.Shutdown();
    }

    RENDER_CHECK(device.WaitIdle() && Axle::RenderTests::GetRHIValidationErrorCount() == 0,
        "M15-U5-26.3: zero RHI validation errors across device reset");
    return true;
}
