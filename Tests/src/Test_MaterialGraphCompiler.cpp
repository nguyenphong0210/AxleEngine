#include "TestWorkspace.hpp"
#include <Axle/MaterialGraphProgram.hpp>
#include <Axle/MaterialSurfaceShader.hpp>
#include <Axle/CustomMaterialProgram.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/Package.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

#define CHECK(value) do { if(!(value)) {std::cerr<<"FAIL "<<__LINE__<<": " #value "\n";return false;} } while(0)
namespace {
    using namespace Axle;
    using namespace Axle::RHI;
    std::string Fixture;
    std::filesystem::path Workspace;

    // Tiny deterministic fixture/capture encoder: PNG RGBA8, filter 0, uncompressed
    // DEFLATE blocks. Uses production CRC32 and atomic writes, no new vendor library.
    bool WritePNG(const std::filesystem::path& path,uint32_t width,uint32_t height,const std::vector<uint8_t>& rgba) {
        if(!width||!height||uint64_t(width)*height*4!=rgba.size())return false;
        std::vector<uint8_t> png{137,80,78,71,13,10,26,10};
        auto big=[](std::vector<uint8_t>& bytes,uint32_t value){for(int shift:{24,16,8,0})bytes.push_back(static_cast<uint8_t>(value>>shift));};
        auto chunk=[&](const char* type,const std::vector<uint8_t>& data) {
            big(png,static_cast<uint32_t>(data.size()));const auto start=png.size();
            png.insert(png.end(),type,type+4);png.insert(png.end(),data.begin(),data.end());
            big(png,PackageWriter::CalculateCRC32(png.data()+start,png.size()-start));
        };
        std::vector<uint8_t> header;big(header,width);big(header,height);header.insert(header.end(),{8,6,0,0,0});chunk("IHDR",header);
        std::vector<uint8_t> raw;
        for(uint32_t y=0;y<height;++y){raw.push_back(0);raw.insert(raw.end(),rgba.begin()+size_t(y)*width*4,rgba.begin()+size_t(y+1)*width*4);}
        std::vector<uint8_t> zlib{0x78,0x01};
        for(size_t offset=0;offset<raw.size();) {
            const auto length=static_cast<uint16_t>(std::min<size_t>(65535,raw.size()-offset));const auto inverse=static_cast<uint16_t>(~length);
            zlib.push_back(offset+length==raw.size()?1:0);
            zlib.insert(zlib.end(),{static_cast<uint8_t>(length),static_cast<uint8_t>(length>>8),static_cast<uint8_t>(inverse),static_cast<uint8_t>(inverse>>8)});
            zlib.insert(zlib.end(),raw.begin()+offset,raw.begin()+offset+length);offset+=length;
        }
        uint32_t a=1,b=0;for(auto value:raw){a=(a+value)%65521;b=(b+a)%65521;}big(zlib,(b<<16)|a);
        chunk("IDAT",zlib);chunk("IEND",{});
        return FileSystem::WriteBinaryFileAtomically(path,png.data(),png.size());
    }

