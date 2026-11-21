#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"
#include "RuntimeLayer.hpp"
#include "RuntimeStartup.hpp"
#include "RuntimeRenderContext.hpp"
#include <Axle/Application.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/Package.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/ImageSource.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <fstream>
#include <chrono>
#include <stdexcept>
using namespace Axle;
using namespace Axle::RHI;

static void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
static void Write(const std::filesystem::path& path,const std::string& text) {
    std::ofstream output(path,std::ios::binary); output << text; Require(bool(output),"fixture write");
}
static std::filesystem::path Fixture(const std::string& mode,bool cooked) {
    auto root = std::filesystem::absolute("build/agent-runs/OPT-05/runtime-fixtures") /
        (mode+(cooked?"-cooked-":"-loose-")+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root/"Resources");
    std::filesystem::copy(std::filesystem::path(AXLE_RUNTIME_TEST_RESOURCES)/"Shaders",root/"Resources/Shaders",std::filesystem::copy_options::recursive);
    std::filesystem::copy_file(AXLE_RUNTIME_TEST_SCRIPTS,root/"AxleScripts.dll");
    Scene scene;
    auto mesh=scene.CreateEntity("Fixture geometry"); mesh.AddComponent<MeshInstance3D>().AssetPath="builtin://cube";
    auto& material=mesh.AddComponent<MaterialComponent>();material.AlbedoColor={1,0,0,1};material.EmissionColor={1,0,0};material.EmissionIntensity=1;
    auto camera=scene.CreateEntity("Fixture camera");camera.GetComponent<TransformComponent>().Translation={0,0,4};camera.AddComponent<CameraComponent>().Primary=true;
    scene.CreateEntity("Authored disabled sky").AddComponent<SkyboxComponent>().Enabled=false;
    auto& fog=scene.CreateEntity("Authored froxel fog").AddComponent<VolumetricFogComponent>();
    fog.Enabled=true;fog.Density=0.16f;fog.Intensity=1.0f;fog.HeightFalloff=0.0f;fog.MaxDistance=40.0f;
    auto receiver=scene.CreateEntity("DDGI matte receiver");
    receiver.GetComponent<TransformComponent>().Translation={-1.1f,-0.35f,-0.05f};
    receiver.GetComponent<TransformComponent>().Scale={0.55f,0.7f,0.12f};
    receiver.AddComponent<MeshInstance3D>().AssetPath="builtin://cube";
    auto& receiverMaterial=receiver.AddComponent<MaterialComponent>();
    receiverMaterial.AlbedoColor={0.6f,0.6f,0.6f,1.0f};receiverMaterial.Roughness=1.0f;

    auto emitter=scene.CreateEntity("Offscreen DDGI emitter");
    emitter.GetComponent<TransformComponent>().Translation={-2.8f,0.2f,0.75f};
    emitter.GetComponent<TransformComponent>().Scale={0.15f,3.0f,2.0f};
    emitter.AddComponent<MeshInstance3D>().AssetPath="builtin://cube";
    auto& emitterMaterial=emitter.AddComponent<MaterialComponent>();
    emitterMaterial.EmissionColor={0.0f,0.0f,1.0f};emitterMaterial.EmissionIntensity=4.0f;

    auto& grid=scene.CreateEntity("Authored DDGI").AddComponent<DDGIProbeGridComponent>();
    grid.GridDimensions={3,2,2};grid.GridOrigin={-2.0f,-0.8f,-0.4f};grid.GridSpacing={1.0f,1.4f,1.6f};
    grid.MaxProbesPerFrame=12;

    auto& particles=scene.CreateEntity("Authored effect").AddComponent<ParticleEmitterComponent>();
    auto config=ParticleEmitterConfig::CreateNeutral();config.EmissionRate=0;config.LightingEnabled=false;config.SoftnessDistance=0;
    config.StartColor={0,1,0,1};config.StartSize=config.EndSize={0.45f,0.45f};
    particles.EmitterStacks.clear();particles.EmitterStacks.emplace_back("Authored emitter",config);particles.SyncPrimary();
    scene.CreateEntity("Authored HUD").AddComponent<UICanvasComponent>().AssetPath="hud.ui";
    SceneSerializer serializer(&scene);
    const auto sceneText=serializer.SerializeToString();
    UIDocument document;
    auto* canvas=document.CreateNode<UICanvasNode>(nullptr);canvas->ReferenceResolution={320,240};
    auto* panel=document.CreateNode<UIRectangleNode>(canvas);panel->Position={8,8};panel->Size={32,32};panel->Style.BackgroundColor={1,0,0,0.5f};
    auto* image=document.CreateNode<UIImageNode>(canvas);image->Position={56,8};image->Size={24,24};image->TexturePath="hud.tga";
    auto* text=document.CreateNode<UITextNode>(canvas);text->Position={8,56};text->Text="AX";text->FontSize=32;
    auto* button=document.CreateNode<UIButtonNode>(canvas);button->ID="click";button->Position={200,180};button->Size={80,32};button->OnClickAction="Probe.Click";
    std::string tga(18,'\0');tga[2]=2;tga[12]=1;tga[14]=1;tga[16]=32;tga[17]=0x28;tga.append("\xff\0\0\xff",4);
    const std::string project="Project:\n  SchemaVersion: 1\n  Name: Runtime fixture\n  StartScene: start.axlescene\n  AssetDirectory: assets\n  RHIBackend: Vulkan\n  Plugins:\n";
    if (cooked) {
        PackageWriter package;Require(package.Begin(root/"Data.axlepak"),"package begin");
        for (const auto& entry : std::vector<std::pair<std::string,std::string>>{{"Project.axleproj",project},{"start.axlescene",sceneText},{"hud.ui",document.Serialize()},{"hud.tga",tga}})
            Require(package.AddData(entry.first,entry.second.data(),entry.second.size()),"package entry");
        Require(package.Finish(),"package finish");
        Require(!std::filesystem::exists(root/"Project.axleproj")&&!std::filesystem::exists(root/"assets"),"cooked fixture has no loose authored payload");
    } else {
        std::filesystem::create_directories(root/"assets");
        Write(root/"Project.axleproj",project);Write(root/"assets/start.axlescene",sceneText);
        Write(root/"assets/hud.ui",document.Serialize());Write(root/"assets/hud.tga",tga);
    }
    return root;
}

