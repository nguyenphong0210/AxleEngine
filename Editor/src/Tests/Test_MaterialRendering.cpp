#include <Axle/RHISceneRenderer.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Scene.hpp>
#include <Axle/MaterialTextureCache.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <fstream>
#include <iostream>
#include <cstring>
#include <numeric>
#include <chrono>

bool VerifyMaterialRendering(const std::filesystem::path& root) {
    using namespace Axle;
    using namespace Axle::RHI;
    auto check = [](bool result, const char* message) {
        if (!result) std::cerr << "[106 GPU] FAIL: " << message << std::endl;
        return result;
    };
    auto writeTGA = [&](const char* name, std::array<uint8_t,4> rgba) {
        auto path = root / name;
        std::ofstream stream(path, std::ios::binary);
        uint8_t header[18]{};
        header[2] = 2; header[12] = 2; header[14] = 2; header[16] = 32; header[17] = 0x28;
        stream.write(reinterpret_cast<char*>(header), sizeof(header));
        uint8_t bgra[] = {rgba[2],rgba[1],rgba[0],rgba[3]};
        for (int i=0;i<4;++i) stream.write(reinterpret_cast<char*>(bgra),4);
        return path.string();
    };
    auto red = writeTGA("red.tga", {255,0,0,255});
    auto green = writeTGA("green.tga", {0,255,0,255});
    auto black = writeTGA("black.tga", {0,0,0,255});
    auto normal = writeTGA("normal.tga", {210,128,210,255});
    auto& device = RHIDevice::Get();
    if (!device.IsInitialized()) {
        RHIDeviceDesc desc;
        desc.PreferredBackend = RHIType::Direct3D12;
        desc.Headless = true;
        if (!check(device.Init(desc), "Headless DX12 device")) return false;
    }
    struct RegistryCleanup {
        ~RegistryCleanup() { RHIMeshRegistry::Get().Shutdown(); RHIMaterial::ReleaseDefaultTextures(); }
    } registryCleanup;
    MaterialTextureCache textureCache;
    auto colorTexture = textureCache.Get(red,true);
    auto dataTexture = textureCache.Get(red,false);
    if (!check(colorTexture && dataTexture && colorTexture->GetFormat()==RHITextureFormat::RGBA8_SRGB && dataTexture->GetFormat()==RHITextureFormat::RGBA8_UNORM && colorTexture!=dataTexture && textureCache.Get(red,true)==colorTexture, "Stable texture cache and separate color/data views")) return false;
    TextureDescriptor descriptor;
    descriptor.Source = red;
    descriptor.Type = "Color";
    auto descriptorPath = root / "role.AXLEASSET";
    if (!check(TextureDescriptor::Save(descriptorPath.string(),descriptor), "Texture descriptor fixture")) return false;
    auto normalDescriptor = textureCache.Get(descriptorPath.string(),false);
    if (!check(normalDescriptor && normalDescriptor->GetFormat()==RHITextureFormat::RGBA8_UNORM, "Normal and data roles remain linear even with a color descriptor")) return false;
    RHISceneRenderer renderer;
    if (!check(renderer.Init(device), "Material renderer initialization")) return false;
    constexpr uint32_t resolution = 128;
    RHITexture color, depth;
    auto colorDesc = RHITextureDesc::RenderTarget2D(resolution,resolution);
    if (!check(color.Init(colorDesc) && depth.Init(RHITextureDesc::DepthStencil2D(resolution,resolution)), "Offscreen material targets")) return false;
    RHIBuffer readback;
    RHIBufferDesc readDesc;
    readDesc.Size = resolution*resolution*4;
    readDesc.MemoryLocation = RHIMemoryLocation::HostReadback;
    if (!check(readback.Init(readDesc), "Readback buffer")) return false;
    RHICommandBuffer copy;
    if (!check(copy.Init(), "Readback command buffer")) return false;
    PerspectiveCamera camera(45.0f,1.0f);
    camera.SetPerspective(45.0f,1.0f);
    camera.SetPosition(glm::vec3(0,0,4));
    auto scene = std::make_shared<Scene>();
    auto left = scene->CreateEntity("LeftSphere");
    auto right = scene->CreateEntity("RightSphere");
    left.GetComponent<TransformComponent>().Translation = glm::vec3(-0.8f,0,0);
    right.GetComponent<TransformComponent>().Translation = glm::vec3(0.8f,0,0);
    left.GetComponent<TransformComponent>().Scale = glm::vec3(0.65f);
    right.GetComponent<TransformComponent>().Scale = glm::vec3(0.65f);
    left.AddComponent<MeshInstance3D>().RHIMeshAsset = RHIMeshRegistry::Get().GetSphere();
    right.AddComponent<MeshInstance3D>().RHIMeshAsset = RHIMeshRegistry::Get().GetSphere();
    auto& lm = left.AddComponent<MaterialComponent>();
    auto& rm = right.AddComponent<MaterialComponent>();
    lm.AlbedoPath = red; lm.UseAlbedoMap = true;
    rm.AlbedoPath = green; rm.UseAlbedoMap = true;
    lm.EnableSpecularHighlights = rm.EnableSpecularHighlights = false;
    lm.EnableEnvironmentReflections = rm.EnableEnvironmentReflections = false;
    SceneRenderSettings settings;
    settings.EnableSkybox = settings.EnableParticles = settings.EnableShadows = false;
    settings.EnableStudioLightLobes = false;
    settings.ClearColor = glm::vec4(0,0,0,1);
    auto render = [&]() {
        renderer.RenderScene(scene,camera,&color,&depth,settings);
        std::vector<uint8_t> pixels(readDesc.Size);
        if (!copy.Reset() || !copy.Begin()) return std::vector<uint8_t>{};
        copy.TransitionTexture(color.GetNative(),RHIResourceLayout::ShaderResource,RHIResourceLayout::CopySource);
        nri::TextureDataLayoutDesc layout{};
        layout.rowPitch = resolution*4;
        layout.slicePitch = resolution*resolution*4;
        nri::TextureRegionDesc region{};
        region.width = region.height = resolution; region.depth = 1;
        device.GetNRICore().CmdReadbackTextureToBuffer(*copy.GetNative(),*readback.GetNative(),layout,*color.GetNative(),region);
        copy.TransitionTexture(color.GetNative(),RHIResourceLayout::CopySource,RHIResourceLayout::ShaderResource);
        if (!copy.End() || !copy.Submit()) return std::vector<uint8_t>{};
        device.WaitIdle();
        auto* data = readback.Map();
        if (!data) return std::vector<uint8_t>{};
        std::memcpy(pixels.data(),data,pixels.size());
        readback.Unmap();
        return pixels;
    };
    auto sum = [](const std::vector<uint8_t>& pixels, uint32_t minX, uint32_t maxX, uint32_t channel) {
        uint64_t total = 0;
        for (uint32_t y=0;y<resolution;++y) for (uint32_t x=minX;x<maxX;++x) total += pixels[(y*resolution+x)*4+channel];
        return total;
    };
    auto first = render();
    if (!check(!first.empty(), "GPU image readback")) return false;
    if (!check(sum(first,0,64,0)>sum(first,0,64,1)*2+1000 && sum(first,64,128,1)>sum(first,64,128,0)*2+1000, "Two draws keep distinct texture bindings")) return false;
    auto originalTime = std::filesystem::last_write_time(red);
    writeTGA("red.tga",{0,0,255,255});
    std::filesystem::last_write_time(red,originalTime+std::chrono::seconds(2));
    auto replacedTexture = render();
    if (!check(!replacedTexture.empty() && sum(replacedTexture,0,64,2)>sum(replacedTexture,0,64,0)*2+1000, "Modified texture uploads new pixels")) return false;
    writeTGA("red.tga",{255,0,0,255});
    std::filesystem::last_write_time(red,originalTime+std::chrono::seconds(4));
    lm.UseAlbedoMap = false;
    lm.AlbedoColor = glm::vec4(0,0,1,1);
    auto cleared = render();
    if (!check(!cleared.empty() && sum(cleared,0,64,2)>sum(cleared,0,64,0)*2+1000, "Disabling texture removes previous GPU assignment")) return false;
    lm.AlbedoPath = (root / "missing.tga").string(); lm.UseAlbedoMap = true;
    auto missingTexture = render();
    if (!check(!missingTexture.empty() && sum(missingTexture,0,64,2)>sum(missingTexture,0,64,0)*2+1000, "Missing texture uses initialized fallback and material color")) return false;
    lm.UseAlbedoMap = false;
    lm.AlbedoColor = glm::vec4(1);
    lm.NormalPath = normal; lm.UseNormalMap = true;
    lm.NormalStrength = 0;
    auto flat = render();
    lm.NormalStrength = 1;
    auto tilted = render();
    if (!check(!tilted.empty() && flat!=tilted, "Normal strength affects actual shading")) return false;
    lm.UseNormalMap = false;
    lm.AOPath = black; lm.UseAOMap = true;
    lm.AOStrength = 0;
    auto noAO = render();
    lm.AOStrength = 1;
    auto withAO = render();
    if (!check(!withAO.empty() && sum(withAO,0,64,0)<sum(noAO,0,64,0), "AO strength affects actual ambient lighting")) return false;
    lm.UseAOMap = false;
    lm.EmissivePath = black; lm.UseEmissiveMap = true;
    lm.EmissionColor = glm::vec3(1);
    lm.EmissionIntensity = 0;
    auto blackEmission0 = render();
    lm.EmissionIntensity = 10;
    auto blackEmission10 = render();
    if (!check(!blackEmission10.empty() && blackEmission0==blackEmission10, "Black emission texels remain black")) return false;
    lm.UseEmissiveMap = false; lm.EmissionIntensity = 0;
    auto checkerPath = root / "checker.tga";
    {
        std::ofstream stream(checkerPath,std::ios::binary);
        uint8_t header[18]{};
        header[2]=2; header[12]=2; header[14]=2; header[16]=32; header[17]=0x28;
        stream.write(reinterpret_cast<char*>(header),sizeof(header));
        const uint8_t pixels[] = {0,0,255,255,0,255,0,255,0,255,0,255,0,0,255,255};
        stream.write(reinterpret_cast<const char*>(pixels),sizeof(pixels));
    }
    lm.AlbedoPath = checkerPath.string(); lm.UseAlbedoMap = true;
    lm.UVScale = glm::vec2(1,1);
    auto uv1 = render();
    lm.UVScale.y = 3;
    auto uv3 = render();
    if (!check(!uv3.empty() && uv1!=uv3, "Independent Y tiling reaches the GPU")) return false;
    lm.UVScale = glm::vec2(1);
    lm.AlbedoPath = writeTGA("alpha.tga",{255,0,0,0});
    lm.SurfaceType = SurfaceType::Cutout;
    lm.AlphaCutoff = 0;
    auto cutoff0 = render();
    lm.AlphaCutoff = 0.5f;
    auto cutoffHalf = render();
    if (!check(!cutoffHalf.empty() && sum(cutoff0,0,64,0)>sum(cutoffHalf,0,64,0)+1000, "Zero alpha cutoff remains valid")) return false;
    lm.SurfaceType = SurfaceType::Opaque;
    lm.UseAlbedoMap = false;
    lm.EnableSpecularHighlights = true;
    lm.Roughness = 0.25f;
    lm.ClearcoatStrength = 0;
    auto baseCoat = render();
    lm.ClearcoatStrength = 1;
    auto coat = render();
    if (!check(!coat.empty() && baseCoat!=coat, "Clearcoat changes rendered image")) return false;
    lm.ClearcoatRoughness = 0.4f;
    auto roughCoat = render();
    if (!check(!roughCoat.empty() && roughCoat!=coat, "Clearcoat roughness is independent from base roughness")) return false;
    lm.ClearcoatStrength = 0;
    lm.Metallic = 1;
    lm.AnisotropyStrength = 0;
    auto isotropic = render();
    lm.AnisotropyStrength = 0.85f;
    auto anisotropic = render();
    if (!check(!anisotropic.empty() && isotropic!=anisotropic, "Anisotropy changes rendered image")) return false;
    lm.AnisotropyRotation = 0.25f;
    auto rotated = render();
    if (!check(!rotated.empty() && rotated!=anisotropic, "Anisotropy rotation changes rendered image")) return false;
    lm.EnableSpecularHighlights = false;
    auto noDirectSpecular = render();
    if (!check(!noDirectSpecular.empty() && noDirectSpecular!=rotated, "Direct specular flag affects actual shading")) return false;
    lm.EnableEnvironmentReflections = true;
    auto withEnvironment = render();
    if (!check(!withEnvironment.empty() && withEnvironment!=noDirectSpecular, "Environment reflection flag affects actual shading")) return false;
    // Editing one external material updates every user; MakeUnique detaches both
    // the asset reference and mutable GPU state for a local override.
    MaterialComponent sharedSource;
    sharedSource.AlbedoColor = glm::vec4(1,0,0,1);
    sharedSource.EnableSpecularHighlights = sharedSource.EnableEnvironmentReflections = false;
    auto sharedPath = root / "shared.axleasset";
    if (!check(SceneHierarchyPanel::SaveMaterialAsset(sharedPath,sharedSource), "Shared asset save")) return false;
    auto shared = AssetManager::GetMaterial(sharedPath);
    auto sharedRHI = AssetManager::GetRHIMaterial(sharedPath);
    if (!check(shared && sharedRHI, "Both shared material caches")) return false;
    lm.SetAsset(0,shared); rm.SetAsset(0,shared);
    auto sharedRed = render();
    sharedSource.AlbedoColor = glm::vec4(0,1,0,1);
    SceneHierarchyPanel::SaveMaterialAsset(sharedPath,sharedSource);
    AssetManager::ReloadMaterial(sharedPath);
    auto sharedGreen = render();
    if (!check(!sharedGreen.empty() && sharedRed!=sharedGreen && sum(sharedGreen,0,64,1)>sum(sharedGreen,0,64,0)*2+1000 && sum(sharedGreen,64,128,1)>sum(sharedGreen,64,128,0)*2+1000 && sharedRHI->GetBaseColor()==sharedSource.AlbedoColor, "Asset reload updates both entity users and RHI cache")) return false;
    rm.MakeUnique(); rm.AlbedoColor = glm::vec4(0,0,1,1);
    sharedSource.AlbedoColor = glm::vec4(1,0,0,1);
    SceneHierarchyPanel::SaveMaterialAsset(sharedPath,sharedSource);
    AssetManager::ReloadMaterial(sharedPath);
    auto unique = render();
    if (!check(!unique.empty() && !rm.HasAsset() && sum(unique,0,64,0)>sum(unique,0,64,2)*2+1000 && sum(unique,64,128,2)>sum(unique,64,128,0)*2+1000, "Unique material stays independent after shared asset reload")) return false;
    lm.MakeUnique();
    left.GetComponent<TransformComponent>().Translation = glm::vec3(0,0,-0.6f);
    right.GetComponent<TransformComponent>().Translation = glm::vec3(0,0,0.6f);
    lm.AlbedoColor = glm::vec4(1,0,0,0.5f);
    rm.AlbedoColor = glm::vec4(0,0,1,0.5f);
    lm.SurfaceType = rm.SurfaceType = SurfaceType::Transparent;
    lm.DepthWrite = rm.DepthWrite = false;
    lm.RenderQueue = rm.RenderQueue = 3000;
    auto sorted = render();
    lm.RenderQueue = 4000;
    auto priority = render();
    constexpr size_t center = (64*resolution+64)*4;
    if (!check(!priority.empty() && priority[center]>sorted[center] && priority[center+2]<sorted[center+2], "Transparent priority overrides distance ordering")) return false;
    rm.DepthWrite = true;
    auto writesDepth = render();
    if (!check(!writesDepth.empty() && writesDepth[center]<priority[center], "Transparent depth-write state affects occlusion")) return false;
    right.RemoveComponent<MeshInstance3D>();
    left.GetComponent<MeshInstance3D>().RHIMeshAsset = RHIMeshRegistry::Get().GetPlane();
    auto& transform = left.GetComponent<TransformComponent>();
    transform.Translation = glm::vec3(0);
    transform.Rotation = glm::quat(glm::radians(glm::vec3(90,0,0)));
    lm.AlbedoColor = glm::vec4(1,0,0,1);
    lm.SurfaceType = SurfaceType::Opaque;
    lm.DepthWrite = true;
    lm.CullMode = CullMode::Back;
    auto cullBack = render();
    lm.CullMode = CullMode::Front;
    auto cullFront = render();
    lm.CullMode = CullMode::None;
    auto twoSided = render();
    if (!check(!twoSided.empty() && cullBack!=cullFront && sum(twoSided,0,128,0)>1000, "Front/back/two-sided culling reaches actual pipeline state")) return false;
    lm.SurfaceType = SurfaceType::Additive;
    lm.DepthWrite = false;
    lm.AlbedoColor.a = 0;
    auto noAdditive = render();
    lm.AlbedoColor.a = 0.5f;
    auto additive = render();
    if (!check(!additive.empty() && sum(additive,0,128,0)>sum(noAdditive,0,128,0)+1000, "Additive blending respects material alpha")) return false;
    std::cout << "[106 GPU] PASS: Per-draw texture colors, disabled maps, normal/AO strength, emission masking, clearcoat and anisotropy readback." << std::endl;
    return true;
}
