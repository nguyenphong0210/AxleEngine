#include "RenderTestCommon.hpp"
#include <Axle/Log.hpp>
#include <Axle/ImageSource.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/MaterialTextureCache.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Package.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Window.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <fstream>
#include <chrono>
#include <cstring>

using namespace Axle;
using namespace Axle::RHI;

struct Fixture {
    std::filesystem::path Previous = FileSystem::GetAssetDirectory(), Root;
    explicit Fixture(const std::filesystem::path& output) : Root(std::filesystem::absolute(output)) {
        std::filesystem::create_directories(Root / "non-default media"); FileSystem::SetAssetDirectory(Root);
    }
    ~Fixture() { FileSystem::UnmountPackage(); FileSystem::SetAssetDirectory(Previous); }
    std::vector<uint8_t> TGA(bool green = false) {
        std::vector<uint8_t> bytes(18 + 8 * 8 * 4); bytes[2] = 2; bytes[12] = bytes[14] = 8; bytes[16] = 32; bytes[17] = 0x28;
        for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
            auto* pixel = bytes.data() + 18 + (y * 8 + x) * 4;
            pixel[0] = !green && y >= 4 ? 255 : 0; pixel[1] = green ? 255 : 0;
            pixel[2] = !green && y < 4 ? 255 : 0; pixel[3] = 255;
        }
        return bytes;
    }
    bool Write(bool green = false) {
        const auto data = TGA(green);
        return FileSystem::WriteBinaryFileAtomically(Root / "non-default media/swatch.tga", data.data(), data.size());
    }
    bool Descriptor(const char* type = "Color") {
        TextureDescriptor desc; desc.Name = "Image source fixture"; desc.Source = "non-default media/swatch.tga";
        desc.Type = type; desc.MaxResolution = 8;
        return TextureDescriptor::Save((Root / "texture.axleasset").string(), desc);
    }
    bool HDR() {
        const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
        std::vector<uint8_t> bytes(header.begin(), header.end());
        for (unsigned i = 0; i < 4; ++i) bytes.insert(bytes.end(), {128, 64, 32, 130});
        return FileSystem::WriteBinaryFileAtomically(Root / "non-default media/float.hdr", bytes.data(), bytes.size());
    }
};

