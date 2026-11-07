#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"
#include <Axle/Log.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/RHI/RHISampler.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <array>
#include <cstring>
#include <limits>

using namespace Axle;
using namespace Axle::RHI;
using Axle::RenderTests::ReadTexture;

static bool Policy() {
    RENDER_CHECK(RHIBufferDesc::Constant(UINT64_MAX).Size == 0, "constant alignment overflow rejects");
    RENDER_CHECK(RHIBufferDesc::Structured(UINT64_MAX, 16).Size == 0, "structured size overflow rejects");
    RENDER_CHECK(ToNRIFormat(static_cast<RHITextureFormat>(255)) == nri::Format::UNKNOWN, "unknown texture format stays unknown");
    return true;
}

static bool Textures() {
    auto& device = RHIDevice::Get();
    auto desc = RHITextureDesc::RenderTarget2D(8, 8);
    RHITexture texture;
    RENDER_CHECK(texture.Init(desc), "real initial texture and views");
    std::vector<uint8_t> pixels(8 * 8 * 4);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>(i * 17 + 31);
    nri::TextureSubresourceUploadDesc sub{pixels.data(), 1, 8 * 4, 8 * 8 * 4};
    nri::TextureUploadDesc upload{};
    upload.texture = texture.GetNative(); upload.subresources = &sub;
    upload.after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::ALL};
    RENDER_CHECK(device.UploadData(&upload, 1, nullptr, 0) == nri::Result::SUCCESS, "real image upload");
    RENDER_CHECK(ReadTexture(texture) == pixels, "initial GPU pixels match source");
    const auto* old = texture.GetNative();
    const auto* oldSRV = texture.GetSRV();
    for (const auto* boundary : {"CreateTexture", "AllocateTextureMemory", "BindTextureMemory", "CreateTextureSRV", "CreateTextureRTV", "CreateTextureDSV"}) {
        unsigned reached = 0;
        const auto replacement = std::string(boundary) == "CreateTextureDSV" ? RHITextureDesc::DepthStencil2D(16, 16) : RHITextureDesc::RenderTarget2D(16, 16);
        {
            ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == boundary) { ++reached; return nri::Result::FAILURE; }
                return {};
            });
            RHITexture fresh;
            RENDER_CHECK(!fresh.Init(replacement) && !fresh.IsValid() && !fresh.GetSRV() && !fresh.GetRTV() && !fresh.GetDSV(), "new texture failure publishes nothing");
            RENDER_CHECK(!texture.Init(replacement) && texture.GetNative() == old && texture.GetSRV() == oldSRV && texture.GetWidth() == 8, "replacement retains last complete generation");
            RENDER_CHECK(reached == 2, "both actual creation paths reached requested failure boundary");
        }
        RENDER_CHECK(ReadTexture(texture) == pixels, "all original pixels survive failed replacement");
    }
    for (unsigned invalid = 0; invalid < 9; ++invalid) {
        auto bad = desc;
        switch (invalid) {
            case 0: bad.Width = 0; break;
            case 1: bad.Width = UINT32_MAX; break;
            case 2: bad.MipLevels = 99; break;
            case 3: bad.SampleCount = 257; break;
            case 4: bad.ArrayLayers = UINT32_MAX; break;
            case 5: bad.Format = static_cast<RHITextureFormat>(255); break;
            case 6: bad.Usage = RHITextureUsage::DepthStencil; break;
            case 7: bad.IsCubeMap = true; break;
            case 8: bad.SampleCount = 4; bad.MipLevels = 2; break;
        }
        RENDER_CHECK(!texture.Init(bad) && texture.GetNative() == old, "invalid descriptor rejects before destructive replacement/narrowing");
    }
    auto volume = desc; volume.Width = volume.Height = 1; volume.Depth = 8; volume.MipLevels = 4;
    volume.Usage = RHITextureUsage::CopySource | RHITextureUsage::CopyDest;
    RHITexture volumeTexture;
    RENDER_CHECK(volumeTexture.Init(volume) && volumeTexture.GetDesc().MipLevels == 4, "3D depth participates in copy-texture mip limit");
    auto sampledVolume = volume; sampledVolume.Usage = RHITextureUsage::ShaderResource; sampledVolume.MipLevels = 1;
    RENDER_CHECK(volumeTexture.Init(sampledVolume) && volumeTexture.GetSRV(), "single-mip 3D shader view is supported");
    const auto originalVolume = volumeTexture.GetHandle(); sampledVolume.MipLevels = 4;
    if (RHIDevice::Get().GetBackend() == RHIType::Vulkan)
        RENDER_CHECK(!volumeTexture.Init(sampledVolume) && volumeTexture.GetHandle() == originalVolume && volumeTexture.GetSRV(),
            "unsupported pinned-NRI multi-mip 3D Vulkan view rejects without narrowing or destroying the published view");
    else RENDER_CHECK(volumeTexture.Init(sampledVolume) && volumeTexture.GetDesc().MipLevels == 4, "D3D12 preserves full 3D mip shader views");
    RENDER_CHECK(texture.Init(RHITextureDesc::RenderTarget2D(16, 16)) && texture.GetWidth() == 16, "replacement recovers after injected failures");
    return true;
}

