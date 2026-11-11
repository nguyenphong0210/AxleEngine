#include "TestWorkspace.hpp"
#include "MaterialGraphTestDraw.hpp"
#include <Axle/TextureDescriptor.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Package.hpp>
#include <Axle/Path.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <cmath>
#include <fstream>
#include <iostream>

#define CHECK(v) do{if(!(v)){std::cerr<<"FAIL "<<__LINE__<<": " #v "\n";return false;}}while(0)
namespace {
using namespace Axle;
using namespace Axle::RHI;
std::filesystem::path Workspace,Assets;
MaterialGraphNode TextureTemplate;

std::vector<uint8_t> TGA(uint16_t width,uint16_t height,const std::vector<uint8_t>& rgba) {
    if(rgba.size()!=size_t(width)*height*4)throw std::runtime_error("Bad TGA fixture dimensions");
    std::vector<uint8_t> bytes(18);bytes[2]=2;bytes[12]=uint8_t(width);bytes[13]=uint8_t(width>>8);
    bytes[14]=uint8_t(height);bytes[15]=uint8_t(height>>8);bytes[16]=32;bytes[17]=0x28;
    for(size_t i=0;i<rgba.size();i+=4)bytes.insert(bytes.end(),{rgba[i+2],rgba[i+1],rgba[i],rgba[i+3]});
    return bytes;
}
bool Write(const std::string& path,const std::vector<uint8_t>& bytes) {
    const auto target=Assets/PathFromUtf8(path);std::filesystem::create_directories(target.parent_path());
    return FileSystem::WriteBinaryFileAtomically(target,bytes.data(),bytes.size());
}
bool Image(const std::string& path,std::vector<uint8_t> rgba,uint16_t width=2,uint16_t height=1) {return Write(path,TGA(width,height,rgba));}
std::vector<uint8_t> Gray(uint8_t value) {return {value,value,value,255,value,value,value,255};}
void Property(MaterialGraphNode& node,const std::string& name,std::string value) {
    for(auto& property:node.Properties)if(property.Name==name){property.Value=std::move(value);return;}
    node.Properties.push_back({name,std::move(value),{}});
}
MaterialGraphDocument Graph(std::string path="textures/gray.tga",std::string space="sRGB",std::string sampler="LinearClamp",float u=0.5f,float v=0.5f) {
    auto doc=MaterialGraph::Create("Texture semantics");doc.CullMode="None";
    for(auto& pin:doc.Nodes[0].Inputs)if(pin.Name=="BaseColor")pin.Default->Components={0,0,0};
    auto texture=TextureTemplate;texture.ID=2;texture.Source={200,41,4};texture.Inputs[0].Default->Components={u,v};
    Property(texture,"texturePath",std::move(path));Property(texture,"colorSpace",std::move(space));Property(texture,"sampler",std::move(sampler));
    doc.Nodes.push_back(texture);doc.Connections.push_back({{2,"rgb"},{1,"Emission"},{}});return doc;
}
bool Resolve(const MaterialGraphDocument& doc,MaterialGraphResourceSet& resources,MaterialGraphDiagnostic* error=nullptr) {
    MaterialHLSLSource source;
    return MaterialGraphHLSL::Lower(doc,source,error)&&MaterialGraphResources::Resolve(doc,source,resources,error);
}
bool Reload(MaterialGraphRuntime& runtime,const MaterialGraphDocument& doc) {
    if(runtime.Reload(doc))return true;
    std::cerr<<runtime.Diagnostic().Code<<": "<<runtime.Diagnostic().Message<<"\n";return false;
}
bool Pixel(const std::vector<uint8_t>& pixels,std::array<int,3> expected) {
    if(pixels.size()!=64*64*4)return false;
    const size_t offset=(32*64+32)*4;
    for(size_t i=0;i<3;++i)if(std::abs(int(pixels[offset+i])-expected[i])>1) {
        std::cerr<<"Pixel "<<i<<" expected "<<expected[i]<<" actual "<<int(pixels[offset+i])<<"\n";return false;
    }
    return true;
}
MaterialGraphDocument Normal(MaterialGraphDocument doc,bool surface=false) {
    MaterialGraphNode unpack;unpack.ID=3;unpack.Type="UnpackNormal";
    unpack.Inputs={{"rgb",MaterialValueType::Float3,{},{}}};unpack.Outputs={{"normal",MaterialValueType::Float3,{},{}}};
    doc.Nodes.push_back(unpack);doc.Connections.clear();doc.Connections.push_back({{2,"rgb"},{3,"rgb"},{}});
    doc.Connections.push_back({{3,"normal"},{1,surface?"Normal":"Emission"},{}});
    if(surface)for(auto& input:doc.Nodes[0].Inputs)if(input.Name=="BaseColor")input.Default->Components={1,1,1};
    return doc;
}
bool CPU() {
    CHECK(!RHIDevice::Get().IsInitialized());
    CHECK(Image("textures/gray.tga",Gray(128)));
    auto doc=Graph();MaterialGraphResourceSet resources;MaterialGraphDiagnostic error;
    CHECK(Resolve(doc,resources,&error)&&resources.Images.size()==1&&resources.Dependencies.size()==1&&resources.Warnings.empty());
    CHECK(resources.Images[0].Source.Image->Options.SRGB&&resources.Images[0].Source.Image->Mips.size()==2);
    const auto first=resources;
    // Linear-space mip generation; alpha is always linear and independent of RGB.
    CHECK(Image("textures/edge.tga",{0,0,0,0,128,128,128,255}));
    doc=Graph("textures/edge.tga");CHECK(Resolve(doc,resources,&error));
    CHECK(std::abs(int(resources.Images[0].Source.Image->Mips[1].Pixels[0])-92)<=1);
    CHECK(resources.Images[0].Source.Image->Mips[1].Pixels[3]==128);
    Property(doc.Nodes[1],"colorSpace","Linear");CHECK(Resolve(doc,resources,&error));
    CHECK(resources.Images[0].Source.Image->Mips[1].Pixels[0]==64&&resources.Images[0].Source.Image->Mips[1].Pixels[3]==128);
    CHECK(Image("textures/odd.tga",{0,0,0,255,0,0,0,255,255,255,255,255},3));
    CHECK(Resolve(Graph("textures/odd.tga","Linear"),resources,&error));
    CHECK(resources.Images[0].Source.Image->Mips.size()==2&&resources.Images[0].Source.Image->Mips[1].Pixels[0]==85);
    // An unrelated neighboring descriptor never overrides a raw path's interpretation.
    TextureDescriptor descriptor;descriptor.Source="textures/gray.tga";descriptor.Type="Data";descriptor.GenerateMips=false;descriptor.MaxResolution=1;
    CHECK(FileSystem::WriteTextFileAtomically(Assets/"textures/gray.axleasset",TextureDescriptor::Serialize(descriptor)));
    CHECK(Resolve(Graph(),resources,&error)&&resources.Images[0].Source.Image->Options.SRGB&&resources.Images[0].Source.Image->Mips.size()==2);
    auto explicitDescriptor=Graph("textures/gray.axleasset","Linear","PointClamp");
    CHECK(Resolve(explicitDescriptor,resources,&error));
    CHECK(resources.Dependencies.size()==2&&resources.Images[0].Source.Image->Mips[0].Width==1&&resources.Images[0].Source.Image->Mips.size()==1);
    Property(explicitDescriptor.Nodes[1],"colorSpace","sRGB");
    const auto savedKey=resources.Key;CHECK(!Resolve(explicitDescriptor,resources,&error)&&error.Code=="MG_TEXTURE_COLORSPACE"&&resources.Key==savedKey);
    CHECK(error.NodeID==2&&error.Source.Line==41&&error.Message.find("gray.axleasset")!=std::string::npos);
    // Strict descriptor structure, nested/escaping references and unsupported types.
    const std::vector<std::string> invalidDescriptors{
        "Texture { source: \"textures/gray.tga\", generateMips: maybe }",
        "Texture { source: \"textures/gray.tga\", source: \"textures/other.tga\" }",
        "Texture { source: \"textures/gray.tga\", maxResolution: -1 }",
        "Texture { source: \"textures/gray.tga\", exposure: 2 }",
        "Texture { source: \"../gray.tga\" }",
        "Texture { source: \"textures/gray.axleasset\" }",
        "Texture { source: \"textures/gray.tga\", type: \"CubemapHDR\" }",
        "Texture { source: \"textures/gray.tga\", unknown: 1 }"};
    for(const auto& text:invalidDescriptors) {
        CHECK(FileSystem::WriteTextFileAtomically(Assets/"bad.axleasset",text));
        auto invalid=Graph("bad.axleasset","Linear");Property(invalid.Nodes[1],"fallback","White");
        CHECK(!Resolve(invalid,resources,&error)&&resources.Key==savedKey);
    }
    const auto sameTime=std::filesystem::last_write_time(Assets/"textures/gray.tga");
    CHECK(Image("textures/gray.tga",Gray(200)));std::filesystem::last_write_time(Assets/"textures/gray.tga",sameTime);
    CHECK(Resolve(Graph(),resources,&error)&&resources.Key!=first.Key&&resources.Images[0].Source.Image->Mips[0].Pixels[0]==200);
    CHECK(std::filesystem::remove(Assets/"textures/gray.tga"));
    CHECK(!Resolve(Graph(),resources,&error)&&error.Code=="MG_TEXTURE_MISSING");
    auto fallback=Graph();Property(fallback.Nodes[1],"fallback","Magenta");CHECK(Resolve(fallback,resources,&error));
    CHECK(resources.Images[0].UsedFallback&&resources.Warnings.size()==1&&resources.Warnings[0].NodeID==2&&resources.Warnings[0].Source.Line==41);
    CHECK(resources.Images[0].Source.Image->Mips[0].Pixels==std::vector<uint8_t>({255,0,255,255}));
    CHECK(Write("textures/gray.tga",{1,2,3}));CHECK(!Resolve(Graph(),resources,&error)&&error.Code=="MG_TEXTURE_DECODE");
    CHECK(Resolve(fallback,resources,&error)&&resources.Images[0].UsedFallback);
    CHECK(Image("textures/gray.tga",Gray(128)));CHECK(Resolve(fallback,resources,&error)&&!resources.Images[0].UsedFallback&&resources.Warnings.empty());
    auto flat=Graph("missing.tga","Linear");Property(flat.Nodes[1],"fallback","FlatNormal");
    CHECK(Resolve(flat,resources,&error)&&resources.Images[0].Source.Image->Mips[0].Pixels==std::vector<uint8_t>({128,128,255,255}));
    Property(flat.Nodes[1],"colorSpace","sRGB");CHECK(!Resolve(flat,resources,&error));
    const std::string hdrHeader="#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
    std::vector<uint8_t> hdr(hdrHeader.begin(),hdrHeader.end());hdr.insert(hdr.end(),{128,128,128,128});CHECK(Write("half.hdr",hdr));
    CHECK(Resolve(Graph("half.hdr","Linear"),resources,&error)&&resources.Images[0].Source.Image->HDR);
    CHECK(!Resolve(Graph("half.hdr"),resources,&error)&&error.Code=="MG_TEXTURE_COLORSPACE");
    // A CWD file with the same reference cannot satisfy a missing project asset.
    const auto cwd=std::filesystem::current_path();std::filesystem::create_directories(Workspace/"cwd");
    const auto outside=TGA(2,1,Gray(230));CHECK(FileSystem::WriteBinaryFileAtomically(Workspace/"cwd/foreign.tga",outside.data(),outside.size()));
    std::filesystem::current_path(Workspace/"cwd");const bool foundForeign=Resolve(Graph("foreign.tga"),resources,&error);std::filesystem::current_path(cwd);
    CHECK(!foundForeign&&error.Code=="MG_TEXTURE_MISSING");
    // The same configured relative root can name a different project after a CWD
    // change. Identical bytes must not alias its dependency identity/metadata.
    const auto relativeBytes=TGA(2,1,Gray(31));
    for(const std::string name:{"root_a","root_b"}) {
        std::filesystem::create_directories(Workspace/name/"assets");
        CHECK(FileSystem::WriteBinaryFileAtomically(Workspace/name/"assets/relative.tga",relativeBytes.data(),relativeBytes.size()));
    }
    FileSystem::SetAssetDirectory("assets");const auto sourceGeneration=FileSystem::GetSourceGeneration();
    MaterialGraphResourceSet relativeA,relativeB;
    std::filesystem::current_path(Workspace/"root_a");CHECK(Resolve(Graph("relative.tga","Linear"),relativeA,&error));
    std::filesystem::current_path(Workspace/"root_b");CHECK(Resolve(Graph("relative.tga","Linear"),relativeB,&error));
    std::filesystem::current_path(cwd);CHECK(FileSystem::GetSourceGeneration()==sourceGeneration);
    CHECK(relativeA.Key!=relativeB.Key&&relativeA.Images[0].Source.SourcePath!=relativeB.Images[0].Source.SourcePath);
    CHECK(relativeA.Images[0].Source.Revision==relativeB.Images[0].Source.Revision);
    FileSystem::SetAssetDirectory("missing_assets");auto absent=Graph("absent.tga","Linear");Property(absent.Nodes[1],"fallback","Magenta");
    CHECK(!std::filesystem::exists(Workspace/"root_a/missing_assets")&&!std::filesystem::exists(Workspace/"root_b/missing_assets"));
    std::filesystem::current_path(Workspace/"root_a");CHECK(Resolve(absent,relativeA,&error));
    std::filesystem::current_path(Workspace/"root_b");CHECK(Resolve(absent,relativeB,&error));
    std::filesystem::current_path(cwd);FileSystem::SetAssetDirectory(Assets);
    CHECK(relativeA.Key!=relativeB.Key&&relativeA.Images[0].Source.SourcePath.is_absolute()&&relativeB.Images[0].Source.SourcePath.is_absolute());
    CHECK(relativeA.Images[0].UsedFallback&&relativeB.Images[0].UsedFallback);
    CHECK(Image("màu ảnh/xám.tga",Gray(42)));CHECK(Resolve(Graph("màu ảnh/xám.tga","Linear"),resources,&error));
    // Package bytes are authoritative and descriptor dependencies remain root-relative.
    PackageWriter writer;const auto packed=TGA(2,1,Gray(230));
    const std::string packedDescriptor="Texture { source: \"textures/gray.tga\", type: \"Data\" }";
    CHECK(writer.Begin(Workspace/"textures.axlepak")&&writer.AddData("textures/gray.tga",packed.data(),packed.size())&&
        writer.AddData("shaders/gray.tga",packed.data(),packed.size())&&
        writer.AddData("packed.axleasset",packedDescriptor.data(),packedDescriptor.size())&&writer.Finish());
    CHECK(FileSystem::MountPackage(Workspace/"textures.axlepak"));CHECK(Resolve(Graph("packed.axleasset","Linear"),resources,&error));
    CHECK(resources.Dependencies.size()==2&&resources.Images[0].Source.Image->Mips[0].Pixels[0]==230);
    std::filesystem::remove(Assets/"assets/textures/gray.tga");
    for(const std::string prefix:{"assets/","Resources/","Engine/Resources/","Editor/assets/","Sandbox/assets/"}) {
        auto alias=Graph(prefix+"textures/gray.tga","Linear");
        CHECK(!Resolve(alias,resources,&error)&&error.Code=="MG_TEXTURE_MISSING");
        Property(alias.Nodes[1],"fallback","White");CHECK(Resolve(alias,resources,&error)&&resources.Images[0].UsedFallback);
    }
    CHECK(!Resolve(Graph("Shaders/gray.tga","Linear"),resources,&error)&&error.Code=="MG_TEXTURE_MISSING");
    // A physical file at the actual requested path wins over an unrelated alias.
    CHECK(Image("assets/textures/gray.tga",Gray(17)));
    CHECK(Resolve(Graph("assets/textures/gray.tga","Linear"),resources,&error)&&resources.Images[0].Source.Image->Mips[0].Pixels[0]==17);
    FileSystem::UnmountPackage();
    // CPU filtering of live symbols avoids loading compiler-eliminated dependencies.
    MaterialHLSLSource lowered;CHECK(MaterialGraphHLSL::Lower(Graph("not-live.tga"),lowered,&error));
    const std::span<const std::string> none;CHECK(MaterialGraphResources::Resolve(Graph(),lowered,resources,&error,&none)&&resources.Images.empty());
    for(auto policy:{ShaderSamplerPolicy::LinearRepeat,ShaderSamplerPolicy::LinearClamp,ShaderSamplerPolicy::PointRepeat,ShaderSamplerPolicy::PointClamp})
        CHECK(MaterialSamplerDescription(policy)&&MaterialSamplerDescription(policy)->Anisotropy==1);
    for(const std::string sampler:{"LinearRepeat","LinearClamp","PointRepeat","PointClamp"})for(auto format:{RHIShaderFormat::DXIL,RHIShaderFormat::SPIRV}) {
        CompiledMaterialGraphProgram program;CHECK(MaterialGraphProgram::Compile(Graph("textures/gray.tga","Linear",sampler),format,program,&error));
        const auto declared=ShaderPropertyParser::ParseFromHLSL(program.HLSL,&program.Reflection);
        auto plan=BuildMaterialBindingPlan(program.Reflection,{},declared);CHECK(std::holds_alternative<MaterialBindingPlan>(plan));
        ShaderSamplerPolicy expected;CHECK(ParseShaderSamplerPolicy(sampler,expected)&&program.BindingPlan.Samplers[0].Policy==expected);
        CHECK(std::get<MaterialBindingPlan>(plan).Samplers[0].Policy==expected);
    }
    CompiledMaterialGraphProgram normal;CHECK(MaterialGraphProgram::Compile(Normal(Graph("normal.tga","Linear")),RHIShaderFormat::SPIRV,normal,&error));
    CHECK(!RHIDevice::Get().IsInitialized());
    std::cout<<"PASS rooted/VFS dependencies, descriptor precedence, mip/alpha/HDR semantics, explicit fallback/recovery and four-sampler DXC\n";return true;
}

bool Native(RHIType backend) {
    auto& device=RHIDevice::Get();RHIDeviceDesc description;description.PreferredBackend=backend;description.AllowFallback=false;description.Headless=true;
    description.EnableNRIValidation=true;description.EnableValidation=RHIDevice::IsGraphicsAPIValidationAvailable(backend);
    CHECK(device.Init(description)&&device.GetBackend()==backend&&!device.IsSoftwareAdapter());
    struct Shutdown{~Shutdown(){RHIDevice::Get().WaitIdle();RHIDevice::Get().Shutdown();}} shutdown;
    const auto format=backend==RHIType::Vulkan?RHIShaderFormat::SPIRV:RHIShaderFormat::DXIL;
    auto doc=Graph();MaterialGraphTarget target;target.Format=format;target.Native=true;MaterialGraphRuntime runtime(doc.Identity,target);
    CHECK(Image("textures/gray.tga",Gray(128)));CHECK(Reload(runtime,doc));
    const auto srgb=Axle::Tests::DrawMaterialGraph(runtime.Ready());CHECK(Pixel(srgb,{55,55,55}));
    auto original=runtime.Ready();auto* pipeline=MaterialGraphRuntime::GetPipeline(*original);
    const auto oldTime=std::filesystem::last_write_time(Assets/"textures/gray.tga");
    CHECK(Image("textures/gray.tga",Gray(200)));std::filesystem::last_write_time(Assets/"textures/gray.tga",oldTime);
    CHECK(Reload(runtime,doc)&&runtime.Stats().ShaderCompiles==1&&runtime.Ready()->ID!=original->ID);
    CHECK(MaterialGraphRuntime::GetPipeline(*runtime.Ready())==pipeline&&runtime.Ready()->Textures[0].Texture!=original->Textures[0].Texture);
    CHECK(Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{147,147,147})&&Pixel(Axle::Tests::DrawMaterialGraph(original),{55,55,55}));
    const auto good=runtime.Ready();CHECK(std::filesystem::remove(Assets/"textures/gray.tga"));
    CHECK(!runtime.Reload(doc)&&runtime.Diagnostic().Code=="MG_TEXTURE_MISSING"&&runtime.Ready()==good);
    CHECK(Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{147,147,147}));
    Property(doc.Nodes[1],"fallback","Magenta");CHECK(Reload(runtime,doc)&&runtime.Ready()->Resources.Warnings.size()==1);
    CHECK(runtime.Stats().ShaderCompiles==1&&Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{255,0,255}));
    CHECK(Image("textures/gray.tga",Gray(128)));CHECK(Reload(runtime,doc)&&runtime.Ready()->Resources.Warnings.empty());
    // Same image, two simultaneous interpretations and sampler bindings.
    auto mix=doc;Property(mix.Nodes[1],"colorSpace","Linear");
    auto second=mix.Nodes[1];second.ID=4;Property(second,"colorSpace","sRGB");Property(second,"sampler","PointRepeat");mix.Nodes.push_back(second);
    MaterialGraphNode blend;blend.ID=3;blend.Type="Lerp";
    blend.Inputs={{"a",MaterialValueType::Float3,{},{}},{"b",MaterialValueType::Float3,{},{}},{"alpha",MaterialValueType::Float,MaterialValue{MaterialValueType::Float,{0.5f}},{}}};
    blend.Outputs={{"result",MaterialValueType::Float3,{},{}}};mix.Nodes.push_back(blend);
    mix.Connections={{{2,"rgb"},{3,"a"},{}},{{4,"rgb"},{3,"b"},{}},{{3,"result"},{1,"Emission"},{}}};
    CHECK(Reload(runtime,mix)&&runtime.Ready()->Textures.size()==2&&runtime.Ready()->Samplers.size()==2);
    CHECK(runtime.Ready()->Textures[0].Texture!=runtime.Ready()->Textures[1].Texture);
    CHECK(Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{92,92,92}));
    Property(mix.Nodes[2],"colorSpace","Linear");CHECK(Reload(runtime,mix));
    CHECK(runtime.Ready()->Textures[0].Texture==runtime.Ready()->Textures[1].Texture);
    CHECK(Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{128,128,128}));
    // Explicit descriptor source/mip edits and VFS remounts refresh only resources.
    TextureDescriptor descriptor;descriptor.Type="Data";descriptor.Source="textures/gray.tga";descriptor.GenerateMips=false;descriptor.MaxResolution=0;
    CHECK(FileSystem::WriteTextFileAtomically(Assets/"native.axleasset",TextureDescriptor::Serialize(descriptor)));
    auto described=Graph("native.axleasset","Linear");described.Identity=doc.Identity;CHECK(Reload(runtime,described));
    const auto compiles=runtime.Stats().ShaderCompiles;const auto resourceKey=runtime.Ready()->Resources.Key;
    CHECK(Image("textures/new.tga",Gray(42)));descriptor.Source="textures/new.tga";
    CHECK(FileSystem::WriteTextFileAtomically(Assets/"native.axleasset",TextureDescriptor::Serialize(descriptor)));
    CHECK(Reload(runtime,described)&&runtime.Stats().ShaderCompiles==compiles&&runtime.Ready()->Resources.Key!=resourceKey);
    CHECK(runtime.Ready()->Resources.Dependencies.size()==2&&Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{42,42,42}));
    PackageWriter package;const auto packed=TGA(2,1,Gray(230));
    const auto descriptorText=TextureDescriptor::Serialize(descriptor);
    CHECK(package.Begin(Workspace/"native.axlepak")&&package.AddData("native.axleasset",descriptorText.data(),descriptorText.size())&&
        package.AddData("textures/new.tga",packed.data(),packed.size())&&package.Finish());
    CHECK(FileSystem::MountPackage(Workspace/"native.axlepak"));CHECK(Reload(runtime,described)&&runtime.Stats().ShaderCompiles==compiles);
    CHECK(Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{230,230,230}));FileSystem::UnmountPackage();
    CHECK(Reload(runtime,described)&&Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{42,42,42}));
    Property(doc.Nodes[1],"colorSpace","Linear");CHECK(Reload(runtime,doc)&&Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{128,128,128}));
    CHECK(Image("textures/edge.tga",{0,0,0,0,255,255,255,255}));
    Property(doc.Nodes[1],"texturePath","textures/edge.tga");
    for(const std::string sampler:{"LinearRepeat","LinearClamp","PointRepeat","PointClamp"}) {
        Property(doc.Nodes[1],"sampler",sampler);doc.Nodes[1].Inputs[0].Default->Components={1.25f,0.5f};
        CHECK(Reload(runtime,doc));const int value=sampler.ends_with("Repeat")?0:255;
        CHECK(Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{value,value,value}));
        doc.Nodes[1].Inputs[0].Default->Components={0.5f,0.5f};CHECK(Reload(runtime,doc));
        const int midpoint=sampler.starts_with("Linear")?128:255;CHECK(Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{midpoint,midpoint,midpoint}));
    }
    CHECK(Image("orientation.tga",{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255},2,2));
    Property(doc.Nodes[1],"texturePath","orientation.tga");doc.Nodes[1].Inputs[0].Default->Components={0.25f,0.25f};
    CHECK(Reload(runtime,doc)&&Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{0,0,255}));
    // Real minification chooses the generated mip; sRGB RGB averages in linear light.
    CHECK(Image("textures/edge.tga",{0,0,0,0,128,128,128,255}));
    Property(doc.Nodes[1],"texturePath","textures/edge.tga");Property(doc.Nodes[1],"sampler","LinearRepeat");Property(doc.Nodes[1],"colorSpace","sRGB");
    MaterialGraphNode uv;uv.ID=5;uv.Type="InputUV";uv.Outputs={{"uv",MaterialValueType::Float2,{},{}}};doc.Nodes.push_back(uv);
    doc.Connections.push_back({{5,"uv"},{2,"uv"},{}});CHECK(Reload(runtime,doc));
    CHECK(Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready(),{}, {},nullptr,true),{27,27,27}));
    doc.Connections[0].From.Pin="a";CHECK(Reload(runtime,doc));
    CHECK(Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready(),{}, {},nullptr,true),{128,128,128}));
    // Explicit normal decoding, including a declared flat-normal fallback.
    auto normal=Normal(Graph("missing-normal.tga","Linear","PointClamp"));normal.Identity=doc.Identity;
    Property(normal.Nodes[1],"fallback","FlatNormal");CHECK(Reload(runtime,normal));
    CHECK(Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{1,1,255}));
    CHECK(Image("normal.tga",{128,128,255,255},1));
    auto surface=Normal(Graph("normal.tga","Linear","PointClamp"),true);surface.Identity=doc.Identity;
    CHECK(Reload(runtime,surface));EngineMaterialValues light;light.CameraPosition={0,0,4,1};light.SunDirection={0,0,1,1};light.SunColor={1,1,1,1};
    const auto flat=Axle::Tests::DrawMaterialGraph(runtime.Ready(),{}, {},&light);CHECK(!flat.empty());
    CHECK(Image("normal.tga",{255,128,128,255},1));CHECK(Reload(runtime,surface));
    const auto tilted=Axle::Tests::DrawMaterialGraph(runtime.Ready(),{}, {},&light);CHECK(!tilted.empty()&&tilted!=flat);
    CHECK(tilted[(32*64+32)*4]<flat[(32*64+32)*4]);
    // GPU publication errors are never hidden by an authored image fallback.
    auto upload=Graph("textures/gray.tga","Linear","LinearClamp");upload.Identity=doc.Identity;
    CHECK(Reload(runtime,upload));const auto beforeUpload=runtime.Ready();CHECK(Image("textures/gray.tga",Gray(80)));
    bool uploadFault=false;
    {ScopedRHIFault fault([&](std::string_view operation)->std::optional<nri::Result>{if(operation=="UploadData"){uploadFault=true;return nri::Result::FAILURE;}return {};});
        CHECK(!runtime.Reload(upload)&&runtime.Ready()==beforeUpload&&runtime.Diagnostic().Code=="MG_TEXTURE_GPU");}
    CHECK(uploadFault&&Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{128,128,128}));CHECK(Reload(runtime,upload));
    const auto beforeSampler=runtime.Ready();Property(upload.Nodes[1],"sampler","PointRepeat");bool samplerFault=false;
    {ScopedRHIFault fault([&](std::string_view operation)->std::optional<nri::Result>{if(operation=="CreateSampler"){samplerFault=true;return nri::Result::FAILURE;}return {};});
        CHECK(!runtime.Reload(upload)&&runtime.Ready()==beforeSampler&&runtime.Diagnostic().Code=="MG_SAMPLER_GPU"&&runtime.Diagnostic().NodeID==2);}
    CHECK(samplerFault&&Pixel(Axle::Tests::DrawMaterialGraph(runtime.Ready()),{80,80,80}));CHECK(Reload(runtime,upload));
    // Isolate both wrappers from every earlier snapshot before testing command-only
    // ownership. Same-path texture reuse would otherwise hide a lifetime regression.
    auto isolated=upload;Property(isolated.Nodes[1],"texturePath","isolated.tga");CHECK(Image("isolated.tga",Gray(96)));
    CHECK(Reload(runtime,isolated));auto recorded=runtime.Ready();
    std::weak_ptr<const MaterialGraphReadyGeneration> weak=recorded;
    std::weak_ptr<const RHITexture> weakTexture=recorded->Textures[0].Texture;
    std::weak_ptr<const RHISampler> weakSampler=recorded->Samplers[0].Sampler;
    bool wrappersExpired=false;
    const auto retained=Axle::Tests::DrawMaterialGraph(recorded,{},[&]{
        runtime.Clear();recorded.reset();RHIPipelineCache::Get().Clear();
        wrappersExpired=weak.expired()&&weakTexture.expired()&&weakSampler.expired();
    });
    CHECK(wrappersExpired&&Pixel(retained,{96,96,96}));
    CHECK(FileSystem::WriteBinaryFileAtomically(Workspace/(backend==RHIType::Vulkan?"vulkan.rgba":"dx12.rgba"),srgb.data(),srgb.size()));
    CHECK(device.WaitIdle());std::cout<<"PASS native texture/sampler descriptors: numeric sRGB/linear/mip/alpha/orientation, four samplers, normals, hot resource reload, fallbacks and GPU rollback/leases\n";
    return true;
}
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    Axle::Tests::InitializeWorkspace();MaterialGraphDocument fixture;MaterialGraphDiagnostic error;
    if(!MaterialGraph::Load(std::filesystem::u8path(argv[2]),fixture,&error))return 3;
    for(const auto& node:fixture.Nodes)if(node.Type=="SampleTexture2D"){TextureTemplate=node;break;}
    const std::string mode=argv[1];Workspace=std::filesystem::absolute(std::filesystem::u8path(argv[3])/mode);Assets=Workspace/"assets";
    std::filesystem::create_directories(Assets);const auto previousRoot=FileSystem::GetAssetDirectory();FileSystem::SetAssetDirectory(Assets);
    struct Restore {std::filesystem::path Root;~Restore(){FileSystem::UnmountPackage();FileSystem::SetAssetDirectory(Root);}} restore{previousRoot};
    try {bool result=mode=="policy"?CPU():mode=="dx12"?Native(RHIType::Direct3D12):mode=="vulkan"?Native(RHIType::Vulkan):false;
        if(GetRHIValidationErrorCount())result=false;RHIShaderCompiler::Get().Shutdown();return result?0:1;
    }catch(const std::exception& failure){std::cerr<<"FAIL exception "<<failure.what()<<"\n";return 1;}
}