static bool CPU(Fixture& fixture) {
    RENDER_CHECK(fixture.Write() && fixture.Descriptor() && fixture.HDR(), "owned raw and descriptor fixtures");
    const auto hdr = LoadImageSource("non-default media/float.hdr");
    float hdrRed = 0; if (hdr.Image) std::memcpy(&hdrRed, hdr.Image->Mips.front().Pixels.data(), sizeof(hdrRed));
    RENDER_CHECK(hdr.Image && hdr.Image->HDR && hdrRed == 2.0f, "context-free HDR decoding preserves values above one");
    ImageDecodeOptions options; options.SRGB = options.GenerateMips = true;
    const auto raw = LoadImageSource("non-default media/swatch.tga", options);
    const auto descriptor = LoadImageSource("texture.axleasset", options);
    RENDER_CHECK(raw.Image && descriptor.Image && raw.Image->Mips.size() == 4 && descriptor.Image->Mips.front().Pixels == raw.Image->Mips.front().Pixels,
        "context-free raw/descriptor RGBA interpretation and mip generation");
    RENDER_CHECK(LoadImageSource("non-default media/swatch.tga", options).Image == raw.Image, "duplicate requests share decoded CPU storage");
    auto flipped = options; flipped.FlipVertically = true;
    const auto flip = LoadImageSource("non-default media/swatch.tga", flipped);
    RENDER_CHECK(flip.Image && flip.Image->Mips.front().Pixels[2] == 255 && raw.Image->Mips.front().Pixels[0] == 255, "orientation belongs to the interpretation key");
    RENDER_CHECK(fixture.Descriptor("Data"), "change authored texture role");
    const auto linear = LoadImageSource("texture.axleasset", options);
    RENDER_CHECK(linear.Image && !linear.Image->Options.SRGB && descriptor.Image->Options.SRGB, "role changes select linear/sRGB CPU interpretation");
    RENDER_CHECK(FileSystem::WriteTextFile(fixture.Root / "texture.axleasset", "invalid descriptor"), "bad descriptor fixture");
    auto broken = LoadImageSource("texture.axleasset", options);
    RENDER_CHECK(broken.UsedLastGood && broken.Image == linear.Image && !broken.Error.empty(), "bad descriptor reload preserves last good CPU data");
    RENDER_CHECK(fixture.Descriptor(), "restore descriptor");
    const auto time = std::filesystem::last_write_time(fixture.Root / "non-default media/swatch.tga");
    RENDER_CHECK(FileSystem::WriteTextFile(fixture.Root / "non-default media/swatch.tga", "not an image"), "bad raw fixture");
    broken = LoadImageSource("non-default media/swatch.tga", options);
    RENDER_CHECK(broken.UsedLastGood && broken.Image == raw.Image, "bad source reload preserves last good pixels");
    RENDER_CHECK(fixture.Write(true), "recover with new source bytes");
    std::filesystem::last_write_time(fixture.Root / "non-default media/swatch.tga", time);
    auto green = LoadImageSource("non-default media/swatch.tga", options);
    RENDER_CHECK(green.Image && green.Revision != raw.Revision && green.Image->Mips.front().Pixels[1] == 255, "content revision catches edits even with restored timestamp");
    std::filesystem::rename(fixture.Root / "non-default media/swatch.tga", fixture.Root / "non-default media/renamed.tga");
    RENDER_CHECK(LoadImageSource("non-default media/swatch.tga", options).UsedLastGood &&
        LoadImageSource("non-default media/renamed.tga", options).SourceKey != raw.SourceKey, "rename/delete retain old references and give new paths distinct source identity");
    auto bytes = fixture.TGA(); PackageWriter package;
    RENDER_CHECK(package.Begin(fixture.Root / "images.axpk") && package.AddData("packed/swatch.tga", bytes.data(), bytes.size()) && package.Finish() &&
        FileSystem::MountPackage(fixture.Root / "images.axpk"), "mount owned VFS image fixture");
    const auto packed = LoadImageSource("packed/swatch.tga", options);
    RENDER_CHECK(packed.Image && packed.Image->Mips.front().Pixels == raw.Image->Mips.front().Pixels && packed.SourceKey != raw.SourceKey, "mounted source namespace uses the same decoder");
    FileSystem::UnmountPackage();
    RENDER_CHECK(fixture.Write() && fixture.Descriptor(), "restore source for GPU fixtures");
    return true;
}

static std::vector<uint8_t> ReadMip(RHITexture& texture, uint16_t mip, uint32_t pixelSize = 4) {
    auto& device = RHIDevice::Get(); const auto alignment = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
    auto align = [](uint32_t value, uint32_t step) { return (value + step - 1) / step * step; };
    const auto width = (std::max)(1u, texture.GetWidth() >> mip), height = (std::max)(1u, texture.GetHeight() >> mip);
    nri::TextureDataLayoutDesc layout{}; layout.rowPitch = align(width * pixelSize, alignment.uploadBufferTextureRow);
    layout.slicePitch = align(layout.rowPitch * height, alignment.uploadBufferTextureSlice);
    RHIBufferDesc desc; desc.Size = layout.slicePitch; desc.MemoryLocation = RHIMemoryLocation::HostReadback;
    RHIBuffer readback; RHICommandBuffer commands;
    if (!readback.Init(desc) || !commands.Init() || !commands.Begin()) return {};
    nri::TextureRegionDesc region{}; region.width = width; region.height = height; region.depth = 1; region.mipOffset = mip;
    commands.ReadbackTextureToBuffer(*readback.GetNative(), layout, *texture.GetNative(), region);
    commands.TransitionTexture(texture.GetNative(), RHIResourceLayout::CopySource, RHIResourceLayout::ShaderResource);
    if (!commands.End() || !commands.Submit() || !device.WaitIdle()) return {};
    const auto* data = static_cast<const uint8_t*>(readback.Map()); if (!data) return {};
    std::vector<uint8_t> result(width * height * pixelSize);
    for (uint32_t y = 0; y < height; ++y) std::memcpy(result.data() + y * width * pixelSize, data + y * layout.rowPitch, width * pixelSize);
    readback.Unmap(); return result;
}

