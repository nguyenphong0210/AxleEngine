#include "TestWorkspace.hpp"
#include <Axle/MaterialGraphRuntime.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

#define CHECK(v) do {if(!(v)){std::cerr<<"FAIL "<<__LINE__<<": " #v "\n";return false;}}while(0)
namespace {
using namespace Axle;
using namespace Axle::RHI;
std::string Fixture;
std::filesystem::path Workspace;

MaterialGraphDocument Sample() {
    MaterialGraphDocument doc;MaterialGraphDiagnostic error;
    if(!MaterialGraph::Parse(Fixture,doc,&error))throw std::runtime_error(error.Message);
    return doc;
}
MaterialGraphParameter& Parameter(MaterialGraphDocument& doc,const std::string& name) {
    for(auto& parameter:doc.Parameters)if(parameter.Name==name)return parameter;
    throw std::runtime_error("Missing fixture parameter");
}
void AddParameter(MaterialGraphDocument& doc,std::string name,MaterialValue value,uint64_t id,std::string input) {
    doc.Parameters.push_back({name,value,{}});
    MaterialGraphNode ref;ref.ID=id;ref.Type="ParameterRef";ref.Attributes={{"parameter",name,{}}};
    ref.Outputs={{"value",value.Type,{},{}}};doc.Nodes.push_back(ref);
    doc.Connections.push_back({{id,"value"},{doc.Nodes[0].ID,std::move(input)},{}});
}
MaterialGraphDocument Glow() {
    auto doc=MaterialGraph::Create("Native graph reload");doc.CullMode="None";
    for(auto& input:doc.Nodes[0].Inputs)if(input.Name=="BaseColor")input.Default->Components={0,0,0};
    AddParameter(doc,"Glow",{MaterialValueType::Float3,{0.2f,0.4f,0.6f}},2,"Emission");
    return doc;
}
bool Reload(MaterialGraphRuntime& runtime,const MaterialGraphDocument& doc) {
    if(runtime.Reload(doc))return true;
    std::cerr<<runtime.Diagnostic().Code<<": "<<runtime.Diagnostic().Message<<"\n";return false;
}
ShaderPropertyInstance* Property(std::vector<ShaderPropertyInstance>& properties,const std::string& name) {
    for(auto& property:properties)if(property.Desc.DisplayName==name)return &property;
    return nullptr;
}

bool CPU() {
    CHECK(!RHIDevice::Get().IsInitialized());
    auto sample=Sample();MaterialGraphDiagnostic error;
    MaterialGraphRuntime dx(sample.Identity,{RHIShaderFormat::DXIL}),vk(sample.Identity,{RHIShaderFormat::SPIRV});
    CHECK(Reload(dx,sample)&&Reload(vk,sample));
    CHECK(dx.Ready()->Program.LayoutKey==vk.Ready()->Program.LayoutKey);
    CHECK(dx.Ready()->Program.ShaderKey!=vk.Ready()->Program.ShaderKey);
    CHECK(dx.Ready()->PipelineKey!=vk.Ready()->PipelineKey);
    const auto initial=dx.Ready();
    auto moved=sample;for(auto& node:moved.Nodes)node.EditorPosition={10,20};moved.Name="Different display name";
    std::reverse(moved.Nodes.begin(),moved.Nodes.end());std::reverse(moved.Connections.begin(),moved.Connections.end());
    CHECK(Reload(dx,moved)&&dx.Ready()==initial&&dx.Stats().ShaderCompiles==1&&dx.Stats().ShaderCacheHits==1);
    auto changed=sample;Parameter(changed,"RustAmount").Default.Components[0]=0.75f;
    CHECK(Reload(dx,changed));CHECK(dx.Ready()!=initial&&dx.Ready()->PipelineKey==initial->PipelineKey);
    CHECK(dx.Stats().ShaderCompiles==1);
    std::vector<ShaderPropertyInstance> properties;
    MaterialGraphInstance instance;CHECK(instance.Resolve(dx.Ready()->Program,properties,&error));
    CHECK(Property(properties,"RustAmount")&&Property(properties,"RustAmount")->FloatVal==0.75f);
    CHECK(instance.Resolve(initial->Program,properties,&error)&&Property(properties,"RustAmount")->FloatVal!=0.75f);
    auto texture=changed;
    for(auto& node:texture.Nodes)for(auto& property:node.Properties)if(property.Name=="texturePath")property.Value=std::string("textures/other.png");
    CHECK(Reload(dx,texture)&&dx.Ready()->PipelineKey==initial->PipelineKey&&dx.Stats().ShaderCompiles==1);
    CHECK(dx.Ready()->Program.Graph.Textures[0].Path=="textures/other.png");
    changed.DepthWrite=!changed.DepthWrite;changed.CullMode="Front";
    CHECK(Reload(dx,changed)&&dx.Ready()->PipelineKey!=initial->PipelineKey&&dx.Stats().ShaderCompiles==1);
    MaterialGraphTarget target;target.Color=nri::Format::RGBA16_SFLOAT;target.Samples=4;
    MaterialGraphRuntime alternate(sample.Identity,target);CHECK(Reload(alternate,sample));
    CHECK(alternate.Ready()->PipelineKey!=initial->PipelineKey);

    // Strict descriptor/type metadata and transactional output, independent of DXC.
    const auto& artifact=initial->Program;
    auto reject=[&](RHIShaderReflection reflection) {
        auto plan=artifact.BindingPlan;std::string key="unchanged";
        if(MaterialGraphProgram::BuildLayout(artifact.Graph,reflection,plan,key,&error))return false;
        return error.Code=="MG_LAYOUT"&&key=="unchanged"&&plan==artifact.BindingPlan;
    };
    auto reflection=artifact.Reflection;reflection.Resources[0].BindCount=2;CHECK(reject(reflection));
    reflection=artifact.Reflection;reflection.Resources[0].Space=9;CHECK(reject(reflection));
    reflection=artifact.Reflection;reflection.Resources.push_back(reflection.Resources[0]);CHECK(reject(reflection));
    reflection=artifact.Reflection;reflection.ConstantBuffers[0].Variables[0].ScalarType=RHIShaderScalarType::Unknown;CHECK(reject(reflection));
    reflection=artifact.Reflection;reflection.ConstantBuffers[0].Variables[0].Offset=4;CHECK(reject(reflection));
    reflection=artifact.Reflection;reflection.ConstantBuffers[0].Variables[0].RowMajor=true;CHECK(reject(reflection));
    reflection=artifact.Reflection;reflection.ConstantBuffers[0].Variables[0].Elements=2;CHECK(reject(reflection));
    reflection=artifact.Reflection;reflection.ConstantBuffers[0].Size=4;CHECK(reject(reflection));
    reflection=artifact.Reflection;reflection.ConstantBuffers[0].Variables.erase(reflection.ConstantBuffers[0].Variables.begin());CHECK(reject(reflection));
    reflection=artifact.Reflection;reflection.Resources[0].AdditionalStages|=nri::StageBits::COMPUTE_SHADER;CHECK(reject(reflection));
    reflection=artifact.Reflection;reflection.Resources[0].Type=RHIResourceType::StorageTexture;CHECK(reject(reflection));
    reflection=artifact.Reflection;for(auto& r:reflection.Resources)if(r.Type==RHIResourceType::Texture){r.TextureDimension=3;break;}CHECK(reject(reflection));
    reflection=artifact.Reflection;
    std::reverse(reflection.Resources.begin(),reflection.Resources.end());std::reverse(reflection.ConstantBuffers.begin(),reflection.ConstantBuffers.end());
    for(auto& buffer:reflection.ConstantBuffers)std::reverse(buffer.Variables.begin(),buffer.Variables.end());
    MaterialBindingPlan reordered;std::string key;CHECK(MaterialGraphProgram::BuildLayout(artifact.Graph,reflection,reordered,key,&error));
    CHECK(key==artifact.LayoutKey);

    // Instances retain names as numeric symbols shift after a rename.
    std::vector<MaterialGraphInstance> instances(2);
    instances[0].Overrides["RustAmount"]={MaterialValueType::Float,{0.125f}};
    instances[1].Overrides["RustAmount"]={MaterialValueType::Float,{0.875f}};
    const auto original=sample;
    CHECK(!RenameMaterialGraphParameter(sample,"RustAmount","BaseMetallic",instances,&error));
    CHECK(instances[0].Overrides.contains("RustAmount")&&Parameter(sample,"RustAmount").Name=="RustAmount");
    CHECK(RenameMaterialGraphParameter(sample,"RustAmount","ZZ_Rust",instances,&error));
    CHECK(!instances[0].Overrides.contains("RustAmount")&&instances[1].Overrides.contains("ZZ_Rust"));
    CHECK(Reload(dx,sample));
    CHECK(instances[0].Resolve(dx.Ready()->Program,properties,&error));CHECK(Property(properties,"ZZ_Rust")->FloatVal==0.125f);
    CHECK(instances[1].Resolve(dx.Ready()->Program,properties,&error));CHECK(Property(properties,"ZZ_Rust")->FloatVal==0.875f);
    auto previous=properties;
    instances[0].Overrides["ZZ_Rust"]={MaterialValueType::Float3,{1,2,3}};
    CHECK(!instances[0].Resolve(dx.Ready()->Program,properties,&error)&&error.Code=="MG_INSTANCE_TYPE");
    CHECK(Property(properties,"ZZ_Rust")->FloatVal==Property(previous,"ZZ_Rust")->FloatVal);
    CHECK(!RenameMaterialGraphParameter(sample,"ZZ_Rust","Again",instances,&error));
    CHECK(Parameter(sample,"ZZ_Rust").Name=="ZZ_Rust"&&instances[1].Overrides.contains("ZZ_Rust"));
    instances[0].Overrides.clear();instances[0].Overrides["ManualUnknown"]={MaterialValueType::Float,{1}};
    CHECK(!instances[0].Resolve(dx.Ready()->Program,properties,&error)&&error.Code=="MG_INSTANCE_PARAMETER");
    instances[0].Overrides.clear();instances[0].Overrides["ZZ_Rust"]={MaterialValueType::Float,{std::numeric_limits<float>::infinity()}};
    CHECK(!instances[0].Resolve(dx.Ready()->Program,properties,&error));
    // An unused authored parameter/override survives without a phantom GPU binding.
    sample.Parameters.push_back({"Unused",{MaterialValueType::Float2,{2,3}},{}});
    instances[0].Overrides.clear();instances[0].Overrides["Unused"]={MaterialValueType::Float2,{4,5}};
    CHECK(Reload(dx,sample)&&instances[0].Resolve(dx.Ready()->Program,properties,&error));

    const auto good=dx.Ready();
    CHECK(!dx.Reload("Material { broken")&&dx.Ready()==good);
    auto invalid=sample;invalid.Connections[0].From.NodeID=98765;
    CHECK(!dx.Reload(invalid)&&dx.Ready()==good&&dx.Diagnostic().Code=="MG_ENDPOINT");
    invalid=sample;++invalid.Identity;CHECK(!dx.Reload(invalid)&&dx.Ready()==good&&dx.Diagnostic().Code=="MG_RELOAD_IDENTITY");
    MaterialGraphRuntime empty(sample.Identity,{RHIShaderFormat::DXBC});CHECK(!empty.Reload(sample)&&!empty.Ready());
    MaterialGraphTarget unavailable;unavailable.Native=true;
    MaterialGraphRuntime noDevice(sample.Identity,unavailable);CHECK(!noDevice.Reload(sample)&&!noDevice.Ready()&&noDevice.Diagnostic().Code=="MG_RELOAD_DEVICE");
    CHECK(Reload(dx,sample)&&dx.Diagnostic().Code.empty());

    // LRU-independent bounded FIFO code retention; evicted source is really recompiled.
    auto glow=Glow();MaterialGraphRuntime bounded(glow.Identity,{RHIShaderFormat::DXIL},1);
    CHECK(Reload(bounded,glow));const auto old=bounded.Ready();
    auto extended=glow;AddParameter(extended,"A_Roughness",{MaterialValueType::Float,{0.5f}},3,"Roughness");
    CHECK(Reload(bounded,extended)&&bounded.ShaderCacheSize()==1);
    CHECK(bounded.Ready()->Program.LayoutKey!=old->Program.LayoutKey&&bounded.Stats().ShaderCompiles==2);
    // Changed types require explicit instance repair; no scalar reinterpretation.
    MaterialGraphInstance typed;typed.Overrides["A_Roughness"]={MaterialValueType::Float3,{1,2,3}};
    CHECK(!typed.Resolve(bounded.Ready()->Program,properties,&error)&&error.Code=="MG_INSTANCE_TYPE");
    CHECK(Reload(bounded,glow)&&bounded.Stats().ShaderCompiles==3);
    bounded.Clear();CHECK(!bounded.Ready()&&bounded.ShaderCacheSize()==0&&old->Program.Vertex.IsValid());

    auto definitions=MaterialNodeRegistry::Builtins();
    MaterialNodeDefinition extension;extension.Type="test.Reload";
    extension.Signatures={{{{"color",MaterialValueType::Float3,{}}},{{"value",MaterialValueType::Float3,{}}}}};
    CHECK(definitions.Register(extension));
    auto custom=glow;MaterialGraphNode operation;operation.ID=4;operation.Type=extension.Type;operation.Source={100,42,2};
    operation.Inputs={{"color",MaterialValueType::Float3,{},{}}};operation.Outputs={{"value",MaterialValueType::Float3,{},{}}};
    custom.Nodes.push_back(operation);custom.Connections[0].To={4,"color"};custom.Connections.push_back({{4,"value"},{1,"Emission"},{}});
    MaterialHLSLNodeLowering lowering;lowering.Type=extension.Type;
    lowering.Emit=[](const auto& context,auto& out,auto&){out.Outputs["value"]=context.Inputs.at("color");return true;};
    auto lowerings=MaterialHLSLRegistry::Builtins();CHECK(lowerings.Register(lowering));
    MaterialGraphRuntime extensible(custom.Identity,{RHIShaderFormat::DXIL});
    CHECK(extensible.Reload(custom,definitions,lowerings));const auto revisionOne=extensible.Ready();
    lowering.LoweringRevision=2;auto revised=MaterialHLSLRegistry::Builtins();CHECK(revised.Register(lowering));
    CHECK(extensible.Reload(custom,definitions,revised)&&extensible.Stats().ShaderCompiles==2);
    CHECK(extensible.Ready()->Program.HLSL==revisionOne->Program.HLSL&&extensible.Ready()->Program.ShaderKey!=revisionOne->Program.ShaderKey);
    const auto revisionTwo=extensible.Ready();
    lowering.LoweringRevision=3;lowering.Emit=[](const auto&,auto& out,auto&){out.Outputs["value"]="missing_reload_identifier";return true;};
    auto broken=MaterialHLSLRegistry::Builtins();CHECK(broken.Register(lowering));
    CHECK(!extensible.Reload(custom,definitions,broken)&&extensible.Ready()==revisionTwo);
    CHECK((extensible.Diagnostic().Code=="MG_DXC_VERTEX"||extensible.Diagnostic().Code=="MG_DXC_FRAGMENT")&&
        extensible.Diagnostic().NodeID==4&&extensible.Diagnostic().Source.Line==42);
    CHECK(extensible.Reload(custom,definitions,revised)&&extensible.Diagnostic().Code.empty());
    std::string serialized;CHECK(MaterialGraph::Serialize(custom,serialized,&error,definitions));
    CHECK(extensible.Reload(serialized,definitions,revised));

    auto alias=*MaterialNodeRegistry::Builtins().Find("ParameterRef");alias.Type="test.ParameterAlias";
    CHECK(definitions.Register(alias));auto aliasDoc=glow;aliasDoc.Nodes[1].Type=alias.Type;
    std::vector<MaterialGraphInstance> aliases(1);aliases[0].Overrides["Glow"]={MaterialValueType::Float3,{0.1f,0.2f,0.3f}};
    CHECK(RenameMaterialGraphParameter(aliasDoc,"Glow","RenamedAlias",aliases,&error,definitions));
    CHECK(std::get<std::string>(aliasDoc.Nodes[1].Attributes[0].Value)=="RenamedAlias"&&aliases[0].Overrides.contains("RenamedAlias"));
    CHECK(!RHIDevice::Get().IsInitialized());
    std::cout<<"PASS reflection ABI, backend keys, code cache, immutable reload, named instances and rename rollback\n";
    return true;
}

// Draw returned compiler bytecodes directly, without writing HLSL or recompiling
// through the legacy custom-source consumer. Descriptor packing uses production APIs.
std::vector<uint8_t> Draw(std::shared_ptr<const MaterialGraphReadyGeneration> generation,
                          const MaterialGraphInstance& instance={},std::function<void()> beforeSubmit={}) {
    auto& device=RHIDevice::Get();
    std::vector<ShaderPropertyInstance> properties;
    if(!instance.Resolve(generation->Program,properties))return {};
    const auto resolved=ResolveMaterialProperties(generation->Program.BindingPlan,properties);
    const auto* values=std::get_if<ResolvedMaterialProperties>(&resolved);if(!values)return {};
    EngineMaterialValues engine;engine.CameraPosition={0,0,4,1};engine.SunDirection.w=0;engine.SunColor=glm::vec4(0);
    const auto packed=PackMaterialUniforms(generation->Program.BindingPlan,engine,*values);
    const auto* buffers=std::get_if<PackedMaterialResultData>(&packed);if(!buffers)return {};
    constexpr uint32_t width=64;
    RHITexture color,depth;RHIBuffer readback;RHIBufferDesc read;read.Size=width*width*4;read.MemoryLocation=RHIMemoryLocation::HostReadback;
    if(!color.Init(RHITextureDesc::RenderTarget2D(width,width))||!depth.Init(RHITextureDesc::DepthStencil2D(width,width))||!readback.Init(read))return {};
    std::vector<RHIVertex3D> vertices(3);
    vertices[0].Position={-1,-1,0.5f};vertices[1].Position={0,1,0.5f};vertices[2].Position={1,-1,0.5f};
    for(auto& vertex:vertices){vertex.Normal={0,0,1};vertex.Tangent={1,0,0,1};}
    RHIMesh mesh;if(!mesh.Init(vertices,{0,1,2}))return {};
    RHIDescriptorPool pool;if(!pool.Init())return {};
    std::vector<RHIBuffer> constants(buffers->Buffers.size());
    for(size_t i=0;i<constants.size();++i)if(!constants[i].Init(RHIBufferDesc::Constant(buffers->Buffers[i].Bytes.size()))||
        !constants[i].UpdateData(buffers->Buffers[i].Bytes.data(),buffers->Buffers[i].Bytes.size()))return {};
    const auto& native=*generation->Native;
    std::vector<nri::DescriptorSet*> sets;
    for(size_t i=0;i<native.LayoutDescription.DescriptorSets.size();++i) {
        auto* set=pool.AllocateDescriptorSet(*native.PipelineLayout,static_cast<uint32_t>(i));if(!set)return {};sets.push_back(set);
        const auto& desc=native.LayoutDescription.DescriptorSets[i];
        for(size_t r=0;r<desc.Ranges.size();++r) {
            if(desc.Ranges[r].descriptorType!=nri::DescriptorType::CONSTANT_BUFFER)return {};
            nri::Descriptor* descriptor=nullptr;
            for(size_t b=0;b<constants.size();++b)if(buffers->Buffers[b].Address==ShaderAddress{desc.RegisterSpace,desc.Ranges[r].baseRegisterIndex})descriptor=constants[b].GetCBV();
            if(!descriptor||!pool.UpdateTexture(set,static_cast<uint32_t>(r),0,descriptor))return {};
        }
    }
    auto* pipeline=MaterialGraphRuntime::GetPipeline(*generation);if(!pipeline)return {};
    RHICommandBuffer commands;if(!commands.Init()||!commands.Begin(pool.GetNative()))return {};
    commands.TransitionTexture(color.GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ColorAttachment);
    commands.TransitionTexture(depth.GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::DepthStencilWrite);
    nri::AttachmentDesc attachment{};attachment.descriptor=color.GetRTV();attachment.loadOp=nri::LoadOp::CLEAR;attachment.storeOp=nri::StoreOp::STORE;
    nri::AttachmentDesc depthAttachment{};depthAttachment.descriptor=depth.GetDSV();depthAttachment.loadOp=nri::LoadOp::CLEAR;depthAttachment.storeOp=nri::StoreOp::STORE;depthAttachment.clearValue.depthStencil.depth=1;
    nri::RenderingDesc rendering{};rendering.colors=&attachment;rendering.colorNum=1;rendering.depth=depthAttachment;
    commands.BeginRendering(rendering);commands.SetViewport({0,0,float(width),float(width),0,1});commands.SetScissor({0,0,width,width});
    commands.SetPipelineLayout(nri::BindPoint::GRAPHICS,*native.PipelineLayout);
    for(size_t i=0;i<sets.size();++i)commands.SetDescriptorSet(nri::BindPoint::GRAPHICS,static_cast<uint32_t>(i),sets[i]);
    commands.SetPipeline(pipeline);mesh.Bind(commands);mesh.Draw(commands);commands.EndRendering();
    commands.TransitionTexture(color.GetNative(),RHIResourceLayout::ColorAttachment,RHIResourceLayout::CopySource);
    nri::TextureDataLayoutDesc layout{};layout.rowPitch=width*4;layout.slicePitch=width*width*4;
    nri::TextureRegionDesc region{};region.width=region.height=width;region.depth=1;
    commands.ReadbackTextureToBuffer(*readback.GetNative(),layout,*color.GetNative(),region);
    if(!commands.End())return {};
    generation.reset();if(beforeSubmit)beforeSubmit();
    if(!commands.Submit()||!device.WaitIdle())return {};
    const auto* memory=readback.Map();if(!memory)return {};
    std::vector<uint8_t> pixels(read.Size);std::memcpy(pixels.data(),memory,pixels.size());readback.Unmap();return pixels;
}
bool Pixel(const std::vector<uint8_t>& pixels,std::array<int,3> expected) {
    if(pixels.size()!=64*64*4)return false;
    const auto offset=(32*64+32)*4;
    for(size_t i=0;i<3;++i)if(std::abs(int(pixels[offset+i])-expected[i])>1)return false;
    return true;
}
bool Native(RHIType backend) {
    auto& device=RHIDevice::Get();RHIDeviceDesc desc;desc.PreferredBackend=backend;desc.AllowFallback=false;desc.Headless=true;
    desc.EnableNRIValidation=true;desc.EnableValidation=RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    CHECK(device.Init(desc)&&device.GetBackend()==backend&&!device.IsSoftwareAdapter());
    struct Shutdown {~Shutdown(){RHIDevice::Get().WaitIdle();RHIDevice::Get().Shutdown();}} shutdown;
    auto doc=Glow();MaterialGraphTarget target;target.Format=backend==RHIType::Vulkan?RHIShaderFormat::SPIRV:RHIShaderFormat::DXIL;target.Native=true;
    MaterialGraphRuntime runtime(doc.Identity,target);CHECK(Reload(runtime,doc));
    auto old=runtime.Ready();auto* firstPipeline=MaterialGraphRuntime::GetPipeline(*old);CHECK(firstPipeline);
    CHECK(old->Native->GenerationID==old->ID);
    CHECK(Pixel(Draw(old),{51,102,153}));
    auto updated=doc;Parameter(updated,"Glow").Default.Components={0.6f,0.1f,0.3f};CHECK(Reload(runtime,updated));
    CHECK(runtime.Ready()->ID!=old->ID&&runtime.Stats().ShaderCompiles==1&&runtime.Stats().LayoutBuilds==1);
    CHECK(MaterialGraphRuntime::GetPipeline(*runtime.Ready())==firstPipeline);
    CHECK(Pixel(Draw(runtime.Ready()),{153,26,77})&&Pixel(Draw(old),{51,102,153}));
    const auto good=runtime.Ready();
    CHECK(!runtime.Reload("invalid source")&&runtime.Ready()==good&&Pixel(Draw(runtime.Ready()),{153,26,77}));
    auto extended=updated;AddParameter(extended,"A_Roughness",{MaterialValueType::Float,{0.8f}},3,"Roughness");
    bool layoutFault=false;
    {ScopedRHIFault fault([&](std::string_view operation)->std::optional<nri::Result>{if(operation=="CreatePipelineLayout"){layoutFault=true;return nri::Result::FAILURE;}return {};});
        CHECK(!runtime.Reload(extended)&&runtime.Diagnostic().Code=="MG_RELOAD_LAYOUT"&&runtime.Ready()==good);}
    CHECK(layoutFault&&Pixel(Draw(runtime.Ready()),{153,26,77}));
    CHECK(Reload(runtime,extended)&&runtime.Ready()->Program.LayoutKey!=good->Program.LayoutKey);
    const auto expanded=runtime.Ready();auto pipelineEdit=extended;pipelineEdit.CullMode="Front";bool pipelineFault=false;
    {ScopedRHIFault fault([&](std::string_view operation)->std::optional<nri::Result>{if(operation=="CreateGraphicsPipeline"){pipelineFault=true;return nri::Result::FAILURE;}return {};});
        CHECK(!runtime.Reload(pipelineEdit)&&runtime.Diagnostic().Code=="MG_RELOAD_PIPELINE"&&runtime.Ready()==expanded);}
    CHECK(pipelineFault&&Pixel(Draw(runtime.Ready()),{153,26,77}));
    CHECK(Reload(runtime,extended)&&runtime.Diagnostic().Code.empty());
    MaterialGraphInstance instance;instance.Overrides["Glow"]={MaterialValueType::Float3,{0.1f,0.8f,0.2f}};
    CHECK(Pixel(Draw(runtime.Ready(),instance),{26,204,51}));
    // Destruction of wrappers after recording must not retire resources still read
    // by an executable command buffer. The command's leases are the sole owners.
    old=runtime.Ready();std::weak_ptr<const MaterialGraphReadyGeneration> weak=old;
    CHECK(Pixel(Draw(old,{},[&]{runtime.Clear();old.reset();RHIPipelineCache::Get().Clear();}),{153,26,77}));
    // good/expanded retain older snapshots intentionally; use a fresh isolated one
    // below to prove actual generation destruction rather than pointer replacement.
    CHECK(Reload(runtime,doc));old=runtime.Ready();weak=old;
    CHECK(Pixel(Draw(old,{},[&]{runtime.Clear();old.reset();RHIPipelineCache::Get().Clear();}),{51,102,153}));CHECK(weak.expired());
    CHECK(Reload(runtime,doc));old=runtime.Ready();const auto epoch=old->DeviceEpoch;
    CHECK(device.WaitIdle());
    CHECK(!device.ObserveResult(nri::Result::DEVICE_LOST,"GraphEpochTest","MaterialGraphRuntime"));
    CHECK(!MaterialGraphRuntime::GetPipeline(*old));
    CHECK(!runtime.Reload(doc)&&runtime.Ready()==old&&runtime.Diagnostic().Code=="MG_RELOAD_DEVICE");
    device.Shutdown();CHECK(!MaterialGraphRuntime::GetPipeline(*old));
    CHECK(device.Init(desc)&&device.GetEpoch()!=epoch);CHECK(!MaterialGraphRuntime::GetPipeline(*old));
    CHECK(Reload(runtime,doc)&&runtime.Ready()->DeviceEpoch==device.GetEpoch()&&Pixel(Draw(runtime.Ready()),{51,102,153}));
    const auto pixels=Draw(runtime.Ready());CHECK(!pixels.empty());
    CHECK(FileSystem::WriteBinaryFileAtomically(Workspace/(backend==RHIType::Vulkan?"vulkan.rgba":"dx12.rgba"),pixels.data(),pixels.size()));
    CHECK(runtime.Ready()->Program.Vertex.SaveToFile(Workspace/(backend==RHIType::Vulkan?"vulkan.vs.bin":"dx12.vs.bin")));
    CHECK(runtime.Ready()->Program.Fragment.SaveToFile(Workspace/(backend==RHIType::Vulkan?"vulkan.ps.bin":"dx12.ps.bin")));
    std::cout<<"PASS native returned bytecodes: default/override oracles, cache reuse, parse/layout/pipeline rollback, recorded leases and epoch rebuild\n";
    return true;
}
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    Axle::Tests::InitializeWorkspace();Fixture=FileSystem::ReadTextFile(std::filesystem::u8path(argv[2]));
    Workspace=std::filesystem::u8path(argv[3]);std::filesystem::create_directories(Workspace);
    try {
        const std::string mode=argv[1];
        bool result=mode=="policy"?CPU():mode=="dx12"?Native(RHIType::Direct3D12):mode=="vulkan"?Native(RHIType::Vulkan):false;
        if(GetRHIValidationErrorCount())result=false;
        RHIShaderCompiler::Get().Shutdown();return result?0:1;
    }catch(const std::exception& e){std::cerr<<"FAIL exception: "<<e.what()<<"\n";return 1;}
}
