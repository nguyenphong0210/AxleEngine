#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"
#include <Axle/AssetPreviewPanel.hpp>
#include <Axle/AssemblyEditorPanel.hpp>
#include <Axle/VFXEditorPanel.hpp>
#include <Axle/RenderParticleData.hpp>
#include <Axle/AssetThumbnailManager.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <Axle/Window.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/ImageSource.hpp>
#include <Axle/FileSystem.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <fstream>
#include <chrono>
#include <stdexcept>
using namespace Axle;
using namespace Axle::RHI;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static size_t Red(const std::vector<uint8_t>& pixels){size_t count=0;for(size_t i=0;i+3<pixels.size();i+=4)if(pixels[i]>pixels[i+1]+30&&pixels[i]>pixels[i+2]+30)++count;return count;}
static std::vector<uint8_t> ReadGL(const std::shared_ptr<Framebuffer>& target) {
    Require(bool(target),"GL preview owns its framebuffer");
    auto width=target->GetSpecification().Width,height=target->GetSpecification().Height;
    std::vector<uint8_t> pixels(width*height*4);
    std::cerr<<"GL preview readback texture="<<target->GetColorAttachmentRendererID()<<" alive="<<int(glIsTexture(target->GetColorAttachmentRendererID()))<<" extent="<<width<<"x"<<height<<'\n';
    glGetTextureImage(target->GetColorAttachmentRendererID(),0,GL_RGBA,GL_UNSIGNED_BYTE,static_cast<GLsizei>(pixels.size()),pixels.data());
    Require(glGetError()==GL_NO_ERROR,"GL preview readback");return pixels;
}
static void Finish(ImGuiLayer& ui,bool native) {
    if(!native){ui.End();Require(glGetError()==GL_NO_ERROR,"queued GL preview UI submission");return;}
    ImGui::Render();RHITexture color;RHICommandBuffer commands;
    Require(color.Init(RHITextureDesc::RenderTarget2D(960,640))&&commands.Init()&&commands.Begin(),"queued preview UI target");
    commands.TransitionTexture(color.GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ColorAttachment);
    nri::AttachmentDesc attachment{};attachment.descriptor=color.GetRTV();attachment.loadOp=nri::LoadOp::CLEAR;attachment.storeOp=nri::StoreOp::STORE;
    nri::RenderingDesc rendering{};rendering.colors=&attachment;rendering.colorNum=1;
    commands.BeginRendering(rendering);Require(ui.Render(&commands),"actual UI preview images record after close/resize");commands.EndRendering();
    Require(commands.End()&&commands.Submit()&&RHIDevice::Get().WaitIdle(),"queued preview submission");ui.End();
}
static bool Run(const std::string& mode) {
    bool native=mode!="gl";auto backend=mode=="vulkan"?RHIType::Vulkan:RHIType::Direct3D12;
    auto root=std::filesystem::absolute("build/agent-runs/OPT-05/preview-fixtures")/(mode+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root);
    std::unique_ptr<Window> window;
    if(native){RHIDeviceDesc desc;desc.PreferredBackend=backend;desc.Headless=true;desc.AllowFallback=false;desc.EnableValidation=desc.EnableNRIValidation=true;
        Require(RHIDevice::IsGraphicsAPIValidationAvailable(backend)&&RHIDevice::Get().Init(desc)&&!RHIDevice::Get().IsSoftwareAdapter(),"physical API-validated preview backend");}
    else{WindowProps props;props.Width=960;props.Height=640;props.Maximized=false;window=std::make_unique<Window>(props);Require(RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(glfwGetProcAddress)),"physical GL preview context");
        glEnable(GL_DEBUG_OUTPUT);glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);glDebugMessageCallback([](GLenum,GLenum type,GLuint,GLenum,GLsizei,const GLchar* message,const void*){if(type==GL_DEBUG_TYPE_ERROR)std::cerr<<"[GL API Error] "<<message<<'\n';},nullptr);}
    {
        ImGuiLayer ui(native?(backend==RHIType::Vulkan?ImGuiBackendType::Vulkan:ImGuiBackendType::Direct3D12):ImGuiBackendType::OpenGL);ui.Init(window?window->GetNativeWindow():nullptr);
        ImGui::GetIO().DisplaySize={960,640};ImGui::GetIO().DeltaTime=1.0f/60;
        ImageTextureCache::SetSourceResolver(LoadImageSource);
        AssemblyData data;data.IsValid=true;data.RootNode.Tag="Authored root";data.RootNode.HasMesh=true;data.RootNode.Mesh.AssetPath="builtin://cube";
        data.RootNode.Transform.Rotation=glm::angleAxis(glm::radians(70.0f),glm::vec3(0,1,0));data.RootNode.Transform.Scale={1.2f,0.8f,1};
        data.RootNode.HasMaterial=true;data.RootNode.Material.AlbedoColor={1,0,0,1};data.RootNode.Material.EmissionColor={1,0,0};data.RootNode.Material.EmissionIntensity=1;
        AssemblyNode child;child.Tag="Authored child";child.HasMesh=true;child.Mesh.AssetPath="builtin://sphere";child.Transform.Translation={1,0.4f,0};child.Transform.Scale=glm::vec3(0.35f);child.HasMaterial=true;child.Material=data.RootNode.Material;data.RootNode.Children.push_back(child);
        auto path=root/"hierarchy.axle";Require(AssemblySerializer::SaveAssemblyData(path,data),"authored assembly fixture");
        Scene main;auto marker=main.CreateEntity("Untouched main");const auto markerID=marker.GetUUID();
        auto assembly=std::make_unique<AssemblyEditorPanel>();assembly->SetSceneContext(&main,nullptr,nullptr);assembly->InitRHI(&ui);assembly->SetAssembly(path);
        auto asset=std::make_unique<AssetPreviewPanel>();asset->SetSceneContext(&main,nullptr,nullptr);asset->InitRHI(&ui);asset->SetAsset(path);
        for(int frame=0;frame<3;++frame) {
            ui.Begin();const float width=frame==1?397.0f:320.0f;
            assembly->RenderPreviewViewport(width,240);
            auto assemblyPixels=native?Axle::RenderTests::ReadTexture(*assembly->GetRHIViewport()->GetColorTexture()):ReadGL(assembly->GetGLFramebuffer());
            Require(Red(assemblyPixels)>100,"actual Assembly Editor native/GL material pixels");
            uint64_t image=native?assembly->GetRHIViewport()->GetImTextureID():assembly->GetGLFramebuffer()->GetColorAttachmentRendererID();
            Require(image!=0,"registered Assembly image lease");
            if(!native)ui.DeferRelease([owner=assembly->GetGLFramebuffer()]{});
            ImGui::GetForegroundDrawList()->AddImage(static_cast<ImTextureID>(image),{500,0},{820,240});
            if(frame==1) {
                assembly->RenderPreviewViewport(width+3,240);
                const uint64_t resized=native?assembly->GetRHIViewport()->GetImTextureID():assembly->GetGLFramebuffer()->GetColorAttachmentRendererID();
                Require(resized!=image,"same-frame resize cannot reuse the queued image generation");
            }
            // The real panel queues its own GL frame owner as well.
            bool open=true;ImGui::SetNextWindowSize({940,600},ImGuiCond_Always);assembly->OnImGuiRender(&open);
            ImGui::SetNextWindowPos({0,0},ImGuiCond_Always);ImGui::SetNextWindowSize({480,360},ImGuiCond_Always);ImGui::Begin("Asset test",nullptr,ImGuiWindowFlags_NoSavedSettings);
            asset->RenderPreviewViewport();ImGui::End();
            auto assetPixels=native?Axle::RenderTests::ReadTexture(*asset->GetRHIViewport()->GetColorTexture()):ReadGL(asset->GetGLFramebuffer());
            Require(Red(assetPixels)>100,"actual Asset Preview assembly material pixels");
            auto world=ExtractRenderWorld(*asset->GetPreviewScene());Require(world.Meshes.size()==2,"Asset Preview retains authored mesh count");
            Require(world.Meshes[0].Material.RHIMaterialAsset&&world.Meshes[1].Material.RHIMaterialAsset,"Asset Preview retains assembly materials");
            const float distance=glm::length(glm::vec3(world.Meshes[0].WorldTransform[3]-world.Meshes[1].WorldTransform[3]));
            const float expected=glm::length(glm::vec3(data.RootNode.Transform.GetTransform()*child.Transform.GetTransform()*glm::vec4(0,0,0,1)));
            Require(std::abs(distance-expected)<0.001f,"Asset Preview retains parent rotation/scale in child world transforms");
            Require(marker&&marker.GetUUID()==markerID&&main.GetRegistry().view<MeshInstance3D>().size()==0,"preview never inserts into the main scene");
            if(frame==2){asset.reset();assembly.reset();}
            Finish(ui,native);
            if(frame==1){assembly->SetOpen(false);assembly->SetAssembly(path);asset->SetAsset(path);}
        }
        // A model package contains two actual imported parts, not builtin cube
        // substitutions. Its file identity survives private view recreation.
        AxleMeshSource imported;imported.Name="Imported octahedron";imported.Vertices={{0,1,0},{1,0,0},{0,0,1},{-1,0,0},{0,0,-1},{0,-1,0}};
        imported.Indices={0,2,1,0,3,2,0,4,3,0,1,4,5,1,2,5,2,3,5,3,4,5,4,1};
        imported.BoundingSize={2,2,2};imported.HalfExtent={1,1,1};imported.ColorPrimary=imported.ColorSecondary={1,0,0,1};
        auto meshPath=root/"part.axlemesh";Require(AssetSerializer::SaveToAxleText(meshPath,imported),"imported source fixture");
        std::vector<AxlePackagePart> parts(2);parts[0].Name="Left";parts[1].Name="Right";
        for(size_t i=0;i<parts.size();++i){parts[i].RelativePath="part.axlemesh";parts[i].LocalTranslation={i?1.2f:-1.2f,0,0};}
        auto packagePath=root/"model.axle";Require(AssetSerializer::SavePackageManifest(packagePath,"Imported model",parts,{4.4f,2,2},{2.2f,1,1}),"imported multi-part package");
        auto importedPreview=std::make_unique<AssetPreviewPanel>();importedPreview->InitRHI(&ui);importedPreview->SetAsset(packagePath);Require(importedPreview->HasAsset(),"model package is not misclassified as an Assembly");
        ui.Begin();ImGui::SetNextWindowSize({480,360},ImGuiCond_Always);ImGui::Begin("Imported preview");importedPreview->RenderPreviewViewport();ImGui::End();
        auto importedWorld=ExtractRenderWorld(*importedPreview->GetPreviewScene());Require(importedWorld.Meshes.size()==2&&importedWorld.Meshes[0].Source&&importedWorld.Meshes[1].Source&&
            importedWorld.Meshes[0].Source->Vertices.size()==6,"all imported geometry retained as immutable CPU source");
        auto importedPixels=native?Axle::RenderTests::ReadTexture(*importedPreview->GetRHIViewport()->GetColorTexture()):ReadGL(importedPreview->GetGLFramebuffer());Require(Red(importedPixels)>80,"actual imported multi-part preview silhouette");
        importedPreview.reset();Finish(ui,native);
        // Raw images and deferred 3D work both respect the configured bounds.
        AssetThumbnailManager manager;manager.SetImGuiLayer(&ui);
        for(int i=0;i<270;++i){auto pending=root/("pending"+std::to_string(i)+".axle");std::filesystem::copy_file(path,pending);manager.GetOrCreateThumbnail(pending);}
        Require(manager.GetPendingCount()==256,"thumbnail pending work is bounded");manager.OnUpdate();Require(manager.GetCachedCount()==6&&manager.GetPendingCount()==250,"thumbnail frame work is bounded to six jobs");manager.Clear();
        for(int i=0;i<260;++i){auto image=root/("raw"+std::to_string(i)+".tga");std::ofstream file(image,std::ios::binary);char header[18]{};header[2]=2;header[12]=header[14]=1;header[16]=32;file.write(header,18);const char color[4]={0,0,char(255),char(255)};file.write(color,4);file.close();Require(manager.GetOrCreateThumbnail(image)!=0,"raw thumbnail fixture");}
        Require(manager.GetCachedCount()<=256,"raw thumbnail memory is bounded too");manager.Clear();
        manager.GetOrCreateThumbnail(packagePath);manager.OnUpdate();Require(manager.GetOrCreateThumbnail(packagePath)!=0,"imported model thumbnail");
        const auto meshStamp=std::filesystem::last_write_time(meshPath);imported.Vertices[0].y=2;
        Require(AssetSerializer::SaveToAxleText(meshPath,imported),"nested mesh edit");std::filesystem::last_write_time(meshPath,meshStamp+std::chrono::seconds(2));
        Require(manager.GetOrCreateThumbnail(packagePath)==0,"nested source revision invalidates unchanged package thumbnail");manager.OnUpdate();Require(manager.GetOrCreateThumbnail(packagePath)!=0,"nested source rerenders");manager.Clear();
        auto materialPath=root/"material.axleasset";Material material("Revision fixture");material.SetAlbedoColor({1,0,0,1});Require(material.Save(materialPath),"material thumbnail fixture");
        auto stamp=std::filesystem::last_write_time(materialPath);manager.GetOrCreateThumbnail(materialPath);manager.OnUpdate();auto first=manager.GetOrCreateThumbnail(materialPath);Require(first,"material thumbnail");
        material.SetAlbedoColor({0,1,0,1});Require(material.Save(materialPath),"material edit");std::filesystem::last_write_time(materialPath,stamp+std::chrono::seconds(2));
        Require(manager.GetOrCreateThumbnail(materialPath)==0,"source edit invalidates thumbnail");manager.OnUpdate();Require(manager.GetOrCreateThumbnail(materialPath)!=0,"edited thumbnail renders again");
        std::filesystem::remove(materialPath);Require(manager.GetOrCreateThumbnail(materialPath)==0,"source deletion cannot retain a stale thumbnail");manager.Clear();
        RenderService::Get().Shutdown();
    }
    RHIMeshRegistry::Get().Shutdown();Require(RHIDevice::Get().Shutdown(),"preview device shutdown");return true;
}
static bool Physics(const std::string& mode) {
    bool native=mode!="gl";auto backend=mode=="vulkan"?RHIType::Vulkan:RHIType::Direct3D12;
    std::unique_ptr<Window> window;if(native){RHIDeviceDesc desc;desc.PreferredBackend=backend;desc.Headless=true;desc.AllowFallback=false;desc.EnableValidation=desc.EnableNRIValidation=true;Require(RHIDevice::Get().Init(desc),"physics view device");}
    else{WindowProps props;props.Width=props.Height=128;props.Maximized=false;window=std::make_unique<Window>(props);Require(RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(glfwGetProcAddress)),"physics GL context");}
    PhysicsEngine::Init();
    {
        PhysicsScene edit,play;edit.CreateBox({-0.5f,0,0},{0.4f,0.4f,0.4f},false);play.CreateSphere({0.6f,0,0},0.4f,false);
        RenderService service;RenderWorld empty;ViewRequest view;view.Width=view.Height=128;view.Settings.EnableSkybox=view.Settings.EnableShadows=false;view.Settings.ClearColor={0,0,0,1};view.Settings.ShowPhysicsDebug=true;
        view.Camera=PerspectiveCamera(45,1,0.1f,100);view.Camera.SetViewMatrix(glm::lookAt(glm::vec3(0,0,3),glm::vec3(0),glm::vec3(0,1,0)),{0,0,3});
        if(!native)view.GLTargetFBO=Framebuffer::Create({128,128,1,false});
        auto render=[&](PhysicsScene* active,float alpha){view.PhysicsScene=active;view.Settings.PhysicsAlphaMultiplier=alpha;Require(service.RenderView(empty,view).Success,"physics-only view through service");return native?Axle::RenderTests::ReadTexture(*service.GetDefaultMainColorTarget()):ReadGL(view.GLTargetFBO);};
        auto blank=render(nullptr,1),edited=render(&edit,1),played=render(&play,1),paused=render(&play,1),faded=render(&play,0),stopped=render(&edit,1),closed=render(nullptr,1);
        Require(edited!=blank&&played!=blank&&edited!=played,"active edit/play PhysicsScene changes actual debug line pixels without GL meshes");
        Require(paused==played&&stopped==edited&&closed==blank&&faded==blank,"physics pause/stop/scene close and alpha isolation");
        service.Shutdown();
    }
    RHIMeshRegistry::Get().Shutdown();Require(RHIDevice::Get().Shutdown(),"physics view shutdown");return true;
}
static bool Surfaces(const std::string& mode) {
    const bool native=mode!="gl";const auto backend=mode=="vulkan"?RHIType::Vulkan:RHIType::Direct3D12;
    std::unique_ptr<Window> window;
    if(native){RHIDeviceDesc desc;desc.PreferredBackend=backend;desc.Headless=true;desc.AllowFallback=false;desc.EnableValidation=desc.EnableNRIValidation=true;Require(RHIDevice::Get().Init(desc),"surface probe device");}
    else{WindowProps props;props.Width=props.Height=128;props.Maximized=false;window=std::make_unique<Window>(props);Require(RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(glfwGetProcAddress)),"surface probe GL context");}
    {
        RenderService service;Scene scene;
        // Front entity is authored before back entity; alpha order cannot depend
        // on registry traversal order or the last queued material.
        auto front=scene.CreateEntity("Transparent front");front.AddComponent<MeshInstance3D>().AssetPath="builtin://cube";
        front.GetComponent<TransformComponent>().Scale=glm::vec3(0.8f);
        auto& material=front.AddComponent<MaterialComponent>();material.AlbedoColor={1,0,0,0.5f};material.EmissionColor={1,0,0};material.EmissionIntensity=1;material.SurfaceType=SurfaceType::Transparent;material.DepthWrite=false;
        auto back=scene.CreateEntity("Opaque background");back.AddComponent<MeshInstance3D>().AssetPath="builtin://cube";back.GetComponent<TransformComponent>().Translation={0,0,-1};back.GetComponent<TransformComponent>().Scale=glm::vec3(1.5f);
        auto& background=back.AddComponent<MaterialComponent>();background.AlbedoColor={0,0,1,1};background.EmissionColor={0,0,1};background.EmissionIntensity=1;
        ViewRequest view;view.Width=view.Height=128;view.Settings.EnableSkybox=view.Settings.EnableShadows=false;view.Settings.ClearColor={0,0,0,1};
        view.Camera=PerspectiveCamera(45,1,0.1f,100);view.Camera.SetViewMatrix(glm::lookAt(glm::vec3(0,0,3),glm::vec3(0),glm::vec3(0,1,0)),{0,0,3});
        if(!native)view.GLTargetFBO=Framebuffer::Create({128,128,1,false});
        auto render=[&](){Require(service.RenderView(scene,view).Success,"surface view through production service");return native?Axle::RenderTests::ReadTexture(*service.GetDefaultMainColorTarget()):ReadGL(view.GLTargetFBO);};
        auto alpha=render();const size_t center=(64*128+64)*4;
        Require(alpha[center]>40&&alpha[center+2]>40,"transparent surface blends front red and opaque blue");
        material.SurfaceType=SurfaceType::Additive;auto additive=render();Require(additive[center]>=alpha[center]&&additive[center+2]>=alpha[center+2],"additive surface preserves destination light");
        material.SurfaceType=SurfaceType::Cutout;material.AlbedoColor.a=0.2f;material.AlphaCutoff=0.5f;material.DepthWrite=true;
        auto cutout=render();Require(cutout[center+2]>cutout[center]+40,"cutout discards below authored threshold");
        material.SurfaceType=SurfaceType::Opaque;material.AlbedoColor.a=1;auto opaque=render();Require(opaque[center]>opaque[center+2]+40,"opaque surface replaces the background");
        // Private-light changes must affect pixels, including the GL key/fill rig.
        material.AlbedoColor={1,1,1,1};material.EmissionIntensity=0;background.EmissionIntensity=0;
        auto fill=scene.CreateEntity("Fill");auto& lamp=fill.AddComponent<DirectionalLightComponent>();lamp.Intensity=0;lamp.AmbientIntensity=0;lamp.CastShadows=false;lamp.Azimuth=0;lamp.Elevation=10;
        auto key=scene.CreateEntity("Dark key");auto& dark=key.AddComponent<DirectionalLightComponent>();dark.Intensity=0;dark.AmbientIntensity=0;dark.CastShadows=false;
        auto unlit=render();lamp.Intensity=3;auto lit=render();Require(lit!=unlit,"second directional preview light changes actual pixels");
        scene.DestroyEntity(fill);scene.DestroyEntity(key);
        const auto beforePrivate=render();
        RenderWorld privateWorld=ExtractRenderWorld(scene);privateWorld.DirectionalLight=RenderDirectionalLight{};
        privateWorld.DirectionalLight->Direction={-1,-1,-1};privateWorld.DirectionalLight->Intensity=4;
        ViewRequest privateView=view;privateView.Kind=RenderViewKind::AssetPreview;
        auto privateTargets=std::make_shared<RHIViewTargets>();
        if(native){Require(privateTargets->Configure(RHIViewTargetDesc{128,128}),"private lighting targets");privateView.ViewTargets=privateTargets;}
        Require(service.RenderView(privateWorld,privateView).Success,"private light view");
        Require(render()==beforePrivate,"no-light main view is independent of preceding private-view direction");

        service.Shutdown();
    }
    RHIMeshRegistry::Get().Shutdown();Require(RHIDevice::Get().Shutdown(),"surface device shutdown");return true;
}

