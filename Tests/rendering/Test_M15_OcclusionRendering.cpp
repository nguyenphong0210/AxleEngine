#include "RenderTestCommon.hpp"
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace Axle;
using namespace Axle::RHI;

namespace {
bool Upload(RHITexture& texture, const void* pixels, uint32_t pixelSize) {
    nri::TextureSubresourceUploadDesc sub{};
    sub.slices = pixels; sub.sliceNum = 1;
    sub.rowPitch = texture.GetWidth() * pixelSize;
    sub.slicePitch = sub.rowPitch * texture.GetHeight();
    nri::TextureUploadDesc upload{};
    upload.texture = texture.GetNative(); upload.subresources = &sub;
    upload.planes = nri::PlaneBits::COLOR;
    upload.after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::ALL};
    return RHIDevice::Get().UploadData(&upload, 1, nullptr, 0) == nri::Result::SUCCESS;
}

bool Read(RHITexture& texture, std::vector<glm::vec4>& pixels) {
    auto& device = RHIDevice::Get();
    const auto alignment = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
    const auto align = [](uint32_t value, uint32_t step) { return (value + step - 1) / step * step; };
    nri::TextureDataLayoutDesc layout{};
    const bool half = texture.GetFormat() == RHITextureFormat::RGBA16_FLOAT;
    const uint32_t pixelSize = half ? 8u : 16u;
    layout.rowPitch = align(texture.GetWidth() * pixelSize, alignment.uploadBufferTextureRow);
    layout.slicePitch = align(layout.rowPitch * texture.GetHeight(), alignment.uploadBufferTextureSlice);
    RHIBufferDesc desc; desc.Size = layout.slicePitch; desc.MemoryLocation = RHIMemoryLocation::HostReadback;
    RHIBuffer readback; RHICommandBuffer commands;
    RENDER_CHECK(readback.Init(desc) && commands.Init() && commands.Begin(), "GTAO readback allocation");
    nri::TextureRegionDesc region{};
    region.width = texture.GetWidth(); region.height = texture.GetHeight(); region.depth = 1;
    commands.ReadbackTextureToBuffer(*readback.GetNative(), layout, *texture.GetNative(), region);
    RENDER_CHECK(commands.End() && commands.Submit() && device.WaitIdle(), "GTAO readback submission");
    const auto* data = static_cast<const uint8_t*>(readback.Map());
    RENDER_CHECK(data, "GTAO readback mapping");
    pixels.resize(size_t(texture.GetWidth()) * texture.GetHeight());
    for (uint32_t y = 0; y < texture.GetHeight(); ++y) {
        if (!half) {
            std::memcpy(pixels.data() + size_t(y) * texture.GetWidth(), data + size_t(y) * layout.rowPitch, texture.GetWidth() * 16);
        } else for (uint32_t x = 0; x < texture.GetWidth(); ++x) {
            uint32_t packed = 0;
            std::memcpy(&packed, data + size_t(y) * layout.rowPitch + size_t(x) * 8, 4);
            pixels[size_t(y) * texture.GetWidth() + x] = glm::vec4(glm::unpackHalf2x16(packed), 0, 1);
        }
    }
    readback.Unmap();
    return true;
}
}

