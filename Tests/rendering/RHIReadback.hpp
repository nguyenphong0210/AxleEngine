#pragma once
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <vector>
#include <cstring>

namespace Axle::RenderTests {
using namespace Axle::RHI;
inline std::vector<uint8_t> ReadTexture(RHITexture& texture) {
    auto& device = RHIDevice::Get();
    const auto& core = device.GetNRICore();
    const auto& alignment = core.GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
    auto align = [](uint32_t value, uint32_t multiple) { return (value + multiple - 1) / multiple * multiple; };
    nri::TextureDataLayoutDesc layout{};
    layout.rowPitch = align(texture.GetWidth() * 4, alignment.uploadBufferTextureRow);
    layout.slicePitch = align(layout.rowPitch * texture.GetHeight(), alignment.uploadBufferTextureSlice);
    RHIBufferDesc desc;
    desc.Size = layout.slicePitch; desc.MemoryLocation = RHIMemoryLocation::HostReadback;
    RHIBuffer readback;
    RHICommandBuffer commands;
    if (!readback.Init(desc) || !commands.Init() || !commands.Begin()) return {};
    commands.TransitionTexture(texture.GetNative(), RHIResourceLayout::ShaderResource, RHIResourceLayout::CopySource);
    nri::TextureRegionDesc region{};
    region.width = texture.GetWidth(); region.height = texture.GetHeight(); region.depth = 1;
    core.CmdReadbackTextureToBuffer(*commands.GetNative(), *readback.GetNative(), layout, *texture.GetNative(), region);
    commands.TransitionTexture(texture.GetNative(), RHIResourceLayout::CopySource, RHIResourceLayout::ShaderResource);
    if (!commands.End() || !commands.Submit() || !device.WaitIdle()) return {};
    auto* bytes = static_cast<const uint8_t*>(readback.Map());
    if (!bytes) return {};
    std::vector<uint8_t> pixels(texture.GetWidth() * texture.GetHeight() * 4);
    for (uint32_t y = 0; y < texture.GetHeight(); ++y)
        std::memcpy(pixels.data() + y * texture.GetWidth() * 4, bytes + y * layout.rowPitch, texture.GetWidth() * 4);
    readback.Unmap();
    return pixels;
}

}
