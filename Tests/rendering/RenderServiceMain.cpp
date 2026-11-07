#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"
#include <Axle/RenderService.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
using namespace Axle;
using namespace Axle::RHI;
using Axle::RenderTests::ReadTexture;

static bool Service(RHIType backend) {
    auto& device = RHIDevice::Get();
    RenderService service;
    RHIDeviceDesc init; init.PreferredBackend = backend; init.AllowFallback = false; init.Headless = true;
    init.EnableNRIValidation = init.EnableValidation = true;
    RENDER_CHECK(RHIDevice::IsGraphicsAPIValidationAvailable(backend), "actual API debug prerequisites required");
    for (int epoch = 0; epoch < 2; ++epoch) {
        RENDER_CHECK(device.Init(init) && device.GetBackend() == backend && !device.IsSoftwareAdapter(), "physical backend without fallback");
        {
            RENDER_CHECK(service.Init(), "service initializes per device epoch");
            Scene scene;
            auto entity = scene.CreateEntity("Arbitrary name");
            entity.AddComponent<MeshInstance3D>().AssetPath = "builtin://cube";
            auto& material = entity.AddComponent<MaterialComponent>();
            material.AlbedoColor = {1,0,0,1}; material.EmissionColor = {1,0,0}; material.EmissionIntensity = 1;
            ViewRequest request; request.Width = request.Height = 48;
            request.Settings.EnableSkybox = request.Settings.EnableShadows = false;
            request.Settings.ClearColor = {0,0,0,1};
            request.Camera = PerspectiveCamera(45,1,0.1f,100);
            request.Camera.SetViewMatrix(glm::lookAt(glm::vec3(0,0,3),glm::vec3(0),glm::vec3(0,1,0)),{0,0,3});
            auto world = ExtractRenderWorld(static_cast<const Scene&>(scene));
            RENDER_CHECK(!material.RHIMaterialAsset && !entity.GetComponent<MeshInstance3D>().RHIMeshAsset,
                "snapshot cannot publish GPU/material caches into authoring data");
            auto result = service.RenderView(world,request);
            RENDER_CHECK(result.Success && result.CompletionToken && result.CompletionToken.Epoch == device.GetEpoch() &&
                result.CompletionToken == device.GetLastSubmittedToken(), "service returns actual successful submission token");
            auto* target = service.GetDefaultMainColorTarget();
            RENDER_CHECK(target, "default main target");
            const auto red = ReadTexture(*target); const auto center = (24*48+24)*4;
            RENDER_CHECK(red.size() == 48*48*4 && red[center] > red[center+1]+30 && red[center] > red[center+2]+30,
                "actual service geometry has intended red material pixels");
            material.AlbedoColor = {0,1,0,1}; material.EmissionColor = {0,1,0};
            auto greenResult = service.RenderView(scene,request);
            RENDER_CHECK(greenResult.Success, "scene adapter uses shared snapshot path");
            const auto green = ReadTexture(*target);
            RENDER_CHECK(green[center+1] > green[center]+30, "later scene material edits change actual output");
            result = service.RenderView(world,request);
            RENDER_CHECK(result.Success && ReadTexture(*target) == red, "saved snapshot material remains immutable after scene edits");
            const auto old = target->GetHandle();
            auto resized = request; resized.Width = resized.Height = 49;
            bool reached = false;
            {
                ScopedRHIFault fault([&](std::string_view operation)->std::optional<nri::Result>{
                    if (operation == "CreateTexture") { reached = true; return nri::Result::OUT_OF_MEMORY; } return {};
                });
                result = service.RenderView(world,resized);
            }
            RENDER_CHECK(reached && !result.Success && !result.CompletionToken && service.GetDefaultMainColorTarget()->GetHandle() == old,
                "failed target replacement preserves last good generation and reports failure");
            auto zero = request; zero.Width = 0;
            const auto prior = device.GetLastSubmittedToken();
            RENDER_CHECK(!service.RenderView(world,zero).Success && device.GetLastSubmittedToken() == prior, "zero extent never submits or allocates");
            auto privateRequest = request; privateRequest.Kind = RenderViewKind::Thumbnail;
            RENDER_CHECK(!service.RenderView(world,privateRequest).Success, "private views cannot borrow shared main targets");
            {
                ScopedRHIFault fault([](std::string_view operation)->std::optional<nri::Result>{
                    if (operation == "QueueSubmit") return nri::Result::FAILURE; return {};
                });
                result = service.RenderView(world,request);
            }
            RENDER_CHECK(!result.Success && !result.CompletionToken, "submit failure cannot fabricate view success or completion");
            RENDER_CHECK(device.GetState() == RHIDeviceState::Stopping && !service.RenderView(world,request).Success, "ambiguous submit failure stops work until a checked restart");
            RENDER_CHECK(device.WaitIdle(), "completed service work");
        }
        RHIMeshRegistry::Get().Shutdown();
        RENDER_CHECK(device.Shutdown() && !service.IsInitialized(), "device close hook retires service before device; same object can reopen next epoch");
    }
    return true;
}
int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting(); Log::Init(); JobSystem::Init(2);
    const auto backend = argc > 1 && std::string(argv[1]) == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12;
    bool passed = false;
    try { passed = Service(backend); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    RenderService::Get().Shutdown(); RHIMeshRegistry::Get().Shutdown(); RHIDevice::Get().Shutdown(); JobSystem::Shutdown();
    passed = passed && GetRHIValidationErrorCount() == 0;
    std::cout << "OPT05 RenderService " << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}