static uint32_t InputFrame=0;
static CursorMode Cursor=CursorMode::Normal;
class DriveInput final : public Layer {
public:
    InputContext Context;
    void OnAttach() override {
        Context=*Input::GetInputContext();
        Context.IsKeyPressedFn=[](KeyCode key){return key==KeyCode::Escape&&(InputFrame==1||InputFrame==5);};
        Context.IsMouseButtonPressedFn=[](MouseCode button){return button==MouseCode::Button0&&(InputFrame==2||InputFrame==4);};
        Context.GetMousePositionFn=[](){return InputFrame<3?std::pair<f32,f32>{220.0f,196.0f}:std::pair<f32,f32>{110.0f,98.0f};};
        Context.SetCursorModeFn=[](CursorMode value){Cursor=value;};Context.GetCursorModeFn=[](){return Cursor;};
        Input::SetInputContext(&Context);
    }
    void OnUpdate(Timestep) override {
        ++InputFrame;
        if(InputFrame==3) {
            auto& window=Application::Get().GetWindow();
            glfwSetWindowSize(window.GetNativeWindow(),160,120);window.PollEvents();
        }
    }
    void OnDetach() override { Input::SetContext(Application::Get().GetWindow().GetNativeWindow()); }
};
class VerifyFrame final : public Layer {
public:
    std::shared_ptr<RuntimeLayer> Player;
    std::filesystem::path Output;
    uint32_t Frames=0,Clicks=0;
    bool Native=false;
    uint8_t FoggedCenterRed=0;
    uint8_t DDGIReceiverBlue=0;
    explicit VerifyFrame(std::shared_ptr<RuntimeLayer> player,std::filesystem::path output):Player(std::move(player)),Output(std::move(output)){}
    void OnAttach() override {
        const auto scene=Player->GetScene();Require(scene&&scene->GetRegistry().view<MeshInstance3D>().size()==3,"actual startup scene loaded");
        scene->GetUIDataContext().RegisterAction("Probe.Click",[this]{++Clicks;});
        auto emitters=scene->GetRegistry().view<ParticleEmitterComponent>();Require(emitters.size()==1,"authored effect loaded");
        auto& effect=emitters.get<ParticleEmitterComponent>(*emitters.begin());Require(effect.EmitterStacks.size()==1,"one authored emitter retained");
        ParticleSpawnData particle;particle.Position={1.3f,0,0};particle.StartSize=particle.EndSize=0.45f;
        particle.StartColor=particle.EndColor={0,1,0,1};particle.Lifetime=10;effect.EmitterStacks[0].System->GetPool().Spawn(particle);
        Native=RHIDevice::Get().IsNativeReady();
    }
    void OnUpdate(Timestep) override {
        ++Frames;Require(!Application::Get().GetUIContext(),"Runtime product has no ImGui UI context");
        auto& window=Application::Get().GetWindow();auto width=window.GetWidth(),height=window.GetHeight();
        Require(width==(Frames<3?320u:160u)&&height==(Frames<3?240u:120u),"real window resize reached the player");
        std::vector<uint8_t> pixels;
        if(Native) {auto* color=RenderService::Get().GetDefaultMainColorTarget();Require(color&&color->GetWidth()==width&&color->GetHeight()==height,"Runtime target follows resize");pixels=Axle::RenderTests::ReadTexture(*color);}
        else {
            pixels.resize(width*height*4);glReadBuffer(GL_BACK);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            Require(glGetError()==GL_NO_ERROR,"GL Runtime readback");
            for(uint32_t y=0;y<height/2;++y)for(uint32_t x=0;x<width*4;++x)std::swap(pixels[y*width*4+x],pixels[(height-1-y)*width*4+x]);
        }
        Require(pixels.size()==width*height*4,"actual Runtime pixels");
        auto at=[&](uint32_t x,uint32_t y){return (size_t(y)*width+x)*4;};
        const auto center=at(width/2,height/2),sprite=at(width*68/320,height*20/240);
        Require(pixels[center]>pixels[center+1]+30&&pixels[center]>pixels[center+2]+30,"Runtime material geometry pixels");
        if(Frames==1) {
            FoggedCenterRed=pixels[center];
            auto fog=Player->GetScene()->GetRegistry().view<VolumetricFogComponent>();
            Require(fog.size()==1&&fog.get<VolumetricFogComponent>(*fog.begin()).Enabled,"authored fog loaded at Runtime");
            fog.get<VolumetricFogComponent>(*fog.begin()).Enabled=false;
        }
        if(Frames==2) {
            if(Native) Require(pixels[center]>FoggedCenterRed+8,"Runtime enabled fog attenuates scene pixels and authoring disables it live");
            else Require(pixels[center]==FoggedCenterRed,"OpenGL preserves scene pixels with unsupported fog requested");
            std::cout<<"U6E_RUNTIME_FOG_CENTER enabled="<<unsigned(FoggedCenterRed)<<" disabled="<<unsigned(pixels[center])<<'\n';
        }
        Require(pixels[sprite+2]>245&&pixels[sprite]<8&&pixels[sprite+1]<8,"Runtime authored HUD sprite, including cooked VFS and DPI");
        size_t strokes=0,green=0;
        for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
            const auto i=at(x,y);
            if(x<width/4&&y>height*50/240&&y<height*100/240&&pixels[i]>180&&pixels[i+1]>180&&pixels[i+2]>180)++strokes;
            if(pixels[i+1]>150&&pixels[i+1]>pixels[i]*2+20&&pixels[i+1]>pixels[i+2]*2+20)++green;
        }
        Require(strokes>5,"Runtime font has actual glyph coverage");Require(green>5,"Runtime authored effect produces actual green particle pixels");
        if(Frames==1||Frames==3) {std::ofstream image(Output/("frame"+std::to_string(Frames)+".rgba"),std::ios::binary);image.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());Require(bool(image),"Runtime capture");}
        if(Frames==2)Require(Clicks==1,"Runtime UI input before resize");
        if(Frames==4)Require(Clicks==2,"Runtime UI input maps physical pixels through DPI after resize");
        const auto receiverBlue=at(width/4,height*3/5)+2;
        if(Frames==4) {
            DDGIReceiverBlue=pixels[receiverBlue];
            auto grids=Player->GetScene()->GetRegistry().view<DDGIProbeGridComponent>();
            Require(grids.size()==1&&grids.get<DDGIProbeGridComponent>(*grids.begin()).Enabled,"authored grid loaded at Runtime");
            grids.get<DDGIProbeGridComponent>(*grids.begin()).Enabled=false;
        }
        if(Frames==5) {
            Require(!Player->IsPaused(),"Runtime resumes through its actual input/simulation path");
            std::cout<<"U7_RUNTIME_DDGI_BLUE enabled="<<unsigned(DDGIReceiverBlue)<<" disabled="<<unsigned(pixels[receiverBlue])<<'\n';
            if(Native) Require(DDGIReceiverBlue>pixels[receiverBlue]+8,"Runtime authored DDGI lights a receiver and disables live");
            else Require(DDGIReceiverBlue==pixels[receiverBlue],"OpenGL keeps diffuse fallback with and without authored DDGI");
        }
    }
};
class PhysicsService final : public IApplicationService {
public:
    const char* GetName() const override{return "Physics";}
    void OnInit() override{PhysicsEngine::Init();}
    void OnShutdown() override{PhysicsEngine::Shutdown();}
};
class FailedPresent final : public ApplicationRenderContext {
public: bool Present(const Window&) override{return false;}void Shutdown() override{}
};
#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/ProjectRenderSettingsSync.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/Material.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/MaterialCache.hpp>

