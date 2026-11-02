#include "Test_MaterialAcceptance.hpp"
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/MaterialTextureCache.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/CustomShaderInterface.hpp>
#include <Axle/ShaderPropertyPersistence.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <glm/gtc/packing.hpp>
#include <cstring>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <unordered_set>

namespace Axle {

    namespace {
        std::string ToLower(std::string str) {
            std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return str;
        }

        std::string Trim(const std::string& str) {
            size_t start = str.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return "";
            size_t end = str.find_last_not_of(" \t\r\n");
            return str.substr(start, end - start + 1);
        }
    }

    bool ParseMaterialBackendOption(const char* envVal, RHI::RHIType& outBackend, std::string& outError) {
        outError.clear();
        if (!envVal || *envVal == '\0') {
            outBackend = RHI::RHIType::Direct3D12; // Unset defaults to D3D12 for compatibility
            return true;
        }

        std::string val = ToLower(Trim(envVal));
        if (val == "dx12" || val == "d3d12" || val == "direct3d12") {
            outBackend = RHI::RHIType::Direct3D12;
            return true;
        }
        if (val == "vulkan" || val == "vk") {
            outBackend = RHI::RHIType::Vulkan;
            return true;
        }

        outError = "Unknown explicit backend requested: '" + std::string(envVal) + "'. Expected 'dx12' or 'vulkan'.";
        return false;
    }

    bool ValidateMaterialCaptureDirectory(const std::string& dirPath, std::filesystem::path& outDir, std::string& outError) {
        outError.clear();
        if (dirPath.empty()) {
            outError = "Capture directory path is empty.";
            return false;
        }

        std::error_code ec;
        auto path=std::filesystem::weakly_canonical(std::filesystem::absolute(dirPath),ec);
        if (ec) { outError="Invalid capture directory: "+ec.message(); return false; }
        const auto repo=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
        for (const auto& protectedPath : {repo/"Engine",repo/"Editor/assets",std::filesystem::path(FileSystem::GetAssetDirectory())}) {
            auto root=std::filesystem::weakly_canonical(protectedPath,ec);
            if (ec) continue;
            auto candidate=ToLower(path.generic_string())+"/";
            auto prefix=ToLower(root.generic_string())+"/";
            if (candidate.starts_with(prefix)) {
                outError="Capture directory must not target project assets or engine sources: "+dirPath;
                return false;
            }
        }
        std::filesystem::create_directories(path, ec);
        if (ec) {
            outError = "Could not create capture directory: " + ec.message();
            return false;
        }

        outDir = std::filesystem::absolute(path);
        return true;
    }

    MaterialReadbackLayout GetMaterialReadbackLayout(uint32_t width, uint32_t height, bool hdr) {
        const uint32_t bytes=hdr?8u:4u;
        const uint32_t pitch=(width*bytes+255u)&~255u;
        return {bytes,pitch,uint64_t(pitch)*height};
    }

    std::string FormatMaterialTimingReport(const std::string& deviceName, const std::string& backend,
                                          uint32_t width, uint32_t height, uint8_t sampleCount,
                                          const std::vector<double>& frameTimesMs) {
        if (frameTimesMs.empty()) return "No timing data recorded.";

        double minMs = *std::min_element(frameTimesMs.begin(), frameTimesMs.end());
        double maxMs = *std::max_element(frameTimesMs.begin(), frameTimesMs.end());
        double sum = std::accumulate(frameTimesMs.begin(), frameTimesMs.end(), 0.0);
        double avgMs = sum / static_cast<double>(frameTimesMs.size());

        double variance = 0.0;
        for (double t : frameTimesMs) {
            variance += (t - avgMs) * (t - avgMs);
        }
        variance /= static_cast<double>(frameTimesMs.size());
        double stddevMs = std::sqrt(variance);

        std::ostringstream oss;
        oss << "\n========================================================\n"
            << "       AxleEngine Material Reference Timing Report       \n"
            << "========================================================\n"
            << " Device:       " << deviceName << "\n"
            << " Backend:      " << backend << "\n"
            << " Resolution:   " << width << " x " << height << "\n"
            << " Sample Count: " << static_cast<int>(sampleCount) << "x\n"
            << " Frames:       " << frameTimesMs.size() << " (warmed-up)\n"
            << "--------------------------------------------------------\n"
            << std::fixed << std::setprecision(3)
            << " Average:      " << avgMs << " ms  (" << (1000.0 / avgMs) << " FPS)\n"
            << " Min Frame:    " << minMs << " ms\n"
            << " Max Frame:    " << maxMs << " ms\n"
            << " Std Deviation:" << stddevMs << " ms\n"
            << "--------------------------------------------------------\n"
            << " [NOTE] Measurements include CPU recording, submission, and\n"
            << " queue idle time. They represent end-to-end frame timing,\n"
            << " not isolated GPU shader invocation costs.\n"
            << "========================================================\n";
        return oss.str();
    }