    MaterialGraphDocument Sample() {
        MaterialGraphDocument doc;MaterialGraphDiagnostic error;
        if(!MaterialGraph::Parse(Fixture,doc,&error))throw std::runtime_error(error.Code+": "+error.Message);
        return doc;
    }
    std::string& Property(MaterialGraphNode& node,const std::string& name,bool attribute=false) {
        auto& properties=attribute?node.Attributes:node.Properties;
        for(auto& p:properties)if(p.Name==name)return std::get<std::string>(p.Value);
        throw std::runtime_error("Missing fixture property");
    }
    bool Compile(const MaterialGraphDocument& doc,RHIShaderFormat format,CompiledMaterialGraphProgram& program) {
        MaterialGraphDiagnostic error;
        if(!MaterialGraphProgram::Compile(doc,format,program,&error)) {std::cerr<<error.Code<<" node "<<error.NodeID<<": "<<error.Message<<"\n";return false;}
        return true;
    }
    bool CompilerPolicy() {
        CHECK(!RHIDevice::Get().IsInitialized());
        auto sample=Sample();MaterialGraphDiagnostic error;
        for(auto format:{RHIShaderFormat::DXIL,RHIShaderFormat::SPIRV}) {
            CompiledMaterialGraphProgram program;CHECK(Compile(sample,format,program));
            CHECK(program.Vertex.IsValid()&&program.Fragment.IsValid());
            CHECK(program.Vertex.Format==format&&program.Fragment.Format==format);
            CHECK(program.Graph.Parameters.size()==5&&program.Graph.Textures.size()==2&&program.DefaultProperties.size()==7);
            CHECK(program.Reflection.FindConstantBuffer("EngineObject")&&program.Reflection.FindConstantBuffer("MaterialParameters"));
            const auto declared=ShaderPropertyParser::ParseFromHLSL(program.HLSL,&program.Reflection);
            const auto plan=BuildMaterialBindingPlan(program.Reflection,{},declared);
            CHECK(std::holds_alternative<MaterialBindingPlan>(plan));
            const auto& bindings=std::get<MaterialBindingPlan>(plan);
            CHECK(bindings.Textures.size()==2&&bindings.Samplers.size()==2);
            for(const auto& texture:bindings.Textures)CHECK(texture.Role==TextureRole::Color);
            for(const auto& sampler:bindings.Samplers)CHECK(sampler.Policy==ShaderSamplerPolicy::LinearRepeat);
            uint32_t magic=0;std::memcpy(&magic,program.Fragment.Data.data(),sizeof(magic));
            CHECK(format==RHIShaderFormat::SPIRV?magic==0x07230203:magic==0x43425844);
        }
        MaterialHLSLSource original;CHECK(MaterialGraphHLSL::Lower(sample,original));
        auto moved=sample;for(auto& node:moved.Nodes)node.EditorPosition={1,2};moved.Name="Renamed";
        std::reverse(moved.Nodes.begin(),moved.Nodes.end());std::reverse(moved.Connections.begin(),moved.Connections.end());
        MaterialHLSLSource second;CHECK(MaterialGraphHLSL::Lower(moved,second));
        CHECK(original.Declarations==second.Declarations&&original.SurfaceFunction==second.SurfaceFunction&&original.SemanticKey==second.SemanticKey);
        moved=sample;moved.Parameters[2].Default.Components[0]=0.125f;
        CHECK(MaterialGraphHLSL::Lower(moved,second));
        CHECK(original.SurfaceFunction==second.SurfaceFunction&&original.Declarations==second.Declarations&&original.SemanticKey!=second.SemanticKey);
        const auto malicious=std::string("name\"; return hacked; //\n");
        sample.Parameters[1].Name=malicious;Property(sample.Nodes[1],"parameter",true)=malicious;
        CompiledMaterialGraphProgram renamed;CHECK(Compile(sample,RHIShaderFormat::DXIL,renamed));
        CHECK(renamed.HLSL.find("hacked")==std::string::npos);
        CHECK(std::any_of(renamed.Graph.Parameters.begin(),renamed.Graph.Parameters.end(),[&](const auto& p){return p.Name==malicious;}));
        auto invalid=Sample();invalid.Connections[0].From.NodeID=99999;
        renamed.HLSL="previous";renamed.Vertex.Data={1,2,3};
        CHECK(!MaterialGraphProgram::Compile(invalid,RHIShaderFormat::DXIL,renamed,&error));
        CHECK(error.Code=="MG_ENDPOINT"&&renamed.HLSL=="previous"&&renamed.Vertex.Data==std::vector<uint8_t>({1,2,3}));
        CHECK(!MaterialGraphProgram::Compile(Sample(),RHIShaderFormat::DXBC,renamed,&error)&&error.Code=="MG_SHADER_FORMAT");
        CHECK(renamed.HLSL=="previous");
        auto sampler=Sample();Property(sampler.Nodes[3],"sampler")="PointClamp";
        CHECK(!MaterialGraphProgram::Compile(sampler,RHIShaderFormat::DXIL,renamed,&error)&&error.Code=="MG_HLSL_SAMPLER"&&error.NodeID==4);
        auto splat=Sample();splat.Connections[9].From={10,"value"};CHECK(Compile(splat,RHIShaderFormat::SPIRV,renamed));
        std::cout<<"PASS DXIL/SPIR-V compilation, metadata, determinism, hostile names, rollback, sampler rejection and splats\n";
        CHECK(!RHIDevice::Get().IsInitialized());return true;
    }

