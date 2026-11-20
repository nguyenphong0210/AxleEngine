#include "OcclusionTestSupport.hpp"
#include "RHIReadback.hpp"
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RenderService.hpp>
#include <Axle/RenderParticleData.hpp>
#include <Axle/ParticleMaterialAdapter.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <Axle/LightingService.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace Axle;
using namespace Axle::RHI;

namespace {
    bool Read3DSliceRGBA16F(RHITexture& volume, uint32_t sliceZ, std::vector<glm::vec4>& outPixels) {
        if (!volume.IsValid() || sliceZ >= volume.GetDepth() || volume.GetFormat() != RHITextureFormat::RGBA16_FLOAT) {
            return false;
        }
        auto& device = RHIDevice::Get();
        const auto alignment = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
        const auto align = [](uint32_t value, uint32_t step) { return (value + step - 1u) / step * step; };
        const uint32_t width = volume.GetWidth();
        const uint32_t height = volume.GetHeight();
        constexpr uint32_t pixelSize = 8u;

        nri::TextureDataLayoutDesc layout{};
        layout.rowPitch = align(width * pixelSize, alignment.uploadBufferTextureRow);
        layout.slicePitch = align(layout.rowPitch * height, alignment.uploadBufferTextureSlice);

        RHIBufferDesc bufDesc;
        bufDesc.Size = layout.slicePitch;
        bufDesc.MemoryLocation = RHIMemoryLocation::HostReadback;
        RHIBuffer readback;
        RHICommandBuffer commands;
        RENDER_CHECK(readback.Init(bufDesc) && commands.Init() && commands.Begin(),
            "3D froxel slice readback allocation");

        commands.TransitionTexture(volume.GetNative(), RHIResourceLayout::ShaderResource, RHIResourceLayout::CopySource);
        nri::TextureRegionDesc region{};
        region.width = static_cast<nri::Dim_t>(width);
        region.height = static_cast<nri::Dim_t>(height);
        region.z = static_cast<nri::Dim_t>(sliceZ);
        region.depth = 1u;
        region.planes = nri::PlaneBits::COLOR;
        commands.ReadbackTextureToBuffer(*readback.GetNative(), layout, *volume.GetNative(), region);
        commands.TransitionTexture(volume.GetNative(), RHIResourceLayout::CopySource, RHIResourceLayout::ShaderResource);
        RENDER_CHECK(commands.End() && commands.Submit() && device.WaitIdle(),
            "3D froxel slice readback submission");

        const auto* data = static_cast<const uint8_t*>(readback.Map());
        RENDER_CHECK(data != nullptr, "3D froxel slice readback map");
        outPixels.resize(size_t(width) * height);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t packed[2]{};
                std::memcpy(packed, data + size_t(y) * layout.rowPitch + size_t(x) * pixelSize, pixelSize);
                outPixels[size_t(y) * width + x] = glm::vec4(
                    glm::unpackHalf2x16(packed[0]),
                    glm::unpackHalf2x16(packed[1]));
            }
        }
        readback.Unmap();
        return true;
    }

    const char* kFroxelProbeShader = R"(
cbuffer FroxelProbeCB : register(b0, space0) {
    uint4 u_GridDims; // x=W, y=H, z=D, w=unused
    float4 u_SampleParams; // x=wCoord
};

#if defined(__spirv__)
[[vk::image_format("rgba16f")]]
#endif
RWTexture3D<float4> u_Out3D : register(u0, space2);

[numthreads(4, 4, 2)]
void CSWrite3D(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= u_GridDims.x || dtid.y >= u_GridDims.y || dtid.z >= u_GridDims.z) return;
    float u = (float(dtid.x) + 0.5) / float(u_GridDims.x);
    float v = (float(dtid.y) + 0.5) / float(u_GridDims.y);
    float w = (float(dtid.z) + 0.5) / float(u_GridDims.z);
    float trans = exp(-0.15 * float(dtid.z));
    u_Out3D[dtid] = float4(u, v, w, trans);
}

Texture3D<float4> u_In3D : register(t0, space1);
SamplerState u_LinearSampler : register(s0, space1);
#if defined(__spirv__)
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> u_Out2D : register(u0, space2);

[numthreads(4, 4, 1)]
void CSSample3D(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= u_GridDims.x || dtid.y >= u_GridDims.y) return;
    float u = (float(dtid.x) + 0.5) / float(u_GridDims.x);
    float v = (float(dtid.y) + 0.5) / float(u_GridDims.y);
    u_Out2D[dtid.xy] = u_In3D.SampleLevel(u_LinearSampler, float3(u, v, u_SampleParams.x), 0.0);
}
)";

    struct alignas(256) FroxelProbeCB {
        uint32_t GridDims[4]{};
        float SampleParams[4]{};
    };
} // namespace