    bool VerifyMaterialReferenceSceneStructure(const Scene& scene, std::string& outError) {
        outError.clear();

        static const std::vector<std::string> requiredEntities = {
            "Reference_CarPaint",
            "Reference_BrushedMetal",
            "Reference_CarbonFiber",
            "Reference_Rubber",
            "Reference_Leather",
            "Reference_Glass",
            "Reference_Backdrop",
            "Reference_Camera",
            "Reference_KeyLight"
        };

        std::unordered_set<std::string> foundTags;
        for (auto entityID : scene.GetRegistry().view<TagComponent>()) {
            const auto& tag = scene.GetRegistry().get<TagComponent>(entityID);
            foundTags.insert(tag.Tag);
        }

        for (const auto& req : requiredEntities) {
            if (foundTags.find(req) == foundTags.end()) {
                outError = "Material reference scene missing required entity: " + req;
                return false;
            }
        }

        return true;
    }

    bool WritePPMImage(const std::filesystem::path& filePath, uint32_t width, uint32_t height, const uint8_t* rgbaPixels) {
        if (!rgbaPixels || width == 0 || height == 0) return false;

        std::ofstream file(filePath, std::ios::binary);
        if (!file) return false;

        file << "P6\n" << width << " " << height << "\n255\n";
        for (uint32_t i = 0; i < width * height; ++i) {
            uint8_t rgb[3] = { rgbaPixels[i * 4 + 0], rgbaPixels[i * 4 + 1], rgbaPixels[i * 4 + 2] };
            file.write(reinterpret_cast<char*>(rgb), 3);
        }

        return file.good();
    }

