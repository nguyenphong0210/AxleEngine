#pragma once
#include "RenderTestCommon.hpp"
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <glm/gtc/packing.hpp>
#include <cstring>

namespace Axle::RenderTests {
using namespace Axle::RHI;
inline bool Upload(RHITexture& texture, const void* pixels, uint32_t pixelSize) {
    nri::TextureSubresourceUploadDesc sub{};
    sub.slices = pixels; sub.sliceNum = 1;
    sub.rowPitch = texture.GetWidth() * pixelSize;
    sub.slicePitch = sub.rowPitch * texture.GetHeight();
    nri::TextureUploadDesc upload{};
    upload.texture = texture.GetNative(); upload.subresources = &sub;
    upload.planes = texture.GetFormat() == RHITextureFormat::D32_FLOAT ? nri::PlaneBits::DEPTH : nri::PlaneBits::COLOR;
    upload.after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::ALL};
    return RHIDevice::Get().UploadData(&upload, 1, nullptr, 0) == nri::Result::SUCCESS;
}

inline bool Read(RHITexture& texture, std::vector<glm::vec4>& pixels) {
    auto& device = RHIDevice::Get();
    const auto alignment = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
    const auto align = [](uint32_t value, uint32_t step) { return (value + step - 1) / step * step; };
    nri::TextureDataLayoutDesc layout{};
    const bool half = texture.GetFormat() == RHITextureFormat::RGBA16_FLOAT;
    const bool scalar = texture.GetFormat() == RHITextureFormat::R32_FLOAT || texture.GetFormat() == RHITextureFormat::D32_FLOAT;
    const uint32_t pixelSize = scalar ? 4u : half ? 8u : 16u;
    layout.rowPitch = align(texture.GetWidth() * pixelSize, alignment.uploadBufferTextureRow);
    layout.slicePitch = align(layout.rowPitch * texture.GetHeight(), alignment.uploadBufferTextureSlice);
    RHIBufferDesc desc; desc.Size = layout.slicePitch; desc.MemoryLocation = RHIMemoryLocation::HostReadback;
    RHIBuffer readback; RHICommandBuffer commands;
    RENDER_CHECK(readback.Init(desc) && commands.Init() && commands.Begin(), "GTAO readback allocation");
    nri::TextureRegionDesc region{};
    region.width = texture.GetWidth(); region.height = texture.GetHeight(); region.depth = 1;
    region.planes = texture.GetFormat() == RHITextureFormat::D32_FLOAT ? nri::PlaneBits::DEPTH : nri::PlaneBits::COLOR;
    commands.ReadbackTextureToBuffer(*readback.GetNative(), layout, *texture.GetNative(), region);
    RENDER_CHECK(commands.End() && commands.Submit() && device.WaitIdle(), "GTAO readback submission");
    const auto* data = static_cast<const uint8_t*>(readback.Map());
    RENDER_CHECK(data, "GTAO readback mapping");
    pixels.resize(size_t(texture.GetWidth()) * texture.GetHeight());
    for (uint32_t y = 0; y < texture.GetHeight(); ++y) {
        if (scalar) {
            for (uint32_t x = 0; x < texture.GetWidth(); ++x) {
                float value; std::memcpy(&value, data + size_t(y) * layout.rowPitch + size_t(x) * 4, 4);
                pixels[size_t(y) * texture.GetWidth() + x] = glm::vec4(value, 0, 0, 1);
            }
        } else if (!half) {
            std::memcpy(pixels.data() + size_t(y) * texture.GetWidth(), data + size_t(y) * layout.rowPitch, texture.GetWidth() * 16);
        } else for (uint32_t x = 0; x < texture.GetWidth(); ++x) {
            uint32_t packed[2]{};
            std::memcpy(packed, data + size_t(y) * layout.rowPitch + size_t(x) * 8, 8);
            pixels[size_t(y) * texture.GetWidth() + x] = glm::vec4(glm::unpackHalf2x16(packed[0]), glm::unpackHalf2x16(packed[1]));
        }
    }
    readback.Unmap();
    return true;
}
}
