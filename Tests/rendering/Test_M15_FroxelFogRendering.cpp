#include "OcclusionTestSupport.hpp"
#include "RHIReadback.hpp"
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
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
            vtDesc.EnableScreenStorage = true;
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

            renderer.RenderWorldView(world, camera, &color, &depth, settings, nullptr, targets);
            RENDER_CHECK(renderer.WasLastRenderSuccessful(),
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

    return device.WaitIdle() && GetRHIValidationErrorCount() == 0;
}
