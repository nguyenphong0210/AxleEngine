#include "RenderTestCommon.hpp"
#include <Axle/Log.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <cmath>
#include <cstring>

using namespace Axle;
using namespace Axle::RHI;

static bool Policy() {
    RHIResourceService resources;
    RENDER_CHECK(resources.Open({}), "open state policy owner");
    int textureAlias, commandAlias, otherAlias;
    auto owner = resources.Publish(RHIResourceKind::Surface, "external owner", {}, {});
    auto* native = reinterpret_cast<nri::Texture*>(&textureAlias);
    RENDER_CHECK(!resources.ImportTexture(native, {}, 3, 2, nri::PlaneBits::DEPTH, {}), "imports require explicit ownership");
    auto image = resources.ImportTexture(native, owner, 3, 2, nri::PlaneBits::DEPTH | nri::PlaneBits::STENCIL, {});
    auto command = resources.Publish(RHIResourceKind::Commands, "recording", {&commandAlias}, {});
    RENDER_CHECK(image && command && resources.BeginRecording(&commandAlias) && resources.RecordUse(&commandAlias, image), "independent generation state");
    nri::TextureBarrierDesc request{}; request.texture = native; request.mipOffset = 1; request.mipNum = 1;
    request.layerOffset = 1; request.layerNum = 1; request.planes = nri::PlaneBits::DEPTH;
    request.after = {nri::AccessBits::COPY_SOURCE, nri::Layout::COPY_SOURCE, nri::StageBits::COPY};
    std::vector<nri::TextureBarrierDesc> barriers;
    RENDER_CHECK(resources.BuildTextureBarriers(&commandAlias, request, barriers) && barriers.size() == 1 &&
        barriers[0].before.layout == nri::Layout::UNDEFINED, "first use and exact range");
    RENDER_CHECK(resources.GetTextureState(image.GetHandle(), 1, 1, nri::PlaneBits::DEPTH)->layout == nri::Layout::UNDEFINED,
        "recording cannot publish committed state");
    resources.AbandonRecording(&commandAlias);
    RENDER_CHECK(resources.GetTextureState(image.GetHandle(), 1, 1, nri::PlaneBits::DEPTH)->layout == nri::Layout::UNDEFINED,
        "abandon preserves first use");
    RENDER_CHECK(resources.BeginRecording(&commandAlias) && resources.RecordUse(&commandAlias, image), "record replacement");
    barriers.clear();
    RENDER_CHECK(resources.BuildTextureBarriers(&commandAlias, request, barriers) && resources.EndRecording(&commandAlias, true), "executable range");
    const void* commands[] = {&commandAlias};
    auto publication = resources.PrepareSubmissionStates(commands, 1);
    RENDER_CHECK(publication != nullptr, "prepare state publication before native submit");
    resources.CommitSubmissionStates(publication); resources.MarkSubmitted(commands, 1);
    RENDER_CHECK(resources.GetTextureState(image.GetHandle(), 1, 1, nri::PlaneBits::DEPTH)->layout == nri::Layout::COPY_SOURCE &&
        resources.GetTextureState(image.GetHandle(), 1, 1, nri::PlaneBits::STENCIL)->layout == nri::Layout::UNDEFINED &&
        resources.GetTextureState(image.GetHandle(), 0, 1, nri::PlaneBits::DEPTH)->layout == nri::Layout::UNDEFINED &&
        resources.GetTextureState(image.GetHandle(), 1, 0, nri::PlaneBits::DEPTH)->layout == nri::Layout::UNDEFINED,
        "mip, layer and plane states are independent");
    resources.AbandonRecording(&commandAlias);
    RENDER_CHECK(resources.BeginRecording(&commandAlias), "invalid usage fixture");
    request.mipNum = 3;
    RENDER_CHECK(!resources.BuildTextureBarriers(&commandAlias, request, barriers), "reject out of range");
    request.mipNum = 1; request.srcQueue = request.dstQueue = reinterpret_cast<nri::Queue*>(&otherAlias);
    RENDER_CHECK(!resources.BuildTextureBarriers(&commandAlias, request, barriers), "reject unsupported ownership transfer");
    const auto old = image.GetHandle();
    publication.reset(); image.Reset(); resources.AbandonRecording(&commandAlias); resources.CollectAfterDrain();
    auto replacement = resources.ImportTexture(native, owner, 1, 1, nri::PlaneBits::COLOR, {});
    RENDER_CHECK(replacement && !resources.Acquire(old) && replacement.GetHandle() != old &&
        resources.GetTextureState(replacement.GetHandle())->layout == nri::Layout::UNDEFINED, "reused address has fresh generation state");
    resources.CloseAfterDrain();
    return true;
}