static bool Buffers() {
    std::array<uint8_t, 256> bytes{};
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<uint8_t>(i);
    auto desc = RHIBufferDesc::Constant(bytes.size());
    RHIBuffer buffer;
    RENDER_CHECK(buffer.Init(desc, bytes.data()), "real bound mapped buffer and CBV");
    auto* old = buffer.GetNative();
    for (const auto* boundary : {"CreateBuffer", "AllocateBufferMemory", "BindBufferMemory", "CreateBufferCBV", "MapBuffer"}) {
        unsigned reached = 0;
        {
            ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                if (operation == boundary) { ++reached; return nri::Result::FAILURE; }
                return {};
            });
            RHIBuffer fresh;
            RENDER_CHECK(!fresh.Init(desc, bytes.data()) && !fresh.IsValid() && !fresh.GetNativeMemory() && !fresh.GetCBV(), "new buffer rollback releases partial allocation/view");
            RENDER_CHECK(!buffer.Init(desc, bytes.data()) && buffer.GetNative() == old, "buffer replacement retains old generation");
            RENDER_CHECK(reached == 2, "both buffer failure boundaries reached");
        }
        auto* mapped = buffer.Map();
        RENDER_CHECK(mapped && std::memcmp(mapped, bytes.data(), bytes.size()) == 0, "mapped bytes survive replacement failure");
        buffer.Unmap();
    }
    RENDER_CHECK(!buffer.Map(UINT64_MAX, 16) && !buffer.Map(128, UINT64_MAX), "mapping rejects overflow and out-of-range requests");
    auto* slice = static_cast<uint8_t*>(buffer.Map(64, 64));
    RENDER_CHECK(slice && buffer.Map(80, 16) == slice + 16 && !buffer.Map(32, 16) && !buffer.Map(120, 16), "nested map is relative to existing mapping and stays inside it");
    buffer.Unmap();
    RENDER_CHECK(!buffer.UpdateData(bytes.data(), UINT64_MAX, 64), "update arithmetic cannot wrap");
    RHIBuffer deviceBuffer;
    auto deviceDesc = RHIBufferDesc::Vertex(bytes.size(), RHIMemoryLocation::Device);
    RENDER_CHECK(deviceBuffer.Init(deviceDesc, bytes.data()), "full device-local helper upload succeeds");
    RENDER_CHECK(!deviceBuffer.UpdateData(bytes.data(), 16, 16), "partial helper upload rejected before source overread");
    {
        bool reached = false;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "UploadBufferData") { reached = true; return nri::Result::FAILURE; }
            return {};
        });
        auto* previous = deviceBuffer.GetNative();
        RENDER_CHECK(!deviceBuffer.Init(deviceDesc, bytes.data()) && reached && deviceBuffer.GetNative() == previous, "failed initial upload cannot publish new device buffer");
    }
    RHIBuffer readback;
    RHIBufferDesc readDesc; readDesc.Size = bytes.size(); readDesc.MemoryLocation = RHIMemoryLocation::HostReadback;
    RHICommandBuffer copy;
    RENDER_CHECK(readback.Init(readDesc) && copy.Init() && copy.Begin(), "real buffer GPU readback setup");
    copy.TransitionBuffer(deviceBuffer.GetNative(), RHIAccess::VertexBuffer, RHIAccess::CopySource, RHIPipelineStage::AllCommands, RHIPipelineStage::Copy);
    auto& device = RHIDevice::Get();
    copy.CopyBuffer(*readback.GetNative(), 0, *deviceBuffer.GetNative(), 0, bytes.size());
    RENDER_CHECK(copy.End() && copy.Submit() && device.WaitIdle(), "GPU readback submission completes");
    auto* mapped = readback.Map();
    RENDER_CHECK(mapped && std::memcmp(mapped, bytes.data(), bytes.size()) == 0, "old device bytes survive failed upload and partial-update rejection");
    readback.Unmap();
    return true;
}