static bool RunM15Parity(const std::string& mode) {
    char name[]="runtime", worker[]="--render-thread=worker", alias[]="--render-transient-aliasing=on";
    char* policyArgs[]{name,worker,alias}; std::string policyError;
    Require(RenderService::Get().ConfigureProductArguments(3,policyArgs,policyError), "Runtime parity worker policy");
    const bool native = mode != "gl";
    const auto backend = mode == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12;
    const std::string backendName = mode == "vulkan" ? "Vulkan" : (mode == "gl" ? "OpenGL" : "Direct3D12");
    const auto root = std::filesystem::absolute("build/agent-runs/U10-B2/runtime-parity") /
        (mode + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto assets = root / "assets";
    std::filesystem::create_directories(assets / "materials");
    std::filesystem::create_directories(assets / "scenes");
    std::filesystem::create_directories(assets / "assemblies");

    const auto prevAssetDir = FileSystem::GetAssetDirectory();
    FileSystem::SetAssetDirectory(assets);
    AssetManager::Init(assets);

    const auto projPath = root / "U10B2Parity.axleproj";
    {
        auto project = Project::New(root);
        Require(bool(project), "Project::New U10B2Parity");
        auto& cfg = project->GetConfig();
        cfg.Name = "U10B2Parity";
        cfg.StartScene = "scenes/Parity.axlescene";
        cfg.AssetDirectory = "assets";
        cfg.RHIBackend = backendName;
        cfg.HasAuthoredRenderSettings = true;
        cfg.RenderSettings.EnableShadows = true;
        cfg.RenderSettings.EnableSkybox = true;
        cfg.RenderSettings.EnableBloom = true;
        cfg.RenderSettings.EnableSSR = true;
        cfg.RenderSettings.EnableGTAO = true;
        cfg.RenderSettings.EnableToneMapping = true;
        cfg.RenderSettings.ToneMappingMode = 1; // Reinhard
        cfg.RenderSettings.Exposure = 0.75f;
        cfg.RenderSettings.MSAASamples = 1;
        ProjectSerializer projSerializer(project);
        Require(projSerializer.Serialize(projPath), "Save U10B2Parity.axleproj");
    }

    const auto matPath = assets / "materials" / "M_Parity.axleasset";
    {
        Material mat("M_Parity");
        mat.SetAlbedoColor({0.85f, 0.16f, 0.16f, 1.0f});
        mat.SetMetallic(0.15f);
        mat.SetRoughness(0.20f);
        mat.SetClearcoatStrength(0.85f);
        mat.SetSheenEnabled(true);
        mat.SetSheenColor({0.95f, 0.75f, 0.25f});
        Require(mat.Save(matPath), "Save M_Parity.axleasset");
    }
    AssetManager::Sync();

    const auto asmPath = assets / "assemblies" / "Rig.axle";
    {
        Scene authorScene;
        auto sun = authorScene.CreateEntity("ParitySun");
        auto& dlc = sun.AddComponent<DirectionalLightComponent>();
        dlc.Intensity = 2.4f; dlc.AmbientIntensity = 0.35f; dlc.Azimuth = 45.0f; dlc.Elevation = 40.0f;
        auto sky = authorScene.CreateEntity("ParitySky");
        auto& sbc = sky.AddComponent<SkyboxComponent>();
        sbc.Enabled = true; sbc.EnvironmentLighting = true;
        auto probe = authorScene.CreateEntity("ParityProbe");
        auto& rpc = probe.AddComponent<ReflectionProbeComponent>();
        rpc.Enabled = true; rpc.DynamicCapture = true; rpc.CaptureResolution = 64;
        auto fog = authorScene.CreateEntity("ParityFog");
        auto& vfc = fog.AddComponent<VolumetricFogComponent>();
        vfc.Enabled = true; vfc.Density = 0.01f;
        Require(AssemblySerializer::Serialize(asmPath, probe), "Save probe assembly");

        auto sphere = authorScene.CreateEntity("HeroSphere");
        sphere.GetComponent<TransformComponent>().Translation = {0.0f, 0.0f, 0.0f};
        sphere.AddComponent<MeshInstance3D>().AssetPath = "builtin://sphere";
        auto& mc = sphere.AddComponent<MaterialComponent>();
        mc.MaterialHandle = AssetManager::GetAssetUUID(matPath);
        mc.AssetPath = "materials/M_Parity.axleasset";
        mc.MaterialAsset = MaterialCache::GetMaterial(matPath);
        mc.SyncFromAsset();

        auto cam = authorScene.CreateEntity("MainCamera");
        cam.GetComponent<TransformComponent>().Translation = {0.0f, 0.0f, 3.2f};
        cam.AddComponent<CameraComponent>().Primary = true;

        Require(SceneSerializer(&authorScene).Serialize(assets / "scenes" / "Parity.axlescene"), "Save Parity.axlescene");
    }

    std::vector<uint8_t> editorReopenedPixels, runtimePixels;
    if (native) {
        RHIDeviceDesc desc; desc.PreferredBackend = backend; desc.Headless = true; desc.AllowFallback = false;
        desc.EnableValidation = desc.EnableNRIValidation = true;
        Require(RHIDevice::Get().Init(desc) && !RHIDevice::Get().IsSoftwareAdapter(), "M15 parity physical device");
    }

    // 1. Reopen saved project + scene in Editor extraction path (07.2)
    auto reopenedProj = Project::Load(projPath);
    Require(bool(reopenedProj), "Reload saved .axleproj");
    const auto& reopenedCfg = reopenedProj->GetConfig();
    Require(reopenedCfg.RenderSettings.ToneMappingMode == 1 && std::abs(reopenedCfg.RenderSettings.Exposure - 0.75f) < 1e-4f,
        "M15-U10-07.2: saved ProjectRenderSettings persist after reload");

    std::unique_ptr<Window> glWindow;
    if (!native) {
        WindowProps props; props.Width = 256; props.Height = 256; props.Maximized = false;
        glWindow = std::make_unique<Window>(props);
        Require(RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(glfwGetProcAddress)), "M15 parity GL context");
    }
    {
        Scene reopenedScene;
        Require(SceneSerializer(&reopenedScene).Deserialize(assets / "scenes" / "Parity.axlescene"), "Reload saved .axlescene");
        auto viewEnt = reopenedScene.GetPrimaryCameraEntity();
        Require(bool(viewEnt), "Reloaded scene preserves Primary Camera");
        PerspectiveCamera cam(45.0f, 1.0f, 0.1f, 200.0f);
        cam.SetViewMatrix(glm::lookAt(glm::vec3(0.0f, 0.0f, 3.2f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)), {0.0f, 0.0f, 3.2f});

        SceneRenderSettings settings;
        const auto authoredFog = ExtractVolumetricFog(reopenedScene);
        const auto authoredDDGI = ExtractDDGIProbeGrid(reopenedScene);
        RenderSettingsSync::ApplyToSceneSettings(reopenedCfg.RenderSettings, settings, std::nullopt, authoredFog.has_value(),
            std::nullopt, authoredDDGI.has_value(), std::nullopt);

        auto& svc = RenderService::Get();
        ViewRequest req; req.Kind = RenderViewKind::Main; req.Width = 256; req.Height = 256;
        req.Camera = cam; req.Settings = settings; req.Name = "ReopenedEditorView";
        std::shared_ptr<Framebuffer> fbo;
        if (!native) { fbo = Framebuffer::Create({256, 256, 1, false}); req.GLTargetFBO = fbo; }
        auto readFrame = [&](const std::shared_ptr<Framebuffer>& srcFbo) {
            if (native) return Axle::RenderTests::ReadTexture(*svc.GetDefaultMainColorTarget());
            std::vector<uint8_t> out(256u * 256u * 4u);
            srcFbo->Bind();
            glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
            srcFbo->Unbind();
            return out;
        };
        Require(svc.RenderView(reopenedScene, req).Success, "Render reopened Editor view");
        editorReopenedPixels = readFrame(fbo);

        // 2. Render Runtime equivalent using Project::Load + RuntimeSettings Sync path (07.3)
        auto runtimeProj = Project::Load(projPath);
        Require(bool(runtimeProj), "Runtime load .axleproj");
        const auto& runtimeCfg = runtimeProj->GetConfig();
        Scene runtimeScene;
        Require(SceneSerializer(&runtimeScene).Deserialize(assets / runtimeCfg.StartScene), "Runtime load StartScene");
        SceneRenderSettings runtimeSettings;
        const auto rtFog = ExtractVolumetricFog(runtimeScene);
        const auto rtDDGI = ExtractDDGIProbeGrid(runtimeScene);
        RenderSettingsSync::ApplyToSceneSettings(runtimeCfg.RenderSettings, runtimeSettings, std::nullopt, rtFog.has_value(),
            std::nullopt, rtDDGI.has_value(), std::nullopt);
        ViewRequest rtReq = req; rtReq.Settings = runtimeSettings; rtReq.Name = "RuntimeParityView";
        Require(svc.RenderView(runtimeScene, rtReq).Success, "Render Runtime parity view");
        runtimePixels = readFrame(fbo);
        svc.Shutdown();
    }

    Require(!editorReopenedPixels.empty() && editorReopenedPixels.size() == runtimePixels.size(), "M15 parity frame sizes match");
    uint64_t crimsonEnergy = 0;
    uint32_t maxDiff = 0;
    for (size_t i = 0; i + 3 < runtimePixels.size(); i += 4) {
        if (runtimePixels[i] > runtimePixels[i + 1] + 20) crimsonEnergy += runtimePixels[i];
        maxDiff = std::max(maxDiff, static_cast<uint32_t>(std::abs(int(editorReopenedPixels[i]) - int(runtimePixels[i]))));
        maxDiff = std::max(maxDiff, static_cast<uint32_t>(std::abs(int(editorReopenedPixels[i+1]) - int(runtimePixels[i+1]))));
        maxDiff = std::max(maxDiff, static_cast<uint32_t>(std::abs(int(editorReopenedPixels[i+2]) - int(runtimePixels[i+2]))));
    }
    Require(crimsonEnergy > 5000, "M15-U10-07: crimson clearcoat sphere rendered non-black in Runtime parity");
    Require(maxDiff <= 2, "M15-U10-07.3: reopened Editor and Runtime pixels match within tolerance");

    RHIMeshRegistry::Get().Shutdown();
    if (native) Require(RHIDevice::Get().Shutdown(), "M15 parity device shutdown");
    AssetManager::Shutdown();
    if (!prevAssetDir.empty()) FileSystem::SetAssetDirectory(prevAssetDir);
    std::error_code ec; std::filesystem::remove_all(root, ec);
    return true;
}

