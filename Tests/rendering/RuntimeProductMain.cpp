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
    const std::string project="Project:\n  Name: Runtime fixture\n  StartScene: start.axlescene\n  AssetDirectory: assets\n  RHIBackend: Vulkan\n";
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
    explicit VerifyFrame(std::shared_ptr<RuntimeLayer> player,std::filesystem::path output):Player(std::move(player)),Output(std::move(output)){}
    void OnAttach() override {
        const auto scene=Player->GetScene();Require(scene&&scene->GetRegistry().view<MeshInstance3D>().size()==1,"actual startup scene loaded");
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
        if(Frames==5)Require(!Player->IsPaused(),"Runtime resumes through its actual input/simulation path");
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
static bool Run(const std::string& mode,bool cooked) {
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
    try {passed=Run(argc>1?argv[1]:"dx12",argc>2&&std::string(argv[2])=="cooked");}
    catch(const std::exception& error){std::cerr<<"OPT05 Runtime FAIL: "<<error.what()<<'\n';}
    RHIDevice::Get().Shutdown();JobSystem::Shutdown();std::cout<<"OPT05 Runtime "<<(passed?"PASS":"FAIL")<<'\n';return passed?0:1;
}