    bool RegistryPolicy() {
        auto definitions=MaterialNodeRegistry::Builtins();auto lowerings=MaterialHLSLRegistry::Builtins();
        MaterialNodeDefinition definition;definition.Type="test.Saturate";
        definition.Signatures={{{{"color",MaterialValueType::Float3,MaterialValue{MaterialValueType::Float3,{0.2f,0.4f,0.6f}}}},{{"value",MaterialValueType::Float3,{}}}}};
        CHECK(definitions.Register(definition));
        auto doc=Sample();MaterialGraphNode node;node.ID=200;node.Type=definition.Type;node.Source={500,41,3};
        node.Inputs={{"color",MaterialValueType::Float3,MaterialValue{MaterialValueType::Float3,{0.2f,0.4f,0.6f}}, {}}};
        node.Outputs={{"value",MaterialValueType::Float3,{},{}}};doc.Nodes.push_back(node);doc.Connections[9].From={200,"value"};
        MaterialGraphDiagnostic error;CompiledMaterialGraphProgram program;program.HLSL="previous";
        CHECK(!MaterialGraphProgram::Compile(doc,RHIShaderFormat::DXIL,program,&error,definitions,lowerings));
        CHECK(error.Code=="MG_HLSL_NODE"&&error.NodeID==200&&program.HLSL=="previous");
        MaterialHLSLNodeLowering lowering;lowering.Type=definition.Type;
        lowering.Emit=[](const auto& context,auto& output,auto&){output.Outputs["value"]="saturate("+context.Inputs.at("color")+")";return true;};
        CHECK(lowerings.Register(lowering));CHECK(!lowerings.Register(lowering,&error)&&error.Code=="MG_HLSL_DEFINITION");
        for(auto format:{RHIShaderFormat::DXIL,RHIShaderFormat::SPIRV})
            CHECK(MaterialGraphProgram::Compile(doc,format,program,&error,definitions,lowerings));
        auto stale=MaterialHLSLRegistry::Builtins();lowering.DefinitionRevision=2;CHECK(stale.Register(lowering));
        CHECK(!MaterialGraphProgram::Compile(doc,RHIShaderFormat::DXIL,program,&error,definitions,stale)&&error.Code=="MG_HLSL_REVISION");
        auto broken=MaterialHLSLRegistry::Builtins();lowering.DefinitionRevision=1;
        lowering.Emit=[](const auto&,auto& output,auto&){output.Outputs["value"]="missing_identifier";return true;};
        CHECK(broken.Register(lowering));program.HLSL="previous";
        CHECK(!MaterialGraphProgram::Compile(doc,RHIShaderFormat::DXIL,program,&error,definitions,broken));
        CHECK((error.Code=="MG_DXC_VERTEX"||error.Code=="MG_DXC_FRAGMENT")&&error.NodeID==200&&program.HLSL=="previous");
        auto missingOutput=MaterialHLSLRegistry::Builtins();lowering.Emit=[](const auto&,auto&,auto&){return true;};CHECK(missingOutput.Register(lowering));
        CHECK(!MaterialGraphProgram::Compile(doc,RHIShaderFormat::DXIL,program,&error,definitions,missingOutput)&&error.Code=="MG_HLSL_OUTPUT");
        auto throwing=MaterialHLSLRegistry::Builtins();
        lowering.Emit=[](const MaterialHLSLNodeContext&,MaterialHLSLNodeEmission&,MaterialGraphDiagnostic&)->bool {throw std::runtime_error("emitter exception");};
        CHECK(throwing.Register(lowering));program.HLSL="previous";
        CHECK(!MaterialGraphProgram::Compile(doc,RHIShaderFormat::DXIL,program,&error,definitions,throwing));
        CHECK(error.Code=="MG_HLSL_EMIT"&&error.NodeID==200&&error.Source.Line==41&&program.HLSL=="previous");
        auto unknownException=MaterialHLSLRegistry::Builtins();
        lowering.Emit=[](const MaterialHLSLNodeContext&,MaterialHLSLNodeEmission&,MaterialGraphDiagnostic&)->bool {throw 7;};
        CHECK(unknownException.Register(lowering));
        CHECK(!MaterialGraphProgram::Compile(doc,RHIShaderFormat::DXIL,program,&error,definitions,unknownException)&&error.NodeID==200&&program.HLSL=="previous");
        std::cout<<"PASS custom registered lowering, revisions, DXC node diagnostics and malformed emitter rollback\n";
        return true;
    }