static bool Native(RHIType backend) {
    auto& device = RHIDevice::Get();
    RHIDeviceDesc init; init.PreferredBackend = backend; init.AllowFallback = false; init.Headless = true;
    init.EnableNRIValidation = true; init.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    RENDER_CHECK(device.Init(init) && device.GetBackend() == backend && !device.IsSoftwareAdapter(), "physical state fixture");
    RHITexture msaa, resolved, depth, depthCopy;
    auto desc = RHITextureDesc::RenderTarget2D(8, 8); desc.SampleCount = 4;
    RENDER_CHECK(msaa.Init(desc) && resolved.Init(RHITextureDesc::RenderTarget2D(8, 8)) &&
        depth.Init(RHITextureDesc::DepthStencil2D(8, 8)) && depthCopy.Init(RHITextureDesc::DepthStencil2D(8, 8)), "color, depth and MSAA generations");
    const auto alignment = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
    auto align = [](uint32_t size, uint32_t step) { return (size + step - 1) / step * step; };
    nri::TextureDataLayoutDesc data{}; data.rowPitch = align(32, alignment.uploadBufferTextureRow);
    data.slicePitch = align(data.rowPitch * 8, alignment.uploadBufferTextureSlice);
    RHIBuffer readback; RHIBufferDesc buffer; buffer.Size = data.slicePitch * 2; buffer.MemoryLocation = RHIMemoryLocation::HostReadback;
    RENDER_CHECK(readback.Init(buffer), "physical readback storage");
    for (int frame = 0; frame < 3; ++frame) {
        RHICommandBuffer commands;
        RENDER_CHECK(commands.Init() && commands.Begin(), "new recording over reused targets");
        nri::AttachmentDesc color{}; color.descriptor = msaa.GetRTV(); color.loadOp = nri::LoadOp::CLEAR; color.storeOp = nri::StoreOp::STORE;
        color.clearValue.color.f = {1, 0, 0, 1};
        nri::RenderingDesc rendering{}; rendering.colors = &color; rendering.colorNum = 1;
        commands.BeginRendering(rendering); commands.EndRendering();
        commands.ResolveTexture(resolved.GetNative(), msaa.GetNative());
        rendering = {}; rendering.depth.descriptor = depth.GetDSV(); rendering.depth.loadOp = nri::LoadOp::CLEAR;
        rendering.depth.storeOp = nri::StoreOp::STORE; rendering.depth.clearValue.depthStencil.depth = 0.25f;
        commands.BeginRendering(rendering); commands.EndRendering();
        commands.CopyTexture(*depthCopy.GetNative(), nullptr, *depth.GetNative(), nullptr);
        nri::TextureRegionDesc region{}; region.width = region.height = 8; region.depth = 1;
        commands.ReadbackTextureToBuffer(*readback.GetNative(), data, *resolved.GetNative(), region);
        auto depthData = data; depthData.offset = data.slicePitch;
        commands.ReadbackTextureToBuffer(*readback.GetNative(), depthData, *depthCopy.GetNative(), region);
        RENDER_CHECK(commands.End() && commands.Submit() && device.WaitIdle(), "submit repeated color, depth copy and MSAA resolve");
        auto* bytes = static_cast<const unsigned char*>(readback.Map());
        RENDER_CHECK(bytes && bytes[0] == 255 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 255, "actual resolved GPU pixels");
        float value = 0; std::memcpy(&value, bytes + data.slicePitch, sizeof(value));
        RENDER_CHECK(std::abs(value - 0.25f) < 0.00001f, "actual copied GPU depth"); readback.Unmap();
    }
    RHITexture pending; RENDER_CHECK(pending.Init(RHITextureDesc::RenderTarget2D(8, 8)), "failed submit target");
    RHICommandBuffer first, stale;
    for (auto* command : {&first, &stale}) {
        RENDER_CHECK(command->Init() && command->Begin(), "independent recording snapshots");
        command->TransitionTexture(pending.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResource);
        RENDER_CHECK(command->End(), "end state snapshot");
    }
    RENDER_CHECK(first.Submit() && device.WaitIdle() && !stale.Submit(), "reject stale encoded barriers after another recording changed state");
    RENDER_CHECK(first.Reset() && first.Begin(), "failed submit recording");
    first.TransitionTexture(pending.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
    RENDER_CHECK(first.End(), "end failed submit recording");
    {
        ScopedRHIFault fail([](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == "QueueSubmit") return nri::Result::FAILURE; return {};
        });
        RENDER_CHECK(!first.Submit() && device.GetResources().GetTextureState(pending.GetHandle())->layout == nri::Layout::SHADER_RESOURCE,
            "failed native submit cannot publish pending state");
    }
    RENDER_CHECK(device.WaitIdle(), "state fixture drain");
    return device.Shutdown();
}

int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting(); Log::Init(); bool ok = false;
    try { const std::string mode = argc > 1 ? argv[1] : "policy"; ok = Policy();
        if (ok && mode != "policy") ok = Native(mode == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    RHIDevice::Get().Shutdown();
    ok = Axle::RenderTests::Check(GetRHIValidationErrorCount() == 0, "native validation including teardown", __FILE__, __LINE__) && ok;
    std::cout << "Resource states: " << (ok ? "PASS" : "FAIL") << '\n'; return ok ? 0 : 1;
}
