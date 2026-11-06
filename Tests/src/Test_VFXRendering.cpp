#include "ScopedAssetTestRoot.hpp"
#include "Test_VFXAcceptance.hpp"
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHIParticleRenderer.hpp>
#include <Axle/ParticleEffectDefinition.hpp>
#include <Axle/ParticleEffectSerializer.hpp>
#include <Axle/ParticleSystem.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <Axle/VFXEditorPanel.hpp>
#include <Axle/AssetThumbnailManager.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <chrono>
#include <array>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace Axle {

    namespace {
        // NRI may return a created wrapper even when committed memory allocation fails.
        // Exercise that real two-stage failure without exhausting the user's GPU memory.
        class ScopedVertexAllocationFailure {
        public:
            explicit ScopedVertexAllocationFailure(const nri::CoreInterface& core)
                : m_Core(const_cast<nri::CoreInterface&>(core)) {
                m_Original = m_Core.CreateCommittedBuffer;
                s_Active = this;
                m_Core.CreateCommittedBuffer = CreateCommittedBuffer;
            }
            ~ScopedVertexAllocationFailure() {
                m_Core.CreateCommittedBuffer = m_Original;
                s_Active = nullptr;
            }
            ScopedVertexAllocationFailure(const ScopedVertexAllocationFailure&) = delete;
            ScopedVertexAllocationFailure& operator=(const ScopedVertexAllocationFailure&) = delete;
            bool WasInjected() const { return m_Injected; }

        private:
            static nri::Result NRI_CALL CreateCommittedBuffer(nri::Device& device,
                nri::MemoryLocation location, float priority, const nri::BufferDesc& desc,
                nri::Buffer*& buffer) {
                auto& self = *s_Active;
                if (!self.m_Injected && (desc.usage & nri::BufferUsageBits::VERTEX)) {
                    self.m_Injected = true;
                    auto result = self.m_Core.CreateBuffer(device, desc, buffer);
                    return result == nri::Result::SUCCESS ? nri::Result::OUT_OF_MEMORY : result;
                }
                return self.m_Original(device, location, priority, desc, buffer);
            }
            nri::CoreInterface& m_Core;
            decltype(nri::CoreInterface::CreateCommittedBuffer) m_Original;
            bool m_Injected = false;
            static inline ScopedVertexAllocationFailure* s_Active = nullptr;
        };
    }

    bool ParseVFXBackendOption(const char* envVal, RHI::RHIType& outBackend, std::string& outError) {
        if (!envVal || envVal[0] == '\0') {
            outError = "AXLE_VFX_TEST_BACKEND environment variable not set. Please set to 'D3D12' or 'Vulkan'.";
            return false;
        }

        std::string val = envVal;
        std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::toupper(c); });

        if (val == "D3D12" || val == "DIRECT3D12" || val == "DX12") {
            outBackend = RHI::RHIType::Direct3D12;
            return true;
        } else if (val == "VULKAN" || val == "VK") {
            outBackend = RHI::RHIType::Vulkan;
            return true;
        }

        outError = "Unsupported AXLE_VFX_TEST_BACKEND value '" + std::string(envVal) + "'. Expected 'D3D12' or 'Vulkan'.";
        return false;
    }

    bool ValidateVFXCaptureDirectory(const std::string& dirPath, std::filesystem::path& outDir, std::string& outError) {
        if (dirPath.empty()) {
            outError = "Capture directory path cannot be empty.";
            return false;
        }

        try {
            outDir = std::filesystem::weakly_canonical(std::filesystem::absolute(dirPath));
            const auto assets = std::filesystem::weakly_canonical(FileSystem::GetAssetDirectory());
            const auto relative = outDir.lexically_relative(assets);
            if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..") {
                outError = "Capture output must be outside the project's asset directory.";
                return false;
            }
            if (std::filesystem::exists(outDir) && !std::filesystem::is_directory(outDir)) {
                outError = "Capture output is not a directory.";
                return false;
            }
            if (!std::filesystem::exists(outDir)) {
                std::filesystem::create_directories(outDir);
            }
            return true;
        } catch (const std::exception& e) {
            outError = "Failed to create or access capture directory: " + std::string(e.what());
            return false;
        }
    }

    VFXReadbackLayout GetVFXReadbackLayout(uint32_t width, uint32_t height, bool hdr) {
        VFXReadbackLayout layout;
        if (!width || !height)
            return layout;
        layout.BytesPerPixel = hdr ? 8 : 4; // 8 bytes for RGBA16F, 4 bytes for RGBA8
        const uint64_t rawRowPitch = static_cast<uint64_t>(width) * layout.BytesPerPixel;
        if (rawRowPitch > 0xffffff00ull)
            return {};
        // Align to 256 bytes per D3D12 texture pitch alignment requirement
        layout.RowPitch = static_cast<uint32_t>((rawRowPitch + 255) & ~255ull);
        layout.SlicePitch = static_cast<uint64_t>(layout.RowPitch) * height;
        return layout;
    }

    std::string FormatVFXTimingReport(const std::string& deviceName, const std::string& backend,
                                     uint32_t width, uint32_t height, uint8_t sampleCount,
                                     const std::vector<double>& frameTimesMs) {
        if (frameTimesMs.empty()) return "No timing samples recorded.";

        double minTime = *std::min_element(frameTimesMs.begin(), frameTimesMs.end());
        double maxTime = *std::max_element(frameTimesMs.begin(), frameTimesMs.end());
        double sum = std::accumulate(frameTimesMs.begin(), frameTimesMs.end(), 0.0);
        double avgTime = sum / frameTimesMs.size();

        std::string report = "=== VFX RHI Rendering Performance Report ===\n";
        report += "Device: " + deviceName + " (" + backend + ")\n";
        report += "Resolution: " + std::to_string(width) + "x" + std::to_string(height) + ", Samples: " + std::to_string(sampleCount) + "\n";
        report += "Frame Count: " + std::to_string(frameTimesMs.size()) + "\n";
        report += "Average Time: " + std::to_string(avgTime) + " ms\n";
        report += "Min Time: " + std::to_string(minTime) + " ms\n";
        report += "Max Time: " + std::to_string(maxTime) + " ms\n";
        return report;
    }

    bool VerifyVFXRendering(const std::filesystem::path& root) {
        using namespace Axle::RHI;

        const char* backendEnv = std::getenv("AXLE_VFX_TEST_BACKEND");
        RHIType targetBackend = RHIType::Direct3D12;
        std::string error;
        if (!ParseVFXBackendOption(backendEnv, targetBackend, error)) {
            std::cout << "  [VFX GPU ACCEPTANCE] " << error << "\n";
            std::cout << "  [VFX GPU ACCEPTANCE] GPU tests were not requested.\n";
            return false;
        }

        auto& device = RHIDevice::Get();
        if (!device.IsInitialized()) {
            RHIDeviceDesc desc;
            desc.PreferredBackend = targetBackend;
            desc.AllowFallback = false;
            desc.Headless = true;
            if (!device.Init(desc)) {
                std::cout << "  [VFX GPU ACCEPTANCE] Failed to initialize the explicitly requested hardware "
                             "backend.\n";
                std::cout << "  [VFX GPU ACCEPTANCE] Physical GPU execution status: NOT RUN.\n";
                return false;
            }
        }

        if (device.GetBackend() != targetBackend) {
            std::cout << "  [VFX GPU ACCEPTANCE] Active backend mismatch; expected "
                      << (targetBackend == RHIType::Direct3D12 ? "Direct3D 12" : "Vulkan") << "\n";
            return false;
        }

        std::cout << "  [VFX GPU ACCEPTANCE] Hardware device active: " << RHITypeToString(device.GetBackend()) << "\n";
        auto check = [](bool ok, const char *message) {
            std::cout << "  [VFX GPU] " << (ok ? "PASS: " : "FAIL: ") << message << std::endl;
            return ok;
        };
        if (!check(!device.GetCapabilities().IsSoftwareAdapter,
                   "Physical hardware; software adapters are rejected"))
            return false;
        {
            RHIBuffer failedBuffer;
            bool failedCleanly = false;
            {
                ScopedVertexAllocationFailure failure(device.GetNRICore());
                failedCleanly = !failedBuffer.Init(RHIBufferDesc::Vertex(1024, RHIMemoryLocation::HostUpload)) &&
                    failure.WasInjected() && !failedBuffer.IsValid() && failedBuffer.GetNative() == nullptr &&
                    failedBuffer.GetSize() == 0;
            }
            if (!check(failedCleanly, "Failed vertex allocation does not publish an unbound NRI buffer"))
                return false;
            if (!check(failedBuffer.Init(RHIBufferDesc::Vertex(1024, RHIMemoryLocation::HostUpload)),
                       "Buffer can recover after a failed allocation"))
                return false;
        }
        std::filesystem::path captures;
        const char *captureEnv = std::getenv("AXLE_VFX_CAPTURE_DIR");
        const auto output = captureEnv ? std::filesystem::path(captureEnv)
                                       : root / "build" / "vfx-captures" /
                                             (targetBackend == RHIType::Vulkan ? "vulkan" : "d3d12");
        if (!ValidateVFXCaptureDirectory(output.string(), captures, error)) {
            std::cerr << error << std::endl;
            return false;
        }
        Axle::Tests::ScopedAssetTestRoot ownedAssets(captures);
        auto writeTexture = [&](const char *name, std::array<uint8_t, 4> rgba) {
            auto path = captures / name;
            std::ofstream file(path, std::ios::binary);
            uint8_t header[18]{};
            header[2] = 2;
            header[12] = 2;
            header[14] = 2;
            header[16] = 32;
            header[17] = 0x28;
            file.write(reinterpret_cast<char *>(header), 18);
            uint8_t bgra[] = {rgba[2], rgba[1], rgba[0], rgba[3]};
            for (int i = 0; i < 4; ++i)
                file.write(reinterpret_cast<char *>(bgra), 4);
            return path.string();
        };
        auto red = writeTexture("red.tga", {255, 0, 0, 255});
        auto green = writeTexture("green.tga", {0, 255, 0, 255});
        auto translucent = writeTexture("translucent.tga", {255, 0, 0, 64});
        if (targetBackend == RHIType::Direct3D12) {
            ImGuiLayer ui(ImGuiBackendType::Direct3D12);
            ui.Init(nullptr);
            AssetThumbnailManager thumbnails;
            thumbnails.SetImGuiLayer(&ui);
            ImGui::GetIO().DisplaySize = ImVec2(128, 128);
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            ui.Begin();
            auto oldHandle = thumbnails.GetOrCreateThumbnail(red);
            if (!check(oldHandle != 0, "Real D3D12 ImGui thumbnail registration")) return false;
            ImGui::GetForegroundDrawList()->AddImage((ImTextureID)oldHandle, ImVec2(0,0), ImVec2(128,128));
            // Right-click Create calls RefreshThumbnails after the images above are queued.
            thumbnails.Clear();
            auto replacement = thumbnails.GetOrCreateThumbnail(green);
            if (!check(replacement != 0 && replacement != oldHandle,
                       "Create refresh cannot recycle a thumbnail descriptor still in the current draw list"))
                return false;
            auto drawQueuedThumbnail = [&](uint32_t expectedChannel) {
                ImGui::Render();
                const auto size = ImGui::GetDrawData()->DisplaySize;
                const uint32_t width = static_cast<uint32_t>(size.x), height = static_cast<uint32_t>(size.y);
                const auto layout = GetVFXReadbackLayout(width, height, false);
                RHITexture guiColor;
                RHIBuffer guiReadback;
                RHICommandBuffer guiCommand;
                RHIBufferDesc rbDesc;
                rbDesc.Size = layout.SlicePitch;
                rbDesc.MemoryLocation = RHIMemoryLocation::HostReadback;
                if (!guiColor.Init(RHITextureDesc::RenderTarget2D(width, height)) ||
                    !guiReadback.Init(rbDesc) || !guiCommand.Init() || !guiCommand.Begin()) return false;
                guiCommand.TransitionTexture(guiColor.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
                nri::AttachmentDesc attachment{};
                attachment.descriptor = guiColor.GetRTV();
                attachment.loadOp = nri::LoadOp::CLEAR;
                attachment.storeOp = nri::StoreOp::STORE;
                nri::RenderingDesc rendering{};
                rendering.colors = &attachment;
                rendering.colorNum = 1;
                guiCommand.BeginRendering(rendering);
                ui.Render(&guiCommand);
                guiCommand.EndRendering();
                guiCommand.TransitionTexture(guiColor.GetNative(), RHIResourceLayout::ColorAttachment, RHIResourceLayout::CopySource);
                nri::TextureDataLayoutDesc data{};
                data.rowPitch = layout.RowPitch;
                data.slicePitch = rbDesc.Size;
                nri::TextureRegionDesc region{};
                region.width = width; region.height = height;
                region.depth = 1;
                device.GetNRICore().CmdReadbackTextureToBuffer(*guiCommand.GetNative(), *guiReadback.GetNative(), data, *guiColor.GetNative(), region);
                if (!guiCommand.End() || !guiCommand.Submit()) return false;
                device.WaitIdle();
                auto* pixel = static_cast<const uint8_t*>(guiReadback.Map());
                if (!pixel) return false;
                const auto center = (height / 2) * layout.RowPitch + (width / 2) * 4;
                bool visible = pixel[center + expectedChannel] > 200 && pixel[center + (1 - expectedChannel)] < 20;
                guiReadback.Unmap();
                ui.End(); // The normal UI submission boundary finally retires old resources.
                return visible;
            };
            if (!check(drawQueuedThumbnail(0), "Cleared thumbnail still renders original red pixels through ImGui on D3D12")) return false;
            ui.Begin();
            ImGui::GetForegroundDrawList()->AddImage((ImTextureID)replacement, ImVec2(0,0), ImVec2(128,128));
            thumbnails.InvalidateThumbnail(green);
            auto nextHandle = thumbnails.GetOrCreateThumbnail(red);
            if (!check(nextHandle != 0 && nextHandle != replacement && drawQueuedThumbnail(1),
                       "Single-thumbnail invalidation preserves queued green pixels until UI submission")) return false;
            // Selecting another texture can happen after Inspector has queued the old image.
            // Exercise the real Inspector; probe for descriptor reuse BEFORE submitting the GPU work.
            // A broken lifetime therefore fails deterministically without sampling a freed resource.
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::GetIO().DisplaySize = ImVec2(1280, 780);
            {
                auto scene = std::make_shared<Scene>();
                auto inspector = std::make_unique<SceneHierarchyPanel>(scene.get());
                inspector->SetImGuiLayer(&ui);
                const auto drawInspector = [&] {
                    bool hierarchyOpen = false, propertiesOpen = true;
                    ImGui::SetNextWindowPos(ImVec2(0, 0));
                    ImGui::SetNextWindowSize(ImVec2(500, 700));
                    inspector->OnImGuiRender(&hierarchyOpen, &propertiesOpen);
                };
                for (int action = 0; action < 4; ++action) {
                    const auto& path = (action % 2 == 0) ? red : green;
                    inspector->SetContext(scene.get());
                    inspector->SetSelectedAsset(path);
                    ui.Begin(); drawInspector(); ui.End(); // Warm up the new child layout.
                    ui.Begin(); drawInspector();
                    ImTextureID image = 0;
                    const auto font = ImGui::GetIO().Fonts->TexRef.GetTexID();
                    for (auto* window : ImGui::GetCurrentContext()->Windows) {
                        if (!window->Active || !std::strstr(window->Name, "TexturePreviewViewport")) continue;
                        for (const auto& command : window->DrawList->CmdBuffer)
                            if (command.ElemCount && command.GetTexID() && command.GetTexID() != font)
                                image = command.GetTexID();
                    }
                    if (!check(image != 0, "Real Inspector queues the selected texture image")) return false;
                    ImGui::GetForegroundDrawList()->AddImage(image, ImVec2(0, 0), ImGui::GetIO().DisplaySize);
                    if (action == 0) inspector->SetSelectedAsset(green);
                    else if (action == 1) inspector->ClearSelectedAsset();
                    else if (action == 2) inspector->SetContext(nullptr);
                    else inspector.reset();
                    RHITexture probe;
                    if (!probe.Init(RHITextureDesc::RenderTarget2D(1, 1))) return false;
                    const auto probeHandle = ui.RegisterTextureDescriptor(probe.GetNativeSRVHandle());
                    if (!check(probeHandle != 0 && probeHandle != image,
                               "Selection, clear, context detach and destruction retain queued Inspector descriptors")) {
                        if (probeHandle) ui.UnregisterTextureDescriptor(probeHandle);
                        ImGui::EndFrame();
                        return false;
                    }
                    if (!check(drawQueuedThumbnail(action % 2),
                               "Retired Inspector texture still renders its original GPU pixels")) return false;
                    ui.UnregisterTextureDescriptor(probeHandle);
                }
            }
            RHITexture descriptorFixture;
            if (!descriptorFixture.Init(RHITextureDesc::RenderTarget2D(1, 1))) return false;
            std::vector<uint64_t> registrations;
            bool full = false;
            for (int i = 0; i < 1025; ++i) {
                auto handle = ui.RegisterTextureDescriptor(descriptorFixture.GetNativeSRVHandle());
                if (!handle) { full = true; break; }
                registrations.push_back(handle);
            }
            if (!check(full && ui.GetAllocatedDescriptorCount() == 1024,
                       "ImGui descriptor exhaustion returns no handle instead of writing outside its heap")) return false;
            for (auto handle : registrations) ui.UnregisterTextureDescriptor(handle);
            // Exercise the production authoring panel and its own scene/viewport, not a duplicate renderer.
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::GetIO().DisplaySize = ImVec2(1280, 780);
            VFXEditorPanel panel;
            panel.SetOpen(true);
            panel.InitRHI(&ui);
            panel.FramePreview();
            auto readTexture = [&](RHITexture& texture) {
                const auto w = texture.GetWidth(), h = texture.GetHeight();
                const auto layout = GetVFXReadbackLayout(w, h, false);
                RHIBuffer buffer;
                RHIBufferDesc desc;
                desc.Size = layout.SlicePitch;
                desc.MemoryLocation = RHIMemoryLocation::HostReadback;
                RHICommandBuffer command;
                if (!buffer.Init(desc) || !command.Init() || !command.Begin()) return std::vector<uint8_t>{};
                command.TransitionTexture(texture.GetNative(), RHIResourceLayout::ShaderResource, RHIResourceLayout::CopySource);
                nri::TextureDataLayoutDesc data{};
                data.rowPitch = layout.RowPitch; data.slicePitch = layout.SlicePitch;
                nri::TextureRegionDesc region{};
                region.width = w; region.height = h; region.depth = 1;
                device.GetNRICore().CmdReadbackTextureToBuffer(*command.GetNative(), *buffer.GetNative(), data, *texture.GetNative(), region);
                command.TransitionTexture(texture.GetNative(), RHIResourceLayout::CopySource, RHIResourceLayout::ShaderResource);
                if (!command.End() || !command.Submit()) return std::vector<uint8_t>{};
                device.WaitIdle();
                auto* mapped = static_cast<const uint8_t*>(buffer.Map());
                if (!mapped) return std::vector<uint8_t>{};
                std::vector<uint8_t> pixels(w * h * 4);
                for (uint32_t y = 0; y < h; ++y) std::memcpy(pixels.data() + y * w * 4, mapped + y * layout.RowPitch, w * 4);
                buffer.Unmap();
                return pixels;
            };
            std::array<size_t, 3> previewRed{};
            for (int frame = 0; frame < 3; ++frame) {
                if (frame == 1) {
                    panel.AddEmitterStack("Authored Emitter");
                    auto config = ParticleEmitterConfig::CreateNeutral();
                    config.EmissionRate = 0; config.InitialSpeed = {0, 0}; config.StartSize = {2, 2};
                    config.StartColor = {1, 0, 0, 1};
                    panel.SetEmitterConfig(config);
                    panel.TriggerBurst(1);
                } else if (frame == 2) panel.RemoveEmitterStack(0);
                ui.Begin();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2(1280, 780));
                panel.OnImGuiRender();
                ImGui::Render();
                RHITexture canvas;
                RHICommandBuffer command;
                if (!canvas.Init(RHITextureDesc::RenderTarget2D(1280, 780)) || !command.Init() || !command.Begin()) return false;
                command.TransitionTexture(canvas.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
                nri::AttachmentDesc attachment{};
                attachment.descriptor = canvas.GetRTV(); attachment.loadOp = nri::LoadOp::CLEAR; attachment.storeOp = nri::StoreOp::STORE;
                nri::RenderingDesc rendering{};
                rendering.colors = &attachment; rendering.colorNum = 1;
                command.BeginRendering(rendering);
                ui.Render(&command);
                command.EndRendering();
                command.TransitionTexture(canvas.GetNative(), RHIResourceLayout::ColorAttachment, RHIResourceLayout::ShaderResource);
                if (!command.End() || !command.Submit()) return false;
                device.WaitIdle();
                auto* viewport = panel.GetRHIViewport();
                if (!check(viewport && viewport->IsValid(), "Authoring panel creates a valid production RHI viewport")) return false;
                const auto pixels = readTexture(*viewport->GetColorTexture());
                const auto w = viewport->GetColorTexture()->GetWidth(), h = viewport->GetColorTexture()->GetHeight();
                if (!check(!pixels.empty(), "Authoring panel preview readback")) return false;
                // Grid geometry can cover the exact center; measure a visible region instead.
                for (size_t pixel = 0; pixel < pixels.size(); pixel += 4)
                    if (pixels[pixel] > 180 && pixels[pixel + 1] < 80 && pixels[pixel + 2] < 80) ++previewRed[frame];
                if (frame == 1) {
                    const auto image = readTexture(canvas);
                    if (!check(!image.empty(), "Full editor workspace renders through real ImGui D3D12 backend")) return false;
                    std::ofstream output(captures / "vfx-editor-workspace.ppm", std::ios::binary);
                    output << "P6\n1280 780\n255\n";
                    for (size_t pixel = 0; pixel < image.size(); pixel += 4) output.write(reinterpret_cast<const char*>(image.data() + pixel), 3);
                    if (!check(bool(output), "Authoring workspace capture written")) return false;
                }
                ui.End();
            }
            if (!check(previewRed[1] > previewRed[0] + 500 && previewRed[1] > previewRed[2] + 500,
                       "Authoring empty -> neutral emitter -> empty changes real preview pixels without phantom emitters")) return false;

        }
        RHISceneRenderer renderer;
        if (!check(renderer.Init(device, false), "Production scene/particle renderer initialization"))
            return false;
        constexpr uint32_t resolution = 128;
        RHITexture color, depth;
        if (!check(color.Init(RHITextureDesc::RenderTarget2D(resolution, resolution)) &&
                       depth.Init(RHITextureDesc::DepthStencil2D(resolution, resolution)),
                   "Offscreen render targets"))
            return false;
        const auto layout = GetVFXReadbackLayout(resolution, resolution, false);
        RHIBuffer readback;
        RHIBufferDesc readDesc;
        readDesc.Size = layout.SlicePitch * 2;
        readDesc.MemoryLocation = RHIMemoryLocation::HostReadback;
        RHICommandBuffer copy;
        if (!check(readback.Init(readDesc) && copy.Init(), "Host readback resources"))
            return false;
        PerspectiveCamera camera(45.0f, 1.0f, 0.1f, 100.0f);
        camera.SetViewMatrix(glm::lookAt(glm::vec3(0, 0, 4), glm::vec3(0), glm::vec3(0, 1, 0)),
                             glm::vec3(0, 0, 4));
        auto scene = std::make_shared<Scene>();
        auto entity = scene->CreateEntity("Particle readback fixture");
        auto &effect = entity.AddComponent<ParticleEmitterComponent>();
        effect.EmitterStacks.clear();
        ParticleEmitterConfig cfg;
        cfg.EmissionRate = 0;
        cfg.LightingEnabled = false;
        cfg.SoftnessDistance = 0;
        cfg.SimulationSpace = ParticleSimulationSpace::World;
        for (int i = 0; i < 2; ++i) {
            ParticleEmitterStack stack(i ? "Right" : "Left", cfg);
            stack.TexturePath = i ? green : red;
            stack.UseTexture = true;
            ParticleSpawnData particle;
            particle.Position = {i ? 0.8f : -0.8f, 0, 0};
            particle.StartSize = particle.EndSize = 0.9f;
            particle.StartColor = glm::vec4(1);
            stack.System->GetPool().Spawn(particle);
            effect.EmitterStacks.push_back(std::move(stack));
        }
        effect.SyncPrimary();
        SceneRenderSettings settings;
        settings.EnableShadows = false;
        settings.EnableSkybox = false;
        settings.EnableStudioLightLobes = false;
        settings.ClearColor = glm::vec4(0, 0, 0, 1);
        std::vector<double> timings;
        auto *renderTarget = &color;
        auto *renderDepth = &depth;
        auto render = [&]() {
            auto start = std::chrono::steady_clock::now();
            renderer.RenderScene(scene, camera, renderTarget, renderDepth, settings);
            timings.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            const auto w = renderTarget->GetWidth(), h = renderTarget->GetHeight();
            const auto imageLayout =
                GetVFXReadbackLayout(w, h, renderTarget->GetFormat() == RHITextureFormat::RGBA16_FLOAT);
            if (imageLayout.SlicePitch > readback.GetSize())
                return std::vector<uint8_t>{};
            if (!copy.Reset() || !copy.Begin())
                return std::vector<uint8_t>{};
            copy.TransitionTexture(renderTarget->GetNative(), RHIResourceLayout::ShaderResource,
                                   RHIResourceLayout::CopySource);
            nri::TextureDataLayoutDesc textureLayout{};
            textureLayout.rowPitch = imageLayout.RowPitch;
            textureLayout.slicePitch = imageLayout.SlicePitch;
            nri::TextureRegionDesc region{};
            region.width = w;
            region.height = h;
            region.depth = 1;
            device.GetNRICore().CmdReadbackTextureToBuffer(*copy.GetNative(), *readback.GetNative(),
                                                           textureLayout, *renderTarget->GetNative(), region);
            copy.TransitionTexture(renderTarget->GetNative(), RHIResourceLayout::CopySource,
                                   RHIResourceLayout::ShaderResource);
            if (!copy.End() || !copy.Submit())
                return std::vector<uint8_t>{};
            device.WaitIdle();
            auto *mapped = static_cast<const uint8_t *>(readback.Map());
            if (!mapped)
                return std::vector<uint8_t>{};
            std::vector<uint8_t> pixels(w * h * imageLayout.BytesPerPixel);
            for (uint32_t y = 0; y < h; ++y)
                std::memcpy(pixels.data() + y * w * imageLayout.BytesPerPixel,
                            mapped + y * imageLayout.RowPitch, w * imageLayout.BytesPerPixel);
            readback.Unmap();
            return pixels;
        };
        auto sum = [](const auto &pixels, uint32_t minX, uint32_t maxX, uint32_t channel) {
            uint64_t value = 0;
            if (pixels.size() != resolution * resolution * 4)
                return value;
            for (uint32_t y = 0; y < resolution; ++y)
                for (uint32_t x = minX; x < maxX; ++x)
                    value += pixels[(y * resolution + x) * 4 + channel];
            return value;
        };
        auto capture = [&](const std::string &name, const std::vector<uint8_t> &pixels) {
            if (pixels.size() != resolution * resolution * 4)
                return false;
            std::ofstream file(captures / (name + ".tga"), std::ios::binary);
            uint8_t header[18]{};
            header[2] = 2;
            header[12] = resolution;
            header[14] = resolution;
            header[16] = 32;
            header[17] = 0x28;
            file.write(reinterpret_cast<char *>(header), 18);
            for (size_t i = 0; i < pixels.size(); i += 4) {
                uint8_t bgra[] = {pixels[i + 2], pixels[i + 1], pixels[i], pixels[i + 3]};
                file.write(reinterpret_cast<char *>(bgra), 4);
            }
            return file.good();
        };
        auto first = render();
        if (!check(sum(first, 0, 64, 0) > sum(first, 0, 64, 1) * 2 + 1000 &&
                       sum(first, 64, 128, 1) > sum(first, 64, 128, 0) * 2 + 1000,
                   "Distinct textures survive two GPU draws"))
            return false;
        if (!capture("descriptor-isolation", first))
            return false;
        std::reverse(effect.EmitterStacks.begin(), effect.EmitterStacks.end());
        if (!check(render() == first,
                   "Reversed disjoint submission preserves descriptors and instance buffers"))
            return false;
        std::reverse(effect.EmitterStacks.begin(), effect.EmitterStacks.end());
        {
            auto& growingPool = effect.EmitterStacks[0].System->GetPool();
            ParticleSpawnData particle;
            particle.Position = {-0.8f, 0, 0};
            particle.StartSize = particle.EndSize = 0.9f;
            particle.StartColor = glm::vec4(1);
            while (growingPool.GetActiveCount() < 300) {
                if (growingPool.Spawn(particle) == ParticlePool::INVALID_INDEX) return false;
            }
            std::vector<uint8_t> skipped;
            bool injected = false;
            {
                ScopedVertexAllocationFailure failure(device.GetNRICore());
                skipped = render();
                injected = failure.WasInjected();
            }
            if (!check(injected && sum(skipped, 0, 64, 0) == 0 && sum(skipped, 64, 128, 1) > 1000,
                       "Failed particle growth skips only that draw and preserves other emitters"))
                return false;
            auto recovered = render();
            if (!check(sum(recovered, 0, 64, 0) > 1000 && sum(recovered, 64, 128, 1) > 1000,
                       "Particle vertex buffer recovers on the next frame after allocation failure"))
                return false;
            growingPool.Reset();
            growingPool.Spawn(particle);
        }
        auto &left = effect.EmitterStacks[0];
        left.UseTexture = false;
        auto disabled = render();
        if (!check(sum(disabled, 0, 64, 1) > 1000,
                   "Disabled texture uses untextured fallback without losing assignment"))
            return false;
        left.UseTexture = true;
        left.TexturePath = green;
        auto replaced = render();
        if (!check(sum(replaced, 0, 64, 1) > sum(replaced, 0, 64, 0) * 2 + 1000,
                   "Texture replacement reaches shader"))
            return false;
        left.TexturePath = "missing-vfx-fixture.png";
        auto missing = render();
        if (!check(sum(missing, 0, 64, 1) > 1000, "Missing texture has a visible fallback"))
            return false;
        left.TexturePath = red;
        Material material("Particle cutout");
        material.SetSurfaceType(SurfaceType::Cutout);
        material.SetAlphaCutoff(0.5f);
        auto materialPath = (captures / "cutout.axleasset").string();
        if (!material.Save(materialPath))
            return false;
        left.MaterialPath = materialPath;
        left.TexturePath = translucent;
        auto cutout = render();
        if (!check(sum(cutout, 0, 64, 0) == 0, "Assigned material alpha cutoff actually discards fragments"))
            return false;
        auto loadedMaterial = MaterialCache::GetMaterial(materialPath);
        if (!loadedMaterial)
            return false;
        loadedMaterial->SetAlphaCutoff(0.1f);
        auto visible = render();
        if (!check(sum(visible, 0, 64, 0) > 1000, "Changed material cutoff reaches the real particle shader"))
            return false;
        loadedMaterial->SetEmissionColor({0, 0, 1});
        loadedMaterial->SetEmissionIntensity(0.5f);
        auto emissive = render();
        if (!check(sum(emissive, 0, 64, 2) > sum(visible, 0, 64, 2) + 1000,
                   "Material emission reaches GPU particles"))
            return false;
        loadedMaterial->SetEmissionIntensity(0);
        loadedMaterial->SetAlbedoColor({0.2f, 1, 1, 1});
        if (!check(sum(render(), 0, 64, 0) + 1000 < sum(visible, 0, 64, 0),
                   "Material base tint reaches GPU particles"))
            return false;
        loadedMaterial->SetAlbedoColor(glm::vec4(1));
        auto split = writeTexture("split.tga", {255, 0, 0, 255});
        {
            std::fstream image(split, std::ios::in | std::ios::out | std::ios::binary);
            image.seekp(18);
            for (int y = 0; y < 2; ++y) {
                uint8_t row[] = {0, 0, 255, 255, 255, 0, 0, 255};
                image.write(reinterpret_cast<char *>(row), 8);
            }
        }
        left.TexturePath = split;
        loadedMaterial->SetUVScale({0.25f, 1});
        loadedMaterial->SetUVOffset({0, 0});
        auto uvLeft = render();
        loadedMaterial->SetUVOffset({0.75f, 0});
        auto uvRight = render();
        if (!check(sum(uvLeft, 0, 64, 0) > sum(uvLeft, 0, 64, 2) * 2 + 1000 &&
                       sum(uvRight, 0, 64, 2) > sum(uvRight, 0, 64, 0) * 2 + 1000,
                   "Material UV transform changes sampled texels"))
            return false;
        left.MaterialPath.clear();
        left.TexturePath = red;
        settings.ClearColor = glm::vec4(0.4f, 0.4f, 0.4f, 1);
        auto alphaOnGray = render();
        left.Config.BlendMode = ParticleBlendMode::Multiply;
        auto multiplied = render();
        if (!check(sum(multiplied, 0, 64, 0) + 1000 < sum(alphaOnGray, 0, 64, 0) &&
                       sum(multiplied, 0, 64, 0) > sum(multiplied, 0, 64, 1) + 1000,
                   "Multiply blending modulates the destination"))
            return false;
        left.Config.BlendMode = ParticleBlendMode::AlphaBlend;
        settings.ClearColor = {0, 0, 0, 1};
        left.Config.NearFadeEnabled = true;
        left.Config.NearFadeStart = 10;
        left.Config.NearFadeEnd = 20;
        if (!check(sum(render(), 0, 64, 0) == 0, "Near camera fading reaches shader"))
            return false;
        left.Config.NearFadeEnabled = false;
        left.Config.FarFadeEnabled = true;
        left.Config.FarFadeStart = 1;
        left.Config.FarFadeEnd = 2;
        if (!check(sum(render(), 0, 64, 0) == 0, "Far camera fading reaches shader"))
            return false;
        left.Config.FarFadeEnabled = false;
        // Normal maps and lighting are changed on already-spawned particles, including paused previews.
        left.Config.LightingEnabled = true;
        auto lit = render();
        left.NormalMapPath = writeTexture("normal.tga", {0, 128, 128, 255});
        left.UseNormalMap = true;
        auto normalMapped = render();
        if (!check(normalMapped != lit, "Normal texture and live lighting flags change GPU pixels"))
            return false;
        left.NormalMapPath.clear();
        left.UseNormalMap = false;
        left.Config.LightingEnabled = false;
        auto occluder = scene->CreateEntity("Opaque depth fixture");
        occluder.AddComponent<MeshInstance3D>().RHIMeshAsset = RHIMeshRegistry::Get().GetCube();
        auto &transform = occluder.GetComponent<TransformComponent>();
        transform.Translation = {-0.8f, 0, -0.4f};
        transform.Scale = {0.45f, 0.45f, 0.05f};
        auto &opaqueMaterial = occluder.AddComponent<MaterialComponent>();
        opaqueMaterial.AlbedoColor = {0, 0, 0, 1};
        opaqueMaterial.EnableSpecularHighlights = false;
        opaqueMaterial.EnableEnvironmentReflections = false;
        left.Config.SoftnessDistance = 0;
        auto hardIntersection = render();
        left.Config.SoftnessDistance = 1;
        auto softIntersection = render();
        if (!check(sum(softIntersection, 0, 64, 0) + 1000 < sum(hardIntersection, 0, 64, 0),
                   "Soft intersections sample actual opaque geometry depth"))
            return false;
        if (!capture("soft-depth", softIntersection))
            return false;
        scene->DestroyEntity(occluder);
        left.Config.SoftnessDistance = 0;
        // Explicit MSAA and HDR fallback paths must remain drawable.
        settings.MSAASamples = 4;
        if (!check(sum(render(), 0, 64, 0) > 1000, "MSAA particle pipeline and resolve"))
            return false;
        settings.MSAASamples = 1;
        settings.EnableToneMapping = true;
        if (!check(sum(render(), 0, 64, 0) > 1000, "HDR particle target and tone mapping"))
            return false;
        settings.EnableToneMapping = false;
        RHITexture oddColor, oddDepth, hdrColor;
        if (!oddColor.Init(RHITextureDesc::RenderTarget2D(65, 33)) ||
            !oddDepth.Init(RHITextureDesc::DepthStencil2D(65, 33)))
            return false;
        renderTarget = &oddColor;
        renderDepth = &oddDepth;
        auto odd = render();
        if (!check(odd.size() == 65 * 33 * 4 && std::any_of(odd.begin(), odd.end(),
                                                            [index = size_t{0}](uint8_t byte) mutable {
                                                                return index++ % 4 != 3 && byte != 0;
                                                            }),
                   "Odd-sized resize uses aligned readback rows"))
            return false;
        if (!hdrColor.Init(
                RHITextureDesc::RenderTarget2D(resolution, resolution, RHITextureFormat::RGBA16_FLOAT)))
            return false;
        renderTarget = &hdrColor;
        renderDepth = &depth;
        auto hdr = render();
        bool finite = hdr.size() == resolution * resolution * 8;
        uint64_t hdrEnergy = 0;
        for (size_t i = 0; i + 1 < hdr.size(); i += 2) {
            uint16_t half = static_cast<uint16_t>(hdr[i] | (hdr[i + 1] << 8));
            if ((half & 0x7c00) == 0x7c00)
                finite = false;
            if (i % 8 < 6)
                hdrEnergy += half;
        }
        if (!check(finite && hdrEnergy > 1000, "RGBA16F readback contains finite rendered HDR pixels"))
            return false;
        renderTarget = &color;
        renderDepth = &depth;
        // Each authored reference is loaded, simulated, saved and reopened through production adapters.
        for (const char *name : {"Campfire_v2", "CollisionSparks", "VehicleBackfire"}) {
            auto source = root / "Tests" / "Fixtures" / "LegacyAssets" / "VFX" / (std::string(name) + ".axle_vfx");
            if (!effect.LoadFromAsset(source.string()))
                return false;
            for (int tick = 0; tick < 30; ++tick)
                effect.Update(1.0f / 60.0f);
            auto before = render();
            if (!check(sum(before, 0, 128, 0) + sum(before, 0, 128, 1) + sum(before, 0, 128, 2) > 100,
                       "Authored reference renders visible particles"))
                return false;
            auto saved = (captures / (std::string(name) + ".axle_vfx")).string();
            if (!effect.SaveToAsset(saved) || !effect.LoadFromAsset(saved))
                return false;
            for (int tick = 0; tick < 30; ++tick)
                effect.Update(1.0f / 60.0f);
            if (!check(render() == before,
                       "Reference save/reopen reproduces GPU output at the same seeded tick"))
                return false;
            if (!capture(name, before))
                return false;
        }
        // New emitters grow every frame; existing tests mostly reused fixed particle counts.
        auto growing = CreateDefaultParticleEffect("New VFX growth regression");
        growing.Emitters[0].Config.EmissionRate = 50;
        growing.Emitters[0].Config.Lifetime = FloatRange(1, 2);
        growing.Emitters[0].Config.InitialSpeed = FloatRange(0, 0);
        growing.Emitters[0].Config.Shape.Radius = 0;
        growing.Emitters[0].Config.StartSize = FloatRange(0.2f, 0.2f);
        effect.SetEffectDefinition(growing);
        for (int tick = 0; tick < 180; ++tick) {
            effect.Update(1.0f / 60.0f);
            auto pixels = render();
            if (pixels.size() != resolution * resolution * 4)
                return false;
        }
        if (!check(renderer.GetDrawCallCount() > 0, "New VFX survives continuous particle buffer growth"))
            return false;

        timings.clear();
        for (int frame = 0; frame < 10; ++frame)
            render();
        std::ofstream report(captures / "report.txt");
        report << FormatVFXTimingReport(device.GetCapabilities().AdapterName,
                                        RHITypeToString(device.GetBackend()), resolution, resolution, 1,
                                        timings);
        report << "CPU wall time includes submission and queue synchronization; no GPU timestamp claim.\n";
        report << "PASS: texture binding/order/replacement/disable/fallback, material "
                  "tint/cutout/emission/UVs, multiply/alpha/additive blending, normal maps, camera fades, "
                  "opaque depth, MSAA, SDR/HDR readback, odd resize, reference save-reopen, "
                  "continuous particle growth and partial-allocation failure recovery.\n";
        return check(report.good(), "Capture metadata written");
    }

} // namespace Axle
