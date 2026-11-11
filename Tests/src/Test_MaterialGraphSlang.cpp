#include "TestWorkspace.hpp"
#include "MaterialGraphTestDraw.hpp"
#include <Axle/FileSystem.hpp>
#include <Axle/Package.hpp>
#include <Axle/RHI/RHISlangCompiler.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#define CHECK(v) do{if(!(v)){std::cerr<<"FAIL "<<__LINE__<<": " #v "\n";return false;}}while(0)
namespace {
using namespace Axle;
using namespace Axle::RHI;
MaterialGraphDocument Fixture;
std::filesystem::path Workspace;
bool Compile(const MaterialGraphDocument& document,RHIShaderFormat format,MaterialGraphCompilerBackend backend,
             CompiledMaterialGraphProgram& result) {
    MaterialGraphDiagnostic error;
    if(MaterialGraphProgram::Compile(document,format,result,&error,MaterialNodeRegistry::Builtins(),MaterialHLSLRegistry::Builtins(),backend))return true;
    std::cerr<<error.Code<<" node"<<error.NodeID<<": "<<error.Message<<"\n";return false;
}
bool Selection() {
    CHECK(RHISlangCompiler::IsEnabled()==bool(AXLE_EXPECT_SLANG));
#ifdef _WIN32
    CHECK(!GetModuleHandleW(L"slang-compiler.dll"));
#endif
    auto doc=MaterialGraph::Create("Default DXC independence");
    CompiledMaterialGraphProgram dxc;CHECK(MaterialGraphProgram::Compile(doc,RHIShaderFormat::DXIL,dxc));
    CHECK(dxc.Compiler==MaterialGraphCompilerBackend::DXC);
#ifdef _WIN32
    CHECK(!GetModuleHandleW(L"slang-compiler.dll"));
#endif
    RHISlangCompiler unavailable(Workspace/"missing/slang-compiler.dll");std::string reason;
    CHECK(!unavailable.IsAvailable(&reason)&&!reason.empty());
    RHIShaderCompileDesc description;description.Source="float4 PSMain():SV_Target{return 1;}";description.Stage=RHIShaderStage::Fragment;description.EntryPoint="PSMain";
    const auto failed=unavailable.Compile(description);CHECK(!failed.Success&&!failed.Bytecode.IsValid()&&!failed.ErrorMessage.empty());
    MaterialGraphDiagnostic error;auto previous=dxc;previous.HLSL="previous";
    const bool slang=MaterialGraphProgram::Compile(doc,RHIShaderFormat::DXIL,previous,&error,MaterialNodeRegistry::Builtins(),MaterialHLSLRegistry::Builtins(),MaterialGraphCompilerBackend::Slang);
    if(AXLE_EXPECT_SLANG) {CHECK(slang&&previous.Compiler==MaterialGraphCompilerBackend::Slang&&previous.ShaderKey!=dxc.ShaderKey);}
    else {CHECK(!slang&&error.Code=="MG_COMPILER_UNAVAILABLE"&&previous.HLSL=="previous"&&previous.Vertex.Data==dxc.Vertex.Data);}
    previous.HLSL="previous";
    CHECK(!MaterialGraphProgram::Compile(doc,RHIShaderFormat::DXIL,previous,&error,MaterialNodeRegistry::Builtins(),MaterialHLSLRegistry::Builtins(),static_cast<MaterialGraphCompilerBackend>(255)));
    CHECK(error.Code=="MG_SHADER_COMPILER"&&previous.HLSL=="previous");
    CHECK(MaterialGraphProgram::Compile(doc,RHIShaderFormat::DXIL,previous)&&previous.Compiler==MaterialGraphCompilerBackend::DXC);
    std::cout<<"PASS explicit backend selection, absent SDK diagnostic and default DXC independence\n";return true;
}
bool Policy() {
    std::string availability;CHECK(RHISlangCompiler::Get().IsAvailable(&availability));
    CHECK(!RHIDevice::Get().IsInitialized());
    std::cout<<"Slang identity: "<<RHISlangCompiler::Get().Identity()<<"\n";
    std::ofstream timings(Workspace/"compile_timings.csv");timings<<"compiler,format,iteration,milliseconds,vs_bytes,ps_bytes\n";
    for(auto format:{RHIShaderFormat::DXIL,RHIShaderFormat::SPIRV}) {
        CompiledMaterialGraphProgram dxc,slang;CHECK(Compile(Fixture,format,MaterialGraphCompilerBackend::DXC,dxc));
        RHIShaderCompileDesc probe;probe.Source=dxc.HLSL;probe.SourcePath="mg_generated_material.hlsl";probe.EntryPoint="PSMain";probe.Stage=RHIShaderStage::Fragment;probe.Format=format;
        const auto raw=RHISlangCompiler::Get().Compile(probe);if(!raw.Success)std::cerr<<raw.ErrorMessage<<"\n";CHECK(raw.Success);
        CHECK(raw.Bytecode.IsValid());
        CHECK(Compile(Fixture,format,MaterialGraphCompilerBackend::Slang,slang));
        CHECK(dxc.ShaderKey!=slang.ShaderKey&&dxc.HLSL==slang.HLSL);
        std::cout<<"Layout equality "<<RHIShaderCompiler::FormatToString(format)<<": "<<(dxc.LayoutKey==slang.LayoutKey)<<"\n";
        CHECK(dxc.LayoutKey==slang.LayoutKey);
        CHECK(slang.Vertex.SaveToFile(Workspace/(format==RHIShaderFormat::DXIL?"slang.vs.dxil":"slang.vs.spv")));
        CHECK(slang.Fragment.SaveToFile(Workspace/(format==RHIShaderFormat::DXIL?"slang.ps.dxil":"slang.ps.spv")));
        for(auto backend:{MaterialGraphCompilerBackend::DXC,MaterialGraphCompilerBackend::Slang})for(int i=0;i<3;++i) {
            const auto begin=std::chrono::steady_clock::now();CompiledMaterialGraphProgram measured;
            CHECK(Compile(Fixture,format,backend,measured));
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            timings<<(backend==MaterialGraphCompilerBackend::DXC?"DXC":"Slang")<<','<<RHIShaderCompiler::FormatToString(format)<<','<<i<<','<<elapsed<<','<<measured.Vertex.Data.size()<<','<<measured.Fragment.Data.size()<<'\n';
        }
        // Valid pin defaults and exposed parameters remain data, not compiler-specific
        // names. CPU instance packing must work with the actual reflected offsets.
        MaterialGraphInstance instance;instance.Overrides["RustAmount"]={MaterialValueType::Float,{0.125f}};
        std::vector<ShaderPropertyInstance> values;MaterialGraphDiagnostic error;
        CHECK(instance.Resolve(slang,values,&error));
        const auto resolved=ResolveMaterialProperties(slang.BindingPlan,values);
        CHECK(std::holds_alternative<ResolvedMaterialProperties>(resolved));
        CHECK(std::holds_alternative<PackedMaterialResultData>(PackMaterialUniforms(slang.BindingPlan,{},std::get<ResolvedMaterialProperties>(resolved))));
    }
    timings.close();CHECK(bool(timings));
    MaterialGraphDiagnostic error;CompiledMaterialGraphProgram previous;previous.HLSL="previous";previous.Vertex.Data={1,2,3};
    auto invalid=Fixture;invalid.Connections[0].From.NodeID=999999;
    CHECK(!MaterialGraphProgram::Compile(invalid,RHIShaderFormat::SPIRV,previous,&error,MaterialNodeRegistry::Builtins(),MaterialHLSLRegistry::Builtins(),MaterialGraphCompilerBackend::Slang));
    CHECK(error.Code=="MG_ENDPOINT"&&previous.HLSL=="previous"&&previous.Vertex.Data==std::vector<uint8_t>({1,2,3}));
    auto definitions=MaterialNodeRegistry::Builtins();auto lowerings=MaterialHLSLRegistry::Builtins();
    MaterialNodeDefinition extension;extension.Type="test.Slang";
    extension.Signatures={{{{"color",MaterialValueType::Float3,MaterialValue{MaterialValueType::Float3,{0.2f,0.4f,0.6f}}}},{{"value",MaterialValueType::Float3,{}}}}};CHECK(definitions.Register(extension));
    auto custom=MaterialGraph::Create("Slang extension");MaterialGraphNode node;node.ID=2;node.Type=extension.Type;node.Source={200,51,4};
    node.Inputs={{"color",MaterialValueType::Float3,MaterialValue{MaterialValueType::Float3,{0.2f,0.4f,0.6f}},{}}};node.Outputs={{"value",MaterialValueType::Float3,{},{}}};
    custom.Nodes.push_back(node);custom.Connections.push_back({{2,"value"},{1,"Emission"},{}});
    MaterialHLSLNodeLowering lowering;lowering.Type=extension.Type;
    lowering.Emit=[](const auto& context,auto& output,auto&){output.Outputs["value"]="saturate("+context.Inputs.at("color")+")";return true;};CHECK(lowerings.Register(lowering));
    MaterialGraphTarget target;target.Compiler=MaterialGraphCompilerBackend::Slang;target.Format=RHIShaderFormat::SPIRV;
    MaterialGraphRuntime runtime(custom.Identity,target);CHECK(runtime.Reload(custom,definitions,lowerings));const auto good=runtime.Ready();
    // A reachable texture ignored by a trusted lowering must not become a phantom
    // resource/dependency after actual target dead-code elimination.
    auto dead=custom;
    auto texture=*std::find_if(Fixture.Nodes.begin(),Fixture.Nodes.end(),[](const auto& n){return n.Type=="SampleTexture2D";});texture.ID=3;
    for(auto& property:texture.Properties)if(property.Name=="texturePath")property.Value=std::string("missing-unused.png");
    dead.Nodes.push_back(texture);dead.Connections.push_back({{3,"rgb"},{2,"color"},{}});
    MaterialHLSLNodeLowering constant;constant.Type=extension.Type;constant.LoweringRevision=3;
    constant.Emit=[](const auto&,auto& output,auto&){output.Outputs["value"]="float3(0,0,0)";return true;};
    auto folded=MaterialHLSLRegistry::Builtins();CHECK(folded.Register(constant));
    for(auto format:{RHIShaderFormat::DXIL,RHIShaderFormat::SPIRV}) {
        CompiledMaterialGraphProgram optimized;
        CHECK(MaterialGraphProgram::Compile(dead,format,optimized,&error,definitions,folded,MaterialGraphCompilerBackend::Slang));
        CHECK(optimized.Graph.Textures.size()==1&&optimized.BindingPlan.Textures.empty()&&optimized.BindingPlan.Samplers.empty());
    }
    auto broken=MaterialHLSLRegistry::Builtins();lowering.LoweringRevision=2;
    lowering.Emit=[](const auto&,auto& output,auto&){output.Outputs["value"]="missing_slang_identifier";return true;};CHECK(broken.Register(lowering));
    CHECK(!runtime.Reload(custom,definitions,broken)&&runtime.Ready()==good);
    CHECK((runtime.Diagnostic().Code=="MG_SLANG_VERTEX"||runtime.Diagnostic().Code=="MG_SLANG_FRAGMENT")&&runtime.Diagnostic().NodeID==2&&runtime.Diagnostic().Source.Line==51);
    CHECK(runtime.Reload(custom,definitions,lowerings)&&runtime.Ready()==good&&runtime.Diagnostic().Code.empty());
    std::string source;CHECK(MaterialGraph::Serialize(Fixture,source,&error));MaterialGraphDocument reloaded;
    CHECK(MaterialGraph::Parse(source,reloaded,&error));std::string roundTrip;CHECK(MaterialGraph::Serialize(reloaded,roundTrip,&error)&&source==roundTrip);
    RHIShaderCompileDesc unsupported;unsupported.Source="float4 PSMain():SV_Target{return 1;}";unsupported.Stage=RHIShaderStage::Compute;
    const auto refused=RHISlangCompiler::Get().Compile(unsupported);CHECK(!refused.Success&&!refused.Bytecode.IsValid());
    std::cout<<"PASS actual Slang DXIL/SPIR-V and layout comparison\n";return true;
}

bool PNG(const std::filesystem::path& path,const std::array<uint8_t,4>& color) {
    // Tiny actual PNG fixture: RGBA8 1x1 with one uncompressed DEFLATE block.
    std::vector<uint8_t> png{137,80,78,71,13,10,26,10};
    auto big=[](auto& bytes,uint32_t value){for(int shift:{24,16,8,0})bytes.push_back(uint8_t(value>>shift));};
    auto chunk=[&](const char* name,const std::vector<uint8_t>& bytes){big(png,uint32_t(bytes.size()));const auto start=png.size();png.insert(png.end(),name,name+4);png.insert(png.end(),bytes.begin(),bytes.end());big(png,PackageWriter::CalculateCRC32(png.data()+start,png.size()-start));};
    std::vector<uint8_t> header;big(header,1);big(header,1);header.insert(header.end(),{8,6,0,0,0});chunk("IHDR",header);
    std::vector<uint8_t> raw{0,color[0],color[1],color[2],color[3]},zlib{0x78,0x01,1,5,0,0xfa,0xff};zlib.insert(zlib.end(),raw.begin(),raw.end());
    uint32_t a=1,b=0;for(auto value:raw){a=(a+value)%65521;b=(b+a)%65521;}big(zlib,(b<<16)|a);chunk("IDAT",zlib);chunk("IEND",{});
    return FileSystem::WriteBinaryFileAtomically(path,png.data(),png.size());
}
bool Native(RHIType backend) {
    Fixture.CullMode="None"; // The numeric triangle fixture is deliberately two-sided.
    auto& device=RHIDevice::Get();RHIDeviceDesc description;description.PreferredBackend=backend;description.AllowFallback=false;description.Headless=true;
    description.EnableNRIValidation=true;description.EnableValidation=RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    CHECK(device.Init(description)&&device.GetBackend()==backend&&!device.IsSoftwareAdapter());
    struct Shutdown {~Shutdown(){RHIDevice::Get().WaitIdle();RHIDevice::Get().Shutdown();}} shutdown;
    const auto assets=Workspace/"assets";std::filesystem::create_directories(assets/"textures");FileSystem::SetAssetDirectory(assets);
    CHECK(PNG(assets/"textures/metal_base_d.png",{180,150,90,255})&&PNG(assets/"textures/rust_d.png",{100,40,20,255}));
    const auto format=backend==RHIType::Vulkan?RHIShaderFormat::SPIRV:RHIShaderFormat::DXIL;
    MaterialGraphTarget target;target.Format=format;target.Native=true;
    MaterialGraphRuntime dxc(Fixture.Identity,target);target.Compiler=MaterialGraphCompilerBackend::Slang;MaterialGraphRuntime slang(Fixture.Identity,target);
    auto refresh=[&](MaterialGraphRuntime& runtime,const MaterialGraphDocument& doc){if(runtime.Reload(doc))return true;std::cerr<<runtime.Diagnostic().Code<<": "<<runtime.Diagnostic().Message<<"\n";return false;};
    EngineMaterialValues engine;engine.Model=glm::translate(glm::mat4(1),glm::vec3(0.1f,0.05f,0))*glm::scale(glm::mat4(1),glm::vec3(0.7f,0.8f,1));
    engine.CameraPosition={0.2f,0.1f,4,1};engine.SunDirection={0.2f,0.3f,1,1};engine.SunColor={1,0.8f,0.6f,1};
    std::ofstream measurements(Workspace/"native_measurements.csv");measurements<<"case,max_error,mean_error,energy\n";
    auto compare=[&](const char* name,const MaterialGraphInstance& instance=MaterialGraphInstance{}) {
        const auto expected=Axle::Tests::DrawMaterialGraph(dxc.Ready(),instance,{},&engine);
        const auto actual=Axle::Tests::DrawMaterialGraph(slang.Ready(),instance,{},&engine);
        if(expected.size()!=64*64*4 || actual.size()!=expected.size())return false;
        unsigned maximum=0;uint64_t total=0,energy=0;
        for(size_t i=0;i<actual.size();++i)if(i%4!=3){const auto delta=unsigned(std::abs(int(actual[i])-int(expected[i])));maximum=std::max(maximum,delta);total+=delta;energy+=expected[i];}
        const auto mean=double(total)/(64*64*3);measurements<<name<<','<<maximum<<','<<mean<<','<<energy<<'\n';
        if(!FileSystem::WriteBinaryFileAtomically(Workspace/(std::string(name)+".dxc.rgba"),expected.data(),expected.size())||
           !FileSystem::WriteBinaryFileAtomically(Workspace/(std::string(name)+".slang.rgba"),actual.data(),actual.size()))return false;
        return maximum<=1 && mean<=0.1 && energy>10000;
    };
    CHECK(refresh(dxc,Fixture)&&refresh(slang,Fixture));CHECK(dxc.Ready()->Program.LayoutKey==slang.Ready()->Program.LayoutKey);
    CHECK(compare("baseline"));
    MaterialGraphInstance override;override.Overrides["RustAmount"]={MaterialValueType::Float,{0.6f}};
    override.Overrides["BaseMetallic"]={MaterialValueType::Float,{0.0f}};
    override.Overrides["BaseRoughness"]={MaterialValueType::Float,{0.9f}};override.Overrides["BaseColorTint"]={MaterialValueType::Float3,{0.6f,1.0f,0.8f}};
    CHECK(compare("overrides",override));
    auto edited=Fixture;
    for(auto& node:edited.Nodes)if(node.Type=="SampleTexture2D")for(auto& property:node.Properties)if(property.Name=="sampler")property.Value=std::string("PointClamp");
    CHECK(refresh(dxc,edited)&&refresh(slang,edited)&&compare("point-clamp"));
    CHECK(PNG(assets/"textures/rust_d.png",{140,70,30,255}));
    const auto compiles=slang.Stats().ShaderCompiles;CHECK(refresh(dxc,edited)&&refresh(slang,edited)&&compare("resource-reload"));
    CHECK(slang.Stats().ShaderCompiles==compiles);
    const auto ready=slang.Ready();CHECK(!slang.Reload("bad material")&&slang.Ready()==ready&&compare("last-good"));
    CHECK(ready->Program.Vertex.SaveToFile(Workspace/"native.slang.vs.bin")&&ready->Program.Fragment.SaveToFile(Workspace/"native.slang.ps.bin"));
    measurements.close();CHECK(bool(measurements));CHECK(device.WaitIdle());
    std::cout<<"PASS native Slang/DXC returned-bytecode comparison, transforms, parameters, textures/samplers, reload and last-good\n";return true;
}
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;Axle::Tests::InitializeWorkspace();MaterialGraphDiagnostic error;
    if(!MaterialGraph::Load(std::filesystem::u8path(argv[2]),Fixture,&error))return 3;
    const std::string mode=argv[1];Workspace=std::filesystem::absolute(std::filesystem::u8path(argv[3])/mode);std::filesystem::create_directories(Workspace);
    try {const bool result=mode=="selection"?Selection():mode=="policy"?Policy():mode=="dx12"?Native(RHIType::Direct3D12):mode=="vulkan"?Native(RHIType::Vulkan):false;
        if(GetRHIValidationErrorCount())return 1;RHIShaderCompiler::Get().Shutdown();return result?0:1;
    }catch(const std::exception& failure){std::cerr<<failure.what()<<"\n";return 1;}
}
