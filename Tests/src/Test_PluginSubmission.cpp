#include "PluginSession.hpp"
#include "PluginSDKConfig.hpp"
#include <Axle/SceneSerializer.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <windows.h>
#define CHECK(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << ": " << #value << '\n'; return 1; } } while (false)
int Run(int argc, wchar_t** argv) {
    CHECK(argc == 3);
    using namespace Axle; using namespace Axle::RHI;
    Log::Init(); JobSystem::Init(2); PhysicsEngine::Init(); ImGui::CreateContext();
    struct Cleanup { ~Cleanup() { ImGui::DestroyContext(); PhysicsEngine::Shutdown(); JobSystem::Shutdown(); } } cleanup;
    RHIDeviceDesc description; description.PreferredBackend = std::wstring(argv[2]) == L"vulkan" ? RHIType::Vulkan : RHIType::Direct3D12;
    description.AllowFallback = false; description.Headless = true; description.EnableNRIValidation = true;
    auto& device = RHIDevice::Get(); CHECK(device.Init(description)); CHECK(!device.GetCapabilities().IsSoftwareAdapter);
    struct DeviceCleanup { ~DeviceCleanup() { RHIDevice::Get().Shutdown(); } } deviceCleanup;
    auto project = Project::Load(argv[1]); CHECK(project);
    PluginSessionEnvironment environment{{}, AXLE_PLUGIN_SDK_DIRECTORY, {}, AXLE_PLUGIN_BUILD_CONFIGURATION,
        AXLE_PLUGIN_SDK_FINGERPRINT, AXLE_PLUGIN_TOOLCHAIN_FINGERPRINT};
    PluginProjectSession session(environment); std::string error; CHECK(session.Bind(error)); CHECK(session.Open(project, argv[1]));
    const auto modulePath = session.LastBuild().GenerationDirectory / "StudioMover_Runtime.dll";
    auto scene = std::make_shared<Scene>(); SceneSerializer serializer(scene.get());
    CHECK(serializer.Deserialize(project->GetProjectDirectoryPath() / "assets/scenes/Mover.axlescene"));
    scene->OnRuntimeStart(); scene->ExecuteFrameScriptPhase(0.5f);
    auto world = ExtractRenderWorld(*scene); CHECK(!world.DebugLines.empty());
    std::weak_ptr<void> code = world.DebugLines.front().OwnerLease; CHECK(!code.expired());
    RHISceneRenderer renderer; CHECK(renderer.Init(device, true));
    constexpr std::uint32_t width = 128, height = 128, rowPitch = width * 4;
    RHITexture color, depth; CHECK(color.Init(RHITextureDesc::RenderTarget2D(width,height)) && depth.Init(RHITextureDesc::DepthStencil2D(width,height)));
    RHIBuffer readback; RHIBufferDesc bytes; bytes.Size = rowPitch * height; bytes.MemoryLocation = RHIMemoryLocation::HostReadback;
    RHICommandBuffer copy; CHECK(readback.Init(bytes) && copy.Init());
    PerspectiveCamera camera(45,1,0.1f,100); const glm::vec3 position{1.75f,0.5f,4};
    camera.SetViewMatrix(glm::lookAt(position,glm::vec3(1.75f,0.5f,0),glm::vec3(0,1,0)),position);
    SceneRenderSettings settings; settings.EnableShadows = false; settings.EnableSkybox = false; settings.EnableToneMapping = false;
    settings.EnableFXAA = false; settings.EnableStudioLightLobes = false; settings.ClearColor = {0,0,0,1};
    renderer.RenderWorldView(world,camera,&color,&depth,settings); CHECK(renderer.WasLastRenderSuccessful());
    const auto completion = renderer.GetLastCompletion(); CHECK(completion);
    scene->OnRuntimeStop(); scene->Clear(); scene.reset(); session.Shutdown(); world.Clear();
    PluginRuntime::NativePlugin::CollectRetiredModules();
    CHECK(!code.expired() && GetModuleHandleW(modulePath.c_str()));
    CHECK(copy.Begin()); copy.TransitionTexture(color.GetNative(),RHIResourceLayout::ShaderResource,RHIResourceLayout::CopySource);
    nri::TextureDataLayoutDesc layout{}; layout.rowPitch = rowPitch; layout.slicePitch = rowPitch * height;
    nri::TextureRegionDesc region{}; region.width = width; region.height = height; region.depth = 1;
    copy.ReadbackTextureToBuffer(*readback.GetNative(),layout,*color.GetNative(),region);
    copy.TransitionTexture(color.GetNative(),RHIResourceLayout::CopySource,RHIResourceLayout::ShaderResource);
    CHECK(copy.End() && copy.Submit() && device.WaitIdle());
    const auto* pixels = static_cast<const unsigned char*>(readback.Map()); CHECK(pixels);
    unsigned green = 0;
    for (unsigned i = 0; i < width*height; ++i) if (pixels[i*4+1] > 140 && pixels[i*4] < 100 && pixels[i*4+2] < 140) ++green;
    readback.Unmap(); CHECK(green > 10);
    renderer.Shutdown(); CHECK(device.WaitIdle()); PluginRuntime::NativePlugin::CollectRetiredModules();
    CHECK(code.expired() && !GetModuleHandleW(modulePath.c_str()));
    std::cout << "Native plugin line pixels=" << green << "; scene/session released after submission, DLL retained through GPU completion and unloaded after command retirement: PASS\n";
    return 0;
}
int wmain(int argc, wchar_t** argv) { try { return Run(argc,argv); } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