static bool MemoryOwnership() {
    auto& device = RHIDevice::Get();
    const auto& core = device.GetNRICore();
    auto desc = RHIBufferDesc::Constant(256);
    nri::BufferDesc native{desc.Size, 0, nri::BufferUsageBits::CONSTANT, false};
    nri::Buffer* raw = nullptr;
    nri::Memory* memory = nullptr;
    RENDER_CHECK(core.CreateBuffer(*device.GetNRIDevice(), native, raw) == nri::Result::SUCCESS, "real external wrapper");
    nri::MemoryDesc requirements{};
    core.GetBufferMemoryDesc(*raw, nri::MemoryLocation::HOST_UPLOAD, requirements);
    nri::AllocateMemoryDesc allocation{}; allocation.size = requirements.size; allocation.type = requirements.type;
    RENDER_CHECK(core.AllocateMemory(*device.GetNRIDevice(), allocation, memory) == nri::Result::SUCCESS, "real external memory");
    core.DestroyBuffer(raw); raw = nullptr;
    {
        RHIBuffer placed;
        RENDER_CHECK(!placed.InitPlaced(desc, memory, 1), "misaligned external placement rejects");
        RENDER_CHECK(placed.InitPlaced(desc, memory, 0), "borrow external allocation");
        auto lease = placed.AcquireLease();
        placed.Shutdown();
        RENDER_CHECK(lease.IsLive(), "the caller keeps borrowed memory alive while a buffer lease survives its wrapper");
        RENDER_CHECK(placed.InitPlaced(desc, memory, 0), "borrowed allocation survives buffer destruction");
    }
    RENDER_CHECK(device.WaitIdle(), "retire all bound buffer generations before the external owner frees its memory");
    core.FreeMemory(memory); memory = nullptr;
    RENDER_CHECK(core.CreateBuffer(*device.GetNRIDevice(), native, raw) == nri::Result::SUCCESS &&
        core.AllocateMemory(*device.GetNRIDevice(), allocation, memory) == nri::Result::SUCCESS, "adoption source objects");
    nri::BindBufferMemoryDesc binding{raw, memory, 0};
    RENDER_CHECK(core.BindBufferMemory(&binding, 1) == nri::Result::SUCCESS, "bind adoption source");
    RHIBuffer adopted;
    {
        ScopedRHIFault fault([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "CreateBufferCBV") return nri::Result::FAILURE;
            return {};
        });
        RENDER_CHECK(!adopted.InitAdopt(desc, raw, memory) && !adopted.IsValid(), "failed adoption leaves ownership with caller");
    }
    RENDER_CHECK(adopted.InitAdopt(desc, raw, memory), "caller retries same live buffer and allocation after rejection");
    adopted.Shutdown();
    std::vector<std::unique_ptr<RHIBuffer>> group;
    std::vector<RHIMemoryAllocation> chunks;
    std::vector<RHIBufferDesc> descs(2, desc);
    RENDER_CHECK(RHIMemoryManager::AllocateBufferGroup(descs, group, chunks, RHIMemoryLocation::HostUpload), "real grouped allocation");
    auto* old = group.front()->GetNative();
    for (const auto* boundary : {"CreateGroupBuffer", "AllocateGroupMemory", "CreateBufferCBV"}) {
        unsigned reached = 0;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == boundary && ++reached == (std::string(boundary) == "AllocateGroupMemory" ? 1u : 2u)) return nri::Result::FAILURE;
            return {};
        });
        RENDER_CHECK(!RHIMemoryManager::AllocateBufferGroup(descs, group, chunks, RHIMemoryLocation::HostUpload) && reached && group.size() == 2 && group.front()->GetNative() == old, "partially built group rolls back and preserves old outputs");
    }
    RENDER_CHECK(RHIMemoryManager::AllocateBufferGroup({}, group, chunks, RHIMemoryLocation::HostUpload) && group.empty() && chunks.empty(), "empty replacement destroys old buffers then frees chunks");
    return true;
}