// Exercise the actual view-target allocator, recorder, compute dispatch and
// apply draw at floor-half odd and minimal extents, with native validation on.
bool RunM15OcclusionDispatchTests(RHISceneRenderer& renderer, const RenderWorld& world,
                                 SceneRenderSettings settings, const std::filesystem::path& output) {
    settings.EnableGTAO = true; settings.EnableContactShadows = false;
    settings.EnableToneMapping = settings.EnableFXAA = settings.EnableTAA = false;
    settings.EnableSSR = settings.EnableBloom = settings.EnableDDGI = settings.EnableVolumetricFog = settings.EnableAutoExposure = false;
    settings.MSAASamples = 1;
    std::ofstream evidence(output / "M15_U4C_dispatch.txt");
    RENDER_CHECK(evidence.is_open(), "GTAO dispatch evidence file");
    const glm::uvec2 extents[] = {{1, 1}, {3, 5}, {127, 63}, {1919, 1079}};
    for (const auto extent : extents) {
        auto targets = std::make_shared<RHIViewTargets>();
        RHIViewTargetDesc desc;
        desc.Width = extent.x; desc.Height = extent.y;
        desc.EnableGTAO = desc.EnableNormal = desc.EnableScreenStorage = true;
        RENDER_CHECK(targets->Configure(desc), "odd GTAO view target allocation");
        PerspectiveCamera camera(55.0f, float(extent.x) / extent.y, 0.1f, 100.0f);
        camera.SetViewMatrix(glm::lookAtRH(glm::vec3(0, 0, 5), glm::vec3(0), glm::vec3(0, 1, 0)), glm::vec3(0, 0, 5));
        renderer.RenderWorldView(world, camera, targets->Get(RHIViewTarget::Color), targets->Get(RHIViewTarget::Depth), settings, nullptr, targets);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors(), "odd GTAO production render");
        auto* ao = targets->GetGTAO();
        RENDER_CHECK(ao && ao->GetWidth() == std::max(1u, extent.x >> 1) && ao->GetHeight() == std::max(1u, extent.y >> 1), "actual odd GTAO storage extent");
        std::vector<glm::vec4> values;
        RENDER_CHECK(Read(*ao, values), "odd GTAO half-float readback");
        for (const auto value : values)
            RENDER_CHECK(std::isfinite(value.x) && value.x >= 0 && value.x <= 1 &&
                std::isfinite(value.y) && value.y >= 0 && value.y <= 1, "all dispatched AO/contact texels are finite and bounded");
        RENDER_CHECK(GetRHIValidationErrorCount() == 0, "odd GTAO dispatch and reconstruction have no validation errors");
        evidence << extent.x << 'x' << extent.y << " -> " << ao->GetWidth() << 'x' << ao->GetHeight()
            << " finite_texels=" << values.size() << '\n';
    }
    std::cout << "PASS M15-U4-C native GTAO dispatch and reconstruction: 1x1, 3x5, 127x63, 1919x1079\n";
    return true;
}

