#include <Axle/RHI/RHITransientResources.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <array>
#include <cstring>

namespace Axle::RHI {
    namespace {
        struct ProbeOutput {
            std::vector<uint8_t> Pixels;
            std::vector<uint32_t> Words;
            uint64_t Bytes = 0;
        };
        bool RunProbe(bool alias, ProbeOutput& output) {
            auto& device = RHIDevice::Get();
            auto image = RHITextureDesc::RenderTarget2D(8, 8);
            image.Usage = image.Usage | RHITextureUsage::CopySource;
            const auto buffer = RHIBufferDesc::Vertex(256);
            RHITransientResources resources;
            if (!resources.InitPlaced({image, image, buffer, buffer}, alias ? std::vector<uint32_t>{0, 0, 1, 1} : std::vector<uint32_t>{})) return false;
            output.Bytes = resources.GetAllocatedBytes();
            std::array<uint32_t, 128> pattern{};
            for (uint32_t i = 0; i < pattern.size(); ++i) pattern[i] = 0x12340000 + i * 17;
            RHIBuffer upload, imageReadback, bufferReadback;
            if (!upload.Init(RHIBufferDesc::Vertex(sizeof(pattern), RHIMemoryLocation::HostUpload), pattern.data())) return false;
            const auto alignments = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
            auto align = [](uint32_t n, uint32_t a) { return (n + a - 1) / a * a; };
            nri::TextureDataLayoutDesc layout{};
            layout.rowPitch = align(32, alignments.uploadBufferTextureRow);
            layout.slicePitch = align(layout.rowPitch * 8, alignments.uploadBufferTextureSlice);
            RHIBufferDesc readback; readback.MemoryLocation = RHIMemoryLocation::HostReadback;
            readback.Size = layout.slicePitch * 2;
            if (!imageReadback.Init(readback)) return false;
            readback.Size = sizeof(pattern);
            if (!bufferReadback.Init(readback)) return false;
            RHICommandBuffer commands;
            if (!commands.Init() || !commands.Begin()) return false;
            // The guard must reject activation before any previous copy-read use.
            if (alias && resources.RecordAlias(commands, 0, 1, RHIAccess::ColorAttachment)) return false;
            for (uint32_t i = 0; i < 2; ++i) {
                if (i && alias) {
                    if (!resources.RecordAlias(commands, 0, 1, RHIAccess::ColorAttachment) ||
                        !resources.RecordAlias(commands, 2, 3, RHIAccess::CopyDest)) return false;
                }
                const auto* texture = resources.Get(i);
                nri::AttachmentDesc color{}; color.descriptor = texture->RTV;
                color.loadOp = nri::LoadOp::CLEAR; color.storeOp = nri::StoreOp::STORE;
                color.clearValue.color.f = i ? nri::Color32f{0, 1, 0, 1} : nri::Color32f{1, 0, 0, 1};
                nri::RenderingDesc rendering{}; rendering.colors = &color; rendering.colorNum = 1;
                commands.BeginRendering(rendering); commands.EndRendering();
                nri::TextureRegionDesc region{}; region.width = region.height = 8; region.depth = 1;
                auto destination = layout; destination.offset = i * layout.slicePitch;
                commands.ReadbackTextureToBuffer(*imageReadback.GetNative(), destination, *texture->Texture, region);
                commands.CopyBuffer(*resources.Get(i + 2)->Buffer, 0, *upload.GetNative(), i * 256, 256);
                commands.CopyBuffer(*bufferReadback.GetNative(), i * 256, *resources.Get(i + 2)->Buffer, 0, 256);
            }
            if (!commands.End() || !commands.Submit() || !device.WaitForCompletion(device.GetLastSubmittedToken())) return false;
            const auto* pixels = static_cast<const uint8_t*>(imageReadback.Map());
            if (!pixels) return false;
            for (uint32_t i = 0; i < 2; ++i) for (uint32_t y = 0; y < 8; ++y) for (uint32_t x = 0; x < 8; ++x) {
                const auto* p = pixels + i * layout.slicePitch + y * layout.rowPitch + x * 4;
                if (p[0] != (i ? 0 : 255) || p[1] != (i ? 255 : 0) || p[2] || p[3] != 255) return false;
                output.Pixels.insert(output.Pixels.end(), p, p + 4);
            }
            imageReadback.Unmap();
            const auto* words = static_cast<const uint32_t*>(bufferReadback.Map());
            if (!words || std::memcmp(words, pattern.data(), sizeof(pattern)) != 0) return false;
            output.Words.assign(words, words + pattern.size());
            bufferReadback.Unmap(); return true;
        }
    }
    RHITransientAliasingProbe RHITransientResources::ProbeAliasing() {
        auto& device = RHIDevice::Get();
        RHITransientAliasingProbe result;
        if (!device.CanSubmit()) { result.Diagnostic = "Aliasing probe requires the live render owner"; return result; }
        device.m_Capabilities.SupportsTransientResourceAliasing = false;
        const auto& capabilities = device.GetCapabilities();
        if (!capabilities.HasPlacedResourcePrimitives || !capabilities.HasResourceAliasingFeature ||
            (device.GetBackend() != RHIType::Vulkan &&
                !(device.GetBackend() == RHIType::Direct3D12 && capabilities.IsEnhancedBarriersSupported))) {
            result.Diagnostic = "Aliasing unsupported: verified global barrier path requires Vulkan or D3D12 enhanced barriers";
            return result;
        }
        const auto validationErrors = GetRHIValidationErrorCount();
        if (validationErrors) { result.Diagnostic = "Aliasing probe rejected existing validation errors"; return result; }
        ProbeOutput reference, aliased;
        if (!RunProbe(false, reference) || !device.WaitIdle() || !RunProbe(true, aliased) || !device.WaitIdle() || reference.Pixels != aliased.Pixels ||
            reference.Words != aliased.Words || aliased.Bytes >= reference.Bytes || GetRHIValidationErrorCount() != validationErrors) {
            result.Diagnostic = "Aliasing native parity/retirement/validation probe failed; keep unaliased allocation";
            return result;
        }
        result.Supported = true; result.UnaliasedBytes = reference.Bytes; result.AliasedBytes = aliased.Bytes;
        result.Diagnostic = "Validated identical RGBA8 color and device-buffer copy/clear aliasing; other classes remain gated";
        device.m_Capabilities.SupportsPlacedResources = true;
        device.m_Capabilities.SupportsTransientResourceAliasing = true;
        return result;
    }
}