static bool HelpersAndMeshes() {
    RHISampler sampler;
    RHIDescriptorPool pool;
    RHIPipelineLayout layout;
    nri::PipelineLayoutDesc layoutDesc{}; layoutDesc.shaderStages = nri::StageBits::ALL;
    RENDER_CHECK(sampler.Init() && pool.Init({}) && layout.Init(layoutDesc), "real helper objects");
    auto* oldSampler = sampler.GetNative(); auto* oldPool = pool.GetNative(); auto* oldLayout = layout.GetNative();
    for (const auto* boundary : {"CreateSampler", "CreateDescriptorPool", "CreatePipelineLayout"}) {
        bool reached = false;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == boundary) { reached = true; return nri::Result::FAILURE; }
            return {};
        });
        const bool result = std::string(boundary) == "CreateSampler" ? sampler.Init() :
            std::string(boundary) == "CreateDescriptorPool" ? pool.Init({}) : layout.Init(layoutDesc);
        RENDER_CHECK(!result && reached && sampler.GetNative() == oldSampler && pool.GetNative() == oldPool && layout.GetNative() == oldLayout, "helper replacement failure preserves old objects");
    }
    std::vector<RHIVertex3D> vertices;
    std::vector<uint32_t> indices;
    RHIMesh::GenerateCube(1, vertices, indices);
    RHIMesh mesh;
    RENDER_CHECK(mesh.Init(vertices, indices, "original"), "real mesh buffers");
    auto* oldVB = mesh.GetVertexBuffer().GetNative();
    auto* oldIB = mesh.GetIndexBuffer().GetNative();
    {
        unsigned count = 0;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "MapBuffer" && ++count == 2) return nri::Result::FAILURE;
            return {};
        });
        RENDER_CHECK(!mesh.Init(vertices, indices, "rejected") && count == 2 && mesh.GetVertexBuffer().GetNative() == oldVB && mesh.GetIndexBuffer().GetNative() == oldIB && mesh.GetDebugName() == "original", "index upload failure preserves full old mesh");
    }
    RHIStreamer streamer;
    RHICommandBuffer commands;
    RENDER_CHECK(streamer.Init() && commands.Init() && commands.Begin(), "real streamer recording");
    {
        unsigned count = 0;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "StreamBufferData" && ++count == 2) return nri::Result::FAILURE;
            return {};
        });
        RENDER_CHECK(!mesh.StreamUpload(streamer, commands, vertices, indices, "rejected stream") && count == 2 && mesh.GetVertexBuffer().GetNative() == oldVB && mesh.GetIndexBuffer().GetNative() == oldIB, "partial streamed upload never records a candidate or replaces old mesh");
    }
    RENDER_CHECK(commands.End(), "abandoned upload recording can end");
    streamer.EndFrame();
    auto* mapped = mesh.GetVertexBuffer().Map();
    RENDER_CHECK(mapped && std::memcmp(mapped, vertices.data(), vertices.size() * sizeof(RHIVertex3D)) == 0, "old mesh vertex bytes retained");
    mesh.GetVertexBuffer().Unmap();
    return true;
}