// Execute the production PSMain with authored depth/visibility, independently of
// GTAO's horizon estimator. This isolates reconstruction errors that average
// scene luminance and CPU-only weight tests cannot detect.
bool RunM15OcclusionRenderingTests(const std::filesystem::path& output) {
    auto& device = RHIDevice::Get();
    std::ifstream sourceFile(AXLE_GTAO_SHADER_PATH);
    const std::string source((std::istreambuf_iterator<char>(sourceFile)), std::istreambuf_iterator<char>());
    RENDER_CHECK(!source.empty(), "production GTAO shader source");
    RHIShaderCompileDesc compile;
    compile.Source = source; compile.SourcePath = AXLE_GTAO_SHADER_PATH;
    compile.Format = device.GetBackend() == RHIType::Vulkan ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
    compile.EntryPoint = "VSMain"; compile.Stage = RHIShaderStage::Vertex;
    const auto vs = RHIShaderCompiler::Get().Compile(compile);
    compile.EntryPoint = "PSMain"; compile.Stage = RHIShaderStage::Fragment;
    const auto ps = RHIShaderCompiler::Get().Compile(compile);
    RENDER_CHECK(vs.Success && ps.Success, "compile production GTAO apply shaders");

    nri::RootDescriptorDesc root{};
    root.descriptorType = nri::DescriptorType::CONSTANT_BUFFER; root.shaderStages = nri::StageBits::ALL;
    nri::DescriptorRangeDesc ranges[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        ranges[i].baseRegisterIndex = i < 3 ? i : 0;
        ranges[i].descriptorNum = 1;
        ranges[i].descriptorType = i < 3 ? nri::DescriptorType::TEXTURE : nri::DescriptorType::SAMPLER;
        ranges[i].shaderStages = nri::StageBits::FRAGMENT_SHADER;
    }
    nri::DescriptorSetDesc setDesc{};
    setDesc.registerSpace = 1; setDesc.ranges = ranges; setDesc.rangeNum = 4;
    nri::PipelineLayoutDesc layoutDesc{};
    layoutDesc.rootDescriptors = &root; layoutDesc.rootDescriptorNum = 1;
    layoutDesc.descriptorSets = &setDesc; layoutDesc.descriptorSetNum = 1;
    layoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;
    RHIPipelineLayout layout;
    RENDER_CHECK(layout.Init(layoutDesc), "GTAO fixture apply layout");
    RHIGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.Layout = &layout; pipelineDesc.Shaders = {vs.Bytecode, ps.Bytecode};
    pipelineDesc.Rasterizer.CullMode = nri::CullMode::NONE;
    pipelineDesc.DepthStencil.DepthTestEnable = pipelineDesc.DepthStencil.DepthWriteEnable = false;
    pipelineDesc.ColorAttachments = {RHIBlendAttachmentDesc::Opaque()};
    pipelineDesc.ColorAttachmentFormats = {nri::Format::RGBA32_SFLOAT};
    // A local cache releases this fixture's PSO before its layout goes away.
    RHIPipelineCache cache;
    auto* pipeline = cache.GetOrCreateGraphicsPipeline(pipelineDesc);
    RHISampler sampler;
    RENDER_CHECK(pipeline && sampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge)), "GTAO fixture apply pipeline");
    RHIBuffer uniform;
    RENDER_CHECK(uniform.Init(RHIBufferDesc::Constant(sizeof(RHIGTAOConstants), RHIMemoryLocation::HostUpload)), "GTAO fixture constants");
    std::ofstream evidence(output / "M15_U4C_reconstruction.txt");
    RENDER_CHECK(evidence.is_open(), "GTAO reconstruction evidence file");

    struct Case { uint32_t W, H; bool Horizontal = false, Reverse = false, Thin = false, Flat = false; };
    const Case cases[] = {
        {8, 4}, {8, 4, false, true}, {9, 5}, {9, 5, true},
        {127, 63}, {1919, 1079}, {8, 4, false, false, true},
        {1, 1, false, false, false, true}, {1, 5, true}, {5, 1}, {3, 5}, {2, 1}, {1, 2, true},
        {9, 5, false, false, false, true}
    };
    for (const auto& c : cases) {
        const uint32_t halfW = std::max(1u, c.W >> 1), halfH = std::max(1u, c.H >> 1);
        const auto foreground = [&](uint32_t x, uint32_t y) {
            return c.Thin ? (x == 0 && y == 0) : c.Flat || (c.Horizontal ? y < c.H / 2 : x < c.W / 2);
        };
        const auto visibility = [&](bool fg) {
            return (fg != c.Reverse) ? glm::vec2(0.2f, 0.85f) : glm::vec2(0.95f, 0.1f);
        };
        std::vector<float> depths(size_t(c.W) * c.H);
        std::vector<glm::vec4> colors(depths.size(), glm::vec4(1, 1, 1, 0.6f));
        for (uint32_t y = 0; y < c.H; ++y) for (uint32_t x = 0; x < c.W; ++x)
            depths[size_t(y) * c.W + x] = foreground(x, y) ? 0.2f : 0.6f;
        std::vector<glm::vec4> occlusion(size_t(halfW) * halfH);
        bool hasForeground = false, hasBackground = false;
        for (uint32_t y = 0; y < halfH; ++y) for (uint32_t x = 0; x < halfW; ++x) {
            // Independent rational mapping of each half-res center to its
            // representative full-res source pixel, including floor odd sizes.
            const auto sourceX = uint32_t((uint64_t(2 * x + 1) * c.W) / (2 * halfW));
            const auto sourceY = uint32_t((uint64_t(2 * y + 1) * c.H) / (2 * halfH));
            const bool fg = foreground(sourceX, sourceY);
            hasForeground |= fg; hasBackground |= !fg;
            occlusion[size_t(y) * halfW + x] = glm::vec4(visibility(fg), 0, 0);
        }
        RHITexture depth, ao, color, target;
        auto inputDesc = RHITextureDesc::RenderTarget2D(c.W, c.H, RHITextureFormat::R32_FLOAT);
        inputDesc.Usage = RHITextureUsage::ShaderResource;
        RENDER_CHECK(depth.Init(inputDesc), "GTAO synthetic scene depth");
        inputDesc.Format = RHITextureFormat::RGBA32_FLOAT;
        RENDER_CHECK(color.Init(inputDesc), "GTAO synthetic scene color");
        inputDesc.Width = halfW; inputDesc.Height = halfH;
        RENDER_CHECK(ao.Init(inputDesc) && target.Init(RHITextureDesc::RenderTarget2D(c.W, c.H, RHITextureFormat::RGBA32_FLOAT)), "GTAO synthetic visibility and output");
        RENDER_CHECK(Upload(depth, depths.data(), 4) && Upload(ao, occlusion.data(), 16) && Upload(color, colors.data(), 16), "GTAO synthetic input upload");
        RHIGTAOConstants constants{};
        constants.InvProjection[2][2] = 10.0f; // Raw depths .2/.6 reconstruct to view Z=2/6.
        constants.ScreenSize = glm::vec4(float(c.W), float(c.H), 1.0f / c.W, 1.0f / c.H);
        constants.GTAOParams.y = constants.ContactParams.z = 1.0f;
        RENDER_CHECK(uniform.UpdateData(&constants, sizeof(constants)), "GTAO synthetic constants update");
        RHIDescriptorPool pool;
        RENDER_CHECK(pool.Init(), "GTAO synthetic descriptor pool");
        auto* set = pool.AllocateDescriptorSet(layout, 0);
        RENDER_CHECK(set && pool.UpdateTexture(set, 0, 0, depth.GetSRV()) && pool.UpdateTexture(set, 1, 0, ao.GetSRV()) &&
            pool.UpdateTexture(set, 2, 0, color.GetSRV()) && pool.UpdateSampler(set, 3, 0, sampler.GetNative()), "GTAO synthetic bindings");
        RHICommandBuffer commands;
        RENDER_CHECK(commands.Init() && commands.Begin(), "GTAO synthetic recording");
        commands.TransitionTexture(target.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
        nri::AttachmentDesc attachment{};
        attachment.descriptor = target.GetRTV(); attachment.loadOp = nri::LoadOp::CLEAR; attachment.storeOp = nri::StoreOp::STORE;
        nri::RenderingDesc rendering{}; rendering.colors = &attachment; rendering.colorNum = 1;
        commands.BeginRendering(rendering);
        commands.SetViewport({0, 0, float(c.W), float(c.H), 0, 1});
        commands.SetScissor({0, 0, static_cast<nri::Dim_t>(c.W), static_cast<nri::Dim_t>(c.H)});
        commands.SetPipeline(pipeline); commands.SetPipelineLayout(nri::BindPoint::GRAPHICS, layout);
        commands.SetRootDescriptor(0, uniform.GetCBV(), 0, nri::BindPoint::GRAPHICS);
        pool.Bind(commands.GetNative()); commands.SetDescriptorSet(nri::BindPoint::GRAPHICS, 0, set);
        commands.Draw(3); commands.EndRendering();
        RENDER_CHECK(commands.End() && commands.Submit() && device.WaitIdle(), "GTAO production apply submission");
        std::vector<glm::vec4> actual;
        RENDER_CHECK(Read(target, actual), "GTAO production apply readback");
        float maxError = 0;
        for (uint32_t y = 0; y < c.H; ++y) for (uint32_t x = 0; x < c.W; ++x) {
            const bool fg = foreground(x, y);
            const auto v = (fg ? hasForeground : hasBackground) ? visibility(fg) : glm::vec2(1);
            const float expected = (0.8375f + 0.1625f * v.x) * (0.9f + 0.1f * v.y);
            const auto pixel = actual[size_t(y) * c.W + x];
            RENDER_CHECK(std::isfinite(pixel.x) && std::isfinite(pixel.y) && std::isfinite(pixel.z) && std::isfinite(pixel.w), "finite GTAO reconstruction output");
            maxError = std::max({maxError, std::abs(pixel.x - expected), std::abs(pixel.y - expected), std::abs(pixel.z - expected), std::abs(pixel.w - 0.6f)});
        }
        evidence << c.W << 'x' << c.H << " horizontal=" << c.Horizontal << " reverse=" << c.Reverse
            << " thin=" << c.Thin << " flat=" << c.Flat << " max_error=" << maxError << '\n';
        evidence.flush();
        if (maxError >= 2e-5f) std::cerr << "GTAO edge error at " << c.W << 'x' << c.H << ": " << maxError << '\n';
        RENDER_CHECK(maxError < 2e-5f, "production bilateral reconstruction preserves each surface and neutral unmatched fallback");
    }
    std::cout << "PASS M15-U4-C production GTAO reconstruction: edge isolation, reversed visibility, thin fallback, odd and minimal extents\n";
    return true;
}
