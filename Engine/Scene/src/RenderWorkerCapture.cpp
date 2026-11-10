#include "RenderWorkerCapture.hpp"
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <cstring>

namespace Axle::Detail {
    bool CaptureWorkerRgba8(RenderService& service, RenderCpuImage& image, RHI::RHICompletionToken& completion) {
        using namespace RHI;
        auto& device = RHIDevice::Get();
        auto* texture = service.GetDefaultMainColorTarget();
        if (!device.CanSubmit() || !texture || texture->GetFormat() != RHITextureFormat::RGBA8_UNORM ||
            !texture->GetWidth() || !texture->GetHeight() || texture->GetDesc().SampleCount != 1) return false;
        const uint64_t pixels = uint64_t(texture->GetWidth()) * texture->GetHeight();
        if (pixels > RenderWorkerMaxCaptureBytes / 4) return false;
        const uint64_t bytes = pixels * 4;
        const auto& limits = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
        auto align = [](uint64_t n, uint32_t a) { return a ? (n + a - 1) / a * a : 0; };
        const auto row = align(uint64_t(texture->GetWidth()) * 4, limits.uploadBufferTextureRow);
        const auto slice = align(row * texture->GetHeight(), limits.uploadBufferTextureSlice);
        if (!row || !slice || row > UINT32_MAX || slice > RenderWorkerMaxCaptureBytes * 4) return false;
        RenderCpuImage candidate; candidate.Width = texture->GetWidth(); candidate.Height = texture->GetHeight();
        candidate.Rgba8.resize(static_cast<size_t>(bytes));
        RHIBufferDesc desc; desc.Size = slice; desc.MemoryLocation = RHIMemoryLocation::HostReadback;
        RHIBuffer readback; RHICommandBuffer commands;
        if (!readback.Init(desc) || !commands.Init() || !commands.Begin()) return false;
        nri::TextureDataLayoutDesc layout{}; layout.rowPitch = static_cast<uint32_t>(row); layout.slicePitch = static_cast<uint32_t>(slice);
        nri::TextureRegionDesc region{}; region.width = candidate.Width; region.height = candidate.Height; region.depth = 1;
        commands.TransitionTexture(texture->GetNative(), RHIResourceLayout::ShaderResource, RHIResourceLayout::CopySource);
        commands.ReadbackTextureToBuffer(*readback.GetNative(), layout, *texture->GetNative(), region);
        commands.TransitionTexture(texture->GetNative(), RHIResourceLayout::CopySource, RHIResourceLayout::ShaderResource);
        if (!commands.End() || !commands.Submit()) return false;
        const auto token = device.GetLastSubmittedToken();
        if (!device.WaitForCompletion(token)) return false;
        const auto* source = static_cast<const uint8_t*>(readback.Map());
        if (!source) return false;
        for (uint32_t y = 0; y < candidate.Height; ++y)
            std::memcpy(candidate.Rgba8.data() + uint64_t(y) * candidate.Width * 4, source + y * row, size_t(candidate.Width) * 4);
        readback.Unmap(); image = std::move(candidate); completion = token; return true;
    }
}