static PFNGLCREATETEXTURESPROC originalCreateTextures=nullptr;
static unsigned extractionTextures=0;
static void APIENTRY CountTextures(GLenum target,GLsizei count,GLuint* textures) {
    extractionTextures+=count;originalCreateTextures(target,count,textures);
}
static bool Particles(const std::string& mode) {
    const bool native=mode!="gl";const auto backend=mode=="vulkan"?RHIType::Vulkan:RHIType::Direct3D12;
    std::unique_ptr<Window> window;
    if(native){RHIDeviceDesc desc;desc.PreferredBackend=backend;desc.Headless=true;desc.AllowFallback=false;desc.EnableValidation=desc.EnableNRIValidation=true;Require(RHIDevice::Get().Init(desc),"particle device");}
    else{WindowProps props;props.Width=960;props.Height=640;props.Maximized=false;window=std::make_unique<Window>(props);Require(RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(glfwGetProcAddress)),"particle GL context");
        glEnable(GL_DEBUG_OUTPUT);glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);glDebugMessageCallback([](GLenum,GLenum type,GLuint,GLenum,GLsizei,const GLchar* message,const void*){if(type==GL_DEBUG_TYPE_ERROR)std::cerr<<"[GL API Error] "<<message<<'\n';},nullptr);}
    {
        auto root=std::filesystem::absolute("build/agent-runs/OPT-05/particle-fixtures")/(mode+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root);
        for(int i=0;i<2;++i){std::ofstream file(root/(i?"green.tga":"red.tga"),std::ios::binary);char header[18]{};header[2]=2;header[12]=header[14]=1;header[16]=32;file.write(header,18);const char color[4]={0,char(i?255:0),char(i?0:255),char(255)};file.write(color,4);}
        struct AssetRootScope { std::filesystem::path Previous=FileSystem::GetAssetDirectory(); ~AssetRootScope(){FileSystem::SetAssetDirectory(Previous);} } assetRoot;
        FileSystem::SetAssetDirectory(root);ImageTextureCache::SetSourceResolver(LoadImageSource);
        auto materialPath=root/"green.axleasset";Material green("Green particle");green.SetAlbedoPath((root/"green.tga").generic_string());green.SetUseAlbedoMap(true);green.SetSurfaceType(SurfaceType::Cutout);Require(green.Save(materialPath),"textured particle material fixture");
        Scene scene;auto entity=scene.CreateEntity("Local emitters");auto& emitter=entity.AddComponent<ParticleEmitterComponent>();emitter.EmitterStacks.clear();
        for(int i=0;i<2;++i){auto config=ParticleEmitterConfig::CreateNeutral();config.EmissionRate=0;config.MaxParticles=4;config.SimulationSpace=ParticleSimulationSpace::Local;config.StartSize=config.EndSize={0.8f,0.8f};config.InitialSpeed={0,0};config.StartColor=glm::vec4(1);config.SoftnessDistance=0;
            ParticleEmitterStack stack(i?"Green material":"Red texture",config);stack.LocalTransform.Translation={i?0.65f:-0.65f,0,0};stack.UseTexture=true;stack.TexturePath=(root/"red.tga").generic_string();if(i)stack.MaterialPath=materialPath.generic_string();
            ParticleSpawnData spawn;spawn.StartSize=spawn.EndSize=0.8f;spawn.Lifetime=10;spawn.StartColor=spawn.EndColor=glm::vec4(1);stack.System->GetPool().Spawn(spawn);emitter.EmitterStacks.push_back(std::move(stack));}
        // The only GL allocation hook surrounds extraction, never resolution.
        if(!native){originalCreateTextures=glad_glCreateTextures;extractionTextures=0;glad_glCreateTextures=CountTextures;}
        auto world=ExtractRenderWorld(scene);
        if(!native){glad_glCreateTextures=originalCreateTextures;Require(extractionTextures==0,"textured material extraction performs zero GPU texture allocations");}
        Require(world.Particles.size()==2&&world.Particles[1]->MaterialResolved&&world.Particles[1]->Material.UseTexture,"owned effective particle material metadata");
        auto& service=RenderService::Get();ViewRequest view;view.Width=view.Height=128;view.Settings.EnableSkybox=view.Settings.EnableShadows=false;view.Settings.ClearColor={0,0,0,1};
        view.Camera=PerspectiveCamera(45,1,0.1f,100);view.Camera.SetViewMatrix(glm::lookAt(glm::vec3(0,0,3),glm::vec3(0),glm::vec3(0,1,0)),{0,0,3});if(!native)view.GLTargetFBO=Framebuffer::Create({128,128,1,false});
        auto render=[&](){Require(service.RenderView(scene,view).Success,"particle production service");return native?Axle::RenderTests::ReadTexture(*service.GetDefaultMainColorTarget()):ReadGL(view.GLTargetFBO);};
        auto greenCount=[](const auto& pixels){size_t n=0;for(size_t i=0;i+3<pixels.size();i+=4)if(pixels[i+1]>pixels[i]+30&&pixels[i+1]>pixels[i+2]+30)++n;return n;};
        auto pixels=render();Require(Red(pixels)>100&&greenCount(pixels)>100,"local multi-emitter texture and referenced material pixels");
        size_t leftRed=0,rightGreen=0;for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x){auto i=(y*128+x)*4;if(x<64&&pixels[i]>pixels[i+1]+30)++leftRed;if(x>=64&&pixels[i+1]>pixels[i]+30)++rightGreen;}
        Require(leftRed>100&&rightGreen>100,"emitter local transforms place independent textures on opposite sides");
        emitter.EmitterStacks[0].UseTexture=false;auto disabled=render();Require(Red(disabled)==0&&greenCount(disabled)>100,"authored emitter texture toggle is respected");emitter.EmitterStacks[0].UseTexture=true;
        // A close opaque surface must fade the intersecting particle using
        // frozen opaque depth, including when another emitter writes cutout depth.
        auto occluder=scene.CreateEntity("Soft intersection");occluder.AddComponent<MeshInstance3D>().AssetPath="builtin://cube";
        occluder.GetComponent<TransformComponent>().Translation={-0.65f,0,-0.4f};occluder.GetComponent<TransformComponent>().Scale={0.8f,0.8f,0.5f};
        occluder.AddComponent<MaterialComponent>().AlbedoColor={0,0,0,1};
        const auto hard=render();
        emitter.EmitterStacks[0].Config.SoftnessDistance=emitter.EmitterStacks[1].Config.SoftnessDistance=0.8f;
        const auto soft=render();
        auto redEnergy=[](const auto& image){uint64_t sum=0;for(size_t i=0;i+3<image.size();i+=4)if(image[i]>image[i+1]+10)sum+=image[i];return sum;};
        Require(redEnergy(soft)<redEnergy(hard)&&redEnergy(soft)>0,"soft particles read copied opaque depth and fade actual intersections");
        scene.DestroyEntity(occluder);
        ImGuiLayer ui(native?(backend==RHIType::Vulkan?ImGuiBackendType::Vulkan:ImGuiBackendType::Direct3D12):ImGuiBackendType::OpenGL);ui.Init(window?window->GetNativeWindow():nullptr);ImGui::GetIO().DisplaySize={960,640};ImGui::GetIO().DeltaTime=1.0f/60;
        auto panel=std::make_unique<VFXEditorPanel>();panel->InitRHI(&ui);panel->SetSceneContext(&scene);panel->OpenEntityEffect(entity);panel->PausePreview();
        for(size_t i=0;i<2;++i){panel->SelectEmitterStack(i);panel->TriggerBurst(1);}panel->FramePreview();
        ui.Begin();ImGui::SetNextWindowSize({940,600},ImGuiCond_Always);bool open=true;panel->OnImGuiRender(&open);
        auto preview=native?Axle::RenderTests::ReadTexture(*panel->GetRHIViewport()->GetColorTexture()):ReadGL(panel->GetGLFramebuffer());
        Require(Red(preview)>30&&greenCount(preview)>30,"actual VFX Editor private preview resolves all emitter resources");
        Require(emitter.EmitterStacks[0].System->GetActiveParticleCount()==1&&emitter.EmitterStacks[1].System->GetActiveParticleCount()==1,"private preview never simulates source scene emitters");panel.reset();Finish(ui,native);service.Shutdown();RenderService::Get().Shutdown();
    }
    RHIMeshRegistry::Get().Shutdown();Require(RHIDevice::Get().Shutdown(),"particle device shutdown");return true;
}

int main(int argc,char** argv) {
    Axle::RenderTests::ConfigureFailureReporting();Log::Init();JobSystem::Init(2);bool passed=false;
    try{passed=argc>2&&std::string(argv[2])=="particles"?Particles(argv[1]):argc>2&&std::string(argv[2])=="surfaces"?Surfaces(argv[1]):argc>2&&std::string(argv[2])=="physics"?Physics(argv[1]):Run(argc>1?argv[1]:"dx12");}catch(const std::exception& error){std::cerr<<"OPT05 preview FAIL: "<<error.what()<<'\n';}
    RenderService::Get().Shutdown();RHIMeshRegistry::Get().Shutdown();RHIDevice::Get().Shutdown();PhysicsEngine::Shutdown();JobSystem::Shutdown();
    if(GetRHIValidationErrorCount())passed=false;std::cout<<"OPT05 preview "<<(passed?"PASS":"FAIL")<<'\n';return passed?0:1;
}