static bool Native(Fixture& fixture, RHIType backend) {
    auto& device = RHIDevice::Get(); RHIDeviceDesc desc; desc.PreferredBackend = backend; desc.AllowFallback = false;
    desc.Headless = true; desc.EnableNRIValidation = true; desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    RENDER_CHECK(device.Init(desc) && !device.IsSoftwareAdapter() && device.GetBackend() == backend, "strict physical image owner");
    ImageDecodeOptions options; options.SRGB = options.GenerateMips = true;
    auto source = LoadImageSource("non-default media/swatch.tga", options);
    auto original = ImageTextureCache::GetNative(source);
    RENDER_CHECK(original && original == ImageTextureCache::GetNative(source) && original->GetFormat() == RHITextureFormat::RGBA8_SRGB,
        "duplicate native requests share source/import/interpretation/epoch generation");
    for (uint16_t mip = 0; mip < source.Image->Mips.size(); ++mip)
        RENDER_CHECK(ReadMip(*original, mip) == source.Image->Mips[mip].Pixels, "physical GPU mip bytes match context-free decode");
    MaterialTextureCache one, two;
    auto hdrSource = LoadImageSource("non-default media/float.hdr");
    auto hdr = ImageTextureCache::GetNative(hdrSource);
    RENDER_CHECK(hdr && hdr->GetFormat() == RHITextureFormat::RGBA32_FLOAT && ReadMip(*hdr, 0, 16) == hdrSource.Image->Mips.front().Pixels,
        "actual native HDR storage preserves decoded float bytes");
    RENDER_CHECK(one.Get("texture.axleasset", true) == two.Get("texture.axleasset", true), "independent renderer consumers share GPU cache storage");
    const auto oldHandle = original->GetHandle();
    RENDER_CHECK(fixture.Write(true), "replace source while old native references remain");
    auto replacement = LoadImageSource("non-default media/swatch.tga", options);
    for (const auto* boundary : {"CreateTextureSRV", "UploadData"}) {
        bool reached = false;
        ScopedRHIFault failure([&](std::string_view operation) -> std::optional<nri::Result> {
            if (operation == boundary) { reached = true; return nri::Result::OUT_OF_MEMORY; } return {};
        });
        RENDER_CHECK(ImageTextureCache::GetNative(replacement) == original && reached && device.CanSubmit(), "recoverable replacement failure preserves last-good native generation");
    }
    auto fresh = ImageTextureCache::GetNative(replacement);
    RENDER_CHECK(fresh && fresh != original && ReadMip(*fresh, 0) == replacement.Image->Mips.front().Pixels &&
        ReadMip(*original, 0) == source.Image->Mips.front().Pixels, "successful reload publishes new pixels without changing retained original pixels");
    RENDER_CHECK(fixture.Descriptor("Data"), "change descriptor role before native lookup");
    auto role = ImageTextureCache::GetNative(LoadImageSource("texture.axleasset", options));
    RENDER_CHECK(role && role->GetFormat() == RHITextureFormat::RGBA8_UNORM && role != fresh, "role/color interpretation cannot alias a different GPU view format");
    RENDER_CHECK(device.Shutdown() && !original->IsValid() && !fresh->IsValid() && device.Init(desc), "cache closes before native device and opens another epoch");
    auto next = ImageTextureCache::GetNative(replacement);
    RENDER_CHECK(next && next->GetHandle().Epoch != oldHandle.Epoch, "reopened device cannot reuse stale GPU cache entries");
    original.reset(); fresh.reset(); role.reset(); one.Clear(); two.Clear();
    RENDER_CHECK(ReadMip(*next, 0) == replacement.Image->Mips.front().Pixels, "stale cache/wrapper cleanup cannot damage new-epoch pixels");
    return device.Shutdown();
}