    std::string ParameterSymbol(const MaterialHLSLSource& source,const std::string& name) {
        for(const auto& parameter:source.Parameters)if(parameter.Name==name)return parameter.Symbol;
        throw std::runtime_error("Missing fixture parameter");
    }
    std::string Reference(const MaterialHLSLSource& source) {
        auto texture=[&](uint64_t id)->const MaterialHLSLTexture& {for(const auto& t:source.Textures)if(t.NodeID==id)return t;throw std::runtime_error("Missing fixture texture");};
        const auto& a=texture(4);const auto& b=texture(5);
        const std::string function="MaterialSurface AxleEvaluateMaterial(float2 uv) {\n"
            "float2 tiled=uv*"+ParameterSymbol(source,"UV_Tiling")+";\n"
            "float3 metal="+a.TextureSymbol+".Sample("+a.SamplerSymbol+",tiled).rgb;\n"
            "float3 rust="+b.TextureSymbol+".Sample("+b.SamplerSymbol+",tiled).rgb;\n"
            "MaterialSurface s; s.BaseColor=lerp(metal,rust,"+ParameterSymbol(source,"RustAmount")+")*"+ParameterSymbol(source,"BaseColorTint")+";\n"
            "s.Metallic="+ParameterSymbol(source,"BaseMetallic")+";s.Roughness="+ParameterSymbol(source,"BaseRoughness")+";\n"
            "s.Normal=float3(0,0,1);s.Emission=float3(0,0,0);s.AO=1;return s;}\n";
        return AssembleMaterialSurfaceShader(source.Declarations,function);
    }