bool RunM15FroxelFogTests(RHISceneRenderer& renderer, const std::filesystem::path& output) {
    auto& device = RHIDevice::Get();

    // -------------------------------------------------------------------------
    // 1. M15-U6-01.1: Native D3D12/Vulkan 3D RWTexture3D UAV compute write,
    //    RenderGraph 3D transitions, Texture3D trilinear sampling & Z-slice readback.
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t kW = 8u;
        constexpr uint32_t kH = 6u;
        constexpr uint32_t kD = 4u;

        const auto shaderFormat = device.GetBackend() == RHIType::Vulkan
            ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        RHIShaderCompileDesc writeDesc{};
        writeDesc.Source = kFroxelProbeShader;
        writeDesc.SourcePath = "U6A_FroxelProbe.hlsl";
        writeDesc.EntryPoint = "CSWrite3D";
        writeDesc.Stage = RHIShaderStage::Compute;
        writeDesc.HLSLVersion = "2021";
        writeDesc.ShaderModel = "6_0";
        writeDesc.Format = shaderFormat;
        writeDesc.Optimization = true;
        const auto writeCompiled = RHIShaderCompiler::Get().Compile(writeDesc);
        RENDER_CHECK(writeCompiled.Success, "M15-U6-01.1: CSWrite3D compiles cleanly");

        RHIShaderCompileDesc sampleDesc = writeDesc;
        sampleDesc.EntryPoint = "CSSample3D";
        const auto sampleCompiled = RHIShaderCompiler::Get().Compile(sampleDesc);
        RENDER_CHECK(sampleCompiled.Success, "M15-U6-01.1: CSSample3D compiles cleanly");

        nri::RootDescriptorDesc root{};
        root.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;

        nri::DescriptorRangeDesc storageRange{};
        storageRange.baseRegisterIndex = 0;
        storageRange.descriptorNum = 1;
        storageRange.descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        storageRange.shaderStages = nri::StageBits::COMPUTE_SHADER;
        storageRange.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc writeSets[1]{};
        writeSets[0].registerSpace = 2;
        writeSets[0].ranges = &storageRange;
        writeSets[0].rangeNum = 1;

        nri::PipelineLayoutDesc writeLayoutDesc{};
        writeLayoutDesc.rootDescriptors = &root;
        writeLayoutDesc.rootDescriptorNum = 1;
        writeLayoutDesc.descriptorSets = writeSets;
        writeLayoutDesc.descriptorSetNum = 1;
        writeLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;

        nri::DescriptorRangeDesc texRanges[2]{};
        texRanges[0].baseRegisterIndex = 0;
        texRanges[0].descriptorNum = 1;
        texRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        texRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        texRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        texRanges[1].baseRegisterIndex = 0;
        texRanges[1].descriptorNum = 1;
        texRanges[1].descriptorType = nri::DescriptorType::SAMPLER;
        texRanges[1].shaderStages = nri::StageBits::COMPUTE_SHADER;
        texRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc sampleSets[2]{};
        sampleSets[0].registerSpace = 1;
        sampleSets[0].ranges = texRanges;
        sampleSets[0].rangeNum = 2;
        sampleSets[1].registerSpace = 2;
        sampleSets[1].ranges = &storageRange;
        sampleSets[1].rangeNum = 1;

        nri::PipelineLayoutDesc sampleLayoutDesc{};
        sampleLayoutDesc.rootDescriptors = &root;
        sampleLayoutDesc.rootDescriptorNum = 1;
        sampleLayoutDesc.descriptorSets = sampleSets;
        sampleLayoutDesc.descriptorSetNum = 2;
        sampleLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;

        RHIPipelineLayout writeLayout, sampleLayout;
        RHIPipelineCache cache;
        RHISampler sampler;
        RENDER_CHECK(writeLayout.Init(writeLayoutDesc) &&
                     sampleLayout.Init(sampleLayoutDesc) &&
                     sampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge)),
            "M15-U6-01.1: 3D froxel probe layouts and linear clamp sampler initialize");

        RHIComputePipelineDesc writePipeDesc{};
        writePipeDesc.Layout = &writeLayout;
        writePipeDesc.ComputeShader = writeCompiled.Bytecode;
        auto* writePipeline = cache.GetOrCreateComputePipeline(writePipeDesc);

        RHIComputePipelineDesc samplePipeDesc{};
        samplePipeDesc.Layout = &sampleLayout;
        samplePipeDesc.ComputeShader = sampleCompiled.Bytecode;
        auto* samplePipeline = cache.GetOrCreateComputePipeline(samplePipeDesc);
        RENDER_CHECK(writePipeline != nullptr && samplePipeline != nullptr,
            "M15-U6-01.1: 3D compute pipelines initialize cleanly");

        RHITexture volume3D, slice2D;
        RENDER_CHECK(volume3D.Init(RHITextureDesc::StorageTexture3D(
                         kW, kH, kD, RHITextureFormat::RGBA16_FLOAT, "U6AProbeVolume3D")) &&
                     slice2D.Init(RHITextureDesc::StorageTexture2D(
                         kW, kH, RHITextureFormat::RGBA16_FLOAT, "U6AProbeSlice2D")),
            "M15-U6-01.1: 3D RGBA16F storage volume and 2D readback slice initialize");

        RHIDescriptorPool pool;
        RHIDescriptorPoolDesc poolDesc{};
        poolDesc.MaxDescriptorSets = 8;
        poolDesc.MaxTextures = 8;
        poolDesc.MaxSamplers = 8;
        poolDesc.MaxStorageTextures = 8;
        RENDER_CHECK(pool.Init(poolDesc), "M15-U6-01.1: descriptor pool init");
        auto* writeUavSet = pool.AllocateDescriptorSet(writeLayout, 0);
        auto* sampleSrvSet = pool.AllocateDescriptorSet(sampleLayout, 0);
        auto* sampleUavSet = pool.AllocateDescriptorSet(sampleLayout, 1);
        RENDER_CHECK(writeUavSet && sampleSrvSet && sampleUavSet &&
                     pool.UpdateStorageTexture(writeUavSet, 0, 0, volume3D.GetUAV()) &&
                     pool.UpdateTexture(sampleSrvSet, 0, 0, volume3D.GetSRV()) &&
                     pool.UpdateSampler(sampleSrvSet, 1, 0, sampler.GetNative()) &&
                     pool.UpdateStorageTexture(sampleUavSet, 0, 0, slice2D.GetUAV()),
            "M15-U6-01.1: 3D SRV/UAV descriptor updates succeed");

        FroxelProbeCB cbData{};
        cbData.GridDims[0] = kW;
        cbData.GridDims[1] = kH;
        cbData.GridDims[2] = kD;
        cbData.SampleParams[0] = 0.5f; // Midway between slice 1 (0.375) and slice 2 (0.625)
        RHIBuffer cb;
        RENDER_CHECK(cb.Init(RHIBufferDesc::Constant(sizeof(FroxelProbeCB), RHIMemoryLocation::HostUpload, "U6AFroxelCB")) &&
                     cb.UpdateData(&cbData, sizeof(cbData)),
            "M15-U6-01.1: constant buffer init & upload");

        RenderGraph graph;
        const auto volRes = graph.Import("FroxelVolume3D", volume3D.GetNative());
        const auto sliceRes = graph.Import("FroxelSample2D", slice2D.GetNative());
        graph.AddPass("Write3D", {{volRes, RHIResourceLayout::ShaderResourceStorage, RenderGraph::Access::Write}});
        graph.AddPass("Sample3D", {
            {volRes, RHIResourceLayout::ShaderResource},
            {sliceRes, RHIResourceLayout::ShaderResourceStorage, RenderGraph::Access::Write}
        });
        RENDER_CHECK(graph.DeclareBuffer("Write3D", cb.AcquireLease(), RHIAccess::ConstantBuffer) &&
                     graph.DeclareBuffer("Sample3D", cb.AcquireLease(), RHIAccess::ConstantBuffer) &&
                     graph.Compile(true, true),
            "M15-U6-01.1: RenderGraph compiles 3D storage write -> SRV read dependency chain");

        RHICommandBuffer commands;
        RENDER_CHECK(commands.Init() && commands.Begin(pool.GetNative()), "M15-U6-01.1: command recording begin");
        pool.Bind(commands.GetNative());

        RENDER_CHECK(graph.BeginPass("Write3D", commands), "M15-U6-01.1: begin Write3D");
        commands.SetPipeline(writePipeline);
        commands.SetPipelineLayout(nri::BindPoint::COMPUTE, writeLayout);
        commands.SetRootDescriptor(0, cb.GetCBV(), 0, nri::BindPoint::COMPUTE);
        RENDER_CHECK(graph.DeclareDescriptorRead(writeUavSet, commands), "declare 3D UAV read/write");
        commands.SetDescriptorSet(nri::BindPoint::COMPUTE, 0, writeUavSet);
        commands.Dispatch((kW + 3u) / 4u, (kH + 3u) / 4u, (kD + 1u) / 2u);

        RENDER_CHECK(graph.BeginPass("Sample3D", commands), "M15-U6-01.1: begin Sample3D");
        commands.SetPipeline(samplePipeline);
        commands.SetPipelineLayout(nri::BindPoint::COMPUTE, sampleLayout);
        commands.SetRootDescriptor(0, cb.GetCBV(), 0, nri::BindPoint::COMPUTE);
        RENDER_CHECK(graph.DeclareDescriptorRead(sampleSrvSet, commands) &&
                     graph.DeclareDescriptorRead(sampleUavSet, commands),
            "declare 3D SRV and 2D UAV descriptors");
        commands.SetDescriptorSet(nri::BindPoint::COMPUTE, 0, sampleSrvSet);
        commands.SetDescriptorSet(nri::BindPoint::COMPUTE, 1, sampleUavSet);
        commands.Dispatch((kW + 3u) / 4u, (kH + 3u) / 4u, 1u);

        graph.EndRecording();
        RENDER_CHECK(commands.End() && commands.Submit() && device.WaitIdle(),
            "M15-U6-01.1: 3D froxel compute graph executes cleanly");

        for (uint32_t z = 0; z < kD; ++z) {
            std::vector<glm::vec4> slicePixels;
            RENDER_CHECK(Read3DSliceRGBA16F(volume3D, z, slicePixels) && slicePixels.size() == kW * kH,
                "M15-U6-01.1: read back 3D volume Z slice");
            const float expectedW = (float(z) + 0.5f) / float(kD);
            const float expectedT = std::exp(-0.15f * float(z));
            for (uint32_t y = 0; y < kH; ++y) {
                for (uint32_t x = 0; x < kW; ++x) {
                    const glm::vec4 px = slicePixels[size_t(y) * kW + x];
                    const float expectedU = (float(x) + 0.5f) / float(kW);
                    const float expectedV = (float(y) + 0.5f) / float(kH);
                    RENDER_CHECK(std::abs(px.r - expectedU) < 2e-3f &&
                                 std::abs(px.g - expectedV) < 2e-3f &&
                                 std::abs(px.b - expectedW) < 2e-3f &&
                                 std::abs(px.a - expectedT) < 2e-3f,
                        "M15-U6-01.1: 3D RWTexture3D slice matches exact expected texel values");
                }
            }
        }

        std::vector<glm::vec4> sampledPixels;
        RENDER_CHECK(Axle::RenderTests::Read(slice2D, sampledPixels) && sampledPixels.size() == kW * kH,
            "M15-U6-01.1: read back Texture3D sampled slice");
        const float expectedInterpT = 0.5f * (std::exp(-0.15f * 1.0f) + std::exp(-0.15f * 2.0f));
        for (uint32_t y = 0; y < kH; ++y) {
            for (uint32_t x = 0; x < kW; ++x) {
                const glm::vec4 px = sampledPixels[size_t(y) * kW + x];
                const float expectedU = (float(x) + 0.5f) / float(kW);
                const float expectedV = (float(y) + 0.5f) / float(kH);
                RENDER_CHECK(std::abs(px.r - expectedU) < 2e-3f &&
                             std::abs(px.g - expectedV) < 2e-3f &&
                             std::abs(px.b - 0.5f) < 2e-3f &&
                             std::abs(px.a - expectedInterpT) < 2e-3f,
                    "M15-U6-01.1: Texture3D hardware trilinear SampleLevel interpolates Z slices accurately");
            }
        }
    }

    // Wait: local helper functions placed cleanly for U6-B suites below
    auto Read2DRGBA16F = [&](RHITexture& texture, std::vector<glm::vec4>& outPixels) -> bool {
        if (!texture.IsValid() || texture.GetFormat() != RHITextureFormat::RGBA16_FLOAT) return false;
        const auto alignment = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
        const auto align = [](uint32_t value, uint32_t step) { return (value + step - 1u) / step * step; };
        const uint32_t width = texture.GetWidth();
        const uint32_t height = texture.GetHeight();
        nri::TextureDataLayoutDesc layout{};
        layout.rowPitch = align(width * 8u, alignment.uploadBufferTextureRow);
        layout.slicePitch = align(layout.rowPitch * height, alignment.uploadBufferTextureSlice);

        RHIBufferDesc bufDesc{};
        bufDesc.Size = layout.slicePitch;
        bufDesc.MemoryLocation = RHIMemoryLocation::HostReadback;
        RHIBuffer readback;
        RHICommandBuffer commands;
        if (!readback.Init(bufDesc) || !commands.Init() || !commands.Begin()) return false;

        commands.TransitionTexture(texture.GetNative(), RHIResourceLayout::ShaderResource, RHIResourceLayout::CopySource);
        nri::TextureRegionDesc region{};
        region.width = static_cast<nri::Dim_t>(width);
        region.height = static_cast<nri::Dim_t>(height);
        region.depth = 1u;
        region.planes = nri::PlaneBits::COLOR;
        commands.ReadbackTextureToBuffer(*readback.GetNative(), layout, *texture.GetNative(), region);
        commands.TransitionTexture(texture.GetNative(), RHIResourceLayout::CopySource, RHIResourceLayout::ShaderResource);
        if (!commands.End() || !commands.Submit() || !device.WaitIdle()) return false;

        const auto* data = static_cast<const uint8_t*>(readback.Map());
        if (!data) return false;
        outPixels.resize(size_t(width) * height);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t packed[2]{};
                std::memcpy(packed, data + size_t(y) * layout.rowPitch + size_t(x) * 8u, 8u);
                outPixels[size_t(y) * width + x] = glm::vec4(
                    glm::unpackHalf2x16(packed[0]),
                    glm::unpackHalf2x16(packed[1]));
            }
        }
        readback.Unmap();
        return true;
    };

    auto ComputeRayDirWorldPerUnitZ = [](const glm::mat4& invProj, const glm::mat4& invView, const glm::vec2& uv) {
        const glm::vec4 clip(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, 0.5f, 1.0f);
        const glm::vec4 vp = invProj * clip;
        const glm::vec3 viewPos = glm::vec3(vp) / std::max(std::abs(vp.w), 1e-6f);
        const glm::vec3 rayView = viewPos / std::max(-viewPos.z, 1e-6f);
        return glm::mat3(invView) * rayView;
    };

    auto HasFallbackPrefix = [](const RenderFrameDiagnostics& frame, const std::string& needle) {
        for (const auto& f : frame.Fallbacks) {
            if (f.find(needle) != std::string::npos) return true;
        }
        return false;
    };

    auto HasPassNamed = [](const RenderFrameDiagnostics& frame, const std::string& name) {
        for (const auto& p : frame.Passes) {
            if (p.Name == name && p.Status == DiagnosticStatus::Success) return true;
        }
        return false;
    };

    auto SampleHDRBlockAvg = [](const std::vector<glm::vec4>& pixels, uint32_t width, uint32_t cx, uint32_t cy) {
        glm::vec3 sum(0.0f);
        uint32_t count = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const uint32_t sx = static_cast<uint32_t>(std::max<int>(0, static_cast<int>(cx) + dx));
                const uint32_t sy = static_cast<uint32_t>(std::max<int>(0, static_cast<int>(cy) + dy));
                sum += glm::vec3(pixels[size_t(sy) * width + sx]);
                ++count;
            }
        }
        return sum / static_cast<float>(count);
    };

    // -------------------------------------------------------------------------
    // 2. M15-U6-01.2 & M15-U6-07.5: RHISceneRenderer froxel target allocation across
    //    quality tiers + native device memory budget scorecard.
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t viewW = 128u;
        constexpr uint32_t viewH = 128u;
        RHITexture color, depth;
        RENDER_CHECK(color.Init(RHITextureDesc::RenderTarget2D(viewW, viewH)) &&
                     depth.Init(RHITextureDesc::DepthStencil2D(viewW, viewH)),
            "M15-U6-01.2: test render targets initialize");

        RenderWorld world;
        PerspectiveCamera camera(55.0f, 1.0f, 0.1f, 100.0f);
        camera.SetViewMatrix(glm::lookAtRH(glm::vec3(0, 0, 4), glm::vec3(0), glm::vec3(0, 1, 0)), glm::vec3(0, 0, 4));
        SceneRenderSettings settings;
        settings.EnableSkybox = false;
        settings.EnableShadows = false;
        settings.EnableParticles = false;
        settings.EnableToneMapping = false;
        settings.EnableVolumetricFog = true;

        for (uint8_t tier = 0u; tier < 4u; ++tier) {
            const auto quality = static_cast<FroxelFogQuality>(tier);
            settings.FogQuality = quality;
            RHIViewTargetDesc vtDesc{};
            vtDesc.Width = viewW;
            vtDesc.Height = viewH;
            vtDesc.OwnPrimary = false;
            vtDesc.HDR = true;
            vtDesc.EnableVolumetricFog = true;
            vtDesc.FroxelQuality = tier;

            auto targets = std::make_shared<RHIViewTargets>();
            RENDER_CHECK(targets->Configure(vtDesc) && targets->Validate(),
                "M15-U6-01.2: RHIViewTargets configures froxel tier");

            const auto expected = FroxelFogContract::ComputeGridExtent(viewW, viewH, quality);
            for (const RHIViewTarget slot : {
                     RHIViewTarget::FroxelScattering, RHIViewTarget::FroxelIntegrated,
                     RHIViewTarget::FroxelHistory0, RHIViewTarget::FroxelHistory1 }) {
                auto* vol = targets->Get(slot);
                RENDER_CHECK(vol && vol->IsValid() &&
                             vol->GetWidth() == expected.Width &&
                             vol->GetHeight() == expected.Height &&
                             vol->GetDepth() == expected.Depth &&
                             vol->GetSRV() && vol->GetUAV(),
                    "M15-U6-01.2: froxel target matches expected tier dimensions");
            }

            if (tier == 0u) {
                bool faultReached = false;
                {
                    ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                        if (operation == "CreateDescriptorPool" && ScopedRHIOperation::Resource() == "FogDescriptorPool") {
                            faultReached = true;
                            return nri::Result::OUT_OF_MEMORY;
                        }
                        return {};
                    });
                    renderer.RenderWorldView(world, camera, &color, &depth, settings, nullptr, targets);
                }
                const auto& diag = renderer.GetDiagnostics().GetFrame();
                RENDER_CHECK(faultReached && renderer.WasLastRenderSuccessful() &&
                             HasFallbackPrefix(diag, "froxel_fog_unavailable") && !HasPassNamed(diag, "FogApply"),
                    "An unavailable fog descriptor pool leaves the scene renderable and reports the fallback");
            }
            renderer.RenderWorldView(world, camera, &color, &depth, settings, nullptr, targets);
            const auto& diag = renderer.GetDiagnostics().GetFrame();
            RENDER_CHECK(renderer.WasLastRenderSuccessful() && HasPassNamed(diag, "FogApply") &&
                         HasPassNamed(diag, "ToneMapping"),
                "M15-U6-01.2: RHISceneRenderer renders cleanly with tier froxel view targets");
        }

        // Write M15-U6-07.5 initial device memory scorecard for Low..Ultra across 720p/1080p/1440p
        std::ofstream scorecard(output / "M15_U6A_froxel_memory.txt");
        RENDER_CHECK(scorecard.is_open(), "M15-U6-07.5: froxel memory scorecard creation");
        scorecard << "backend=" << (device.GetBackend() == RHIType::Vulkan ? "Vulkan" : "D3D12") << "\n";
        for (const auto [w, h] : std::vector<std::pair<uint32_t, uint32_t>>{ {1280u, 720u}, {1920u, 1080u}, {2560u, 1440u} }) {
            for (uint8_t tier = 0u; tier < 4u; ++tier) {
                const auto q = static_cast<FroxelFogQuality>(tier);
                const auto spec = FroxelFogContract::GetTierSpec(q);
                const auto budget = FroxelFogContract::EstimateMemoryBudget(w, h, q, true);
                RENDER_CHECK(budget.DeviceQueryAvailable &&
                             budget.DeviceBytesPerTexture >= budget.VolumeBytesPerTexture &&
                             budget.TotalDeviceReportedBytes >= budget.TotalLogicalBytes,
                    "M15-U6-07.5: native device reports valid 3D memory requirements >= logical size");
                scorecard << w << "x" << h << " tier=" << spec.Name
                          << " grid=" << budget.Extent.Width << "x" << budget.Extent.Height << "x" << budget.Extent.Depth
                          << " logical_bytes=" << budget.TotalLogicalBytes
                          << " device_bytes=" << budget.TotalDeviceReportedBytes
                          << " align=" << budget.DeviceAllocationAlignment << "\n";
            }
        }
        scorecard.close();
        RENDER_CHECK(scorecard.good(), "M15-U6-07.5: complete memory scorecard written");
    }

    // -------------------------------------------------------------------------
    // 3. M15-U6-B Native GPU Verification:
    //    - 3D froxel media injection (CSInjectMedia) & slice integration (CSIntegrate)
    //    - Analytic homogeneous & continuous height-falloff quadrature agreement
    //    - Henyey-Greenstein forward vs backward asymmetry & world component override
    //    - Zero-density & zero-intensity HDR preservation (no clamp, no washout)
    //    - Unlit scene extinction without invented directional/ambient glow + diagnostics
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t kViewW = 1280u;
        constexpr uint32_t kViewH = 720u;
        RHITexture hdrColor, hdrDepth;
        RENDER_CHECK(hdrColor.Init(RHITextureDesc::RenderTarget2D(kViewW, kViewH, RHITextureFormat::RGBA8_UNORM, "U6B_Display")) &&
                     hdrDepth.Init(RHITextureDesc::DepthStencil2D(kViewW, kViewH)),
            "M15-U6-B: display render target & D32 depth target init");

        RHIViewTargetDesc vtDesc{};
        vtDesc.Width = kViewW;
        vtDesc.Height = kViewH;
        vtDesc.ColorFormat = RHITextureFormat::RGBA8_UNORM;
        vtDesc.HDR = true;
        vtDesc.OwnPrimary = false;
        vtDesc.EnableVolumetricFog = true;
        vtDesc.FroxelQuality = static_cast<uint8_t>(FroxelFogQuality::High);
        auto targets = std::make_shared<RHIViewTargets>();
        RENDER_CHECK(targets->Configure(vtDesc) && targets->Validate(),
            "M15-U6-B: view targets configured for High froxel grid (160x90x64)");

        auto* scatteringVol = targets->GetFroxelScattering();
        auto* integratedVol = targets->GetFroxelIntegrated();
        RENDER_CHECK(scatteringVol && integratedVol &&
                     scatteringVol->GetWidth() == 160u &&
                     scatteringVol->GetHeight() == 90u &&
                     scatteringVol->GetDepth() == 64u,
            "M15-U6-02.1: 160x90x64 FroxelScattering & FroxelIntegrated targets ready");

        auto cubeMesh = RHIMesh::CreateCube(1.0f);
        RENDER_CHECK(cubeMesh && cubeMesh->IsValid(), "M15-U6-B: emissive wall cube fixture created");
        auto cubeView = cubeMesh->CaptureView();

        RHIMaterialDesc wallDesc{};
        wallDesc.SurfaceType = SurfaceType::Opaque;
        wallDesc.CullMode = CullMode::None;
        wallDesc.BaseColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        wallDesc.Metallic = 1.0f;
        wallDesc.Roughness = 1.0f;
        wallDesc.EmissiveColor = glm::vec3(2.4f, 1.2f, 0.6f);
        wallDesc.EmissiveIntensity = 1.0f;
        auto emissiveWallMat = std::make_shared<RHIMaterial>(wallDesc);

        const auto makeBaseSettings = []() {
            SceneRenderSettings s{};
            s.EnableSkybox = false;
            s.EnableShadows = false;
            s.EnableParticles = false;
            s.EnableToneMapping = true;
            s.EnableFXAA = false;
            s.EnableBloom = false;
            s.EnableGTAO = false;
            s.EnableSSR = false;
            s.EnableAutoExposure = false;
            s.EnableStudioLightLobes = false;
            s.EnableVolumetricFog = true;
            s.FogQuality = FroxelFogQuality::High;
            s.FogTemporalWeight = 0.0f;
            s.ClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            s.FogNearDistance = 0.25f;
            s.FogMaxDistance = 100.0f;
            s.FogDensity = 0.0f;
            s.FogHeightFalloff = 0.0f;
            s.FogScattering = 0.5f;
            s.FogColor = glm::vec3(0.75f, 0.85f, 0.95f);
            return s;
        };

        auto renderAndReadHDR = [&](const RenderWorld& world,
                                    const PerspectiveCamera& camera,
                                    const SceneRenderSettings& settings,
                                    std::vector<glm::vec4>& outHDR,
                                    RenderFrameDiagnostics& outDiag) -> bool {
            renderer.RenderWorldView(
                world, camera, &hdrColor, &hdrDepth, settings, nullptr,
                settings.EnableVolumetricFog ? targets : nullptr);
            outDiag = renderer.GetDiagnostics().GetFrame();
            if (!renderer.WasLastRenderSuccessful() || renderer.GetDiagnostics().HasErrors() || !device.WaitIdle()) {
                return false;
            }
            auto* linearHDR = renderer.GetIntermediateHDRTarget();
            return linearHDR && Read2DRGBA16F(*linearHDR, outHDR) && outHDR.size() == size_t(kViewW) * kViewH;
        };

        LightingService lightingSvc;
        std::ofstream physicsLog(output / "M15_U6B_froxel_physics.txt");
        RENDER_CHECK(physicsLog.is_open(), "M15-U6-B: physics verification log created");
        physicsLog << "backend=" << (device.GetBackend() == RHIType::Vulkan ? "Vulkan" : "D3D12") << "\n";

        PerspectiveCamera cam(60.0f, float(kViewW) / float(kViewH), 0.1f, 300.0f);
        const glm::vec3 camPos(0.0f, 1.5f, 0.0f);
        cam.SetViewMatrix(glm::lookAtRH(camPos, glm::vec3(0.0f, 1.5f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f)), camPos);
        const glm::mat4 invView = glm::inverse(cam.GetViewMatrix());
        const glm::mat4 invProj = glm::inverse(cam.GetProjectionMatrixZO());

        // Wall placed at z = -40.0m (cube [-0.5, +0.5] centered at z = -40.5m)
        RenderMeshInstance wallInst{};
        wallInst.EntityID = 9001u;
        wallInst.NativeView = cubeView;
        wallInst.Material.RHIMaterialAsset = emissiveWallMat;
        wallInst.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -40.5f)),
                                             glm::vec3(200.0f, 200.0f, 1.0f));
        wallInst.CastsShadows = false;

        RenderWorld litWallWorld;
        litWallWorld.SceneRevision = 9201u;
        RenderDirectionalLight sun{};
        sun.Direction = glm::normalize(glm::vec3(0.3f, 0.6f, -0.7f));
        sun.Color = glm::vec3(1.0f, 0.95f, 0.85f);
        sun.Intensity = 2.0f;
        sun.AmbientIntensity = 0.04f;
        litWallWorld.DirectionalLight = sun;
        litWallWorld.Meshes.push_back(wallInst);

        // Baseline: Fog OFF on linear HDR scene
        SceneRenderSettings offSettings = makeBaseSettings();
        offSettings.EnableVolumetricFog = false;
        std::vector<glm::vec4> hdrOffPixels;
        RenderFrameDiagnostics diag;
        RENDER_CHECK(renderAndReadHDR(litWallWorld, cam, offSettings, hdrOffPixels, diag),
            "M15-U6-B: Fog-OFF HDR baseline renders cleanly");
        const glm::vec3 wallBaselineHDR = SampleHDRBlockAvg(hdrOffPixels, kViewW, kViewW / 2u, kViewH / 2u);
        RENDER_CHECK(wallBaselineHDR.r > 2.0f && wallBaselineHDR.g > 1.0f && wallBaselineHDR.b > 0.4f,
            "M15-U6-B: HDR RGBA16F baseline preserves scene radiance > 1.0");

        // -----------------------------------------------------------------
        // Case A (M15-U6-02.1 / 03.1 / 03.2): Homogeneous 3D Froxel Physics
        // -----------------------------------------------------------------
        {
            SceneRenderSettings uniformSettings = makeBaseSettings();
            uniformSettings.FogDensity = 0.04f;
            uniformSettings.FogHeightFalloff = 0.0f;
            uniformSettings.FogScattering = 0.5f;
            uniformSettings.FogColor = glm::vec3(0.75f, 0.85f, 0.95f);

            std::vector<glm::vec4> hdrUniform;
            RENDER_CHECK(renderAndReadHDR(litWallWorld, cam, uniformSettings, hdrUniform, diag),
                "M15-U6-02.1: 3D froxel fog render completes");
            RENDER_CHECK(HasPassNamed(diag, "FogCopy") &&
                         HasPassNamed(diag, "FogInject") &&
                         HasPassNamed(diag, "FogIntegrate") &&
                         HasPassNamed(diag, "FogApply"),
                "M15-U6-02.1: RenderGraph executes FogCopy -> FogInject -> FogIntegrate -> FogApply");

            const auto media = FroxelFogContract::ResolveMediaState(
                litWallWorld, uniformSettings, lightingSvc.BuildFrame(litWallWorld, uniformSettings));

            const uint32_t centerFX = 80u, centerFY = 45u;
            const uint32_t offFX = 20u, offFY = 15u;
            const glm::vec2 centerUV((float(centerFX) + 0.5f) / 160.0f, (float(centerFY) + 0.5f) / 90.0f);
            const glm::vec2 offUV((float(offFX) + 0.5f) / 160.0f, (float(offFY) + 0.5f) / 90.0f);
            const glm::vec3 centerRay = ComputeRayDirWorldPerUnitZ(invProj, invView, centerUV);
            const glm::vec3 offRay = ComputeRayDirWorldPerUnitZ(invProj, invView, offUV);

            for (uint32_t z : { 0u, 15u, 31u, 47u, 63u }) {
                std::vector<glm::vec4> scatSlice, intSlice;
                RENDER_CHECK(Read3DSliceRGBA16F(*scatteringVol, z, scatSlice) &&
                             Read3DSliceRGBA16F(*integratedVol, z, intSlice),
                    "M15-U6-02.1 / 03.1: read FroxelScattering & FroxelIntegrated Z slices");

                auto checkFroxelCoord = [&](uint32_t fx, uint32_t fy, const glm::vec3& rayPerUnitZ,
                                            float& outT, const char* label) -> bool {
                    const size_t idx = size_t(fy) * 160u + fx;
                    const glm::vec4 gpuScat = scatSlice[idx];
                    const glm::vec4 gpuInt = intSlice[idx];

                    const glm::vec3 unitRay = glm::normalize(rayPerUnitZ);
                    const float cosTheta = glm::dot(unitRay, media.SunDirectionToLight);
                    const float phase = FroxelFogContract::EvaluateHenyeyGreenstein(cosTheta, media.ScatteringAsymmetry);
                    const glm::vec3 expectedInScatPerMeter = (media.Albedo * media.EffectiveBaseDensity) *
                        (media.SunRadiance * phase + media.AmbientRadiance);

                    const float zEnd = FroxelFogContract::SliceBoundaryToViewDistance(
                        z + 1u, 64u, media.NearDistance, media.MaxDistance);
                    const auto analytic = FroxelFogContract::EvaluateAnalyticHomogeneousFog(
                        media, unitRay, zEnd, glm::length(rayPerUnitZ));

                    const bool scatOk =
                        glm::length(glm::vec3(gpuScat) - expectedInScatPerMeter) < 3e-3f &&
                        std::abs(gpuScat.a - media.EffectiveBaseDensity) < 5e-4f;
                    const bool intOk =
                        glm::length(glm::vec3(gpuInt) - analytic.InScattering) < 4e-3f &&
                        std::abs(gpuInt.a - analytic.Transmittance) < 4e-3f;
                    RENDER_CHECK(scatOk && intOk, label);
                    outT = gpuInt.a;
                    return true;
                };

                float centerT = 1.0f, offT = 1.0f;
                RENDER_CHECK(checkFroxelCoord(centerFX, centerFY, centerRay, centerT,
                    "M15-U6-02.1 / 03.1 / 03.2: center froxel scattering & integrated integral match analytic Beer-Lambert") &&
                             checkFroxelCoord(offFX, offFY, offRay, offT,
                    "M15-U6-03.2: off-axis oblique froxel ray matches Euclidean ds analytic Beer-Lambert"),
                    "M15-U6-02.1 / 03.1 / 03.2: froxel slice readback assertions");
                if (z >= 31u) {
                    RENDER_CHECK(offT < centerT,
                        "M15-U6-03.2: off-axis oblique froxel has strictly lower transmittance than center ray");
                }
            }

            const glm::vec3 gpuCenterHDR = SampleHDRBlockAvg(hdrUniform, kViewW, kViewW / 2u, kViewH / 2u);
            const auto analytic40 = FroxelFogContract::EvaluateAnalyticHomogeneousFog(
                media, glm::normalize(glm::vec3(0.0f, 0.0f, -1.0f)), 40.0f, 1.0f);
            const glm::vec3 expectedHDR40 = wallBaselineHDR * analytic40.Transmittance + analytic40.InScattering;
            RENDER_CHECK(glm::length(gpuCenterHDR - expectedHDR40) < 0.03f,
                "M15-U6-03.1 / 03.2: final composited HDR center pixel at z=40m matches analytic transmittance & in-scattering");

            physicsLog << "caseA_wall_base=(" << wallBaselineHDR.r << "," << wallBaselineHDR.g << "," << wallBaselineHDR.b << ") "
                       << "gpu_hdr=(" << gpuCenterHDR.r << "," << gpuCenterHDR.g << "," << gpuCenterHDR.b << ") "
                       << "ref_hdr=(" << expectedHDR40.r << "," << expectedHDR40.g << "," << expectedHDR40.b << ")\n";
        }

        // -----------------------------------------------------------------
        // Case B (M15-U6-03.2): Height-Falloff Density & Reference Quadrature
        // -----------------------------------------------------------------
        {
            RenderWorld heightWorld;
            heightWorld.SceneRevision = 9202u;
            heightWorld.DirectionalLight = sun;

            PerspectiveCamera pitchedCam(60.0f, float(kViewW) / float(kViewH), 0.1f, 300.0f);
            const glm::vec3 pitchedOrigin(0.0f, 4.0f, 0.0f);
            pitchedCam.SetViewMatrix(glm::lookAtRH(pitchedOrigin, glm::vec3(0.0f, 1.0f, -10.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                                     pitchedOrigin);
            const glm::mat4 pInvView = glm::inverse(pitchedCam.GetViewMatrix());
            const glm::mat4 pInvProj = glm::inverse(pitchedCam.GetProjectionMatrixZO());

            SceneRenderSettings heightSettings = makeBaseSettings();
            heightSettings.ClearColor = glm::vec4(0.5f, 0.4f, 0.3f, 1.0f);
            heightSettings.FogDensity = 0.12f;
            heightSettings.FogHeightFalloff = 0.25f;
            heightSettings.FogScattering = 0.4f;
            heightSettings.FogMaxDistance = 80.0f;

            std::vector<glm::vec4> hdrHeight;
            RENDER_CHECK(renderAndReadHDR(heightWorld, pitchedCam, heightSettings, hdrHeight, diag),
                "M15-U6-03.2: height-falloff HDR sky render succeeds");

            const auto media = FroxelFogContract::ResolveMediaState(
                heightWorld, heightSettings, lightingSvc.BuildFrame(heightWorld, heightSettings));
            const glm::vec3 centerRay = ComputeRayDirWorldPerUnitZ(pInvProj, pInvView, glm::vec2(0.5f, 0.5f));
            const auto refSky = FroxelFogContract::EvaluateReferenceRayQuadrature(
                media, pitchedOrigin, centerRay, media.MaxDistance, 2048u);
            const glm::vec3 expectedSkyHDR = glm::vec3(heightSettings.ClearColor) * refSky.Transmittance + refSky.InScattering;
            const glm::vec3 gpuSkyHDR = SampleHDRBlockAvg(hdrHeight, kViewW, kViewW / 2u, kViewH / 2u);
            RENDER_CHECK(glm::length(gpuSkyHDR - expectedSkyHDR) < 0.02f,
                "M15-U6-03.2: GPU height-falloff HDR pixel matches 2048-step reference ray quadrature");

            physicsLog << "caseB_gpu_hdr=(" << gpuSkyHDR.r << "," << gpuSkyHDR.g << "," << gpuSkyHDR.b << ") "
                       << "ref_hdr=(" << expectedSkyHDR.r << "," << expectedSkyHDR.g << "," << expectedSkyHDR.b << ")\n";
        }

        // -----------------------------------------------------------------
        // Case C (M15-U6-03.2): Henyey-Greenstein Forward vs Backward + Component Override
        // -----------------------------------------------------------------
        {
            RenderWorld phaseWorld;
            phaseWorld.SceneRevision = 9203u;
            RenderDirectionalLight forwardSun{};
            forwardSun.Direction = glm::normalize(glm::vec3(0.0f, 0.15f, -1.0f));
            forwardSun.Color = glm::vec3(1.0f, 0.96f, 0.90f);
            forwardSun.Intensity = 3.0f;
            forwardSun.AmbientIntensity = 0.0f;
            phaseWorld.DirectionalLight = forwardSun;
            phaseWorld.VolumetricFog = RenderVolumetricFog{
                .Enabled = true,
                .Density = 0.05f,
                .HeightFalloff = 0.0f,
                .ScatteringAsymmetry = 0.72f,
                .Color = glm::vec3(0.85f, 0.9f, 1.0f),
                .MaxDistance = 80.0f,
                .Intensity = 1.4f
            };

            // Global settings deliberately conflict so VolumetricFogComponent override is proven
            SceneRenderSettings phaseSettings = makeBaseSettings();
            phaseSettings.FogDensity = 0.001f;
            phaseSettings.FogScattering = 0.0f;
            phaseSettings.FogMaxDistance = phaseSettings.FogNearDistance;

            PerspectiveCamera camToward(60.0f, float(kViewW) / float(kViewH), 0.1f, 300.0f);
            camToward.SetViewMatrix(glm::lookAtRH(camPos, camPos + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f)), camPos);
            PerspectiveCamera camAway(60.0f, float(kViewW) / float(kViewH), 0.1f, 300.0f);
            camAway.SetViewMatrix(glm::lookAtRH(camPos, camPos + glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f)), camPos);

            std::vector<glm::vec4> hdrToward, hdrAway;
            RENDER_CHECK(renderAndReadHDR(phaseWorld, camToward, phaseSettings, hdrToward, diag),
                "M15-U6-03.2: forward-looking Henyey-Greenstein render succeeds");
            const glm::vec3 fwdGlow = SampleHDRBlockAvg(hdrToward, kViewW, kViewW / 2u, kViewH / 2u);
            RENDER_CHECK(renderAndReadHDR(phaseWorld, camAway, phaseSettings, hdrAway, diag),
                "M15-U6-03.2: away-looking Henyey-Greenstein render succeeds");
            const glm::vec3 backGlow = SampleHDRBlockAvg(hdrAway, kViewW, kViewW / 2u, kViewH / 2u);

            const float cosFwd = glm::dot(glm::vec3(0.0f, 0.0f, -1.0f), forwardSun.Direction);
            const float expectedRatio =
                FroxelFogContract::EvaluateHenyeyGreenstein(cosFwd, 0.72f) /
                FroxelFogContract::EvaluateHenyeyGreenstein(-cosFwd, 0.72f);
            const float measuredRatio = fwdGlow.r / std::max(backGlow.r, 1e-6f);
            RENDER_CHECK(fwdGlow.r > 10.0f * backGlow.r &&
                         std::abs(measuredRatio - expectedRatio) / expectedRatio < 0.03f,
                "M15-U6-03.2: component-overridden fog produces exact forward-vs-back Henyey-Greenstein scattering ratio");

            physicsLog << "caseC_fwd=" << fwdGlow.r << " back=" << backGlow.r
                       << " measured_ratio=" << measuredRatio << " expected_ratio=" << expectedRatio << "\n";
        }

        // -----------------------------------------------------------------
        // Case D (M15-U6-05.3): Zero Density & Zero Intensity Exact HDR Identity
        // -----------------------------------------------------------------
        {
            auto verifyExactZeroIdentity = [&](const RenderWorld& world, const SceneRenderSettings& settings, const char* label) -> bool {
                std::vector<glm::vec4> hdrZero;
                RENDER_CHECK(renderAndReadHDR(world, cam, settings, hdrZero, diag), label);

                for (uint32_t z : { 0u, 31u, 63u }) {
                    std::vector<glm::vec4> scatSlice, intSlice;
                    RENDER_CHECK(Read3DSliceRGBA16F(*scatteringVol, z, scatSlice) &&
                                 Read3DSliceRGBA16F(*integratedVol, z, intSlice),
                        "M15-U6-05.3: read froxel slices under zero density/intensity");
                    const glm::vec4 centerScat = scatSlice[size_t(45u) * 160u + 80u];
                    const glm::vec4 centerInt = intSlice[size_t(45u) * 160u + 80u];
                    RENDER_CHECK(centerScat == glm::vec4(0.0f, 0.0f, 0.0f, 0.0f) &&
                                 centerInt == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
                        "M15-U6-05.3: zero density/intensity injects (0,0,0,0) and integrates to exact (0,0,0,1)");
                }

                RENDER_CHECK(hdrZero == hdrOffPixels,
                    "M15-U6-05.3: zero density/intensity preserves RGBA16F linear HDR image identically to Fog-OFF");
                return true;
            };

            SceneRenderSettings zeroDensitySettings = makeBaseSettings();
            zeroDensitySettings.FogDensity = 0.0f;
            RENDER_CHECK(verifyExactZeroIdentity(litWallWorld, zeroDensitySettings, "M15-U6-05.3: Density=0 HDR render completes"),
                "M15-U6-05.3: zero density HDR identity");

            RenderWorld zeroIntWorld = litWallWorld;
            zeroIntWorld.VolumetricFog = RenderVolumetricFog{
                .Enabled = true,
                .Density = 0.12f,
                .HeightFalloff = 0.0f,
                .ScatteringAsymmetry = 0.6f,
                .Color = glm::vec3(0.8f, 0.9f, 1.0f),
                .MaxDistance = 90.0f,
                .Intensity = 0.0f
            };
            SceneRenderSettings positiveFallbackSettings = makeBaseSettings();
            positiveFallbackSettings.FogDensity = 0.08f;
            RENDER_CHECK(verifyExactZeroIdentity(zeroIntWorld, positiveFallbackSettings, "M15-U6-05.3: Intensity=0 HDR render completes"),
                "M15-U6-05.3: zero intensity HDR identity");
        }

        // -----------------------------------------------------------------
        // Case E (M15-U6-06): Unlit Scene No Invented Sun/Ambient + Diagnostics
        // -----------------------------------------------------------------
        {
            RenderWorld unlitAbsorptiveWorld;
            unlitAbsorptiveWorld.SceneRevision = 9205u;
            unlitAbsorptiveWorld.Meshes.push_back(wallInst); // Emissive wall only, NO DirectionalLight, NO Skybox

            SceneRenderSettings unlitSettings = makeBaseSettings();
            unlitSettings.FogDensity = 0.04f;
            unlitSettings.FogHeightFalloff = 0.0f;
            unlitSettings.FogMaxDistance = 100.0f;

            std::vector<glm::vec4> hdrUnlitFogOff;
            auto unlitOffSettings = unlitSettings;
            unlitOffSettings.EnableVolumetricFog = false;
            RENDER_CHECK(renderAndReadHDR(unlitAbsorptiveWorld, cam, unlitOffSettings, hdrUnlitFogOff, diag),
                "M15-U6-06: unlit emissive Fog-OFF baseline renders");
            const glm::vec3 unlitWallBase = SampleHDRBlockAvg(hdrUnlitFogOff, kViewW, kViewW / 2u, kViewH / 2u);

            std::vector<glm::vec4> hdrUnlitFogOn;
            RENDER_CHECK(renderAndReadHDR(unlitAbsorptiveWorld, cam, unlitSettings, hdrUnlitFogOn, diag),
                "M15-U6-06: unlit absorptive fog render succeeds");
            RENDER_CHECK(HasFallbackPrefix(diag, "froxel_fog_no_directional_sun"),
                "M15-U6-06: unlit fog records explicit froxel_fog_no_directional_sun diagnostic");

            std::vector<glm::vec4> scatSlice, intSlice;
            RENDER_CHECK(Read3DSliceRGBA16F(*scatteringVol, 47u, scatSlice) &&
                         Read3DSliceRGBA16F(*integratedVol, 47u, intSlice),
                "M15-U6-06: read unlit froxel slices");
            const glm::vec4 unlitScat = scatSlice[size_t(45u) * 160u + 80u];
            const glm::vec4 unlitInt = intSlice[size_t(45u) * 160u + 80u];
            RENDER_CHECK(glm::vec3(unlitScat) == glm::vec3(0.0f) &&
                         std::abs(unlitScat.a - 0.04f) < 5e-4f &&
                         glm::vec3(unlitInt) == glm::vec3(0.0f) &&
                         unlitInt.a > 0.0f && unlitInt.a < 0.99f,
                "M15-U6-06: unlit scene has exact zero in-scattering while retaining Beer-Lambert extinction");

            const glm::vec3 gpuUnlitHDR = SampleHDRBlockAvg(hdrUnlitFogOn, kViewW, kViewW / 2u, kViewH / 2u);
            const glm::vec3 expectedUnlitHDR = unlitWallBase * std::exp(-0.04f * 40.0f);
            RENDER_CHECK(glm::length(gpuUnlitHDR - expectedUnlitHDR) < 0.02f,
                "M15-U6-06: unlit fog attenuates emissive HDR wall purely by Beer-Lambert without fake sun glow");

            // Invalid authored MaxDistance fallback emits froxel_fog_depth_fallback without failing render
            RenderWorld badDistWorld = litWallWorld;
            badDistWorld.VolumetricFog = RenderVolumetricFog{
                .Enabled = true,
                .Density = 0.03f,
                .HeightFalloff = 0.0f,
                .ScatteringAsymmetry = 0.5f,
                .Color = glm::vec3(0.8f, 0.8f, 0.8f),
                .MaxDistance = 0.05f,
                .Intensity = 1.0f
            };
            std::vector<glm::vec4> hdrFallback;
            RENDER_CHECK(renderAndReadHDR(badDistWorld, cam, unlitSettings, hdrFallback, diag) &&
                         HasFallbackPrefix(diag, "froxel_fog_depth_fallback"),
                "M15-U6-06: invalid authored MaxDistance falls back cleanly and emits froxel_fog_depth_fallback");

            physicsLog << "caseE_unlit_gpu=(" << gpuUnlitHDR.r << "," << gpuUnlitHDR.g << "," << gpuUnlitHDR.b << ") "
                       << "expected=(" << expectedUnlitHDR.r << "," << expectedUnlitHDR.g << "," << expectedUnlitHDR.b << ")\n";
        }

        // Dense fog against a wall inside the first slice. Orthographic rays have
        // different origins but identical path lengths across the screen.
        {
            auto ortho = cam;
            ortho.SetOrthographic(160.0f, float(kViewW) / float(kViewH), 0.1f, 300.0f);
            auto settings = makeBaseSettings();
            settings.FogNearDistance = 20.0f;
            settings.FogDensity = 4.0f;
            const float z1 = FroxelFogContract::SliceBoundaryToViewDistance(1, 64,
                settings.FogNearDistance, settings.FogMaxDistance);
            const float wallZ = (settings.FogNearDistance + z1) * 0.5f;
            RenderWorld world;
            auto wall = wallInst;
            wall.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, 1.5f, -wallZ - 0.5f}),
                                             glm::vec3(500.0f, 500.0f, 1.0f));
            world.Meshes.push_back(wall);
            std::vector<glm::vec4> pixels;
            RENDER_CHECK(renderAndReadHDR(world, ortho, settings, pixels, diag),
                "Orthographic dense fog renders against an opaque surface inside the first slice");
            const glm::vec3 expected = wallBaselineHDR *
                std::exp(-settings.FogDensity * (wallZ - settings.FogNearDistance));
            const auto center = SampleHDRBlockAvg(pixels, kViewW, kViewW / 2, kViewH / 2);
            const auto offAxis = SampleHDRBlockAvg(pixels, kViewW, kViewW / 8, kViewH / 2);
            RENDER_CHECK(glm::length(center - expected) < 0.012f &&
                         glm::length(offAxis - expected) < 0.012f,
                "Partial first slice follows Beer-Lambert at center and off axis in an orthographic view");
            physicsLog << "partial_orthographic center=" << center.r << " off_axis=" << offAxis.r
                       << " expected=" << expected.r << '\n';

            ortho.SetViewMatrix(glm::mat4(1.0f), glm::vec3(0.0f));
            settings.FogHeightFalloff = 4.0f;
            auto sampleBoundary = [&](float z, glm::vec3& sample) {
                world.Meshes.front().WorldTransform[3].z = -z - 0.5f;
                if (!renderAndReadHDR(world, ortho, settings, pixels, diag)) return false;
                sample = SampleHDRBlockAvg(pixels, kViewW, kViewW / 2, kViewH / 2);
                return true;
            };
            glm::vec3 before, after;
            RENDER_CHECK(sampleBoundary(z1 - 0.001f, before) && sampleBoundary(z1 + 0.001f, after) &&
                         glm::length(before - after) < 0.015f,
                "XY filtering stays continuous across a depth boundary in strongly stratified fog");
            const auto upper = SampleHDRBlockAvg(pixels, kViewW, kViewW / 2, kViewH / 4);
            const auto lower = SampleHDRBlockAvg(pixels, kViewW, kViewW / 2, 3 * kViewH / 4);
            RENDER_CHECK(upper.r > 2.0f && lower.r < 0.4f,
                "Orthographic columns inject local density at their distinct world-space origins");
            physicsLog << "stratified_boundary before=" << before.r << " after=" << after.r << '\n';
        }

        // A finite depth in the last few D32 values still belongs to the wall.
        {
            auto farCamera = cam;
            farCamera.SetPerspective(60.0f, float(kViewW) / float(kViewH), 0.1f, 200.0f);
            auto settings = makeBaseSettings();
            settings.FogDensity = 0.004f;
            settings.FogMaxDistance = 600.0f;
            constexpr float wallZ = 199.9f;
            RenderWorld world;
            auto wall = wallInst;
            wall.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, 1.5f, -wallZ - 0.025f}),
                                             glm::vec3(800.0f, 800.0f, 0.05f));
            world.Meshes.push_back(wall);
            std::vector<glm::vec4> pixels;
            RENDER_CHECK(renderAndReadHDR(world, farCamera, settings, pixels, diag),
                "Distant opaque surface renders near the camera far clip");
            const auto center = SampleHDRBlockAvg(pixels, kViewW, kViewW / 2, kViewH / 2);
            const glm::vec3 expected = wallBaselineHDR *
                std::exp(-settings.FogDensity * (wallZ - settings.FogNearDistance));
            RENDER_CHECK(glm::length(center - expected) < 0.012f,
                "Finite D32 depth near 1 uses the opaque surface distance");
            physicsLog << "distant_surface center=" << center.r << " expected=" << expected.r << '\n';
        }

        physicsLog.close();
        RENDER_CHECK(physicsLog.good(), "M15-U6-B: physics verification log flushed cleanly");

        // ---------------------------------------------------------------------
        // 4. M15-U6-C Native GPU Verification:
        //    - M15-U6-02.2: Directional CSM + local spot/point/rect light injection
        //    - M15-U6-07.1: Wall occlusion behind blockers (CSM + local shadows)
        //    - M15-U6-07.2: Light shafts through blocker slit & zero shafts without sun
        // ---------------------------------------------------------------------
        {
            std::ofstream shadowLog(output / "M15_U6C_froxel_shadows.txt");
            RENDER_CHECK(shadowLog.is_open(), "M15-U6-C: open shadow verification log");

            RHIMaterialDesc blockerDesc{};
            blockerDesc.SurfaceType = SurfaceType::Opaque;
            blockerDesc.CullMode = CullMode::None;
            blockerDesc.BaseColor = glm::vec4(0.01f, 0.01f, 0.01f, 1.0f);
            blockerDesc.Metallic = 0.0f;
            blockerDesc.Roughness = 1.0f;
            auto blockerMat = std::make_shared<RHIMaterial>(blockerDesc);

            auto makeBlocker = [&](uint32_t entityId, const glm::vec3& center, const glm::vec3& scale) {
                RenderMeshInstance wall{};
                wall.EntityID = entityId;
                wall.NativeView = cubeView;
                wall.Material.RHIMaterialAsset = blockerMat;
                wall.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), center), scale);
                wall.CastsShadows = true;
                return wall;
            };

            auto sliceDepthAt = [](uint32_t k, float nearD, float maxD) {
                return nearD * std::pow(maxD / nearD, (static_cast<float>(k) + 0.5f) / 64.0f);
            };
            auto sliceNearestDepth = [](float depthMeters, float nearD, float maxD) {
                const float t = std::log(depthMeters / nearD) / std::log(maxD / nearD);
                const int k = static_cast<int>(std::round(t * 64.0f - 0.5f));
                return static_cast<uint32_t>(std::clamp(k, 0, 63));
            };
            auto froxelSample = [&](uint32_t ix, uint32_t iy, uint32_t kz, float nearD, float maxD,
                                    glm::vec3& outWorldPos, glm::vec3& outRayDirWS) {
                const float u = (static_cast<float>(ix) + 0.5f) / 160.0f;
                const float v = (static_cast<float>(iy) + 0.5f) / 90.0f;
                const glm::vec4 clip(u * 2.0f - 1.0f, (1.0f - v) * 2.0f - 1.0f, 1.0f, 1.0f);
                const glm::vec4 viewH = invProj * clip;
                const glm::vec3 viewDir = glm::normalize(glm::vec3(viewH) / viewH.w);
                const float zSlice = sliceDepthAt(kz, nearD, maxD);
                const glm::vec3 viewPos = viewDir * (zSlice / std::max(-viewDir.z, 1e-4f));
                outWorldPos = glm::vec3(invView * glm::vec4(viewPos, 1.0f));
                outRayDirWS = glm::normalize(glm::vec3(invView * glm::vec4(viewDir, 0.0f)));
            };
            auto froxelAt = [](const std::vector<glm::vec4>& slice, uint32_t ix, uint32_t iy) {
                return glm::vec3(slice[size_t(iy) * 160u + ix]);
            };
            auto avgHDR = [&](const std::vector<glm::vec4>& img, float u, float v) {
                const uint32_t cx = std::clamp(static_cast<uint32_t>(u * static_cast<float>(kViewW)), 2u, kViewW - 3u);
                const uint32_t cy = std::clamp(static_cast<uint32_t>(v * static_cast<float>(kViewH)), 2u, kViewH - 3u);
                return SampleHDRBlockAvg(img, kViewW, cx, cy);
            };

            // C1. M15-U6-07.1: Directional CSM wall occlusion behind blocker
            {
                auto settings = makeBaseSettings();
                settings.EnableShadows = true;
                settings.ShadowCascadeCount = 4;
                settings.FogDensity = 0.03f;
                settings.FogHeightFalloff = 0.3f;
                settings.FogScattering = 0.0f;
                settings.FogColor = glm::vec3(0.9f, 0.9f, 0.9f);
                settings.FogNearDistance = 0.25f;
                settings.FogMaxDistance = 35.0f;

                RenderWorld world;
                world.DirectionalLight = RenderDirectionalLight{
                    glm::normalize(glm::vec3(1.0f, 0.15f, 0.0f)),
                    glm::vec3(1.0f, 0.96f, 0.90f),
                    2.5f,
                    0.0f};
                // Matte wall off-axis at x=+5.5 spanning z in [-35.5, -3.5], casting shadow across camera center/left
                world.Meshes.push_back(makeBlocker(9201u, {5.5f, 1.5f, -19.5f}, {2.0f, 30.0f, 32.0f}));

                auto offSettings = settings;
                offSettings.EnableShadows = false;

                std::vector<glm::vec4> hdrOff, hdrOn, fgOff, fgOn, bgOff, bgOn;
                const uint32_t kFg = sliceNearestDepth(2.0f, settings.FogNearDistance, settings.FogMaxDistance);
                const uint32_t kBg = sliceNearestDepth(19.0f, settings.FogNearDistance, settings.FogMaxDistance);
                constexpr uint32_t fx = 56u; // u = 0.35 (left-center clear view ray)
                constexpr uint32_t fy = 45u;

                RENDER_CHECK(renderAndReadHDR(world, cam, offSettings, hdrOff, diag) &&
                             Read3DSliceRGBA16F(*scatteringVol, kFg, fgOff) &&
                             Read3DSliceRGBA16F(*scatteringVol, kBg, bgOff),
                    "M15-U6-07.1 (CSM): render unshadowed baseline");

                RENDER_CHECK(renderAndReadHDR(world, cam, settings, hdrOn, diag) &&
                             renderer.GetCascadedShadows().IsActive() &&
                             Read3DSliceRGBA16F(*scatteringVol, kFg, fgOn) &&
                             Read3DSliceRGBA16F(*scatteringVol, kBg, bgOn),
                    "M15-U6-07.1 (CSM): render shadowed blocker scene");

                const auto sameExtinction = [](const auto& a, const auto& b) {
                    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                        [](const glm::vec4& lhs, const glm::vec4& rhs) { return lhs.a == rhs.a; });
                };
                RENDER_CHECK(sameExtinction(fgOff, fgOn) && sameExtinction(bgOff, bgOn),
                    "Enabling shadow maps leaves the entire stratified medium extinction slice unchanged");

                const glm::vec3 sFgOff = froxelAt(fgOff, fx, fy);
                const glm::vec3 sFgOn = froxelAt(fgOn, fx, fy);
                const glm::vec3 sBgOff = froxelAt(bgOff, fx, fy);
                const glm::vec3 sBgOn = froxelAt(bgOn, fx, fy);
                const glm::vec3 pixOff = avgHDR(hdrOff, 0.35f, 0.5f);
                const glm::vec3 pixOn = avgHDR(hdrOn, 0.35f, 0.5f);
                std::cout << "  [U6-C CSM] fgOff=" << glm::length(sFgOff) << " fgOn=" << glm::length(sFgOn)
                          << " bgOff=" << glm::length(sBgOff) << " bgOn=" << glm::length(sBgOn)
                          << " pixOff=" << glm::length(pixOff) << " pixOn=" << glm::length(pixOn) << "\n";

                RENDER_CHECK(glm::length(sFgOn) > 0.01f &&
                             glm::length(sFgOn - sFgOff) < 0.05f * glm::length(sFgOff) &&
                             glm::length(sBgOff) > 0.01f &&
                             glm::length(sBgOn) < 0.05f * glm::length(sBgOff) &&
                             glm::length(pixOn) < 0.20f * glm::length(pixOff),
                    "M15-U6-07.1: directional CSM blocker wall occludes froxels behind wall while keeping foreground lit");
                shadowLog << "csm fg_off=" << glm::length(sFgOff) << " fg_on=" << glm::length(sFgOn)
                          << " bg_off=" << glm::length(sBgOff) << " bg_on=" << glm::length(sBgOn)
                          << " hdr_off=" << glm::length(pixOff) << " hdr_on=" << glm::length(pixOn) << '\n';

                RHISceneRenderer fallbackRenderer;
                bool faultReached = false;
                {
                    ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                        if (operation == "CreateSampler" && ScopedRHIOperation::Resource() == "FogShadowSampler") {
                            faultReached = true;
                            return nri::Result::OUT_OF_MEMORY;
                        }
                        return {};
                    });
                    RENDER_CHECK(fallbackRenderer.Init(device, false) && faultReached,
                        "A failed fog comparison sampler leaves the renderer available");
                }
                fallbackRenderer.RenderWorldView(world, cam, &hdrColor, &hdrDepth, settings, nullptr, targets);
                std::vector<glm::vec4> fallbackSlice;
                RENDER_CHECK(fallbackRenderer.WasLastRenderSuccessful() &&
                             fallbackRenderer.GetCascadedShadows().IsActive() &&
                             HasFallbackPrefix(fallbackRenderer.GetDiagnostics().GetFrame(), "froxel_shadows_unavailable") &&
                             HasPassNamed(fallbackRenderer.GetDiagnostics().GetFrame(), "FogApply") &&
                             Read3DSliceRGBA16F(*scatteringVol, kBg, fallbackSlice) && fallbackSlice == bgOff,
                    "Fog with no comparison sampler matches the unshadowed volume while surface CSM remains active");
                fallbackRenderer.Shutdown();
            }

            // C2. M15-U6-07.1 & 02.2: Local shadowed spot light & point light wall occlusion
            {
                auto settings = makeBaseSettings();
                settings.EnableShadows = true;
                settings.LocalShadowResolution = 256u;
                settings.FogDensity = 0.08f;
                settings.FogHeightFalloff = 0.0f;
                settings.FogScattering = 0.0f;
                settings.FogColor = glm::vec3(1.0f);
                settings.FogNearDistance = 0.25f;
                settings.FogMaxDistance = 40.0f;
                const uint32_t kMid = sliceNearestDepth(15.0f, settings.FogNearDistance, settings.FogMaxDistance);

                // C2a: Spot light shining from x=-6 across x=+4 with blocker wall at x=-1
                RenderWorld spotWorld;
                RenderSpotLight spot{};
                spot.EntityID = 9210u;
                spot.Position = glm::vec3(-6.0f, 1.5f, -15.0f);
                spot.Direction = glm::vec3(1.0f, 0.0f, 0.0f);
                spot.Range = 30.0f;
                spot.Intensity = 4000.0f;
                spot.Color = glm::vec3(1.0f, 0.65f, 0.25f);
                spot.InnerAngle = 25.0f;
                spot.OuterAngle = 45.0f;
                spot.Shadows.CastShadows = true;
                spotWorld.SpotLights.push_back(spot);
                spotWorld.Meshes.push_back(makeBlocker(9211u, {-1.0f, 1.5f, -15.0f}, {0.8f, 12.0f, 12.0f}));

                auto offSettings = settings;
                offSettings.EnableShadows = false;
                std::vector<glm::vec4> hdrOff, hdrOn, volOff, volOn;
                RENDER_CHECK(renderAndReadHDR(spotWorld, cam, offSettings, hdrOff, diag) &&
                             Read3DSliceRGBA16F(*scatteringVol, kMid, volOff),
                    "M15-U6-07.1 (Spot): render unshadowed spot fog");
                const auto offLighting = lightingSvc.BuildFrame(spotWorld, offSettings);
                const auto offMedia = FroxelFogContract::ResolveMediaState(spotWorld, offSettings, offLighting);

                RENDER_CHECK(renderAndReadHDR(spotWorld, cam, settings, hdrOn, diag) &&
                             renderer.GetLocalShadows().IsActive() &&
                             renderer.GetLocalShadows().GetFrame().SpotViews[0] >= 0 &&
                             Read3DSliceRGBA16F(*scatteringVol, kMid, volOn),
                    "M15-U6-07.1 (Spot): render shadowed spot blocker fog");

                constexpr uint32_t fxSpotLit = 58u;    // u ~ 0.36 (in front of blocker wall)
                constexpr uint32_t fxSpotShadow = 102u; // u ~ 0.64 (behind blocker wall)
                glm::vec3 sampleWS{}, rayWS{};
                froxelSample(fxSpotLit, 45u, kMid, settings.FogNearDistance, settings.FogMaxDistance, sampleWS, rayWS);
                const glm::vec3 cpuSpotLit = FroxelFogContract::EvaluateSpotLightInScatter(
                    offMedia, offLighting.SpotLights[0], sampleWS, rayWS, 1.0f);
                const glm::vec3 vSpotLitOff = froxelAt(volOff, fxSpotLit, 45u);
                const glm::vec3 vSpotLitOn = froxelAt(volOn, fxSpotLit, 45u);
                const glm::vec3 vSpotShdOff = froxelAt(volOff, fxSpotShadow, 45u);
                const glm::vec3 vSpotShdOn = froxelAt(volOn, fxSpotShadow, 45u);
                const glm::vec3 hSpotShdOff = avgHDR(hdrOff, 0.64f, 0.5f);
                const glm::vec3 hSpotShdOn = avgHDR(hdrOn, 0.64f, 0.5f);
                std::cout << "  [U6-C Spot] litCpu=" << glm::length(cpuSpotLit) << " litOff=" << glm::length(vSpotLitOff)
                          << " litOn=" << glm::length(vSpotLitOn) << " shdOff=" << glm::length(vSpotShdOff)
                          << " shdOn=" << glm::length(vSpotShdOn) << " hdrOff=" << glm::length(hSpotShdOff)
                          << " hdrOn=" << glm::length(hSpotShdOn) << "\n";

                RENDER_CHECK(glm::length(cpuSpotLit) > 0.05f &&
                             glm::length(vSpotLitOff - cpuSpotLit) < 0.05f * glm::length(cpuSpotLit) &&
                             glm::length(vSpotLitOn - vSpotLitOff) < 0.05f * glm::length(vSpotLitOff) &&
                             glm::length(vSpotShdOff) > 0.01f &&
                             glm::length(vSpotShdOn) < 0.05f * glm::length(vSpotShdOff) &&
                             glm::length(hSpotShdOn) < 0.15f * glm::length(hSpotShdOff),
                    "M15-U6-07.1 / 02.2: shadowed spot light matches CPU on lit side and occludes behind blocker wall");
                shadowLog << "spot lit_cpu=" << glm::length(cpuSpotLit) << " lit_on=" << glm::length(vSpotLitOn)
                          << " shd_off=" << glm::length(vSpotShdOff) << " shd_on=" << glm::length(vSpotShdOn)
                          << " hdr_shd_on=" << glm::length(hSpotShdOn) << "/" << glm::length(hSpotShdOff) << '\n';

                // The outer cone crosses this slice. Samples outside the cone
                // must not contribute neutral visibility to the blocked center.
                RenderWorld edgeWorld;
                auto edgeSpot = spotWorld.SpotLights.front();
                froxelSample(80u, 45u, kMid, settings.FogNearDistance, settings.FogMaxDistance, sampleWS, rayWS);
                edgeSpot.Position = sampleWS + glm::vec3(-6.0f, 0.0f, 5.7f * std::tan(glm::radians(6.0f)));
                edgeSpot.Direction = glm::vec3(1.0f, 0.0f, 0.0f);
                edgeSpot.InnerAngle = edgeSpot.OuterAngle = 6.0f;
                edgeWorld.SpotLights.push_back(edgeSpot);
                edgeWorld.Meshes.push_back(makeBlocker(9212u, sampleWS - glm::vec3(3.0f, 0.0f, 0.0f),
                    {0.5f, 20.0f, 20.0f}));
                RENDER_CHECK(renderAndReadHDR(edgeWorld, cam, offSettings, hdrOff, diag) &&
                             Read3DSliceRGBA16F(*scatteringVol, kMid, volOff) &&
                             renderAndReadHDR(edgeWorld, cam, settings, hdrOn, diag) &&
                             renderer.GetLocalShadows().IsActive() &&
                             Read3DSliceRGBA16F(*scatteringVol, kMid, volOn),
                    "Render the same narrow spotlight cone boundary with shadows off and on");
                const float edgeOff = glm::length(froxelAt(volOff, 80u, 45u));
                const float edgeOn = glm::length(froxelAt(volOn, 80u, 45u));
                RENDER_CHECK(edgeOff > 0.01f && edgeOn < 0.05f * edgeOff,
                    "Unlit taps beyond a narrow spot frustum cannot leak into a blocked froxel");
                shadowLog << "spot_edge off=" << edgeOff << " on=" << edgeOn << '\n';

                // C2b: Point light at (0, 1.5, -15) with blocker wall on +X at x=+2.5
                RenderWorld pointWorld;
                RenderPointLight point{};
                point.EntityID = 9220u;
                point.Position = glm::vec3(0.0f, 1.5f, -15.0f);
                point.Radius = 25.0f;
                point.Intensity = 4000.0f;
                point.Color = glm::vec3(0.25f, 0.75f, 1.0f);
                point.Shadows.CastShadows = true;
                RenderDirectionalLight disabledDirectional{};
                disabledDirectional.Intensity = 0.0f;
                pointWorld.AdditionalDirectionalLights.push_back(disabledDirectional);
                pointWorld.PointLights.push_back(point);
                pointWorld.Meshes.push_back(makeBlocker(9221u, {1.0f, 1.5f, -15.0f}, {0.6f, 16.0f, 20.0f}));

                RENDER_CHECK(renderAndReadHDR(pointWorld, cam, offSettings, hdrOff, diag) &&
                             Read3DSliceRGBA16F(*scatteringVol, kMid, volOff),
                    "M15-U6-07.1 (Point): render unshadowed point fog");
                const auto ptLighting = lightingSvc.BuildFrame(pointWorld, offSettings);
                const auto ptMedia = FroxelFogContract::ResolveMediaState(pointWorld, offSettings, ptLighting);

                RENDER_CHECK(renderAndReadHDR(pointWorld, cam, settings, hdrOn, diag) &&
                             renderer.GetLocalShadows().IsActive() &&
                             renderer.GetLocalShadows().GetFrame().PointViews[0] == -1 &&
                             renderer.GetLocalShadows().GetFrame().PointViews[1] >= 0 &&
                             Read3DSliceRGBA16F(*scatteringVol, kMid, volOn),
                    "M15-U6-07.1 (Point): render shadowed point blocker fog");

                constexpr uint32_t fxPtLit = 45u;    // u = 0.28 (clear left side)
                constexpr uint32_t fxPtShadow = 115u; // u = 0.72 (behind wall at x=+2.5)
                froxelSample(fxPtLit, 45u, kMid, settings.FogNearDistance, settings.FogMaxDistance, sampleWS, rayWS);
                const glm::vec3 cpuPtLit = FroxelFogContract::EvaluatePointLightInScatter(
                    ptMedia, ptLighting.PointLights[1], sampleWS, rayWS, 1.0f);
                const glm::vec3 vPtLitOff = froxelAt(volOff, fxPtLit, 45u);
                const glm::vec3 vPtLitOn = froxelAt(volOn, fxPtLit, 45u);
                const glm::vec3 vPtShdOff = froxelAt(volOff, fxPtShadow, 45u);
                const glm::vec3 vPtShdOn = froxelAt(volOn, fxPtShadow, 45u);
                const glm::vec3 hPtShdOff = avgHDR(hdrOff, 0.72f, 0.5f);
                const glm::vec3 hPtShdOn = avgHDR(hdrOn, 0.72f, 0.5f);
                std::cout << "  [U6-C Point] litCpu=" << glm::length(cpuPtLit) << " litOff=" << glm::length(vPtLitOff)
                          << " litOn=" << glm::length(vPtLitOn) << " shdOff=" << glm::length(vPtShdOff)
                          << " shdOn=" << glm::length(vPtShdOn) << " hdrOff=" << glm::length(hPtShdOff)
                          << " hdrOn=" << glm::length(hPtShdOn) << "\n";

                RENDER_CHECK(glm::length(cpuPtLit) > 0.05f &&
                             glm::length(vPtLitOff - cpuPtLit) < 0.05f * glm::length(cpuPtLit) &&
                             glm::length(vPtLitOn - vPtLitOff) < 0.05f * glm::length(vPtLitOff) &&
                             glm::length(vPtShdOff) > 0.01f &&
                             glm::length(vPtShdOn) < 0.05f * glm::length(vPtShdOff) &&
                             glm::length(hPtShdOn) < 0.20f * glm::length(hPtShdOff),
                    "M15-U6-07.1 / 02.2: shadowed point light matches CPU on lit side and occludes behind blocker wall");
                shadowLog << "point lit_cpu=" << glm::length(cpuPtLit) << " lit_on=" << glm::length(vPtLitOn)
                          << " shd_off=" << glm::length(vPtShdOff) << " shd_on=" << glm::length(vPtShdOn)
                          << " hdr_shd_on=" << glm::length(hPtShdOn) << "/" << glm::length(hPtShdOff) << '\n';

                // C3: Local rect area light injection parity check (unshadowed local emissive scatter)
                RenderWorld rectWorld;
                RenderRectLight rl{};
                rl.EntityID = 9230u;
                rl.Position = glm::vec3(0.0f, 1.5f, -10.0f);
                rl.Direction = glm::vec3(0.0f, 0.0f, -1.0f);
                rl.Up = glm::vec3(0.0f, 1.0f, 0.0f);
                rl.Color = glm::vec3(0.9f, 0.6f, 1.0f);
                rl.Intensity = 120.0f;
                rl.Width = 4.0f;
                rl.Height = 2.0f;
                rl.Range = 20.0f;
                rl.TwoSided = false;
                rl.Shape = AreaLightShape::Rectangle;
                rl.ShadowPolicy = AreaLightShadowPolicy::UnshadowedAnalyticLTC;
                rectWorld.RectLights.push_back(rl);

                std::vector<glm::vec4> hdrRect, volRect;
                RENDER_CHECK(renderAndReadHDR(rectWorld, cam, settings, hdrRect, diag) &&
                             Read3DSliceRGBA16F(*scatteringVol, kMid, volRect),
                    "M15-U6-02.2 (Rect): render rect area light volumetric scatter");
                const auto rectLighting = lightingSvc.BuildFrame(rectWorld, settings);
                const auto rectMedia = FroxelFogContract::ResolveMediaState(rectWorld, settings, rectLighting);
                froxelSample(80u, 45u, kMid, settings.FogNearDistance, settings.FogMaxDistance, sampleWS, rayWS);
                const glm::vec3 cpuRect = FroxelFogContract::EvaluateRectLightInScatter(
                    rectMedia, rectLighting.RectLights[0], sampleWS, rayWS);
                const glm::vec3 gpuRect = froxelAt(volRect, 80u, 45u);
                RENDER_CHECK(glm::length(cpuRect) > 0.001f &&
                             glm::length(gpuRect - cpuRect) < 0.05f * glm::length(cpuRect),
                    "M15-U6-02.2: GPU rect area light froxel scatter matches CPU contract");
                shadowLog << "rect cpu=" << glm::length(cpuRect) << " gpu=" << glm::length(gpuRect) << '\n';

                rectWorld.RectLights.front().Shape = AreaLightShape::Disk;
                rectWorld.RectLights.front().TwoSided = true;
                rectWorld.RectLights.front().Direction = -rl.Direction;
                RENDER_CHECK(renderAndReadHDR(rectWorld, cam, settings, hdrRect, diag) &&
                             Read3DSliceRGBA16F(*scatteringVol, kMid, volRect),
                    "Render a two-sided disk through its back face");
                const auto diskLighting = lightingSvc.BuildFrame(rectWorld, settings);
                const auto cpuDisk = FroxelFogContract::EvaluateRectLightInScatter(rectMedia,
                    diskLighting.RectLights[0], sampleWS, rayWS);
                const auto gpuDisk = froxelAt(volRect, 80u, 45u);
                RENDER_CHECK(glm::length(cpuDisk) > 0.0f && glm::length(gpuDisk) < glm::length(gpuRect) &&
                             glm::length(gpuDisk - cpuDisk) < 0.05f * glm::length(cpuDisk),
                    "Disk uses its smaller emitter area and the two-sided flag");

                RenderWorld directionalWorld;
                RenderDirectionalLight secondary;
                secondary.Direction = glm::vec3(0.0f, 0.0f, -1.0f);
                secondary.Intensity = 2.0f;
                directionalWorld.AdditionalDirectionalLights.push_back(secondary);
                RENDER_CHECK(renderAndReadHDR(directionalWorld, cam, settings, hdrRect, diag) &&
                             Read3DSliceRGBA16F(*scatteringVol, kMid, volRect),
                    "Render a packed additional directional light without a primary sun");
                const auto secondaryLighting = lightingSvc.BuildFrame(directionalWorld, settings);
                const auto cpuSecondary = FroxelFogContract::EvaluatePointLightInScatter(rectMedia,
                    secondaryLighting.PointLights[0], sampleWS, rayWS);
                const auto gpuSecondary = froxelAt(volRect, 80u, 45u);
                RENDER_CHECK(glm::length(cpuSecondary) > 0.01f &&
                             glm::length(gpuSecondary - cpuSecondary) < 0.05f * glm::length(cpuSecondary),
                    "A packed additional directional light contributes its authored radiance");
                shadowLog << "disk cpu=" << glm::length(cpuDisk) << " gpu=" << glm::length(gpuDisk)
                          << " additional_directional cpu=" << glm::length(cpuSecondary)
                          << " gpu=" << glm::length(gpuSecondary) << '\n';
            }

            // C4. M15-U6-07.2: Bounded light shafts through blocker slit & zero shafts without directional sun
            {
                auto settings = makeBaseSettings();
                settings.EnableShadows = true;
                settings.ShadowCascadeCount = 4;
                settings.FogDensity = 0.04f;
                settings.FogHeightFalloff = 0.0f;
                settings.FogScattering = 0.5f;
                settings.FogColor = glm::vec3(0.95f, 0.92f, 0.85f);
                settings.FogNearDistance = 0.25f;
                settings.FogMaxDistance = 30.0f;

                RenderWorld slitWorld;
                slitWorld.DirectionalLight = RenderDirectionalLight{
                    glm::normalize(glm::vec3(0.0f, 0.25f, -1.0f)),
                    glm::vec3(1.0f, 0.96f, 0.88f),
                    3.5f,
                    0.0f};
                // Two blocker walls at z = -24 leaving an open slit x in (-0.8, +0.8)
                slitWorld.Meshes.push_back(makeBlocker(9240u, {-9.4f, 1.5f, -24.0f}, {17.2f, 24.0f, 1.5f}));
                slitWorld.Meshes.push_back(makeBlocker(9241u, { 9.4f, 1.5f, -24.0f}, {17.2f, 24.0f, 1.5f}));

                const uint32_t kSlit = sliceNearestDepth(14.0f, settings.FogNearDistance, settings.FogMaxDistance);
                std::vector<glm::vec4> hdrShafts, volShafts;
                RENDER_CHECK(renderAndReadHDR(slitWorld, cam, settings, hdrShafts, diag) &&
                             Read3DSliceRGBA16F(*scatteringVol, kSlit, volShafts),
                    "M15-U6-07.2: render slit light shaft scene");

                const glm::vec3 vLeft = froxelAt(volShafts, 45u, 45u);   // u = 0.28 (behind left wall)
                const glm::vec3 vCenter = froxelAt(volShafts, 80u, 45u); // u = 0.50 (through center slit)
                const glm::vec3 vRight = froxelAt(volShafts, 115u, 45u); // u = 0.72 (behind right wall)
                const glm::vec3 hLeft = avgHDR(hdrShafts, 0.28f, 0.5f);
                const glm::vec3 hCenter = avgHDR(hdrShafts, 0.50f, 0.5f);
                const glm::vec3 hRight = avgHDR(hdrShafts, 0.72f, 0.5f);
                std::cout << "  [U6-C Shafts] vLeft=" << glm::length(vLeft) << " vCenter=" << glm::length(vCenter)
                          << " vRight=" << glm::length(vRight) << " hLeft=" << glm::length(hLeft)
                          << " hCenter=" << glm::length(hCenter) << " hRight=" << glm::length(hRight) << "\n";

                RENDER_CHECK(glm::length(vCenter) > 0.01f &&
                             glm::length(vLeft) < 0.05f * glm::length(vCenter) &&
                             glm::length(vRight) < 0.05f * glm::length(vCenter) &&
                             glm::length(hCenter) > 0.02f &&
                             glm::length(hLeft) < 0.15f * glm::length(hCenter) &&
                             glm::length(hRight) < 0.15f * glm::length(hCenter),
                    "M15-U6-07.2: blocker slit produces bright bounded center light shaft with dark left and right shadows");

                glm::vec3 centerPosition, neighborPosition, ray;
                froxelSample(80u, 45u, kSlit, settings.FogNearDistance, settings.FogMaxDistance, centerPosition, ray);
                froxelSample(81u, 45u, kSlit, settings.FogNearDistance, settings.FogMaxDistance, neighborPosition, ray);
                const float slitWithFilterMargin = 0.8f + 2.0f * std::abs(neighborPosition.x - centerPosition.x);
                unsigned brightColumns = 0u;
                for (uint32_t x = 0; x < 160u; ++x) {
                    if (glm::length(froxelAt(volShafts, x, 45u)) <= 0.15f * glm::length(vCenter)) continue;
                    glm::vec3 position;
                    froxelSample(x, 45u, kSlit, settings.FogNearDistance, settings.FogMaxDistance, position, ray);
                    RENDER_CHECK(std::abs(position.x) <= slitWithFilterMargin,
                        "Bright shaft columns stay within the geometric slit plus the filter margin");
                    ++brightColumns;
                }
                RENDER_CHECK(brightColumns >= 2u, "The slit produces a shaft wider than one froxel");
                shadowLog << "shaft_bright_columns=" << brightColumns << '\n';

                // Remove directional light completely on the identical slit geometry -> zero shafts
                slitWorld.DirectionalLight.reset();
                std::vector<glm::vec4> hdrNoSun, volNoSun;
                RENDER_CHECK(renderAndReadHDR(slitWorld, cam, settings, hdrNoSun, diag) &&
                             Read3DSliceRGBA16F(*scatteringVol, kSlit, volNoSun) &&
                             HasFallbackPrefix(diag, "froxel_fog_no_directional_sun"),
                    "M15-U6-07.2: slit scene without directional sun renders cleanly with explicit diagnostic");

                float maxSliceScatter = 0.0f;
                for (const auto& voxel : volNoSun) {
                    maxSliceScatter = std::max(maxSliceScatter, glm::length(glm::vec3(voxel)));
                }
                const float maxHdrNoSun = std::max({
                    glm::length(avgHDR(hdrNoSun, 0.28f, 0.5f)),
                    glm::length(avgHDR(hdrNoSun, 0.50f, 0.5f)),
                    glm::length(avgHDR(hdrNoSun, 0.72f, 0.5f))});
                RENDER_CHECK(maxSliceScatter == 0.0f && maxHdrNoSun < 1e-4f,
                    "M15-U6-07.2: absent directional sun produces zero light shafts across the entire slit volume and HDR output");
                shadowLog << "shafts left=" << glm::length(vLeft) << " center=" << glm::length(vCenter)
                          << " right=" << glm::length(vRight) << " hdr_center=" << glm::length(hCenter)
                          << " nosun_max=" << maxSliceScatter << " nosun_hdr=" << maxHdrNoSun << '\n';
            }

            shadowLog.close();
            RENDER_CHECK(shadowLog.good(), "M15-U6-C: shadow verification log flushed cleanly");
        }
    }

    // M15-U6-D: observe production history and current media independently.
    {
        RHITexture color, depth;
        RHIViewTargetDesc desc{};
        desc.Width = 256;
        desc.Height = 144;
        desc.OwnPrimary = false;
        desc.HDR = true;
        desc.EnableVolumetricFog = true;
        desc.FroxelQuality = static_cast<uint8_t>(FroxelFogQuality::Low);
        auto targets = std::make_shared<RHIViewTargets>();
        const auto configure = [&]() {
            return targets->Configure(desc) && targets->Validate() &&
                color.Init(RHITextureDesc::RenderTarget2D(desc.Width, desc.Height)) &&
                depth.Init(RHITextureDesc::DepthStencil2D(desc.Width, desc.Height));
        };
        RENDER_CHECK(configure(), "U6-D: configure initial Low fog targets");

        SceneRenderSettings settings;
        settings.EnableSkybox = settings.EnableShadows = settings.EnableParticles = false;
        settings.EnableToneMapping = true;
        settings.EnableGTAO = settings.EnableBloom = false;
        settings.EnableFXAA = settings.EnableAutoExposure = settings.EnableSSR = false;
        settings.EnableStudioLightLobes = false;
        settings.EnableVolumetricFog = true;
        settings.FogQuality = FroxelFogQuality::Low;
        settings.FogNearDistance = 0.5f;
        settings.FogMaxDistance = 36.0f;
        settings.FogTemporalWeight = 0.8f;

        PerspectiveCamera camera(60.0f, float(desc.Width) / desc.Height, 0.1f, 100.0f);
        glm::vec3 position(0.0f, 3.0f, 0.0f);
        auto pose = [&](glm::vec3 direction = {0, 0, -1}) {
            camera.SetViewMatrix(glm::lookAtRH(position, position + direction, {0, 1, 0}), position);
        };
        pose();

        RenderWorld world;
        world.SceneInstanceID = 22;
        world.SceneRevision = 1;
        world.VolumetricFog = RenderVolumetricFog{true, 0.12f, 0.16f, 0.0f, glm::vec3(1.0f), 36.0f, 1.0f};
        world.PointLights.emplace_back();
        auto& light = world.PointLights.back();
        light.Position = {3.0f, 4.0f, -9.0f};
        light.Intensity = 80.0f;
        light.Radius = 15.0f;

        auto render = [&](bool cameraCut = false) {
            renderer.RenderWorldView(world, camera, &color, &depth, settings, nullptr,
                                     settings.EnableVolumetricFog ? targets : nullptr, cameraCut);
            return renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors() && device.WaitIdle();
        };
        const auto& diag = renderer.GetDiagnostics();
        constexpr uint32_t slice = 18;
        std::vector<glm::vec4> raw, seed, moved;
        const auto read = [&](uint32_t slot, std::vector<glm::vec4>& history) {
            return Read3DSliceRGBA16F(*targets->GetFroxelScattering(), slice, raw) &&
                   Read3DSliceRGBA16F(*targets->GetFroxelHistory(slot), slice, history);
        };
        const auto seedFrame = [&](bool cameraCut = false) {
            std::vector<glm::vec4> fresh;
            return render(cameraCut) && HasPassNamed(diag.GetFrame(), "FogReproject") &&
                read(0, fresh) && fresh == raw;
        };

        RENDER_CHECK(render() && HasPassNamed(diag.GetFrame(), "FogReproject") &&
                     diag.GetFrame().FrameIndex == 0 && read(0, seed) && seed == raw,
            "M15-U6-04.1: first frame seeds history slot 0 with exactly the current raw media");

        const auto* volume = targets->GetFroxelScattering();
        const uint32_t gridW = volume->GetWidth(), gridH = volume->GetHeight();
        const float z = FroxelFogContract::SliceCenterToViewDistance(
            slice, volume->GetDepth(), settings.FogNearDistance, settings.FogMaxDistance);
        constexpr uint32_t shift = 2;
        position.x += 2.0f * float(shift) * z / (float(gridW) * camera.GetProjectionMatrixZO()[0][0]);
        pose();
        light.Intensity *= 1.025f; // A modest change makes accepted history measurable.
        RENDER_CHECK(render() && read(1, moved) && seed.size() == moved.size(),
            "M15-U6-07.3: camera translation writes the opposite history slot");

        std::vector<glm::vec4> above, below;
        RENDER_CHECK(Read3DSliceRGBA16F(*targets->GetFroxelScattering(), slice - 1, below) &&
                     Read3DSliceRGBA16F(*targets->GetFroxelScattering(), slice + 1, above),
            "M15-U6-04.2: read the adjacent raw depth slices for neighborhood bounds");
        uint32_t blended = 0, confirmedReprojected = 0;
        float maximumDelta = 0.0f;
        for (uint32_t y = 0; y < gridH; ++y) {
            for (uint32_t x = 0; x < gridW; ++x) {
                const size_t i = size_t(y) * gridW + x;
                const float current = raw[i].r;
                const float actual = moved[i].r;
                const float lo = std::min({current, below[i].r, above[i].r,
                    raw[x > 0 ? i - 1 : i].r, raw[x + 1 < gridW ? i + 1 : i].r,
                    raw[y > 0 ? i - gridW : i].r, raw[y + 1 < gridH ? i + gridW : i].r});
                const float hi = std::max({current, below[i].r, above[i].r,
                    raw[x > 0 ? i - 1 : i].r, raw[x + 1 < gridW ? i + 1 : i].r,
                    raw[y > 0 ? i - gridW : i].r, raw[y + 1 < gridH ? i + gridW : i].r});
                RENDER_CHECK(std::isfinite(actual) && moved[i].a == raw[i].a && actual >= lo && actual <= hi &&
                             std::abs(actual - current) <= current * 0.20f + 1e-5f,
                    "M15-U6-04.2: history is finite, bounded by current neighbors and preserves extinction");
                if (x + shift >= gridW) {
                    RENDER_CHECK(moved[i] == raw[i], "M15-U6-04.2: newly exposed columns reject history outside the old frustum");
                    continue;
                }
                if (actual == current) continue;
                ++blended;
                maximumDelta = std::max(maximumDelta, std::abs(actual - current));
                const float expected = glm::mix(current, seed[i + shift].r, settings.FogTemporalWeight);
                const float sameIndex = glm::mix(current, seed[i].r, settings.FogTemporalWeight);
                if (std::abs(actual - expected) <= std::max(expected * 0.001f, 1e-6f) &&
                    std::abs(actual - sameIndex) > std::max(expected * 0.01f, 1e-5f))
                    ++confirmedReprojected;
            }
        }
        std::cout << "  [U6-D Temporal] blended=" << blended << " world_reprojected=" << confirmedReprojected
                  << " max_scatter_delta=" << maximumDelta << '\n';
        RENDER_CHECK(blended > 0 && confirmedReprojected > 0,
            "M15-U6-07.3: accepted history matches world-space reprojection under motion and differs from same-index blending");

        std::vector<glm::vec4> temporalHDR, staticHDR;
        RENDER_CHECK(Read2DRGBA16F(*renderer.GetIntermediateHDRTarget(), temporalHDR),
            "M15-U6-04.1: capture the composed linear HDR output with reused history");
        settings.FogTemporalWeight = 0.0f;
        RENDER_CHECK(render() && !HasPassNamed(diag.GetFrame(), "FogReproject") &&
                     Read2DRGBA16F(*renderer.GetIntermediateHDRTarget(), staticHDR) && temporalHDR != staticHDR,
            "M15-U6-04.1: history affects the composed output and zero weight bypasses temporal filtering");
        settings.FogTemporalWeight = 0.8f;
        RENDER_CHECK(seedFrame(), "U6-D: re-enabling temporal filtering seeds fresh lit history");

        pose({0.14f, 0.0f, -1.0f});
        light.Intensity = 0.0f;
        RENDER_CHECK(render() && read(1, moved) && moved == raw &&
                     std::all_of(raw.begin(), raw.end(), [](const auto& value) { return value.r == 0.0f; }),
            "M15-U6-07.3: rotation after extinguishing the light leaves no scattering trail");

        light.Intensity = 75.0f;
        pose();
        RENDER_CHECK(seedFrame(true), "U6-D: seed a straight-ahead view for the fog-range rejection test");
        position.z += 0.15f;
        pose();
        light.Intensity *= 1.025f;
        RENDER_CHECK(render() &&
                     Read3DSliceRGBA16F(*targets->GetFroxelScattering(), 0, raw) &&
                     Read3DSliceRGBA16F(*targets->GetFroxelHistory(1), 0, moved) && moved == raw &&
                     std::any_of(raw.begin(), raw.end(), [](const auto& value) { return value.r > 0.0f; }),
            "M15-U6-04.2: lit near-slice history is rejected outside the old fog depth range inside the camera clip planes");

        RENDER_CHECK(seedFrame(true) && diag.GetFrame().FrameIndex == 0,
            "M15-U6-04.3: explicit camera cut seeds history");
        RENDER_CHECK(render() && read(1, moved), "M15-U6-04.3: history resumes after the cut");
        ++world.SceneRevision;
        RENDER_CHECK(seedFrame() && diag.GetFrame().FrameIndex == 0,
            "M15-U6-04.3: scene revision seeds history");
        RENDER_CHECK(render(), "U6-D: accumulate before replacing the scene");
        ++world.SceneInstanceID;
        RENDER_CHECK(seedFrame() && diag.GetFrame().FrameIndex == 0,
            "M15-U6-04.3: distinct scene instance with same revision seeds history");
        RENDER_CHECK(render(), "U6-D: accumulate before editing the authored medium");
        world.VolumetricFog->Density *= 1.35f;
        RENDER_CHECK(seedFrame(), "M15-U6-04.3: authored fog edits seed history without a scene revision");
        RENDER_CHECK(render(), "U6-D: accumulate before changing feature policy");
        settings.EnableShadows = true;
        RENDER_CHECK(seedFrame() && diag.GetFrame().FrameIndex == 0,
            "M15-U6-04.3: feature policy change seeds history");
        RENDER_CHECK(render(), "U6-D: accumulate before disabling fog");
        settings.EnableVolumetricFog = false;
        RENDER_CHECK(render() && !HasPassNamed(diag.GetFrame(), "FogReproject"),
            "U6-D: disabled fog skips temporal work");
        settings.EnableVolumetricFog = true;
        RENDER_CHECK(seedFrame(), "M15-U6-04.3: re-enabled fog starts without old history");

        RENDER_CHECK(render(), "U6-D: accumulate before resizing");
        const auto oldGeneration = targets->GetGeneration();
        desc.Width = 320;
        desc.Height = 180;
        RENDER_CHECK(configure() && targets->GetGeneration() != oldGeneration && seedFrame() &&
                     diag.GetFrame().FrameIndex == 0,
            "M15-U6-07.4: resizing the same view target wrapper resets history before the first new frame");
        RENDER_CHECK(render() && read(1, moved) && moved.size() == raw.size(),
            "M15-U6-07.4: opposite slot uses the resized grid on the second frame");
        settings.FogQuality = FroxelFogQuality::Medium;
        desc.FroxelQuality = static_cast<uint8_t>(settings.FogQuality);
        RENDER_CHECK(configure() && seedFrame(), "M15-U6-07.4: changing grid tier starts with a fresh volume");

        bool faultReached = false;
        {
            ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == "EndCommandBuffer") {
                    faultReached = true;
                    return nri::Result::FAILURE;
                }
                return {};
            });
            renderer.RenderWorldView(world, camera, &color, &depth, settings, nullptr, targets);
        }
        RENDER_CHECK(faultReached && !renderer.WasLastRenderSuccessful() && seedFrame(),
            "M15-U6-04.3: a frame that fails command finalization cannot publish history");

        RHISceneRenderer staticFogRenderer;
        bool layoutFaultReached = false;
        {
            ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == "CreatePipelineLayout" && ScopedRHIOperation::Resource() == "FogReprojectLayout") {
                    layoutFaultReached = true;
                    return nri::Result::OUT_OF_MEMORY;
                }
                return {};
            });
            RENDER_CHECK(staticFogRenderer.Init(), "U6-D: renderer initializes without a temporal pipeline layout");
        }
        staticFogRenderer.RenderWorldView(world, camera, &color, &depth, settings, nullptr, targets);
        const auto& fallback = staticFogRenderer.GetDiagnostics().GetFrame();
        RENDER_CHECK(layoutFaultReached && staticFogRenderer.WasLastRenderSuccessful() &&
                     HasPassNamed(fallback, "FogApply") && !HasPassNamed(fallback, "FogReproject") &&
                     HasFallbackPrefix(fallback, "froxel_temporal_unavailable"),
            "M15-U6-04.1: missing temporal pipeline retains static fog and reports the fallback");
        staticFogRenderer.Shutdown();

        // Render through the same mutable scene adapter used by Editor and Runtime.
        RenderService service;
        RENDER_CHECK(service.Init(), "U6-D: initialize production RenderService for scene extraction");
        Scene scene;
        auto lightEntity = scene.CreateEntity("Temporal fog point light");
        lightEntity.GetComponent<TransformComponent>().Translation = light.Position;
        auto& point = lightEntity.AddComponent<PointLightComponent>();
        point.Intensity = 80.0f;
        point.Radius = light.Radius;
        auto fogEntity = scene.CreateEntity("Authored temporal fog");
        auto& medium = fogEntity.AddComponent<VolumetricFogComponent>();
        medium.Density = world.VolumetricFog->Density;
        medium.HeightFalloff = world.VolumetricFog->HeightFalloff;
        medium.ScatteringAsymmetry = 0.0f;
        medium.Color = glm::vec3(1.0f);
        medium.MaxDistance = 36.0f;

        ViewRequest request;
        request.Width = desc.Width;
        request.Height = desc.Height;
        request.Settings = settings;
        request.ViewTargets = targets;
        request.ColorTarget = &color;
        request.DepthTarget = &depth;
        position = {0.0f, 3.0f, 0.0f};
        pose();
        request.Camera = camera;

        bool uniformOom = false;
        {
            ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == "CreateBuffer" && ScopedRHIOperation::Resource() == "FogReprojectConstants") {
                    uniformOom = true;
                    return nri::Result::OUT_OF_MEMORY;
                }
                return {};
            });
            RENDER_CHECK(service.RenderView(scene, request).Success && uniformOom,
                "M15-U6-04.1: a temporal constant allocation failure preserves the production view");
        }
        RENDER_CHECK(service.RenderView(scene, request).Success && read(0, seed) && seed == raw,
            "M15-U6-04.1: temporal reprojection recovers after the optional constant allocation");
        const auto sceneRevision = scene.GetSceneGeneration();
        const auto transformRevision = scene.GetTransformRevision();

        auto previewDesc = desc;
        previewDesc.OwnPrimary = true;
        previewDesc.EnableVolumetricFog = false;
        previewDesc.FroxelQuality = RHIViewTargets::DefaultFroxelQualityTier;
        auto previewTargets = std::make_shared<RHIViewTargets>();
        RENDER_CHECK(previewTargets->Configure(previewDesc), "U6-D: allocate a separate preview view");
        ViewRequest preview = request;
        preview.Kind = RenderViewKind::AssetPreview;
        preview.Settings.EnableVolumetricFog = false;
        preview.ViewTargets = previewTargets;
        preview.ColorTarget = nullptr;
        preview.DepthTarget = nullptr;
        Scene previewScene;
        RENDER_CHECK(service.RenderView(previewScene, preview).Success,
            "U6-D: interleave a real preview render between main view frames");

        const auto* mainVolume = targets->GetFroxelScattering();
        const float depthAtSlice = FroxelFogContract::SliceCenterToViewDistance(
            slice, mainVolume->GetDepth(), settings.FogNearDistance, medium.MaxDistance);
        position.x += 2.0f * float(shift) * depthAtSlice /
            (float(mainVolume->GetWidth()) * camera.GetProjectionMatrixZO()[0][0]);
        pose();
        request.Camera = camera;
        point.Intensity *= 1.025f;
        RENDER_CHECK(service.RenderView(scene, request).Success && read(1, moved) &&
                     seed.size() == raw.size() && moved.size() == raw.size() &&
                     scene.GetSceneGeneration() == sceneRevision && scene.GetTransformRevision() > transformRevision,
            "M15-U6-07.3: render the moved live scene after an interleaved preview");
        uint32_t productionReprojected = 0;
        const uint32_t mainWidth = mainVolume->GetWidth();
        for (size_t i = 0; i < raw.size(); ++i) {
            RENDER_CHECK(moved[i].a == raw[i].a && raw[i].a > 0.0f,
                "M15-U6-07.3: the production view writes current extinction to the opposite slot");
            if (i % mainWidth + shift >= mainWidth || moved[i].r == raw[i].r) continue;
            const float expected = glm::mix(raw[i].r, seed[i + shift].r, settings.FogTemporalWeight);
            if (std::abs(moved[i].r - expected) <= std::max(expected * 0.001f, 1e-6f))
                ++productionReprojected;
        }
        RENDER_CHECK(productionReprojected > 0,
            "M15-U6-07.3: real scene extraction and an interleaved preview preserve world-space history reuse");
        request.CameraCut = true;
        RENDER_CHECK(service.RenderView(scene, request).Success && read(0, moved) && moved == raw,
            "M15-U6-04.3: RenderService forwards an explicit cut in the same live scene");
        request.CameraCut = false;
        const auto cutSeed = moved;
        scene.CreateEntity("Structural edit");
        point.Intensity *= 0.9f;
        RENDER_CHECK(service.RenderView(scene, request).Success && read(0, moved) &&
                     moved == raw && moved != cutSeed && scene.GetSceneGeneration() != sceneRevision,
            "M15-U6-04.3: a structural scene edit invalidates real view history");

        bool historyOom = false;
        const auto historyName = "ViewTarget_" + std::to_string(static_cast<unsigned>(RHIViewTarget::FroxelHistory1));
        auto staticTargets = std::make_shared<RHIViewTargets>();
        {
            ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == "CreateTexture" && ScopedRHIOperation::Resource() == historyName) {
                    historyOom = true;
                    return nri::Result::OUT_OF_MEMORY;
                }
                return {};
            });
            RENDER_CHECK(staticTargets->Configure(desc, nullptr, nullptr, FroxelHistoryPolicy::AllowStaticFog) &&
                         historyOom && staticTargets->Validate() && !staticTargets->GetFroxelHistory(0) &&
                         !staticTargets->GetFroxelHistory(1),
                "M15-U6-04.1: a history texture allocation failure publishes a valid static-only target");
        }
        renderer.RenderWorldView(world, camera, &color, &depth, settings, nullptr, staticTargets);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && HasPassNamed(diag.GetFrame(), "FogApply") &&
                     !HasPassNamed(diag.GetFrame(), "FogReproject") &&
                     HasFallbackPrefix(diag.GetFrame(), "froxel_temporal_unavailable"),
            "M15-U6-04.1: allocation fallback retains visible static fog and reports the lost history");
        RENDER_CHECK(staticTargets->Configure(desc) && staticTargets->GetFroxelHistory(0) && staticTargets->GetFroxelHistory(1),
            "M15-U6-04.1: target reconfiguration can restore the full history pair");
        service.Shutdown();

        std::ofstream temporalLog(output / "M15_U6D_froxel_temporal.txt");
        temporalLog << "blended=" << blended << " world_reprojected=" << confirmedReprojected
                    << " max_scatter_delta=" << maximumDelta << " rejected_frustum=" << shift * gridH
                    << " extinguished_light_trail=0 resize_and_reset=PASS"
                    << " production_reprojected=" << productionReprojected
                    << " production_cut_and_scene_reset=PASS temporal_oom_fallbacks=PASS\n";
        temporalLog.close();
        RENDER_CHECK(temporalLog.good(), "M15-U6-D: temporal readback evidence written");
    }

    // -------------------------------------------------------------------------
    // 6. M15-U6-E Native GPU Verification:
    //    - M15-U6-05.1: Transparent / Additive / Refractive / Particle depth interaction
    //    - M15-U6-05.2: No-light extinction with skybox background, disabled lights, temporal reprojection & local-light-only
    //    - M15-U6-07.5 & 07.6: MSAA fallback, integrated VRAM & real GPU timestamp scorecard across Low..Ultra
    // -------------------------------------------------------------------------
    {
        constexpr uint32_t kViewW = 640u;
        constexpr uint32_t kViewH = 360u;
        RHITexture displayColor, displayDepth;
        RENDER_CHECK(displayColor.Init(RHITextureDesc::RenderTarget2D(kViewW, kViewH, RHITextureFormat::RGBA8_UNORM, "U6E_Display")) &&
                     displayDepth.Init(RHITextureDesc::DepthStencil2D(kViewW, kViewH)),
            "M15-U6-E: display & depth targets init");

        RHIViewTargetDesc vtDesc{};
        vtDesc.Width = kViewW;
        vtDesc.Height = kViewH;
        vtDesc.ColorFormat = RHITextureFormat::RGBA8_UNORM;
        vtDesc.HDR = true;
        vtDesc.OwnPrimary = false;
        vtDesc.EnableVolumetricFog = true;
        vtDesc.FroxelQuality = static_cast<uint8_t>(FroxelFogQuality::High);
        auto targets = std::make_shared<RHIViewTargets>();
        RENDER_CHECK(targets->Configure(vtDesc) && targets->Validate(),
            "M15-U6-E: view targets configured for High froxel grid");

        auto cubeMesh = RHIMesh::CreateCube(1.0f);
        RENDER_CHECK(cubeMesh && cubeMesh->IsValid(), "M15-U6-E: cube fixture valid");
        auto cubeView = cubeMesh->CaptureView();

        auto makeEmissiveMat = [](SurfaceType surface, const glm::vec4& baseColor, const glm::vec3& emissive, bool depthWrite, bool refract = false) {
            RHIMaterialDesc d{};
            d.SurfaceType = surface;
            d.CullMode = CullMode::Back;
            d.DepthWrite = depthWrite;
            d.BaseColor = baseColor;
            d.Metallic = 1.0f;
            d.Roughness = 1.0f;
            d.EmissiveColor = emissive;
            d.EmissiveIntensity = 1.0f;
            d.EnableSpecularHighlights = false;
            d.EnableEnvironmentReflections = false;
            d.RefractionEnabled = refract;
            d.RefractionStrength = 0.25f;
            d.RefractionIOR = 1.5f;
            d.RefractionThickness = 0.5f;
            return std::make_shared<RHIMaterial>(d);
        };

        auto wallMat = makeEmissiveMat(SurfaceType::Opaque, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), glm::vec3(3.0f, 0.2f, 0.2f), true);
        auto transMat = makeEmissiveMat(SurfaceType::Transparent, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f), glm::vec3(0.0f, 4.0f, 0.0f), false);
        auto addMat = makeEmissiveMat(SurfaceType::Additive, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 4.0f, 0.0f), false);
        auto glassMat = makeEmissiveMat(SurfaceType::Transparent, glm::vec4(0.2f, 0.2f, 0.2f, 0.35f), glm::vec3(0.1f, 0.3f, 0.5f), false, true);

        auto renderHDR = [&](const RenderWorld& world,
                             const PerspectiveCamera& cam,
                             const SceneRenderSettings& settings,
                             std::vector<glm::vec4>& outHDR,
                             RenderFrameDiagnostics& outDiag) -> bool {
            renderer.RenderWorldView(world, cam, &displayColor, &displayDepth, settings, nullptr,
                (settings.EnableVolumetricFog && settings.MSAASamples == 1) ? targets : nullptr);
            outDiag = renderer.GetDiagnostics().GetFrame();
            if (!renderer.WasLastRenderSuccessful() || renderer.GetDiagnostics().HasErrors() || !device.WaitIdle()) {
                return false;
            }
            auto* linearHDR = renderer.GetIntermediateHDRTarget();
            return linearHDR && Read2DRGBA16F(*linearHDR, outHDR) && outHDR.size() == size_t(kViewW) * kViewH;
        };

        PerspectiveCamera cam(60.0f, float(kViewW) / float(kViewH), 0.1f, 300.0f);
        const glm::vec3 camPos(0.0f, 1.5f, 0.0f);
        cam.SetViewMatrix(glm::lookAtRH(camPos, glm::vec3(0.0f, 1.5f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f)), camPos);

        SceneRenderSettings baseFogSettings{};
        baseFogSettings.EnableSkybox = false;
        baseFogSettings.EnableShadows = false;
        baseFogSettings.EnableParticles = true;
        baseFogSettings.EnableToneMapping = true;
        baseFogSettings.EnableVolumetricFog = true;
        baseFogSettings.FogQuality = FroxelFogQuality::High;
        baseFogSettings.FogTemporalWeight = 0.0f;
        baseFogSettings.FogNearDistance = 0.25f;
        baseFogSettings.FogMaxDistance = 120.0f;
        baseFogSettings.FogDensity = 0.08f;
        baseFogSettings.FogHeightFalloff = 0.0f;
        baseFogSettings.FogScattering = 0.5f;
        baseFogSettings.FogColor = glm::vec3(0.8f, 0.85f, 0.9f);
        baseFogSettings.ClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

        RenderMeshInstance wallInst{};
        wallInst.EntityID = 9501u;
        wallInst.NativeView = cubeView;
        wallInst.Material.RHIMaterialAsset = wallMat;
        wallInst.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -60.5f)),
            glm::vec3(400.0f, 400.0f, 1.0f));

        RenderWorld baseWorld;
        baseWorld.SceneRevision = 9601u;
        RenderDirectionalLight sun{};
        sun.Direction = glm::normalize(glm::vec3(0.2f, 0.4f, -0.8f));
        sun.Color = glm::vec3(1.0f, 0.95f, 0.85f);
        sun.Intensity = 2.0f;
        sun.AmbientIntensity = 0.05f;
        baseWorld.DirectionalLight = sun;
        baseWorld.Meshes.push_back(wallInst);

        LightingService lightingSvc;
        const auto media = FroxelFogContract::ResolveMediaState(baseWorld, baseFogSettings, lightingSvc.BuildFrame(baseWorld, baseFogSettings));
        const glm::vec3 forwardRay(0.0f, 0.0f, -1.0f);
        const auto analytic4 = FroxelFogContract::EvaluateAnalyticHomogeneousFog(media, forwardRay, 4.0f, 1.0f);
        const auto analytic40 = FroxelFogContract::EvaluateAnalyticHomogeneousFog(media, forwardRay, 40.0f, 1.0f);
        const auto analytic60 = FroxelFogContract::EvaluateAnalyticHomogeneousFog(media, forwardRay, 60.0f, 1.0f);

        RenderFrameDiagnostics diag;
        std::vector<glm::vec4> hdrWallFog;
        RENDER_CHECK(renderHDR(baseWorld, cam, baseFogSettings, hdrWallFog, diag),
            "M15-U6-05.1: opaque z=60m wall fogged HDR baseline succeeds");
        const glm::vec3 wallFoggedCenter = SampleHDRBlockAvg(hdrWallFog, kViewW, kViewW / 2u, kViewH / 2u);

        // 6a. Alpha-blended Transparent quad at z = -4.0m (front face at z=-4.0, center at z=-4.5)
        {
            RenderMeshInstance quadInst{};
            quadInst.EntityID = 9502u;
            quadInst.NativeView = cubeView;
            quadInst.Material.RHIMaterialAsset = transMat;
            quadInst.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -4.5f)),
                glm::vec3(4.0f, 4.0f, 1.0f));

            RenderWorld transWorld = baseWorld;
            transWorld.SceneRevision = 9602u;
            transWorld.Meshes.push_back(quadInst);

            auto noFog = baseFogSettings;
            noFog.EnableVolumetricFog = false;
            std::vector<glm::vec4> hdrTransNoFog, hdrWallNoFog;
            RENDER_CHECK(renderHDR(baseWorld, cam, noFog, hdrWallNoFog, diag) &&
                         renderHDR(transWorld, cam, noFog, hdrTransNoFog, diag),
                "M15-U6-05.1: Fog-OFF transparent baseline succeeds");
            const glm::vec3 wallNoFogCenter = SampleHDRBlockAvg(hdrWallNoFog, kViewW, kViewW / 2u, kViewH / 2u);
            const glm::vec3 transNoFogCenter = SampleHDRBlockAvg(hdrTransNoFog, kViewW, kViewW / 2u, kViewH / 2u);
            const glm::vec3 quadSurface = (transNoFogCenter - 0.5f * wallNoFogCenter) / 0.5f;

            std::vector<glm::vec4> hdrTransFog;
            RENDER_CHECK(renderHDR(transWorld, cam, baseFogSettings, hdrTransFog, diag),
                "M15-U6-05.1: Fog-ON transparent render succeeds");
            const glm::vec3 gpuTrans = SampleHDRBlockAvg(hdrTransFog, kViewW, kViewW / 2u, kViewH / 2u);
            const glm::vec3 expectedTrans = 0.5f * wallFoggedCenter + 0.5f * (quadSurface * analytic4.Transmittance + analytic4.InScattering);
            const glm::vec3 buggyOpaqueDepth = (0.5f * quadSurface) * analytic60.Transmittance + 0.5f * wallFoggedCenter + 0.5f * analytic60.InScattering;
            std::cout << "  [U6-E Trans] gpu=(" << gpuTrans.r << "," << gpuTrans.g << "," << gpuTrans.b
                      << ") exp=(" << expectedTrans.r << "," << expectedTrans.g << "," << expectedTrans.b
                      << ") wallFog=" << wallFoggedCenter.g << " quadSurf=" << quadSurface.g << "\n";
            RENDER_CHECK(glm::length(gpuTrans - expectedTrans) < 0.08f &&
                         gpuTrans.g > buggyOpaqueDepth.g * 1.5f &&
                         glm::length(gpuTrans - buggyOpaqueDepth) > 0.8f,
                "M15-U6-05.1: close alpha-blended transparent surface applies z=4m foreground fog rather than z=60m opaque overwrite");

            // Occluded behind a near opaque wall (z=-4.0m) -> invisible
            RenderWorld occludedWorld = baseWorld;
            occludedWorld.SceneRevision = 9603u;
            RenderMeshInstance frontWall = wallInst;
            frontWall.EntityID = 9503u;
            frontWall.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -4.5f)),
                glm::vec3(400.0f, 400.0f, 1.0f));
            RenderMeshInstance backQuad = quadInst;
            backQuad.EntityID = 9504u;
            backQuad.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -10.5f)),
                glm::vec3(4.0f, 4.0f, 1.0f));
            occludedWorld.Meshes = { frontWall, backQuad };
            std::vector<glm::vec4> hdrOccluded, hdrFrontOnly;
            RenderWorld frontOnlyWorld = baseWorld;
            frontOnlyWorld.Meshes = { frontWall };
            RENDER_CHECK(renderHDR(frontOnlyWorld, cam, baseFogSettings, hdrFrontOnly, diag) &&
                         renderHDR(occludedWorld, cam, baseFogSettings, hdrOccluded, diag),
                "M15-U6-05.1: depth-occluded transparent mesh behind opaque wall renders cleanly");
            const glm::vec3 frontCenter = SampleHDRBlockAvg(hdrFrontOnly, kViewW, kViewW / 2u, kViewH / 2u);
            const glm::vec3 occCenter = SampleHDRBlockAvg(hdrOccluded, kViewW, kViewW / 2u, kViewH / 2u);
            RENDER_CHECK(glm::length(frontCenter - occCenter) < 0.02f,
                "M15-U6-05.1: depth test prevents occluded transparent geometry behind wall from glowing");
        }

        // 6b. Additive quad at z=-4m vs z=-40m (foreground transmittance without double-added scatter)
        {
            RenderMeshInstance addNear{};
            addNear.EntityID = 9505u;
            addNear.NativeView = cubeView;
            addNear.Material.RHIMaterialAsset = addMat;
            addNear.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -4.5f)),
                glm::vec3(4.0f, 4.0f, 1.0f));
            RenderWorld addWorld = baseWorld;
            addWorld.SceneRevision = 9604u;
            addWorld.Meshes.push_back(addNear);

            std::vector<glm::vec4> hdrAddNear;
            RENDER_CHECK(renderHDR(addWorld, cam, baseFogSettings, hdrAddNear, diag),
                "M15-U6-05.1: additive near surface renders cleanly");
            const glm::vec3 gpuAddNear = SampleHDRBlockAvg(hdrAddNear, kViewW, kViewW / 2u, kViewH / 2u);
            const glm::vec3 expectedAddNear = wallFoggedCenter + glm::vec3(0.0f, 4.0f, 0.0f) * analytic4.Transmittance;
            RENDER_CHECK(glm::length(gpuAddNear - expectedAddNear) < 0.08f &&
                         std::abs(gpuAddNear.r - wallFoggedCenter.r) < 0.05f,
                "M15-U6-05.1: additive mesh attenuates by foreground transmittance without doubling fog in-scattering");

            addWorld.Meshes.back().WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -40.5f)),
                glm::vec3(40.0f, 40.0f, 1.0f));
            addWorld.SceneRevision = 9605u;
            std::vector<glm::vec4> hdrAddFar;
            RENDER_CHECK(renderHDR(addWorld, cam, baseFogSettings, hdrAddFar, diag),
                "M15-U6-05.1: additive far surface renders cleanly");
            const glm::vec3 gpuAddFar = SampleHDRBlockAvg(hdrAddFar, kViewW, kViewW / 2u, kViewH / 2u);
            const glm::vec3 expectedAddFar = wallFoggedCenter + glm::vec3(0.0f, 4.0f, 0.0f) * analytic40.Transmittance;
            const float nearDeltaG = gpuAddNear.g - wallFoggedCenter.g;
            const float farDeltaG = gpuAddFar.g - wallFoggedCenter.g;
            std::cout << "  [U6-E Add] near=" << gpuAddNear.g << " (exp " << expectedAddNear.g << ") far=" << gpuAddFar.g
                      << " (exp " << expectedAddFar.g << ") deltaNear=" << nearDeltaG << " deltaFar=" << farDeltaG << "\n";
            RENDER_CHECK(farDeltaG < nearDeltaG * 0.15f &&
                         glm::length(gpuAddFar - expectedAddFar) < 0.08f,
                "M15-U6-05.1: distant additive mesh is much darker than near additive mesh at true depth");
        }

        // 6c. Refractive glass in dense fog (RefractionCopy after FogApply)
        {
            RenderMeshInstance glassInst{};
            glassInst.EntityID = 9506u;
            glassInst.NativeView = cubeView;
            glassInst.Material.RHIMaterialAsset = glassMat;
            glassInst.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -4.5f)),
                glm::vec3(4.0f, 4.0f, 1.0f));
            RenderWorld glassWorld = baseWorld;
            glassWorld.SceneRevision = 9606u;
            glassWorld.Meshes.push_back(glassInst);
            std::vector<glm::vec4> hdrGlass;
            RENDER_CHECK(renderHDR(glassWorld, cam, baseFogSettings, hdrGlass, diag) &&
                         HasPassNamed(diag, "RefractionCopy"),
                "M15-U6-05.1: refractive glass composes cleanly with froxel fog and executes RefractionCopy");
        }

        // 6d. ParticleSystem (AlphaBlend & Additive at z=-4m vs z=-40m vs occluded)
        {
            auto makeParticleBatch = [](ParticleBlendMode mode, const glm::vec3& pos, const glm::vec4& color) {
                auto batch = std::make_shared<RenderParticleData>();
                batch->Config = ParticleEmitterConfig::CreateNeutral();
                batch->Config.BlendMode = mode;
                batch->Config.LightingEnabled = false;
                batch->Config.SoftnessDistance = 0.0f;
                batch->Config.NearFadeEnabled = false;
                batch->Config.FarFadeEnabled = false;
                batch->Config.StartColor = color;
                batch->Config.ColorOverLifetime = { color, color };
                batch->Config.StartSize = 6.0f;
                batch->Config.EndSize = 6.0f;
                batch->Material = ParticleMaterialAdapter::Resolve(batch->Config, batch->Resources);
                batch->Material.BlendMode = mode;
                batch->Material.LightingEnabled = false;
                batch->Material.BaseColor = glm::vec4(1.0f);
                batch->MaterialResolved = true;

                ParticleSpawnData spawn{};
                spawn.Position = pos;
                spawn.StartSize = 6.0f;
                spawn.EndSize = 6.0f;
                spawn.StartColor = color;
                spawn.EndColor = color;
                spawn.Lifetime = 100.0f;
                batch->Pool.Allocate(4);
                batch->Pool.Spawn(spawn);
                return batch;
            };

            RenderWorld alphaPartWorld = baseWorld;
            alphaPartWorld.SceneRevision = 9607u;
            alphaPartWorld.Particles.push_back(makeParticleBatch(
                ParticleBlendMode::AlphaBlend, glm::vec3(0.0f, 1.5f, -4.0f), glm::vec4(0.0f, 6.0f, 0.0f, 0.5f)));
            std::vector<glm::vec4> hdrPartAlpha;
            RENDER_CHECK(renderHDR(alphaPartWorld, cam, baseFogSettings, hdrPartAlpha, diag),
                "M15-U6-05.1: AlphaBlend particle renders in volumetric fog");
            const glm::vec3 gpuPartAlpha = SampleHDRBlockAvg(hdrPartAlpha, kViewW, kViewW / 2u, kViewH / 2u);
            const glm::vec3 expectedPartAlpha = 0.5f * wallFoggedCenter + 0.5f * (glm::vec3(0.0f, 6.0f, 0.0f) * analytic4.Transmittance + analytic4.InScattering);
            const glm::vec3 buggyPartOpaque = 0.5f * wallFoggedCenter + 0.5f * (glm::vec3(0.0f, 6.0f, 0.0f) * analytic60.Transmittance + analytic60.InScattering);
            std::cout << "  [U6-E PartAlpha] gpu=" << gpuPartAlpha.g << " exp=" << expectedPartAlpha.g << " buggy60=" << buggyPartOpaque.g << "\n";
            RENDER_CHECK(glm::length(gpuPartAlpha - expectedPartAlpha) < 0.08f &&
                         gpuPartAlpha.g > buggyPartOpaque.g * 1.8f &&
                         glm::length(gpuPartAlpha - buggyPartOpaque) > 1.2f,
                "M15-U6-05.1: AlphaBlend particle at z=4m applies foreground fog at true depth instead of background opaque depth");

            RenderWorld addPartNearWorld = baseWorld;
            addPartNearWorld.SceneRevision = 9608u;
            addPartNearWorld.Particles.push_back(makeParticleBatch(
                ParticleBlendMode::Additive, glm::vec3(0.0f, 1.5f, -4.0f), glm::vec4(0.0f, 4.0f, 0.0f, 1.0f)));
            std::vector<glm::vec4> hdrPartAddNear;
            RENDER_CHECK(renderHDR(addPartNearWorld, cam, baseFogSettings, hdrPartAddNear, diag),
                "M15-U6-05.1: Additive particle at z=4m renders");
            const glm::vec3 gpuPartAddNear = SampleHDRBlockAvg(hdrPartAddNear, kViewW, kViewW / 2u, kViewH / 2u);
            const glm::vec3 expectedPartAddNear = wallFoggedCenter + glm::vec3(0.0f, 4.0f, 0.0f) * analytic4.Transmittance;
            RENDER_CHECK(glm::length(gpuPartAddNear - expectedPartAddNear) < 0.08f,
                "M15-U6-05.1: Additive particle at z=4m attenuates emission by foreground transmittance without doubling fog");

            RenderWorld addPartFarWorld = baseWorld;
            addPartFarWorld.SceneRevision = 9609u;
            addPartFarWorld.Particles.push_back(makeParticleBatch(
                ParticleBlendMode::Additive, glm::vec3(0.0f, 1.5f, -40.0f), glm::vec4(0.0f, 4.0f, 0.0f, 1.0f)));
            std::vector<glm::vec4> hdrPartAddFar;
            RENDER_CHECK(renderHDR(addPartFarWorld, cam, baseFogSettings, hdrPartAddFar, diag),
                "M15-U6-05.1: Additive particle at z=40m renders");
            const glm::vec3 gpuPartAddFar = SampleHDRBlockAvg(hdrPartAddFar, kViewW, kViewW / 2u, kViewH / 2u);
            const float nearPartDeltaG = gpuPartAddNear.g - wallFoggedCenter.g;
            const float farPartDeltaG = gpuPartAddFar.g - wallFoggedCenter.g;
            std::cout << "  [U6-E PartAdd] near=" << gpuPartAddNear.g << " far=" << gpuPartAddFar.g
                      << " deltaNear=" << nearPartDeltaG << " deltaFar=" << farPartDeltaG << "\n";
            RENDER_CHECK(farPartDeltaG < nearPartDeltaG * 0.15f,
                "M15-U6-05.1: distant Additive particle attenuates with deeper optical thickness");
        }

        // 6e. M15-U6-05.2: Unlit skybox background + disabled zero light + temporal reprojection
        {
            RenderWorld skyBgOnlyWorld;
            skyBgOnlyWorld.SceneRevision = 9610u;
            skyBgOnlyWorld.Skybox.Authored = true;
            skyBgOnlyWorld.Skybox.Enabled = true;
            skyBgOnlyWorld.Skybox.EnvironmentLighting = false;
            RenderPointLight disabledLight{};
            disabledLight.Position = glm::vec3(0.0f, 1.5f, -5.0f);
            disabledLight.Radius = 20.0f;
            disabledLight.Intensity = 0.0f;
            skyBgOnlyWorld.PointLights.push_back(disabledLight);
            RenderMeshInstance unlitWall = wallInst;
            unlitWall.WorldTransform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -40.5f)),
                glm::vec3(400.0f, 400.0f, 1.0f));
            skyBgOnlyWorld.Meshes.push_back(unlitWall);

            SceneRenderSettings temporalUnlit = baseFogSettings;
            temporalUnlit.FogDensity = 0.04f;
            temporalUnlit.FogTemporalWeight = 0.8f;
            temporalUnlit.EnableSkybox = false;

            auto noFogUnlit = temporalUnlit;
            noFogUnlit.EnableVolumetricFog = false;
            std::vector<glm::vec4> hdrNoFogUnlit;
            RENDER_CHECK(renderHDR(skyBgOnlyWorld, cam, noFogUnlit, hdrNoFogUnlit, diag),
                "M15-U6-05.2: unlit skybox-background baseline renders");
            const glm::vec3 baseUnlit = SampleHDRBlockAvg(hdrNoFogUnlit, kViewW, kViewW / 2u, kViewH / 2u);

            std::vector<glm::vec4> hdrUnlitTemporal;
            for (int f = 0; f < 3; ++f) {
                RENDER_CHECK(renderHDR(skyBgOnlyWorld, cam, temporalUnlit, hdrUnlitTemporal, diag),
                    "M15-U6-05.2: temporal unlit frame renders");
            }
            RENDER_CHECK(HasFallbackPrefix(diag, "froxel_fog_no_directional_sun") &&
                         HasFallbackPrefix(diag, "froxel_fog_unlit_extinction_only"),
                "M15-U6-05.2: skybox-background-only + disabled-light temporal fog emits extinction-only diagnostic");
            std::vector<glm::vec4> scatUnlit, intUnlit;
            const uint32_t fgW = targets->GetFroxelScattering()->GetWidth();
            const uint32_t fgH = targets->GetFroxelScattering()->GetHeight();
            RENDER_CHECK(fgW > 0u && fgH > 0u &&
                         Read3DSliceRGBA16F(*targets->GetFroxelScattering(), 32u, scatUnlit) &&
                         Read3DSliceRGBA16F(*targets->GetFroxelIntegrated(), 32u, intUnlit) &&
                         scatUnlit.size() == size_t(fgW) * fgH &&
                         intUnlit.size() == size_t(fgW) * fgH,
                "M15-U6-05.2: read unlit froxel volumes after temporal frames");
            const size_t centerFroxel = size_t(fgH / 2u) * fgW + (fgW / 2u);
            const glm::vec4 centerS = scatUnlit[centerFroxel];
            const glm::vec4 centerI = intUnlit[centerFroxel];
            const glm::vec3 gpuUnlitHDR = SampleHDRBlockAvg(hdrUnlitTemporal, kViewW, kViewW / 2u, kViewH / 2u);
            const glm::vec3 expectedUnlitHDR = baseUnlit * std::exp(-0.04f * (40.0f - temporalUnlit.FogNearDistance));
            std::cout << "  [U6-E Unlit] centerS=(" << centerS.r << "," << centerS.g << "," << centerS.b << "," << centerS.a
                      << ") centerI.a=" << centerI.a << " gpu=" << gpuUnlitHDR.r << " exp=" << expectedUnlitHDR.r << std::endl;
            RENDER_CHECK(glm::vec3(centerS) == glm::vec3(0.0f) && std::abs(centerS.a - 0.04f) < 5e-4f &&
                         glm::vec3(centerI) == glm::vec3(0.0f) && centerI.a > 0.0f && centerI.a < 0.99f &&
                         glm::length(gpuUnlitHDR - expectedUnlitHDR) < 0.03f,
                "M15-U6-05.2: temporal unlit fog retains exact zero in-scattering and pure Beer-Lambert extinction");

            // Point light only without directional sun: scatters light, does NOT emit extinction-only
            RenderWorld localOnlyWorld;
            localOnlyWorld.SceneRevision = 9611u;
            RenderPointLight activePt{};
            activePt.Position = glm::vec3(0.0f, 1.5f, -6.0f);
            activePt.Radius = 25.0f;
            activePt.Intensity = 60.0f;
            activePt.Color = glm::vec3(1.0f, 0.85f, 0.7f);
            localOnlyWorld.PointLights.push_back(activePt);
            localOnlyWorld.Meshes.push_back(unlitWall);
            std::vector<glm::vec4> hdrLocalOnly;
            RENDER_CHECK(renderHDR(localOnlyWorld, cam, temporalUnlit, hdrLocalOnly, diag),
                "M15-U6-05.2: point-light-only fog render succeeds");
            RENDER_CHECK(HasFallbackPrefix(diag, "froxel_fog_no_directional_sun") &&
                         !HasFallbackPrefix(diag, "froxel_fog_unlit_extinction_only"),
                "M15-U6-05.2: active local light without directional sun scatters without unlit diagnostic");
        }

        // 6f. MSAA 4x fallback diagnostic on GPU
        {
            RHITexture singleColor, singleDepth;
            RENDER_CHECK(singleColor.Init(RHITextureDesc::RenderTarget2D(128u, 128u, RHITextureFormat::RGBA8_UNORM, "U6E_MSAA_Resolve")) &&
                         singleDepth.Init(RHITextureDesc::DepthStencil2D(128u, 128u)),
                "M15-U6-07.6: MSAA 4x single-sample resolve targets init");
            RHIViewTargetDesc msaaVtDesc{};
            msaaVtDesc.Width = 128u;
            msaaVtDesc.Height = 128u;
            msaaVtDesc.Samples = 4u;
            msaaVtDesc.ColorFormat = RHITextureFormat::RGBA8_UNORM;
            msaaVtDesc.EnableDepth = true;
            msaaVtDesc.OwnPrimary = false;
            msaaVtDesc.EnableVolumetricFog = false;
            auto msaaTargets = std::make_shared<RHIViewTargets>();
            RENDER_CHECK(msaaTargets->Configure(msaaVtDesc) && msaaTargets->Validate(),
                "M15-U6-07.6: MSAA 4x fallback targets configured");
            auto msaaSettings = baseFogSettings;
            msaaSettings.MSAASamples = 4u;
            msaaSettings.EnableToneMapping = false;
            renderer.RenderWorldView(baseWorld, cam, &singleColor, &singleDepth, msaaSettings, nullptr, msaaTargets);
            const auto& msaaDiag = renderer.GetDiagnostics().GetFrame();
            RENDER_CHECK(renderer.WasLastRenderSuccessful() && device.WaitIdle() &&
                         HasFallbackPrefix(msaaDiag, "froxel_targets_disabled:multisampled_depth") &&
                         !HasPassNamed(msaaDiag, "FogApply"),
                "M15-U6-07.6: MSAA 4x gracefully disables froxel fog and records froxel_targets_disabled:multisampled_depth");
        }

        // 6g. M15-U6-07.5 & M15-U6-07.6: Full integrated VRAM + hardware GPU pass timestamp scorecard across all 4 tiers
        {
            std::ofstream scorecard(output / "M15_U6E_froxel_scorecard.txt");
            RENDER_CHECK(scorecard.is_open(), "M15-U6-E: scorecard file created");
            const auto& caps = device.GetCapabilities();
#if defined(_DEBUG) || !defined(NDEBUG)
            const char* buildCfg = "Debug";
#else
            const char* buildCfg = "Release";
#endif
            scorecard << "config=" << buildCfg
                      << " backend=" << (device.GetBackend() == RHIType::Vulkan ? "Vulkan" : "D3D12")
                      << " adapter=\"" << caps.AdapterName << "\""
                      << " timestamp_hz=" << caps.TimestampFrequencyHz << "\n";

            RenderWorld perfWorld = baseWorld;
            perfWorld.SceneRevision = 9700u;
            perfWorld.Meshes[0].CastsShadows = false;
            RenderPointLight perfPoint{};
            perfPoint.Position = glm::vec3(1.5f, 2.0f, -6.0f);
            perfPoint.Radius = 25.0f;
            perfPoint.Intensity = 40.0f;
            perfPoint.Shadows.CastShadows = true;
            perfWorld.PointLights.push_back(perfPoint);

            struct PerfRes { uint32_t W; uint32_t H; };
            for (const auto [pw, ph] : std::vector<PerfRes>{ {1920u, 1080u}, {2560u, 1440u} }) {
                RHITexture pColor, pDepth;
                RENDER_CHECK(pColor.Init(RHITextureDesc::RenderTarget2D(pw, ph, RHITextureFormat::RGBA8_UNORM, "U6E_PerfColor")) &&
                             pDepth.Init(RHITextureDesc::DepthStencil2D(pw, ph)),
                    "M15-U6-07.6: perf color/depth init");

                for (uint8_t tier = 0u; tier < 4u; ++tier) {
                    const auto q = static_cast<FroxelFogQuality>(tier);
                    const auto spec = FroxelFogContract::GetTierSpec(q);
                    const auto mem = FroxelFogContract::EstimateMemoryBudget(pw, ph, q, true);

                    RHIViewTargetDesc pvtDesc{};
                    pvtDesc.Width = pw;
                    pvtDesc.Height = ph;
                    pvtDesc.ColorFormat = RHITextureFormat::RGBA8_UNORM;
                    pvtDesc.HDR = true;
                    pvtDesc.OwnPrimary = false;
                    pvtDesc.EnableVolumetricFog = true;
                    pvtDesc.FroxelQuality = tier;
                    auto pTargets = std::make_shared<RHIViewTargets>();
                    RENDER_CHECK(pTargets->Configure(pvtDesc) && pTargets->Validate(),
                        "M15-U6-07.6: perf view targets init");

                    auto pSettings = baseFogSettings;
                    pSettings.FogQuality = q;
                    pSettings.FogTemporalWeight = 0.8f;
                    pSettings.EnableShadows = true;

                    double sumCopy = 0.0, sumInject = 0.0, sumReproj = 0.0, sumInteg = 0.0, sumApply = 0.0, sumTotal = 0.0;
                    uint32_t measuredFrames = 0u;
                    constexpr int kWarmup = 4;
                    constexpr int kSamples = 6;
                    for (int f = 0; f < kWarmup + kSamples; ++f) {
                        PerspectiveCamera perfCam = cam;
                        const float xJitter = 0.02f * static_cast<float>(f % 3);
                        perfCam.SetViewMatrix(glm::lookAtRH(camPos + glm::vec3(xJitter, 0.0f, 0.0f),
                            glm::vec3(xJitter, 1.5f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                            camPos + glm::vec3(xJitter, 0.0f, 0.0f));
                        renderer.RenderWorldView(perfWorld, perfCam, &pColor, &pDepth, pSettings, nullptr, pTargets);
                        RENDER_CHECK(renderer.WasLastRenderSuccessful() && device.WaitIdle(),
                            "M15-U6-07.6: perf frame executes cleanly");
                        const auto& timing = renderer.GetLastGPUTimingFrame();
                        if (f >= kWarmup && caps.TimestampFrequencyHz > 0u) {
                            auto findPass = [&](const char* name, double& outMs) {
                                for (const auto& p : timing.Passes) {
                                    if (p.Name == name) {
                                        outMs = p.Milliseconds;
                                        return p.Validity == DiagnosticValidity::Valid;
                                    }
                                }
                                return false;
                            };
                            double tCopy = 0.0, tInj = 0.0, tRep = 0.0, tInt = 0.0, tApp = 0.0;
                            if (timing.Validity == DiagnosticValidity::Valid &&
                                findPass("FogCopy", tCopy) && findPass("FogInject", tInj) &&
                                findPass("FogReproject", tRep) && findPass("FogIntegrate", tInt) &&
                                findPass("FogApply", tApp)) {
                                sumCopy += tCopy;
                                sumInject += tInj;
                                sumReproj += tRep;
                                sumInteg += tInt;
                                sumApply += tApp;
                                sumTotal += timing.Milliseconds;
                                ++measuredFrames;
                            }
                        }
                    }
                    if (caps.TimestampFrequencyHz > 0u) {
                        RENDER_CHECK(measuredFrames > 0u,
                            "M15-U6-07.6: valid per-pass GPU timestamps captured for all 5 fog passes");
                    }
                    const double denom = std::max<double>(measuredFrames, 1.0);
                    const double avgCopy = sumCopy / denom;
                    const double avgInj = sumInject / denom;
                    const double avgRep = sumReproj / denom;
                    const double avgInt = sumInteg / denom;
                    const double avgApp = sumApply / denom;
                    const double avgFogSum = avgCopy + avgInj + avgRep + avgInt + avgApp;
                    const double avgTotal = sumTotal / denom;

                    scorecard << pw << "x" << ph
                              << " tier=" << spec.Name
                              << " grid=" << mem.Extent.Width << "x" << mem.Extent.Height << "x" << mem.Extent.Depth
                              << " logical_bytes=" << mem.TotalLogicalBytes
                              << " device_bytes=" << mem.TotalDeviceReportedBytes
                              << " fog_copy_ms=" << avgCopy
                              << " fog_inject_ms=" << avgInj
                              << " fog_reproject_ms=" << avgRep
                              << " fog_integrate_ms=" << avgInt
                              << " fog_apply_ms=" << avgApp
                              << " fog_total_ms=" << avgFogSum
                              << " frame_total_ms=" << avgTotal << "\n";
                }
            }
            scorecard.close();
            RENDER_CHECK(scorecard.good(), "M15-U6-E: scorecard written cleanly");
        }
    }

    return device.WaitIdle() && GetRHIValidationErrorCount() == 0;
}