static bool Run(const std::string& mode,bool cooked) {
    char name[]="runtime", worker[]="--render-thread=worker", alias[]="--render-transient-aliasing=on";
    char* policyArgs[]{name,worker,alias}; std::string policyError;
    Require(RenderService::Get().ConfigureProductArguments(3,policyArgs,policyError) &&
        RenderService::Get().GetProductRollout().FellBack && !RenderService::Get().GetProductRollout().Worker &&
        !RenderService::Get().GetProductRollout().TransientAliasing,"actual Runtime consumers retain explicit safe product fallback");
    const auto root=Fixture(mode,cooked);std::cout<<"OPT05_RUNTIME_FIXTURE="<<root.generic_string()<<'\n';
    const auto priorDirectory=std::filesystem::current_path();std::filesystem::current_path(root);
    auto inherited=PrepareRuntimeStartup(root/"Project.axleproj",{});
    Require(inherited.ProjectDefinition&&inherited.Backend.PreferredBackend==RHIType::Vulkan,"project backend resolved before GPU construction, including absolute cooked project path");
    RHIBackendConfig request;request.RawArgument=mode;request.AllowFallback=mode=="fallback";request.EnableValidation=true;
    request.PreferredBackend=mode=="vulkan"?RHIType::Vulkan:mode=="gl"?RHIType::OpenGL:RHIType::Direct3D12;
    auto startup=PrepareRuntimeStartup(root/"Project.axleproj",request);
    Require(startup.Backend.PreferredBackend==request.PreferredBackend,"CLI rendering backend overrides project");
    std::unique_ptr<ScopedRHIFault> fallback;
    if(mode=="fallback")fallback=std::make_unique<ScopedRHIFault>([](std::string_view operation)->std::optional<nri::Result>{if(operation=="CreateDevice")return nri::Result::UNSUPPORTED;return {};});
    if(mode=="dx12"||mode=="vulkan")Require(RHIDevice::IsGraphicsAPIValidationAvailable(request.PreferredBackend),"real Runtime API validation prerequisite");
    ImageTextureCache::SetSourceResolver(LoadImageSource);
    std::unique_ptr<Application> app;std::shared_ptr<RuntimeLayer> player;std::shared_ptr<VerifyFrame> verifier;
    RuntimeRenderContext* presenter=nullptr;
    Require(RHISwitcher::InitializeProduct(startup.Backend,[&](GraphicsAPI api) {
        try {
            ApplicationSpecification spec;spec.Name="OPT05 Runtime product";spec.Width=320;spec.Height=240;spec.Maximized=false;spec.VSync=false;spec.GraphicsBackend=api;
            spec.RendererInitCallback=[](void* address){return RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(address));};
            spec.ViewportResizeCallback=[](u32 w,u32 h){RenderCommand::SetViewport(0,0,w,h);};
            app=std::make_unique<Application>(spec);app->RegisterService(std::make_shared<PhysicsService>());
            // Push input before the player; its public host input context drives
            // the production pause/hit-test path without OS event timing races.
            app->PushLayer(std::make_shared<DriveInput>());
            player=std::make_shared<RuntimeLayer>(startup.ProjectPath,startup.ProjectDefinition);app->PushLayer(player);
            verifier=std::make_shared<VerifyFrame>(player,root);app->PushLayer(verifier);
            if(api!=GraphicsAPI::OpenGL){auto context=std::make_unique<RuntimeRenderContext>(app->GetWindow());presenter=context.get();app->SetRenderContext(std::move(context));}
            Require(api==RHISwitcher::ToGraphicsAPI(mode=="fallback"?RHIType::OpenGL:request.PreferredBackend),"actual selected Runtime backend");
            return true;
        }catch(const std::exception& error){std::cerr<<error.what()<<'\n';verifier.reset();player.reset();app.reset();return false;}
    }),"Runtime product initializes");
    fallback.reset();
    Require(app->Run(8)&&verifier->Frames==8&&verifier->Clicks==2,"actual bounded Application loop, UI input and player frames");
    Require(RenderService::Get().GetProductRollout().FellBack && RenderService::Get().GetOwnershipReport().OwnershipAligned,
        "Runtime scene/UI rendering uses the accepted synchronous owner policy");
    if(presenter)Require(presenter->GetPresentCount()==8,"all Runtime frames actually presented");
    app->SetRenderContext(std::make_unique<FailedPresent>());
    Require(!app->Run(1),"presentation failure is reported to the Runtime host");
    // LayerStack owns detach; observers must not extend GPU/Jolt owner lifetimes.
    verifier.reset();player.reset();app.reset();
    Require(!RenderService::Get().IsInitialized(),"Runtime layer shuts down the shared service");
    Require(RHIDevice::Get().Shutdown(),"Runtime native shutdown");
    Require(!GetRHIValidationErrorCount(),"zero API/NRI errors including Runtime teardown");
    FileSystem::UnmountPackage();std::filesystem::current_path(priorDirectory);
    return true;
}
int main(int argc,char** argv) {
    Axle::RenderTests::ConfigureFailureReporting();Log::Init();JobSystem::Init(2);
    bool passed=false;
    try {passed=(argc>2&&std::string(argv[2])=="m15parity")?RunM15Parity(argv[1]):Run(argc>1?argv[1]:"dx12",argc>2&&std::string(argv[2])=="cooked");}
    catch(const std::exception& error){std::cerr<<"OPT05 Runtime FAIL: "<<error.what()<<'\n';}
    RHIDevice::Get().Shutdown();JobSystem::Shutdown();std::cout<<"OPT05 Runtime "<<(passed?"PASS":"FAIL")<<'\n';return passed?0:1;
}
