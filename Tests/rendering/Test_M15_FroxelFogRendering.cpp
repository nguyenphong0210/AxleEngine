#include "OcclusionTestSupport.hpp"
#include "RHIReadback.hpp"
#include <Axle/RHISceneRenderer.hpp>
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
    }

    return device.WaitIdle() && GetRHIValidationErrorCount() == 0;
}
