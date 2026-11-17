#include "OcclusionTestSupport.hpp"
#include "RHIReadback.hpp"
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace Axle;
using namespace Axle::RHI;
using Axle::RenderTests::Read;
using Axle::RenderTests::Upload;

#ifndef AXLE_HIZ_SHADER_PATH
#define AXLE_HIZ_SHADER_PATH "Engine/Resources/Shaders/HiZ.hlsl"
#endif

namespace {
    std::vector<float> ExtractScalarChannel(const std::vector<glm::vec4>& rgba) {
        std::vector<float> values;
        values.reserve(rgba.size());
        for (const auto& p : rgba) values.push_back(p.x);
        return values;
    }

    bool ReadColor8(RHITexture& texture, std::vector<glm::vec4>& pixels) {
        if (texture.GetFormat() != RHITextureFormat::RGBA8_UNORM) {
            return Read(texture, pixels);
        }
        const std::vector<uint8_t> bytes = Axle::RenderTests::ReadTexture(texture);
        const size_t count = size_t(texture.GetWidth()) * texture.GetHeight();
        if (bytes.size() != count * 4) return false;
        pixels.resize(count);
        for (size_t i = 0; i < count; ++i) {
            pixels[i] = glm::vec4(bytes[i * 4 + 0], bytes[i * 4 + 1], bytes[i * 4 + 2], bytes[i * 4 + 3]) / 255.0f;
        }
        return true;
    }

    bool WriteDisplayPPM(const std::filesystem::path& path, const std::vector<glm::vec4>& rgba,
                         uint32_t width, uint32_t height, bool isHDR = false) {
        std::ofstream image(path, std::ios::binary);
        if (!image.is_open()) return false;
        image << "P6\n" << width << ' ' << height << "\n255\n";
        for (const auto& p : rgba) {
            for (int c = 0; c < 3; ++c) {
                float v = std::max(0.0f, p[c]);
                if (isHDR) {
                    v = std::clamp((v * (2.51f * v + 0.03f)) / (v * (2.43f * v + 0.59f) + 0.14f), 0.0f, 1.0f);
                    v = std::pow(v, 1.0f / 2.2f);
                } else {
                    v = std::clamp(v, 0.0f, 1.0f);
                }
                const auto byte = static_cast<unsigned char>(std::lround(v * 255.0f));
                image.write(reinterpret_cast<const char*>(&byte), 1);
            }
        }
        return bool(image);
    }
}