    bool RunMaterialAcceptanceTests(const std::filesystem::path& projectRoot) {
        using namespace Axle::RHI;

        auto check = [](bool cond, const char* msg) {
            if (!cond) std::cerr << "  [ACCEPTANCE FAIL] " << msg << std::endl;
            return cond;
        };

        std::cout << "\n========================================================\n"
                  << "  AxleEngine Automotive Material Acceptance Test Suite  \n"
                  << "========================================================\n";

        // 1. Backend environment variable configuration
        const char* backendEnv = std::getenv("AXLE_MATERIAL_TEST_BACKEND");
        RHIType targetBackend = RHIType::Direct3D12;
        std::string backendError;
        if (!ParseMaterialBackendOption(backendEnv, targetBackend, backendError)) {
            std::cerr << "  [ERROR] " << backendError << std::endl;
            return false;
        }

        std::string backendName = (targetBackend == RHIType::Vulkan) ? "Vulkan" : "Direct3D12";
        std::cout << "  - Requested Backend: " << backendName << " ("
                  << (backendEnv ? backendEnv : "default") << ")\n";

        // 2. Capture directory environment variable configuration
        const char* captureEnv = std::getenv("AXLE_MATERIAL_CAPTURE_DIR");
        std::filesystem::path captureDir;
        bool doCapture = false;
        if (captureEnv && *captureEnv != '\0') {
            std::string capError;
            if (!ValidateMaterialCaptureDirectory(captureEnv, captureDir, capError)) {
                std::cerr << "  [ERROR] " << capError << std::endl;
                return false;
            }
            doCapture = true;
            std::cout << "  - Capture Output Directory: " << captureDir.string() << "\n";
        }

        // 3. Initialize Headless RHI Device
        auto& device = RHIDevice::Get();
        if (!device.IsInitialized()) {
            RHIDeviceDesc desc;
            desc.PreferredBackend = targetBackend;
            desc.AllowFallback = false;
            desc.Headless = true;
            if (!check(device.Init(desc), "Headless device initialization for requested backend")) return false;
        }

        // Verify that backend fallback did NOT silently occur
        if (backendEnv && *backendEnv != '\0') {
            if (!check(device.GetBackend() == targetBackend, "Requested backend must match active device backend (no fallback allowed)")) {
                return false;
            }
        }

        struct RegistryCleanup {
            ~RegistryCleanup() { RHIMeshRegistry::Get().Shutdown(); RHIMaterial::ReleaseDefaultTextures(); }
        } registryCleanup;
        // 4. Load MaterialReference scene
        std::filesystem::path scenePath = projectRoot / "Editor/assets/scenes/MaterialReference.axlescene";
        auto scene = std::make_shared<Scene>();
        SceneSerializer serializer(scene.get());
        if (!check(serializer.Deserialize(scenePath.string()), "Deserialize MaterialReference.axlescene")) return false;

        std::string structError;
        if (!check(VerifyMaterialReferenceSceneStructure(*scene, structError), structError.c_str())) return false;
        std::cout << "  - MaterialReference scene loaded & verified (9 reference entities present).\n";

        // 5. Save to temp path and reopen to verify round-trip
        auto tempScenePath = std::filesystem::temp_directory_path() /
            ("axle_reference_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".axlescene");
        struct TempCleanup {
            std::filesystem::path Path;
            ~TempCleanup() { std::error_code ec; std::filesystem::remove(Path,ec); }
        } tempCleanup{tempScenePath};
        if (!check(serializer.Serialize(tempScenePath.string()), "Serialize scene to temp path")) return false;

        auto reloadedScene = std::make_shared<Scene>();
        SceneSerializer reloadSerializer(reloadedScene.get());
        if (!check(reloadSerializer.Deserialize(tempScenePath.string()), "Deserialize from temp path")) return false;
        if (!check(VerifyMaterialReferenceSceneStructure(*reloadedScene, structError), "Reopened scene structure verification")) return false;
        std::error_code ec;
        std::filesystem::remove(tempScenePath, ec);
        std::cout << "  - Round-trip save and reload verified.\n";

        // 6. Setup offscreen renderer and targets
        RHISceneRenderer renderer;
        if (!check(renderer.Init(device), "RHISceneRenderer initialization")) return false;

        constexpr uint32_t width = 1024;
        constexpr uint32_t height = 640;

        RHITexture colorSDR, colorHDR, depthTarget;
        if (!check(colorSDR.Init(RHITextureDesc::RenderTarget2D(width, height, RHITextureFormat::RGBA8_UNORM)), "Init SDR Target")) return false;
        if (!check(colorHDR.Init(RHITextureDesc::RenderTarget2D(width, height, RHITextureFormat::RGBA16_FLOAT)), "Init HDR Target")) return false;
        if (!check(depthTarget.Init(RHITextureDesc::DepthStencil2D(width, height, RHITextureFormat::D32_FLOAT)), "Init Depth Target")) return false;

        RHIBuffer readbackBuffer;
        RHIBufferDesc readDesc;
        readDesc.Size = GetMaterialReadbackLayout(width,height,true).SlicePitch;
        readDesc.MemoryLocation = RHIMemoryLocation::HostReadback;
        if (!check(readbackBuffer.Init(readDesc), "Init Readback Buffer")) return false;

        RHICommandBuffer copyCmd;
        if (!check(copyCmd.Init(), "Init Copy Command Buffer")) return false;

        PerspectiveCamera camera(45.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.0f);
        camera.SetPosition(glm::vec3(0.0f, 0.0f, 10.0f));

        SceneRenderSettings settings;
        settings.EnableSkybox = false;
        settings.EnableParticles = false;
        settings.EnableShadows = false;
        settings.ClearColor = glm::vec4(0.05f, 0.05f, 0.07f, 1.0f);

        auto readbackPixels = [&](RHITexture& texture) -> std::vector<uint8_t> {
            const bool hdr=texture.GetFormat()==RHITextureFormat::RGBA16_FLOAT;
            const auto footprint=GetMaterialReadbackLayout(width,height,hdr);
            std::vector<uint8_t> pixels(width*height*4, 0);
            if (!copyCmd.Reset() || !copyCmd.Begin()) return {};

            copyCmd.TransitionTexture(texture.GetNative(), RHIResourceLayout::ShaderResource, RHIResourceLayout::CopySource);
            nri::TextureDataLayoutDesc layout{};
            layout.rowPitch = footprint.RowPitch;
            layout.slicePitch = footprint.SlicePitch;
            nri::TextureRegionDesc region{};
            region.width = width;
            region.height = height;
            region.depth = 1;

            device.GetNRICore().CmdReadbackTextureToBuffer(*copyCmd.GetNative(), *readbackBuffer.GetNative(), layout, *texture.GetNative(), region);
            copyCmd.TransitionTexture(texture.GetNative(), RHIResourceLayout::CopySource, RHIResourceLayout::ShaderResource);

            if (!copyCmd.End() || !copyCmd.Submit()) return {};
            device.WaitIdle();

            void* data = readbackBuffer.Map();
            if (!data) return {};
            for(uint32_t y=0;y<height;++y) {
                const auto* row=static_cast<const uint8_t*>(data)+y*footprint.RowPitch;
                if (!hdr) std::memcpy(pixels.data()+y*width*4,row,width*4);
                else for(uint32_t x=0;x<width*4;++x) {
                    uint16_t half;
                    std::memcpy(&half,row+x*2,2);
                    const float value=glm::unpackHalf1x16(half);
                    if (!std::isfinite(value)) { readbackBuffer.Unmap(); return {}; }
                    pixels[y*width*4+x]=static_cast<uint8_t>(std::lround(std::clamp(value,0.0f,1.0f)*255.0f));
                }
            }
            readbackBuffer.Unmap();
            return pixels;
        };

        // 7. Render SDR and capture if requested
        renderer.RenderScene(scene, camera, &colorSDR, &depthTarget, settings);
        auto sdrPixels = readbackPixels(colorSDR);
        if (!check(!sdrPixels.empty(), "SDR Render & Readback")) return false;
        renderer.RenderScene(reloadedScene,camera,&colorSDR,&depthTarget,settings);
        if (!check(readbackPixels(colorSDR)==sdrPixels,"Saved/reopened reference scene renders identical pixels")) return false;
        for(auto id:reloadedScene->GetRegistry().view<MaterialComponent>())
            reloadedScene->GetRegistry().get<MaterialComponent>(id).MakeUnique();
        renderer.RenderScene(reloadedScene,camera,&colorSDR,&depthTarget,settings);
        if (!check(readbackPixels(colorSDR)==sdrPixels,"Detached inline materials match shared asset rendering")) return false;

        if (doCapture) {
            auto sdrCapturePath = captureDir / ("capture_reference_sdr_" + ToLower(backendName) + "_" + std::to_string(width) + "x" + std::to_string(height) + ".ppm");
            if (!check(WritePPMImage(sdrCapturePath, width, height, sdrPixels.data()), "Write SDR PPM capture")) return false;
            std::cout << "  - Saved SDR PPM capture to: " << sdrCapturePath.string() << "\n";
        }

        // 8. Render HDR and capture if requested
        settings.EnableToneMapping=true;
        renderer.RenderScene(scene, camera, &colorHDR, &depthTarget, settings);
        auto hdrPixels = readbackPixels(colorHDR);
        if (!check(!hdrPixels.empty(), "HDR Render & Readback")) return false;
        settings.EnableToneMapping=false;

        if (doCapture) {
            auto hdrCapturePath = captureDir / ("capture_reference_hdr_" + ToLower(backendName) + "_" + std::to_string(width) + "x" + std::to_string(height) + ".ppm");
            if (!check(WritePPMImage(hdrCapturePath, width, height, hdrPixels.data()), "Write HDR PPM capture")) return false;
            std::cout << "  - Saved HDR PPM capture to: " << hdrCapturePath.string() << "\n";
        }

        // 9. Same scene/camera for each optional-path configuration. These short
        // end-to-end measurements are diagnostic, not isolated shader benchmarks.
        auto timings=[&](const std::shared_ptr<Scene>& target,const char* label) {
            for(int i=0;i<3;++i) {renderer.RenderScene(target,camera,&colorSDR,&depthTarget,settings);device.WaitIdle();}
            std::vector<double> times;
            for(int i=0;i<24;++i) {
                auto start=std::chrono::steady_clock::now();
                renderer.RenderScene(target,camera,&colorSDR,&depthTarget,settings);device.WaitIdle();
                times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
            }
            return std::string("\nConfiguration: ")+label+FormatMaterialTimingReport(device.GetCapabilities().AdapterName,backendName,width,height,1,times);
        };
        std::string report=timings(scene,"detail normal + refraction");
        for(auto id:reloadedScene->GetRegistry().view<MaterialComponent>())
            reloadedScene->GetRegistry().get<MaterialComponent>(id).RefractionEnabled=false;
        report+=timings(reloadedScene,"detail normal, refraction disabled");
        for(auto id:reloadedScene->GetRegistry().view<MaterialComponent>())
            reloadedScene->GetRegistry().get<MaterialComponent>(id).UseDetailNormalMap=false;
        report+=timings(reloadedScene,"baseline, detail normal and refraction disabled");
        std::cout << report << "\n";

        if (doCapture) {
            auto timingPath = captureDir / ("timings_" + ToLower(backendName) + ".txt");
            std::ofstream tf(timingPath);
            tf << report;
            tf.flush();
            if (!check(tf.good(),"Write material timing report")) return false;
        }

        std::cout << "  [PASS] Automotive material rendering acceptance checks completed successfully.\n";
        return true;
    }

} // namespace Axle