static bool GL(Fixture& fixture, bool fallback) {
    auto& device = RHIDevice::Get();
    std::unique_ptr<ScopedRHIFault> reject;
    if (fallback) reject = std::make_unique<ScopedRHIFault>([](std::string_view operation) -> std::optional<nri::Result> {
        if (operation == "CreateDevice") return nri::Result::FAILURE; return {};
    });
    RHIDeviceDesc desc; desc.PreferredBackend = fallback ? RHIType::Direct3D12 : RHIType::OpenGL;
    RENDER_CHECK(device.Init(desc) && device.GetBackend() == RHIType::OpenGL, "explicit/fallback GL owner selection"); reject.reset();
    ImageTextureCache::SetSourceResolver(LoadImageSource);
    std::shared_ptr<Texture2D> old;
    uint64_t oldContext = 0;
    for (unsigned context = 0; context < 2; ++context) {
        WindowProps props; props.Width = props.Height = 96; props.Maximized = false; props.GraphicsBackend = GraphicsAPI::OpenGL;
        Window window(props);
        RENDER_CHECK(RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(glfwGetProcAddress)), "real GL entry points");
        const auto generation = Window::GetContextGeneration();
        if (context) RENDER_CHECK(generation != oldContext && !old->IsLoaded() && !old->GetRendererID(), "closed GL generation cannot publish reused numeric names");
        ImageDecodeOptions options; options.SRGB = options.GenerateMips = true;
        auto source = LoadImageSource("texture.axleasset", options);
        auto image = ImageTextureCache::GetGL(source);
        RENDER_CHECK(image && image == ImageTextureCache::GetGL(source), "shared GL source interpretation");
        RENDER_CHECK(image->GetPath() == source.SourcePath.generic_string() && !image->GetPath().empty(), "cached file textures retain their resolved source path");
        auto hdrSource = LoadImageSource("non-default media/float.hdr"); auto hdr = ImageTextureCache::GetGL(hdrSource);
        RENDER_CHECK(hdr != nullptr, "shared GL HDR upload");
        std::vector<uint8_t> floats(hdrSource.Image->Mips.front().Pixels.size());
        glGetTextureImage(hdr->GetRendererID(), 0, GL_RGBA, GL_FLOAT, static_cast<GLsizei>(floats.size()), floats.data());
        RENDER_CHECK(floats == hdrSource.Image->Mips.front().Pixels, "actual GL HDR bytes match the same context-free decoder");
        for (unsigned mip = 0; mip < source.Image->Mips.size(); ++mip) {
            std::vector<uint8_t> pixels(source.Image->Mips[mip].Pixels.size());
            glGetTextureImage(image->GetRendererID(), mip, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(pixels.size()), pixels.data());
            RENDER_CHECK(pixels == source.Image->Mips[mip].Pixels, "actual GL mip bytes match the canonical CPU data");
            RENDER_CHECK(FileSystem::WriteBinaryFileAtomically(fixture.Root / ("mip-" + std::to_string(mip) + ".bin"), pixels.data(), pixels.size()), "saved GL/fallback evidence");
        }
        if (!context) { old = image; oldContext = generation; }
        else { old.reset(); RENDER_CHECK(glIsTexture(image->GetRendererID()) == GL_TRUE, "old-context destruction cannot delete new-context textures"); }
        RENDER_CHECK(glGetError() == GL_NO_ERROR, "GL image operations report no API error");
    }
    return device.Shutdown();
}
int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting(); Log::Init(); bool ok = false;
    try {
        const std::string mode = argc > 1 ? argv[1] : "policy";
        const auto output = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("build/Tests/Work/opt04-images") / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        Fixture fixture(output); ok = CPU(fixture);
        if (ok && mode != "policy") ok = mode == "gl" || mode == "fallback" ? GL(fixture, mode == "fallback") : Native(fixture, mode == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    RHIDevice::Get().Shutdown();
    ok = Axle::RenderTests::Check(GetRHIValidationErrorCount() == 0, "native validation including teardown", __FILE__, __LINE__) && ok;
    std::cout << "Image source/cache: " << (ok ? "PASS" : "FAIL") << '\n'; return ok ? 0 : 1;
}