    bool Native(RHIType backend) {
        JobSystem::Init(1);
        struct JobsShutdown {~JobsShutdown(){ScriptHotReloadManager::Get().Shutdown();JobSystem::Shutdown();}} jobs;
        CHECK(JobSystem::IsInitialized());
        const auto initialErrors=GetRHIValidationErrorCount();
        auto& device=RHIDevice::Get();RHIDeviceDesc init;init.PreferredBackend=backend;init.AllowFallback=false;init.Headless=true;
        init.EnableNRIValidation=true;init.EnableValidation=RHIDevice::IsGraphicsAPIValidationAvailable(backend);
        CHECK(device.Init(init)&&device.GetBackend()==backend&&!device.IsSoftwareAdapter());
        struct DeviceShutdown {~DeviceShutdown(){RHIDevice::Get().WaitIdle();RHIDevice::Get().Shutdown();}} shutdown;
        const auto assets=Workspace/"assets";std::filesystem::create_directories(assets/"textures");
        FileSystem::SetAssetDirectory(assets);
        std::vector<uint8_t> metal(8*8*4),rust(metal.size());
        for(size_t i=0;i<64;++i) {
            const bool alternate=((i%8)/2+(i/8)/2)%2!=0;
            const std::array<uint8_t,4> ma=alternate?std::array<uint8_t,4>{80,160,220,255}:std::array<uint8_t,4>{200,180,80,255};
            const std::array<uint8_t,4> ru=alternate?std::array<uint8_t,4>{220,55,30,255}:std::array<uint8_t,4>{80,20,10,255};
            std::copy(ma.begin(),ma.end(),metal.begin()+i*4);std::copy(ru.begin(),ru.end(),rust.begin()+i*4);
        }
        CHECK(WritePNG(assets/"textures/metal_base_d.png",8,8,metal));
        CHECK(WritePNG(assets/"textures/rust_d.png",8,8,rust));
        const auto format=backend==RHIType::Vulkan?RHIShaderFormat::SPIRV:RHIShaderFormat::DXIL;
        auto doc=Sample();CompiledMaterialGraphProgram program;CHECK(Compile(doc,format,program));
        const auto generated=assets/"generated.hlsl",reference=assets/"reference.hlsl";
        CHECK(FileSystem::WriteTextFileAtomically(generated,program.HLSL));CHECK(FileSystem::WriteTextFileAtomically(reference,Reference(program.Graph)));
        CHECK(FileSystem::WriteTextFileAtomically(Workspace/"baseline_graph.hlsl",program.HLSL));
        CHECK(FileSystem::WriteTextFileAtomically(Workspace/"baseline_reference.hlsl",Reference(program.Graph)));
        CHECK(program.Vertex.SaveToFile(Workspace/"graph.vs.bin")&&program.Fragment.SaveToFile(Workspace/"graph.ps.bin"));
        auto scene=std::make_shared<Scene>();auto entity=scene->CreateEntity("Graph fixture sphere");
        entity.AddComponent<MeshInstance3D>().RHIMeshAsset=RHIMeshRegistry::Get().GetSphere();
        auto& material=entity.AddComponent<MaterialComponent>();material.UseCustomShader=true;material.CustomShaderPath="generated.hlsl";material.CustomProperties=program.DefaultProperties;
        auto sun=scene->CreateEntity("Fixture sun");sun.AddComponent<DirectionalLightComponent>().CastShadows=false;scene->UpdateWorldTransforms();
        RHISceneRenderer renderer;CHECK(renderer.Init(device,false));
        constexpr uint32_t width=128;RHITexture color,depth;
        CHECK(color.Init(RHITextureDesc::RenderTarget2D(width,width))&&depth.Init(RHITextureDesc::DepthStencil2D(width,width)));
        RHIBuffer readback;RHIBufferDesc read;read.Size=width*width*4;read.MemoryLocation=RHIMemoryLocation::HostReadback;CHECK(readback.Init(read));
        RHICommandBuffer copy;CHECK(copy.Init());PerspectiveCamera camera(45,1);camera.SetPerspective(45,1);camera.SetPosition({0,0,4});
        SceneRenderSettings settings;settings.EnableSkybox=settings.EnableParticles=settings.EnableShadows=false;
        settings.EnableStudioLightLobes=false;settings.ClearColor={0,0,0,1};
        auto render=[&](const std::string& path)->std::vector<uint8_t> {
            material.CustomShaderPath=path;renderer.RenderScene(scene,camera,&color,&depth,settings);
            if(!renderer.WasLastRenderSuccessful()||material.CustomShaderGeneration==0||!material.CustomShaderDiagnostic.empty()) {
                std::cerr<<"Custom graph draw failed: "<<material.CustomShaderDiagnostic<<"\n";return {};
            }
            if(!copy.Reset()||!copy.Begin())return {};
            copy.TransitionTexture(color.GetNative(),RHIResourceLayout::ShaderResource,RHIResourceLayout::CopySource);
            nri::TextureDataLayoutDesc layout{};layout.rowPitch=width*4;layout.slicePitch=width*width*4;
            nri::TextureRegionDesc region{};region.width=region.height=width;region.depth=1;
            copy.ReadbackTextureToBuffer(*readback.GetNative(),layout,*color.GetNative(),region);
            copy.TransitionTexture(color.GetNative(),RHIResourceLayout::CopySource,RHIResourceLayout::ShaderResource);
            if(!copy.End()||!copy.Submit()||!device.WaitIdle())return {};
            const auto* data=readback.Map();if(!data)return {};
            std::vector<uint8_t> pixels(read.Size);std::memcpy(pixels.data(),data,pixels.size());readback.Unmap();return pixels;
        };
        const auto graph=render("generated.hlsl");CHECK(graph.size()==read.Size);
        const auto expected=render("reference.hlsl");CHECK(expected.size()==graph.size());
        unsigned maximumError=0;uint64_t energy=0;
        for(size_t i=0;i<graph.size();++i)if(i%4!=3) {maximumError=std::max(maximumError,static_cast<unsigned>(std::abs(int(graph[i])-int(expected[i]))));energy+=graph[i];}
        CHECK(maximumError<=1&&energy>10000);
        CHECK(WritePNG(Workspace/"graph.png",width,width,graph));
        CHECK(WritePNG(Workspace/"reference.png",width,width,expected));
        auto setFloat=[&](const char* name,float value) {const auto symbol=ParameterSymbol(program.Graph,name);for(auto& p:material.CustomProperties)if(p.Desc.Name==symbol){p.FloatVal=value;p.Vec4Val.x=value;}};
        setFloat("RustAmount",0);const auto bare=render("generated.hlsl");CHECK(!bare.empty()&&bare!=graph);
        setFloat("RustAmount",1);const auto rusted=render("generated.hlsl");CHECK(!rusted.empty()&&rusted!=bare);
        setFloat("BaseRoughness",0.95f);const auto rough=render("generated.hlsl");CHECK(!rough.empty()&&rough!=rusted);
        setFloat("BaseMetallic",0);const auto dielectric=render("generated.hlsl");CHECK(!dielectric.empty()&&dielectric!=rough);
        const auto uvSymbol=ParameterSymbol(program.Graph,"UV_Tiling");
        for(auto& p:material.CustomProperties)if(p.Desc.Name==uvSymbol)p.Vec4Val={1,1,0,0};
        const auto retiled=render("generated.hlsl");CHECK(!retiled.empty()&&retiled!=dielectric);
        const auto tintSymbol=ParameterSymbol(program.Graph,"BaseColorTint");
        for(auto& p:material.CustomProperties)if(p.Desc.Name==tintSymbol)p.Vec4Val={0.2f,0.8f,0.3f,0};
        const auto tinted=render("generated.hlsl");CHECK(!tinted.empty()&&tinted!=retiled);
        CHECK(WritePNG(Workspace/"tinted.png",width,width,tinted));
        for(auto& p:material.CustomProperties)if(p.Desc.Name==tintSymbol)p.Vec4Val={1,1,1,0};
        auto surfaceDefault=[&](const std::string& name,std::vector<float> value){for(auto& pin:doc.Nodes.back().Inputs)if(pin.Name==name)pin.Default->Components=std::move(value);};
        auto updateSource=[&](){return Compile(doc,format,program)&&FileSystem::WriteTextFileAtomically(generated,program.HLSL);};
        const auto normalBase=render("generated.hlsl");CHECK(!normalBase.empty());
        surfaceDefault("Normal",{0.5f,0,0.8660254f});CHECK(updateSource());
        const auto tilted=render("generated.hlsl");CHECK(!tilted.empty()&&tilted!=normalBase);
        surfaceDefault("AO",{0});CHECK(updateSource());
        const auto noAO=render("generated.hlsl");CHECK(!noAO.empty()&&noAO!=tilted);
        surfaceDefault("Emission",{0.1f,0.05f,0.0f});CHECK(updateSource());
        const auto emissive=render("generated.hlsl");CHECK(!emissive.empty()&&emissive!=noAO);
        CHECK(WritePNG(Workspace/"emissive.png",width,width,emissive));

        // Independent numeric oracle, rather than a second shader sharing graph logic:
        // hardware sRGB decodes texels BEFORE bilinear filtering. Both midpoint and
        // uniform-gray checks are needed to distinguish no-decode and late-decode.
        auto oracle=MaterialGraph::Create("sRGB filtering oracle");
        for(auto& pin:oracle.Nodes[0].Inputs) {
            if(pin.Name=="BaseColor")pin.Default->Components={0,0,0};
            if(pin.Name=="Metallic")pin.Default->Components={1};
        }
        auto sampleNode=Sample().Nodes[3];sampleNode.ID=2;
        sampleNode.Inputs[0].Default->Components={0.5f,0.5f};Property(sampleNode,"texturePath")="textures/filter.png";
        oracle.Nodes.push_back(sampleNode);oracle.Connections.push_back({{2,"rgb"},{1,"Emission"},{}});
        sun.GetComponent<DirectionalLightComponent>().Intensity=0;
        CHECK(WritePNG(assets/"textures/filter.png",2,1,{0,0,0,255,255,255,255,255}));
        CompiledMaterialGraphProgram oracleProgram;CHECK(Compile(oracle,format,oracleProgram));
        const auto filterSource=assets/"filter.hlsl";CHECK(FileSystem::WriteTextFileAtomically(filterSource,oracleProgram.HLSL));
        material.CustomProperties=oracleProgram.DefaultProperties;
        const auto filtered=render("filter.hlsl");CHECK(!filtered.empty());
        const size_t center=(width/2*width+width/2)*4;
        CHECK(std::abs(int(filtered[center])-128)<=1);
        CHECK(WritePNG(Workspace/"srgb_midpoint.png",width,width,filtered));
        CHECK(WritePNG(assets/"textures/gray.png",2,1,{128,128,128,255,128,128,128,255}));
        Property(oracle.Nodes.back(),"texturePath")="textures/gray.png";
        CHECK(Compile(oracle,format,oracleProgram));CHECK(FileSystem::WriteTextFileAtomically(filterSource,oracleProgram.HLSL));
        material.CustomProperties=oracleProgram.DefaultProperties;
        const auto srgbGray=render("filter.hlsl");CHECK(!srgbGray.empty()&&std::abs(int(srgbGray[center])-55)<=1);
        CHECK(WritePNG(Workspace/"srgb_gray.png",width,width,srgbGray));
        Property(oracle.Nodes.back(),"colorSpace")="Linear";
        CHECK(Compile(oracle,format,oracleProgram));CHECK(FileSystem::WriteTextFileAtomically(filterSource,oracleProgram.HLSL));
        material.CustomProperties=oracleProgram.DefaultProperties;
        const auto linearGray=render("filter.hlsl");CHECK(!linearGray.empty()&&std::abs(int(linearGray[center])-128)<=1);
        CHECK(WritePNG(Workspace/"linear_gray.png",width,width,linearGray));
        std::ofstream measurements(Workspace/"measurements.txt");measurements<<"backend="<<RHITypeToString(backend)<<"\nsize=128x128\nreference_max_error="<<maximumError<<"\nrgb_energy="<<energy<<"\nparameter_changes=6\nsurface_changes=3\nsrgb_midpoint="<<int(filtered[center])<<"\nsrgb_gray="<<int(srgbGray[center])<<"\nlinear_gray="<<int(linearGray[center])<<"\n";measurements.close();CHECK(bool(measurements));
        CHECK(device.WaitIdle());renderer.Shutdown();scene.reset();RHIMeshRegistry::Get().Shutdown();MaterialCache::ClearAll();
        CHECK(GetRHIValidationErrorCount()==initialErrors);
        std::cout<<"PASS native "<<RHITypeToString(backend)<<" reference image max error "<<maximumError<<", all five parameters and three surface defaults, no fallback\n";
        return true;
    }
}

int main(int argc,char** argv) {
    if(argc!=4)return 2;
    Axle::Tests::InitializeWorkspace();
    Fixture=FileSystem::ReadTextFile(std::filesystem::u8path(argv[2]));
    Workspace=std::filesystem::u8path(argv[3])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(Workspace);
    try {
        const std::string mode=argv[1];bool success=false;
        if(mode=="policy")success=CompilerPolicy()&&RegistryPolicy();
        else if(mode=="dx12")success=Native(RHIType::Direct3D12);
        else if(mode=="vulkan")success=Native(RHIType::Vulkan);
        else return 2;
        if(mode!="policy"&&GetRHIValidationErrorCount()!=0)success=false;
        RHIShaderCompiler::Get().Shutdown();return success?0:1;
    }catch(const std::exception& e){std::cerr<<"FAIL exception "<<e.what()<<"\n";return 1;}
}