bool RunM15HiZTests(RHISceneRenderer& renderer, const std::filesystem::path& output) {
    auto& device = RHIDevice::Get();
    std::ofstream evidence(output / "M15_U5A_hiz.txt");
    RENDER_CHECK(evidence.is_open(), "U5-A evidence log creation");

    // -------------------------------------------------------------------------
    // 1. Compile production HiZ.hlsl and verify standalone GPU reduction vs CPU
    //    reference at canonical 4x4, 1x1, 3x5 (including trailing odd corner),
    //    127x63, and 1919x1079 (11 levels down to 1x1).
    // -------------------------------------------------------------------------
    {
        std::ifstream file(AXLE_HIZ_SHADER_PATH);
        std::string shaderSource((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        RENDER_CHECK(!shaderSource.empty(), "production HiZ.hlsl loaded");

        RHIShaderCompileDesc compileDesc{};
        compileDesc.Source = shaderSource;
        compileDesc.SourcePath = AXLE_HIZ_SHADER_PATH;
        compileDesc.EntryPoint = "CSMain";
        compileDesc.Stage = RHIShaderStage::Compute;
        compileDesc.HLSLVersion = "2021";
        compileDesc.ShaderModel = "6_0";
        compileDesc.Format = device.GetBackend() == RHIType::Vulkan ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        compileDesc.Optimization = true;
        const auto compiled = RHIShaderCompiler::Get().Compile(compileDesc);
        RENDER_CHECK(compiled.Success, "production HiZ.hlsl CSMain compiles cleanly");

        nri::RootDescriptorDesc root{};
        root.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;

        nri::DescriptorRangeDesc texRanges[2]{};
        texRanges[0].baseRegisterIndex = 0; texRanges[0].descriptorNum = 1;
        texRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        texRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        texRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        texRanges[1].baseRegisterIndex = 0; texRanges[1].descriptorNum = 1;
        texRanges[1].descriptorType = nri::DescriptorType::SAMPLER;
        texRanges[1].shaderStages = nri::StageBits::COMPUTE_SHADER;
        texRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorRangeDesc storageRange{};
        storageRange.baseRegisterIndex = 0; storageRange.descriptorNum = 1;
        storageRange.descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        storageRange.shaderStages = nri::StageBits::COMPUTE_SHADER;
        storageRange.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc sets[2]{};
        sets[0].registerSpace = 1; sets[0].ranges = texRanges; sets[0].rangeNum = 2;
        sets[1].registerSpace = 2; sets[1].ranges = &storageRange; sets[1].rangeNum = 1;

        nri::PipelineLayoutDesc layoutDesc{};
        layoutDesc.rootDescriptors = &root; layoutDesc.rootDescriptorNum = 1;
        layoutDesc.descriptorSets = sets; layoutDesc.descriptorSetNum = 2;
        layoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        RHIPipelineLayout layout;
        RHIPipelineCache cache;
        RHISampler sampler;
        RENDER_CHECK(layout.Init(layoutDesc) && sampler.Init(RHISamplerDesc::Point(RHIAddressMode::ClampToEdge)),
            "standalone Hi-Z pipeline layout and sampler");

        RHIComputePipelineDesc pipeDesc{};
        pipeDesc.Layout = &layout; pipeDesc.ComputeShader = compiled.Bytecode;
        auto* pipeline = cache.GetOrCreateComputePipeline(pipeDesc);
        RENDER_CHECK(pipeline != nullptr, "standalone Hi-Z compute pipeline");

        struct StandaloneCase { uint32_t W, H; bool BorderCornerBlocker; const char* Label; };
        const StandaloneCase cases[] = {
            {4, 4, false, "canonical-4x4"},
            {1, 1, false, "minimal-1x1"},
            {3, 5, false, "odd-3x5"},
            {3, 5, true,  "odd-3x5-border"},
            {127, 63, true, "odd-127x63-border"},
            {1919, 1079, false, "odd-1919x1079"},
            {1919, 1079, true, "odd-1919x1079-border"}
        };

        for (const auto& c : cases) {
            const uint32_t useful = HiZContract::ComputeUsefulLevelCount(c.W, c.H);
            std::vector<float> inputDepth(size_t(c.W) * size_t(c.H));
            if (c.W == 4 && c.H == 4) {
                inputDepth = {
                    0.90f, 0.80f, 0.70f, 0.60f,
                    0.50f, 0.40f, 0.30f, 0.20f,
                    0.15f, 0.25f, 0.35f, 0.45f,
                    0.05f, 0.55f, 0.65f, 0.75f
                };
            } else if (c.W == 1 && c.H == 1) {
                inputDepth = { 0.37f };
            } else {
                for (size_t i = 0; i < inputDepth.size(); ++i) {
                    inputDepth[i] = 0.20f + 0.75f * float((i * 73u + 17u) % 997u) / 996.0f;
                }
                if (c.BorderCornerBlocker) {
                    // Place nearest blocker strictly on the bottom-right trailing odd corner.
                    inputDepth[size_t(c.H - 1u) * c.W + (c.W - 1u)] = 0.015f;
                }
            }

            std::vector<std::vector<float>> refLevels;
            std::vector<HiZContract::PyramidExtent> refExtents;
            RENDER_CHECK(HiZContract::BuildMinReductionPyramid(inputDepth, c.W, c.H, useful, refLevels, refExtents),
                "CPU min reduction reference build");

            RHITexture sourceInput;
            auto srcDesc = RHITextureDesc::RenderTarget2D(c.W, c.H, RHITextureFormat::R32_FLOAT);
            srcDesc.Usage = RHITextureUsage::ShaderResource;
            RENDER_CHECK(sourceInput.Init(srcDesc) && Upload(sourceInput, inputDepth.data(), 4),
                "standalone Hi-Z source depth upload");

            std::vector<RHITexture> gpuLevels(useful);
            std::vector<RHIBuffer> cbvs(useful);
            for (uint32_t l = 0; l < useful; ++l) {
                const auto ext = refExtents[l];
                RENDER_CHECK(gpuLevels[l].Init(RHITextureDesc::StorageTexture2D(ext.Width, ext.Height, RHITextureFormat::R32_FLOAT)) &&
                    cbvs[l].Init(RHIBufferDesc::Constant(sizeof(RHIHiZConstants), RHIMemoryLocation::HostUpload)),
                    "standalone Hi-Z level target/CBV");
            }

            RHIDescriptorPool pool;
            RHIDescriptorPoolDesc poolDesc{};
            poolDesc.MaxDescriptorSets = useful * 2;
            poolDesc.MaxTextures = useful;
            poolDesc.MaxSamplers = useful;
            poolDesc.MaxStorageTextures = useful;
            poolDesc.MaxConstantBuffers = useful;
            RENDER_CHECK(pool.Init(poolDesc), "standalone Hi-Z descriptor pool");

            RHICommandBuffer cmd;
            RENDER_CHECK(cmd.Init() && cmd.Begin(), "standalone Hi-Z recording begin");
            for (uint32_t l = 0; l < useful; ++l) {
                RHITexture& srcTex = (l == 0) ? sourceInput : gpuLevels[l - 1];
                RHITexture& dstTex = gpuLevels[l];
                if (l > 0) {
                    cmd.TransitionTexture(srcTex.GetNative(), RHIResourceLayout::ShaderResourceStorage, RHIResourceLayout::ShaderResource);
                }
                cmd.TransitionTexture(dstTex.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResourceStorage);

                RHIHiZConstants constants{};
                constants.SourceSize = {float(srcTex.GetWidth()), float(srcTex.GetHeight()),
                                        1.0f / srcTex.GetWidth(), 1.0f / srcTex.GetHeight()};
                constants.DestinationSize = {float(dstTex.GetWidth()), float(dstTex.GetHeight()),
                                             1.0f / dstTex.GetWidth(), 1.0f / dstTex.GetHeight()};
                constants.Params = {l == 0 ? 1.0f : 0.0f, float(l), 0.0f, 0.0f};
                RENDER_CHECK(cbvs[l].UpdateData(&constants, sizeof(constants)), "standalone Hi-Z CBV update");

                auto* texSet = pool.AllocateDescriptorSet(layout, 0);
                auto* stoSet = pool.AllocateDescriptorSet(layout, 1);
                RENDER_CHECK(texSet && stoSet &&
                    pool.UpdateTexture(texSet, 0, 0, srcTex.GetSRV()) &&
                    pool.UpdateSampler(texSet, 1, 0, sampler.GetNative()) &&
                    pool.UpdateStorageTexture(stoSet, 0, 0, dstTex.GetUAV()),
                    "standalone Hi-Z descriptor set update");

                cmd.SetPipeline(pipeline);
                cmd.SetPipelineLayout(nri::BindPoint::COMPUTE, layout);
                cmd.SetRootDescriptor(0, cbvs[l].GetCBV(), 0, nri::BindPoint::COMPUTE);
                pool.Bind(cmd.GetNative());
                cmd.SetDescriptorSet(nri::BindPoint::COMPUTE, 0, texSet);
                nri::SetDescriptorSetDesc uavBinding{};
                uavBinding.bindPoint = nri::BindPoint::COMPUTE;
                uavBinding.setIndex = 1;
                uavBinding.descriptorSet = stoSet;
                device.GetNRICore().CmdSetDescriptorSet(*cmd.GetNative(), uavBinding);
                cmd.Dispatch((dstTex.GetWidth() + 7) / 8, (dstTex.GetHeight() + 7) / 8, 1);
            }
            RENDER_CHECK(cmd.End() && cmd.Submit() && device.WaitIdle(), "standalone Hi-Z submit");

            float worstError = 0.0f;
            for (uint32_t l = 0; l < useful; ++l) {
                std::vector<glm::vec4> rgba;
                RENDER_CHECK(Read(gpuLevels[l], rgba), "standalone Hi-Z level readback");
                RENDER_CHECK(rgba.size() == refLevels[l].size(), "standalone Hi-Z level texel count matches extent");
                for (size_t i = 0; i < rgba.size(); ++i) {
                    RENDER_CHECK(std::isfinite(rgba[i].x), "finite Hi-Z texel");
                    worstError = std::max(worstError, std::abs(rgba[i].x - refLevels[l][i]));
                }
                if (c.BorderCornerBlocker) {
                    RENDER_CHECK(std::abs(rgba.back().x - 0.015f) < 1e-6f,
                        "standalone conservative odd-border corner propagates exact 0.015 blocker to every level");
                }
            }
            RENDER_CHECK(worstError < 1e-6f, "standalone GPU Hi-Z matches CPU min reduction reference");
            RENDER_CHECK(GetRHIValidationErrorCount() == 0, "standalone Hi-Z zero native validation errors");
            evidence << "standalone=" << c.Label << " useful=" << useful
                     << " last_extent=" << refExtents.back().Width << "x" << refExtents.back().Height
                     << " max_error=" << worstError << '\n';
        }
    }

    // -------------------------------------------------------------------------
    // 2. Full RHISceneRenderer odd-resolution Hi-Z & SSR pipeline tests (1x1,
    //    3x5, 127x63, 1919x1079) reading back rendered HiZ0..HiZ(N-1) and
    //    comparing every texel against HiZContract::BuildMinReductionPyramid.
    // -------------------------------------------------------------------------
    {
        RenderWorld hizWorld;
        auto mesh = std::make_shared<RHIMesh>();
        std::vector<RHIVertex3D> verts(4);
        const glm::vec2 corners[] = {{-1.5f, -1.5f}, {0.25f, -1.5f}, {0.25f, 1.5f}, {-1.5f, 1.5f}};
        for (size_t i = 0; i < 4; ++i) {
            verts[i].Position = {corners[i].x, corners[i].y, 0.35f * corners[i].x};
            verts[i].Normal = glm::normalize(glm::vec3(-0.35f, 0.0f, 1.0f));
            verts[i].Tangent = {1, 0, 0, 1};
            verts[i].TexCoord = (corners[i] + 1.5f) / 3.0f;
        }
        const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
        RENDER_CHECK(mesh->Init(verts, indices, "U5AHiZSceneMesh"), "U5-A scene mesh init");
        RenderMeshInstance inst;
        inst.EntityID = 5101;
        inst.NativeView = mesh->CaptureView();
        RHIMaterialDesc matDesc;
        matDesc.BaseColor = {0.75f, 0.7f, 0.65f, 1.0f};
        matDesc.Roughness = 0.25f;
        matDesc.CullMode = CullMode::None;
        inst.Material.RHIMaterialAsset = std::make_shared<RHIMaterial>(matDesc);
        hizWorld.Meshes.push_back(inst);
        hizWorld.DirectionalLight = RenderDirectionalLight{};
        hizWorld.DirectionalLight->Direction = {0.3f, 0.8f, 0.5f};

        SceneRenderSettings ssrSettings;
        ssrSettings.EnableShadows = ssrSettings.EnableParticles = ssrSettings.EnableSkybox = false;
        ssrSettings.EnableGTAO = ssrSettings.EnableContactShadows = false;
        ssrSettings.EnableToneMapping = ssrSettings.EnableFXAA = false;
        ssrSettings.EnableSSR = true;

        const glm::uvec2 oddExtents[] = {{1, 1}, {3, 5}, {127, 63}, {1919, 1079}};
        for (const auto ext : oddExtents) {
            auto targets = std::make_shared<RHIViewTargets>();
            RHIViewTargetDesc desc;
            desc.Width = ext.x; desc.Height = ext.y;
            desc.EnableNormal = desc.EnableScreenStorage = desc.EnableHiZ = true;
            RENDER_CHECK(targets->Configure(desc), "odd Hi-Z RHIViewTargets configure");
            RENDER_CHECK(targets->GetHiZUsefulLevelCount() == HiZContract::ComputeUsefulLevelCount(ext.x, ext.y) &&
                targets->GetHiZLevelCount() == HiZContract::ComputeAllocatedLevelCount(ext.x, ext.y),
                "odd RHIViewTargets useful/allocated Hi-Z level counts match contract");

            PerspectiveCamera cam(55.0f, float(ext.x) / float(ext.y), 0.1f, 100.0f);
            cam.SetViewMatrix(glm::lookAtRH(glm::vec3(0, 0, 5), glm::vec3(0), glm::vec3(0, 1, 0)), {0, 0, 5});

            renderer.RenderWorldView(hizWorld, cam, targets->Get(RHIViewTarget::Color), targets->Get(RHIViewTarget::Depth),
                                     ssrSettings, nullptr, targets);
            RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors(),
                "odd renderer Hi-Z + SSR frame succeeded without errors");

            const uint32_t levelCount = targets->GetHiZLevelCount();
            const uint32_t usefulCount = targets->GetHiZUsefulLevelCount();
            std::vector<glm::vec4> level0Rgba;
            RENDER_CHECK(targets->GetHiZ(0) && Read(*targets->GetHiZ(0), level0Rgba), "read rendered HiZ0");
            const std::vector<float> level0 = ExtractScalarChannel(level0Rgba);

            std::vector<std::vector<float>> expectedLevels;
            std::vector<HiZContract::PyramidExtent> expectedExtents;
            RENDER_CHECK(HiZContract::BuildMinReductionPyramid(level0, ext.x, ext.y, levelCount, expectedLevels, expectedExtents),
                "build CPU reference from actual rendered HiZ0");

            float worstRenderedError = 0.0f;
            for (uint32_t l = 0; l < levelCount; ++l) {
                auto* tex = targets->GetHiZ(l);
                RENDER_CHECK(tex && tex->IsValid() &&
                    tex->GetWidth() == expectedExtents[l].Width &&
                    tex->GetHeight() == expectedExtents[l].Height,
                    "rendered Hi-Z level extent valid");
                std::vector<glm::vec4> rgba;
                RENDER_CHECK(Read(*tex, rgba) && rgba.size() == expectedLevels[l].size(),
                    "read rendered Hi-Z level");
                for (size_t i = 0; i < rgba.size(); ++i) {
                    RENDER_CHECK(std::isfinite(rgba[i].x) && rgba[i].x >= 0.0f && rgba[i].x <= 1.0f,
                        "rendered Hi-Z texel finite in [0,1]");
                    worstRenderedError = std::max(worstRenderedError, std::abs(rgba[i].x - expectedLevels[l][i]));
                }
            }
            RENDER_CHECK(worstRenderedError < 1e-5f,
                "rendered HiZ1..HiZ(N-1) matches CPU reference on real scene depth");
            RENDER_CHECK(GetRHIValidationErrorCount() == 0,
                "rendered odd Hi-Z + SSR transition barriers have zero native validation errors");
            evidence << "rendered=" << ext.x << "x" << ext.y
                     << " useful=" << usefulCount << " allocated=" << levelCount
                     << " max_error=" << worstRenderedError << '\n';
        }

        // ---------------------------------------------------------------------
        // 3. Per-level Hi-Z visualization pass verification (M15-U5-07.1)
        //    Covers SDR, HDR, clamping fallback, level-to-level spreading, and
        //    recovery back to normal shaded scene when disabled (-1).
        // ---------------------------------------------------------------------
        constexpr uint32_t visSize = 64;
        auto visTargets = std::make_shared<RHIViewTargets>();
        RHIViewTargetDesc visDesc;
        visDesc.Width = visDesc.Height = visSize;

        PerspectiveCamera visCam(55.0f, 1.0f, 0.1f, 100.0f);
        visCam.SetViewMatrix(glm::lookAtRH(glm::vec3(0, 0, 5), glm::vec3(0), glm::vec3(0, 1, 0)), {0, 0, 5});

        SceneRenderSettings visSettings = ssrSettings;
        visSettings.EnableSSR = false;
        visSettings.EnableToneMapping = false;

        visSettings.HiZVisualizationLevel = -1;
        visDesc.EnableHiZ = false;
        RENDER_CHECK(visTargets->Configure(visDesc), "baseline unvisualized target configure");
        renderer.RenderWorldView(hizWorld, visCam, visTargets->Get(RHIViewTarget::Color),
                                 visTargets->Get(RHIViewTarget::Depth), visSettings, nullptr, visTargets);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors(),
            "baseline unvisualized render succeeded");
        std::vector<glm::vec4> normalColor;
        RENDER_CHECK(ReadColor8(*visTargets->Get(RHIViewTarget::Color), normalColor), "read unvisualized scene");

        visDesc.EnableHiZ = true;
        RENDER_CHECK(visTargets->Configure(visDesc), "visualization enabled target configure");
        std::vector<glm::vec4> vis0, vis3, vis6, visClamped;
        for (const int reqLevel : {0, 3, 6, 99}) {
            visSettings.HiZVisualizationLevel = reqLevel;
            renderer.RenderWorldView(hizWorld, visCam, visTargets->Get(RHIViewTarget::Color),
                                     visTargets->Get(RHIViewTarget::Depth), visSettings, nullptr, visTargets);
            RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors(),
                "Hi-Z visualization frame succeeded");
            if (reqLevel == 99) {
                const auto& fallbacks = renderer.GetDiagnostics().GetFrame().Fallbacks;
                RENDER_CHECK(std::any_of(fallbacks.begin(), fallbacks.end(), [](const std::string& msg) {
                    return msg.find("hiz_visualization_clamped_or_tail") != std::string::npos;
                }), "clamped/tail visualization level emits explicit fallback diagnostic");
            }
            std::vector<glm::vec4> out;
            RENDER_CHECK(ReadColor8(*visTargets->Get(RHIViewTarget::Color), out), "read visualization color target");
            for (const auto& px : out) {
                for (int c = 0; c < 4; ++c) RENDER_CHECK(std::isfinite(px[c]), "finite Hi-Z visualization pixel");
            }
            if (reqLevel == 0) {
                vis0 = out;
                RENDER_CHECK(WriteDisplayPPM(output / "M15_U5A_vis-level0.ppm", out, visSize, visSize),
                    "write vis level0 ppm");
            } else if (reqLevel == 3) {
                vis3 = out;
                RENDER_CHECK(WriteDisplayPPM(output / "M15_U5A_vis-level3.ppm", out, visSize, visSize),
                    "write vis level3 ppm");
            } else if (reqLevel == 6) {
                vis6 = out;
                RENDER_CHECK(WriteDisplayPPM(output / "M15_U5A_vis-level6.ppm", out, visSize, visSize),
                    "write vis level6 ppm");
            } else {
                visClamped = out;
            }
        }
        // Level 0 left quad is bright depth tone while right sky edge is black
        RENDER_CHECK(vis0[32 * visSize + 16].x > 0.25f && vis0[32 * visSize + 60].x == 0.0f,
            "Hi-Z visualization level 0 contrasts geometry vs sky cleanly");
        // Coarser levels differ cleanly and level 6 (1x1) makes entire screen uniform near tone
        RENDER_CHECK(vis0 != vis3 && vis3 != vis6 && vis6.front() == vis6.back() && vis6.front().x > 0.25f,
            "Hi-Z coarse visualization propagates minimum depth across tiles down to uniform 1x1");
        // Requested level 99 clamps cleanly to level 7 (1x1) matching level 6
        RENDER_CHECK(visClamped == vis6, "clamped level 99 matches coarsest 1x1 level");

        // HDR tone-mapped path with visualization enabled
        visSettings.EnableToneMapping = true;
        visSettings.HiZVisualizationLevel = 2;
        visDesc.HDR = true;
        RENDER_CHECK(visTargets->Configure(visDesc), "HDR visualization target configure");
        renderer.RenderWorldView(hizWorld, visCam, visTargets->Get(RHIViewTarget::Color),
                                 visTargets->Get(RHIViewTarget::Depth), visSettings, nullptr, visTargets);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors(),
            "HDR Hi-Z visualization succeeded");
        std::vector<glm::vec4> hdrVis;
        RENDER_CHECK(ReadColor8(*visTargets->Get(RHIViewTarget::Color), hdrVis), "read HDR Hi-Z visualization");
        for (const auto& px : hdrVis) for (int c = 0; c < 4; ++c) RENDER_CHECK(std::isfinite(px[c]), "finite HDR vis");

        // Disabling visualization recovers normal scene color cleanly
        visSettings.EnableToneMapping = false;
        visSettings.HiZVisualizationLevel = -1;
        visDesc.HDR = false;
        visDesc.EnableHiZ = false;
        RENDER_CHECK(visTargets->Configure(visDesc), "recovered target configure");
        renderer.RenderWorldView(hizWorld, visCam, visTargets->Get(RHIViewTarget::Color),
                                 visTargets->Get(RHIViewTarget::Depth), visSettings, nullptr, visTargets);
        std::vector<glm::vec4> recoveredColor;
        RENDER_CHECK(ReadColor8(*visTargets->Get(RHIViewTarget::Color), recoveredColor) && recoveredColor == normalColor,
            "disabling Hi-Z visualization recovers exact scene rendering");
        RENDER_CHECK(GetRHIValidationErrorCount() == 0, "Hi-Z visualization zero validation errors");
        evidence << "visualization_level0_left=" << vis0[32 * visSize + 16].x
                 << " visualization_level0_sky=" << vis0[32 * visSize + 60].x
                 << " visualization_level6_1x1=" << vis6.front().x << '\n';
    }

    std::cout << "PASS M15-U5-A native Hi-Z reduction, odd extents, barriers, and per-level visualization\n";
    return true;
}