static bool PipelineFailures() {
    auto& compiler = RHIShaderCompiler::Get();
    RENDER_CHECK(compiler.IsInitialized() || compiler.Init(), "real shader compiler for pipeline rollback");
    RHIShaderCompileDesc shader;
    shader.Format = RHIDevice::Get().GetBackend() == RHIType::Vulkan ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
    shader.Source = "float4 VSMain(uint id:SV_VertexID):SV_Position{return float4(float(id)-1,0,0,1);} "
        "float4 PSMain():SV_Target{return float4(1,0,0,1);} [numthreads(1,1,1)] void CSMain(){}";
    shader.EntryPoint = "VSMain"; shader.Stage = RHIShaderStage::Vertex;
    auto vertex = compiler.Compile(shader);
    shader.EntryPoint = "PSMain"; shader.Stage = RHIShaderStage::Fragment;
    auto pixel = compiler.Compile(shader);
    shader.EntryPoint = "CSMain"; shader.Stage = RHIShaderStage::Compute;
    auto compute = compiler.Compile(shader);
    RENDER_CHECK(vertex.Success && pixel.Success && compute.Success, "native pipeline fixture shader bytecode");
    RHIPipelineLayout graphicsLayout, computeLayout, alternateComputeLayout;
    nri::PipelineLayoutDesc layout{}; layout.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;
    RENDER_CHECK(graphicsLayout.Init(layout), "graphics layout");
    layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
    RENDER_CHECK(computeLayout.Init(layout) && alternateComputeLayout.Init(layout), "compute layouts");
    RHIGraphicsPipelineDesc graphics;
    graphics.Layout = &graphicsLayout; graphics.Shaders = {vertex.Bytecode, pixel.Bytecode};
    graphics.DepthStencil.DepthTestEnable = graphics.DepthStencil.DepthWriteEnable = false;
    graphics.ColorAttachmentFormats = {nri::Format::RGBA8_UNORM};
    RHIComputePipelineDesc computeDesc; computeDesc.Layout = &computeLayout; computeDesc.ComputeShader = compute.Bytecode;
    auto& cache = RHIPipelineCache::Get();
    auto* oldGraphics = cache.GetOrCreateGraphicsPipeline(graphics);
    auto* oldCompute = cache.GetOrCreateComputePipeline(computeDesc);
    RENDER_CHECK(oldGraphics && oldCompute, "real cached pipelines");
    {
        bool graphicsReached = false, computeReached = false;
        ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "CreateGraphicsPipeline") { graphicsReached = true; return nri::Result::FAILURE; }
            if (operation == "CreateComputePipeline") { computeReached = true; return nri::Result::FAILURE; }
            return {};
        });
        auto replacement = graphics; replacement.Rasterizer.CullMode = nri::CullMode::NONE;
        auto computeReplacement = computeDesc; computeReplacement.Layout = &alternateComputeLayout;
        RENDER_CHECK(!cache.GetOrCreateGraphicsPipeline(replacement) && graphicsReached &&
            !cache.GetOrCreateComputePipeline(computeReplacement) && computeReached, "native pipeline failures reject uncached candidates");
        RENDER_CHECK(cache.GetOrCreateGraphicsPipeline(graphics) == oldGraphics && cache.GetOrCreateComputePipeline(computeDesc) == oldCompute, "failed creation neither evicts nor poisons prior cache entries");
    }
    cache.Clear();
    return true;
}

int main(int argc, char** argv) {
    Log::Init();
    bool ok = Policy();
    const std::string mode = argc > 1 ? argv[1] : "policy";
    if (ok && mode != "policy") {
        RHIDeviceDesc desc;
        desc.PreferredBackend = mode == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12;
        desc.AllowFallback = false; desc.Headless = true; desc.EnableNRIValidation = true;
        desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(desc.PreferredBackend);
        auto& device = RHIDevice::Get();
        ok = device.Init(desc) && device.GetBackend() == desc.PreferredBackend && !device.IsSoftwareAdapter();
        if (ok) ok = Textures() && Buffers() && MemoryOwnership() && HelpersAndMeshes() && PipelineFailures();
        device.Shutdown();
    }
    ok = Axle::RenderTests::Check(Axle::RHI::GetRHIValidationErrorCount() == 0,
        "native validation reported no errors, including teardown", __FILE__, __LINE__) && ok;
    std::cout << "RHIResource: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
