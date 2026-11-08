#include "PluginSession.hpp"
#include "PluginSDKConfig.hpp"
#include <Axle/Scene.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/ExtensionDocument.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/Log.hpp>
#include <Axle/Path.hpp>
#include <imgui.h>
#include <fstream>
#include <sstream>
#include <windows.h>
#define CHECK(v) do { if (!(v)) { std::cerr << "FAIL " << __LINE__ << ": " << #v << '\n'; return 1; } } while(false)
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    using namespace Axle;
    CHECK(argc==3 || argc==4); Log::Init();
    const auto root=std::filesystem::temp_directory_path()/"AxleEngine"/(L"Optional \u0111\u1ed3 "+std::to_wstring(static_cast<std::uint64_t>(Axle::UUID{})));
    std::filesystem::create_directories(root/"assets/scenes");
    auto project=Project::New(root); const auto file=root/L"Optional \u0111\u1ed3.axleproj";
    project->GetConfig().SelectedPlugins=argc==4?std::vector<std::string>{PathToUtf8(argv[3])}:std::vector<std::string>{"axle.vehicle","axle.terrain"};
    project->GetConfig().StartScene="scenes/Foundation.axlescene";
    Project::SetActive(project); CHECK(ProjectSerializer(project).Serialize(file));
    PluginSessionEnvironment environment{argv[2],AXLE_PLUGIN_SDK_DIRECTORY,argv[1],AXLE_PLUGIN_BUILD_CONFIGURATION,
        AXLE_PLUGIN_SDK_FINGERPRINT,AXLE_PLUGIN_TOOLCHAIN_FINGERPRINT,AXLE_PLUGIN_CMAKE_GENERATOR,AXLE_PLUGIN_CMAKE_TOOLSET,AXLE_PLUGIN_CMAKE_INSTANCE};
    auto catalog=Plugin::Discover({{argv[2]},{}});
    PluginBuildRequest request; request.Plan=Plugin::CreateBuildPlan(Plugin::Resolve(catalog,project->GetConfig().SelectedPlugins,Plugin::ProductKind::Editor),
        Plugin::ProductKind::Editor,environment.Configuration,environment.SdkFingerprint,environment.ToolchainFingerprint);
    request.ProjectDirectory=root; request.CMake=argv[1]; request.SdkDirectory=environment.SdkDirectory;
    request.Generator=environment.Generator; request.GeneratorToolset=environment.GeneratorToolset; request.GeneratorInstance=environment.GeneratorInstance;
    std::atomic_bool cancel{false}; const auto built=BuildPlugins(request,cancel);
    std::cout << "Fixture: " << PathToUtf8(root) << "\nBuild log: " << PathToUtf8(built.LogPath) << '\n';
    if(!built.Success) { for(const auto& diagnostic:request.Plan.Diagnostics) std::cerr<<diagnostic.Message<<'\n'; std::cerr<<built.Error<<'\n'; return 1; }
    if(argc==4) { std::cout<<"Actual selected plugin SDK build PASS\n"; return 0; }
    CHECK(request.Plan.Artifacts.size()==6);
    JobSystem::Init(2); PhysicsEngine::Init(); ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={800,600}; io.DeltaTime=1.f/60;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    struct Cleanup { ~Cleanup(){ ImGui::DestroyContext(); PhysicsEngine::Shutdown(); JobSystem::Shutdown(); Project::SetActive(nullptr); } } cleanup;
    std::string error;
    auto edit=std::make_shared<Scene>();
    const auto add=[&](std::uint64_t id,const char* type,bool refs) {
        auto entity=edit->CreateEntityWithUUID(Axle::UUID(id),type);
        std::ostringstream text; WriteExtensionDocument(text,{type,1,true,refs,""});
        std::istringstream input(text.str()); std::string header,line; std::getline(input,header); std::vector<std::string> lines;
        while(std::getline(input,line)) lines.push_back(line);
        edit->StorePassiveSection(entity,header.substr(4,header.size()-5),lines); return entity;
    };
    auto chassis=add(100,"axle.vehicle/VehicleChassis",true);
    add(101,"axle.vehicle/VehicleWheel",true); add(200,"axle.spline/Spline",false); add(300,"axle.terrain/Terrain",false);
    std::vector<std::string> errors; CHECK(!edit->ExtensionsReady(errors));
    PluginProjectSession session(environment); CHECK(session.Bind(error));
    if(!session.Open(project,file)) { std::cerr<<session.Error()<<'\n'; return 1; }
    CHECK(session.ActiveIds()==std::vector<std::string>({"axle.spline","axle.terrain","axle.vehicle"}));
    CHECK(edit->ExtensionsReady(errors));
    const auto generation=session.LastBuild().GenerationDirectory;
    for(const auto& module:request.Plan.Artifacts) CHECK(GetModuleHandleW((generation/(module.ModuleName+".dll")).c_str()));
    auto unknown=std::make_shared<Scene>(); auto bad=unknown->CreateEntity("Wrong schema");
    std::ostringstream envelope; WriteExtensionDocument(envelope,{"axle.terrain/Terrain",999,true,false,"bad"});
    std::istringstream badInput(envelope.str()); std::string header,line; std::getline(badInput,header); std::vector<std::string> lines;
    while(std::getline(badInput,line)) lines.push_back(line);
    unknown->StorePassiveSection(bad,header.substr(4,header.size()-5),lines); CHECK(!unknown->ExtensionsReady(errors));
    unknown.reset();
    for(const auto& [type,payload]:std::vector<std::pair<std::string,std::string>>{
        {"axle.spline/Spline","spline-native-v1 8 1 999999999"},
        {"axle.terrain/Terrain","terrain-native-v1 999999999 999999999 1 0 0.8 0"}}) {
        const auto path=root/"assets/scenes/Invalid.axlescene";
        { std::ofstream out(path); out<<"Scene: Invalid\nEntities:\n  - Entity: 99\n    TagComponent:\n      Tag: Invalid\n"; WriteExtensionDocument(out,{type,1,true,false,payload}); }
        auto malformed=std::make_shared<Scene>(); CHECK(SceneSerializer(malformed.get()).Deserialize(path));
        CHECK(!malformed->ExtensionsReady(errors));
    }
    SceneSerializer serializer(edit.get()); const auto authored=serializer.SerializeToString(); CHECK(!authored.empty());
    CHECK(serializer.Serialize(root/"assets/scenes/Foundation.axlescene"));
    CHECK(AssemblySerializer::Serialize(root/"assets/Vehicle.axle",chassis));
    auto reopened=std::make_shared<Scene>(); CHECK(SceneSerializer(reopened.get()).Deserialize(root/"assets/scenes/Foundation.axlescene"));
    CHECK(reopened->ExtensionsReady(errors));
    // EnTT enumeration may reverse after load; compare each authored entity.
    for(auto id:{100ull,101ull,200ull,300ull}) {
        auto a=edit->GetEntityByUUID(Axle::UUID(id)), b=reopened->GetEntityByUUID(Axle::UUID(id)); CHECK(a&&b);
        std::ostringstream left,right; CHECK(edit->SerializeExtensions(a,left)&&reopened->SerializeExtensions(b,right)); CHECK(left.str()==right.str());
        CHECK(a.GetComponent<TagComponent>().Tag==b.GetComponent<TagComponent>().Tag);
        CHECK(a.GetComponent<TransformComponent>().Translation==b.GetComponent<TransformComponent>().Translation);
    }
    auto sample=std::make_shared<Scene>();
    CHECK(SceneSerializer(sample.get()).Deserialize(std::filesystem::path(argv[2])/"Vehicle/Content/Scenes/SampleVehicle.axlescene"));
    CHECK(sample->ExtensionsReady(errors)); CHECK(Scene::Copy(sample)); sample.reset();
    ImGui::NewFrame(); ImGui::SetNextWindowSize({760,550}); ImGui::Begin("Native optional Inspectors");
    for(auto id:{100ull,101ull,200ull,300ull}) session.EditorServices().DrawComponents(edit->GetEntityByUUID(Axle::UUID(id)));
    ImGui::End(); ImGui::Render(); CHECK(ImGui::GetDrawData()->TotalVtxCount>100);
    for(int cycle=0;cycle<3;++cycle) {
        auto play=Scene::Copy(edit); CHECK(play); play->OnRuntimeStart(); CHECK(play->IsRuntimeActive());
        for(int frame=0;frame<4;++frame) { play->ExecuteFrameScriptPhase(1.f/60); play->ExecuteFixedTick(1.f/60); play->ExecuteLatePresentationPhase(1.f/60); }
        auto world=ExtractRenderWorld(*play); CHECK(world.DebugLines.size()>=3);
        play->OnRuntimeStop(); CHECK(!play->IsRuntimeActive()); CHECK(ExtractRenderWorld(*play).DebugLines.empty());
        CHECK(serializer.SerializeToString()==authored); world.Clear(); play.reset();
    }
    session.Quiesce(); edit->Clear(); reopened->Clear(); edit.reset(); reopened.reset(); session.Shutdown();
    PluginRuntime::NativePlugin::CollectRetiredModules();
    for(const auto& module:request.Plan.Artifacts) CHECK(!GetModuleHandleW((generation/(module.ModuleName+".dll")).c_str()));
    CHECK(std::filesystem::is_empty(root/"Intermediate/Plugins/Sessions"));
    std::ofstream(root/"workflow.state")<<request.Plan.Fingerprint<<'\n';
    std::cout<<"Actual optional six-DLL activation, persistence, private Inspector, repeated Play/Stop and complete unload PASS\n";
    std::cout<<"Project: "<<PathToUtf8(file)<<"\nFingerprint: "<<request.Plan.Fingerprint<<'\n'; return 0;
}
