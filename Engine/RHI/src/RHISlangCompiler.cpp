#include <Axle/RHI/RHISlangCompiler.hpp>
#include <Axle/Path.hpp>
#include "SlangBuildConfig.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#if AXLE_ENABLE_SLANG
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <combaseapi.h>
#include <wrl/client.h>
#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-tag-version.h>
#include <dxcapi.h>
#include <d3d12shader.h>
#endif

namespace Axle::RHI {
struct RHISlangCompiler::Impl {
    std::filesystem::path Library;
    std::string Error, Tag;
#if AXLE_ENABLE_SLANG
    HMODULE Module = nullptr;
    Slang::ComPtr<slang::IGlobalSession> Global;
    ~Impl() {Global.setNull();if(Module)FreeLibrary(Module);}
#endif
};
RHISlangCompiler::RHISlangCompiler(std::filesystem::path library):m_Impl(std::make_unique<Impl>()) {
    m_Impl->Library=library.empty()?PathFromUtf8(AXLE_SLANG_LIBRARY):std::move(library);
}
RHISlangCompiler::~RHISlangCompiler()=default;
RHISlangCompiler& RHISlangCompiler::Get(){static RHISlangCompiler compiler;return compiler;}
bool RHISlangCompiler::IsEnabled(){return AXLE_ENABLE_SLANG!=0;}
bool RHISlangCompiler::IsAvailable(std::string* error) {
    if(error)error->clear();
#if AXLE_ENABLE_SLANG
    if(m_Impl->Global)return true;
    auto fail=[&](std::string message){m_Impl->Error=std::move(message);if(error)*error=m_Impl->Error;return false;};
    std::error_code ec;
    if(!std::filesystem::is_regular_file(m_Impl->Library,ec))return fail("Optional Slang compiler DLL is missing: "+PathToUtf8(m_Impl->Library));
    const auto configured=PathFromUtf8(AXLE_SLANG_LIBRARY);
    if(!std::filesystem::equivalent(m_Impl->Library,configured,ec) || ec)
        return fail("The Slang evaluation adapter requires its configured SDK; configure AXLE_SLANG_SDK_ROOT to change it");
    if(const auto existing=GetModuleHandleW(L"slang-compiler.dll");existing && !m_Impl->Module) {
        wchar_t path[32768]{};
        if(!GetModuleFileNameW(existing,path,32768) || !std::filesystem::equivalent(std::filesystem::path(path),configured,ec) || ec)
            return fail("A different Slang SDK is already loaded in this process");
    }
    if(!m_Impl->Module)m_Impl->Module=LoadLibraryExW(m_Impl->Library.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!m_Impl->Module)return fail("Cannot load optional Slang compiler DLL (Win32 "+std::to_string(GetLastError())+")");
    using CreateGlobalSession2 = SlangResult (*)(const SlangGlobalSessionDesc*,slang::IGlobalSession**);
    const auto create=reinterpret_cast<CreateGlobalSession2>(GetProcAddress(m_Impl->Module,"slang_createGlobalSession2"));
    if(!create)return fail("Slang SDK does not export the required compilation API");
    SlangGlobalSessionDesc description{};
    if(SLANG_FAILED(create(&description,m_Impl->Global.writeRef())))return fail("Slang global session initialization failed");
    m_Impl->Global->setDownstreamCompilerPath(SLANG_PASS_THROUGH_DXC,AXLE_SLANG_DXC_DIRECTORY);
    m_Impl->Tag=m_Impl->Global->getBuildTagString();
    if(m_Impl->Tag!=SLANG_TAG_VERSION) {m_Impl->Global.setNull();return fail("Slang SDK headers and compiler DLL build tags differ");}
    m_Impl->Error.clear();return true;
#else
    if(error)*error="Optional Slang backend was not built (AXLE_ENABLE_SLANG=OFF)";
    return false;
#endif
}
std::string RHISlangCompiler::Identity() {
    if(!IsAvailable())return "Slang:unavailable";
    return "SlangGraph:1:"+m_Impl->Tag+":"+PathToUtf8(m_Impl->Library)+":SM6_0:column:dx-layout:s128:SPV1_3";
}

#if AXLE_ENABLE_SLANG
namespace {
    std::string Diagnostics(slang::IBlob* blob) {return blob?std::string(static_cast<const char*>(blob->getBufferPointer()),blob->getBufferSize()):std::string{};}
    uint32_t Size(size_t size) {if(size>UINT32_MAX)throw std::runtime_error("Unsupported/unresolved Slang reflection extent");return static_cast<uint32_t>(size);}
    struct SPIRVInterface {
        std::set<std::pair<uint32_t,uint32_t>> Descriptors;
        std::set<uint32_t> Inputs;
    };
    SPIRVInterface InspectSPIRV(const RHIShaderBytecode& code) {
        if(code.Data.size()<20 || code.Data.size()%4)throw std::runtime_error("Malformed Slang SPIR-V size");
        std::vector<uint32_t> words(code.Data.size()/4);std::memcpy(words.data(),code.Data.data(),code.Data.size());
        if(words[0]!=0x07230203)throw std::runtime_error("Slang output is not SPIR-V");
        struct Decorator {uint32_t Set=UINT32_MAX,Binding=UINT32_MAX,Location=UINT32_MAX;};
        std::map<uint32_t,Decorator> decorations;std::map<uint32_t,uint32_t> storage;
        for(size_t i=5;i<words.size();) {
            const auto count=words[i]>>16,operation=words[i]&65535;
            if(!count || count>words.size()-i)throw std::runtime_error("Invalid SPIR-V instruction");
            if(operation==71 && count>=4) {
                auto& d=decorations[words[i+1]];
                if(words[i+2]==33)d.Binding=words[i+3];if(words[i+2]==34)d.Set=words[i+3];if(words[i+2]==30)d.Location=words[i+3];
            }
            if(operation==59 && count>=4)storage[words[i+2]]=words[i+3];
            i+=count;
        }
        SPIRVInterface result;
        for(const auto& [id,space]:storage) {
            const auto d=decorations[id];
            if((space==0 || space==2) && d.Set!=UINT32_MAX && d.Binding!=UINT32_MAX)result.Descriptors.emplace(d.Set,d.Binding);
            if(space==1 && d.Location!=UINT32_MAX)result.Inputs.insert(d.Location);
        }
        return result;
    }
    RHIShaderReflection ReflectSlang(slang::ProgramLayout* layout,slang::IMetadata* metadata,const RHIShaderBytecode& code) {
        using Kind=slang::TypeReflection::Kind;
        RHIShaderReflection result;result.Stage=code.Stage;result.Format=code.Format;result.EntryPoint=code.EntryPoint;
        SPIRVInterface spirv;if(code.Format==RHIShaderFormat::SPIRV)spirv=InspectSPIRV(code);
        for(unsigned i=0;i<layout->getParameterCount();++i) {
            auto* variable=layout->getParameterByIndex(i);auto* type=variable->getTypeLayout();
            const auto kind=type->getKind();RHIShaderResourceBinding binding;binding.Name=variable->getName();
            binding.Stage=code.Stage;binding.Space=variable->getBindingSpace();binding.BindPoint=variable->getBindingIndex();
            SlangParameterCategory category=SLANG_PARAMETER_CATEGORY_NONE;
            switch(kind) {
                case Kind::ConstantBuffer:binding.Type=RHIResourceType::ConstantBuffer;category=SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER;break;
                case Kind::Resource:
                    if(type->getType()->getResourceShape()!=SLANG_TEXTURE_2D || type->getType()->getResourceAccess()!=SLANG_RESOURCE_ACCESS_READ)
                        throw std::runtime_error("Slang graph supports individual read-only Texture2D only");
                    binding.Type=RHIResourceType::Texture;binding.TextureDimension=2;category=SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE;break;
                case Kind::SamplerState:binding.Type=RHIResourceType::Sampler;category=SLANG_PARAMETER_CATEGORY_SAMPLER_STATE;break;
                default:throw std::runtime_error("Unsupported Slang graph resource kind: "+binding.Name);
            }
            bool used=false;
            if(code.Format==RHIShaderFormat::SPIRV) {
                // Slang exposes the target's unified physical binding namespace.
                if(SLANG_FAILED(metadata->isParameterLocationUsed(category,binding.Space,binding.BindPoint,used)))
                    throw std::runtime_error("Slang did not provide post-codegen Vulkan resource usage");
                if(used && !spirv.Descriptors.contains({binding.Space,binding.BindPoint}))
                    throw std::runtime_error("Slang reflection binding is absent from its actual SPIR-V output");
                if(binding.Type==RHIResourceType::Sampler) {
                    if(binding.BindPoint<128)throw std::runtime_error("Slang sampler binding did not preserve the NRI shift");
                    binding.BindPoint-=128;
                }
            } else if(SLANG_FAILED(metadata->isParameterLocationUsed(category,binding.Space,binding.BindPoint,used)))
                throw std::runtime_error("Slang did not provide post-codegen resource usage");
            if(!used)continue;
            result.Resources.push_back(binding);
            if(kind!=Kind::ConstantBuffer)continue;
            auto* element=type->getElementTypeLayout();RHIShaderConstantBuffer buffer;
            buffer.Name=binding.Name;buffer.BindPoint=binding.BindPoint;buffer.Space=binding.Space;buffer.Size=Size(element->getSize());
            for(unsigned field=0;field<element->getFieldCount();++field) {
                auto* member=element->getFieldByIndex(field);auto* value=member->getTypeLayout();auto* scalar=value->getType();
                RHIShaderVariable entry;entry.Name=member->getName();entry.Offset=Size(member->getOffset());entry.Size=Size(value->getSize());
                entry.Rows=Size(scalar->getRowCount());entry.Columns=Size(scalar->getColumnCount());
                if(value->getKind()==Kind::Scalar)entry.Rows=entry.Columns=1;
                if(value->getKind()==Kind::Vector){entry.Rows=1;entry.Columns=Size(scalar->getElementCount());}
                if(value->getKind()==Kind::Array || value->getKind()==Kind::Struct)throw std::runtime_error("Slang graph cbuffers require scalar/vector/matrix fields");
                entry.RowMajor=value->getKind()==Kind::Matrix && value->getMatrixLayoutMode()==SLANG_MATRIX_LAYOUT_ROW_MAJOR;
                switch(scalar->getScalarType()) {
                    case slang::TypeReflection::Float32:entry.ScalarType=RHIShaderScalarType::Float;break;
                    case slang::TypeReflection::Int32:entry.ScalarType=RHIShaderScalarType::Int;break;
                    case slang::TypeReflection::UInt32:entry.ScalarType=RHIShaderScalarType::UInt;break;
                    case slang::TypeReflection::Bool:entry.ScalarType=RHIShaderScalarType::Bool;break;
                    default:throw std::runtime_error("Unsupported Slang graph scalar type");
                }
                buffer.Variables.push_back(std::move(entry));
            }
            result.ConstantBuffers.push_back(std::move(buffer));
        }
        if(code.Stage==RHIShaderStage::Vertex) {
            auto* entry=layout->getEntryPointByIndex(0);
            for(unsigned i=0;i<entry->getParameterCount();++i) {
                auto* input=entry->getParameterByIndex(i);auto* type=input->getTypeLayout();
                if(type->getKind()!=Kind::Struct)throw std::runtime_error("Slang graph vertex input must be the canonical struct");
                for(unsigned f=0;f<type->getFieldCount();++f) {
                    auto* field=type->getFieldByIndex(f);auto* value=field->getType();
                    const auto location=Size(field->getOffset(SLANG_PARAMETER_CATEGORY_VARYING_INPUT));
                    if(code.Format==RHIShaderFormat::SPIRV && !spirv.Inputs.contains(location))continue;
                    const auto* semantic=field->getSemanticName();if(!semantic)throw std::runtime_error("Missing Slang vertex semantic");
                    result.VertexInputs.push_back({semantic,Size(field->getSemanticIndex()),location,Size(value->getElementCount())});
                }
            }
        }
        return result;
    }
    void ValidateDXIL(const RHIShaderBytecode& code,const RHIShaderReflection& reflection) {
        auto& dxc=RHIShaderCompiler::Get();
        if(!dxc.IsInitialized() && !dxc.Init())throw std::runtime_error("DXIL container reflection initialization failed");
        const auto native=dxc.Reflect(code);
        // Slang emits each source cbuffer as a struct wrapper. The existing RHI
        // reader deliberately leaves generic struct fields opaque; inspect these
        // numeric wrapper members here without broadening the legacy shader ABI.
        const auto create=reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(GetModuleHandleW(L"dxcompiler.dll"),"DxcCreateInstance"));
        Slang::ComPtr<IDxcUtils> utils;Slang::ComPtr<ID3D12ShaderReflection> storageReflection;
        // Use the SDK-declared interface IID, matching the bundled DXC reader.
        const GUID iid=__uuidof(ID3D12ShaderReflection);
        if(!create || FAILED(create(CLSID_DxcUtils,IID_PPV_ARGS(utils.writeRef()))))throw std::runtime_error("DXIL storage reader creation failed");
        DxcBuffer blob{code.Data.data(),code.Data.size(),0};
        if(FAILED(utils->CreateReflection(&blob,iid,reinterpret_cast<void**>(storageReflection.writeRef()))))throw std::runtime_error("Cannot inspect actual Slang DXIL storage");
        if(native.Resources.size()!=reflection.Resources.size() || (code.Stage==RHIShaderStage::Vertex && native.VertexInputs.size()!=reflection.VertexInputs.size()))
            throw std::runtime_error("Slang reflection differs from its actual DXIL interface");
        for(const auto& resource:reflection.Resources) {
            const auto actual=std::find_if(native.Resources.begin(),native.Resources.end(),[&](const auto& value){
                return value.Type==resource.Type && value.Space==resource.Space && value.BindPoint==resource.BindPoint;
            });
            if(actual==native.Resources.end() || actual->BindCount!=1 || actual->TextureDimension!=resource.TextureDimension)
                throw std::runtime_error("Slang/DXIL descriptor disagreement: "+resource.Name);
            if(resource.Type!=RHIResourceType::ConstantBuffer)continue;
            const auto* buffer=reflection.FindConstantBuffer(resource.Name);const auto* storage=native.FindConstantBuffer(actual->Name);
            if(!buffer || !storage || buffer->Size!=storage->Size)
                throw std::runtime_error("Slang/DXIL cbuffer storage disagreement: "+resource.Name);
            auto* nativeBuffer=storageReflection->GetConstantBufferByName(actual->Name.c_str());D3D12_SHADER_BUFFER_DESC bufferInfo{};
            if(!nativeBuffer || FAILED(nativeBuffer->GetDesc(&bufferInfo)))throw std::runtime_error("Missing actual DXIL cbuffer");
            std::map<uint32_t,D3D12_SHADER_TYPE_DESC> members;
            auto flatten=[&](auto&& self,ID3D12ShaderReflectionType* type,uint32_t offset,unsigned depth)->void {
                D3D12_SHADER_TYPE_DESC information{};
                if(!type || depth>16 || FAILED(type->GetDesc(&information)) || information.Elements)throw std::runtime_error("Unsupported actual DXIL member type");
                if(information.Class==D3D_SVC_STRUCT) {
                    for(unsigned i=0;i<information.Members;++i) {
                        auto* child=type->GetMemberTypeByIndex(i);D3D12_SHADER_TYPE_DESC field{};
                        if(!child || FAILED(child->GetDesc(&field)))throw std::runtime_error("Cannot inspect DXIL struct member");
                        self(self,child,offset+field.Offset,depth+1);
                    }
                } else if(!members.emplace(offset,information).second)throw std::runtime_error("Overlapping actual DXIL members");
            };
            for(unsigned i=0;i<bufferInfo.Variables;++i) {
                auto* value=nativeBuffer->GetVariableByIndex(i);D3D12_SHADER_VARIABLE_DESC information{};
                if(!value || FAILED(value->GetDesc(&information)))throw std::runtime_error("Cannot inspect DXIL variable");
                flatten(flatten,value->GetType(),information.StartOffset,0);
            }
            if(members.size()!=buffer->Variables.size())throw std::runtime_error("Slang/DXIL member count differs");
            for(const auto& variable:buffer->Variables) {
                const auto member=members.find(variable.Offset);
                if(member==members.end() || member->second.Type!=D3D_SVT_FLOAT || variable.ScalarType!=RHIShaderScalarType::Float ||
                   member->second.Rows!=variable.Rows || member->second.Columns!=variable.Columns ||
                   member->second.Rows*member->second.Columns*4!=variable.Size ||
                   (member->second.Class==D3D_SVC_MATRIX_ROWS)!=variable.RowMajor)
                    throw std::runtime_error("Slang/DXIL member storage disagreement: "+variable.Name);
            }
        }
        for(const auto& input:reflection.VertexInputs) {
            const auto actual=std::find_if(native.VertexInputs.begin(),native.VertexInputs.end(),[&](const auto& value){
                return value.SemanticName==input.SemanticName && value.SemanticIndex==input.SemanticIndex;
            });
            if(actual==native.VertexInputs.end() || actual->Register!=input.Register || actual->ComponentCount!=input.ComponentCount)
                throw std::runtime_error("Slang/DXIL vertex input disagreement: "+input.SemanticName);
        }
    }
}
#endif

RHIShaderCompileResult RHISlangCompiler::Compile(const RHIShaderCompileDesc& description) {
    RHIShaderCompileResult result;
    if(description.Source.empty() || description.Source.find('\0')!=std::string::npos ||
       (description.Stage!=RHIShaderStage::Vertex && description.Stage!=RHIShaderStage::Fragment) ||
       (description.Format!=RHIShaderFormat::DXIL && description.Format!=RHIShaderFormat::SPIRV) || description.ShaderModel!="6_0" ||
       description.HLSLVersion!="2021" || !description.EnableVulkanDXLayout || description.VulkanTargetEnv!="vulkan1.3" || description.GenerateDebugInfo) {
        result.ErrorMessage="Unsupported options for the optional Slang graph profile";return result;
    }
    if(!IsAvailable(&result.ErrorMessage))return result;
#if AXLE_ENABLE_SLANG
    try {
        std::vector<const char*> arguments{"-target",description.Format==RHIShaderFormat::SPIRV?"spirv":"dxil","-profile","sm_6_0",
            "-matrix-layout-column-major",description.Optimization?"-O3":"-O0"};
        if(description.Format==RHIShaderFormat::SPIRV) {
            const std::vector<const char*> vulkan{"-fvk-use-dx-layout","-fvk-b-shift","0","all","-fvk-t-shift","0","all",
                "-fvk-s-shift","128","all","-fvk-use-entrypoint-name","-capability","spirv_1_3"};
            arguments.insert(arguments.end(),vulkan.begin(),vulkan.end());
        }
        slang::SessionDesc sessionDescription;Slang::ComPtr<ISlangUnknown> allocation;
        if(SLANG_FAILED(m_Impl->Global->parseCommandLineArguments(static_cast<int>(arguments.size()),arguments.data(),&sessionDescription,allocation.writeRef())))
            throw std::runtime_error("Slang rejected graph compiler options");
        std::vector<const char*> paths;for(const auto& path:description.IncludeDirs)paths.push_back(path.c_str());
        sessionDescription.searchPaths=paths.data();sessionDescription.searchPathCount=static_cast<SlangInt>(paths.size());
        std::vector<slang::PreprocessorMacroDesc> macros;
        if(description.Format==RHIShaderFormat::SPIRV)macros.push_back({"__spirv__","1"});
        else {macros.push_back({"__directx__","1"});macros.push_back({"_DX12","1"});}
        for(const auto& macro:description.Macros)macros.push_back({macro.Name.c_str(),macro.Definition.c_str()});
        sessionDescription.preprocessorMacros=macros.data();sessionDescription.preprocessorMacroCount=static_cast<SlangInt>(macros.size());
        Slang::ComPtr<slang::ISession> session;
        if(SLANG_FAILED(m_Impl->Global->createSession(sessionDescription,session.writeRef())))throw std::runtime_error("Slang session creation failed");
        Slang::ComPtr<slang::IBlob> diagnostics;
        std::string warnings;
        auto retainWarnings=[&]{const auto message=Diagnostics(diagnostics);if(!message.empty())warnings+=message;};
        auto* module=session->loadModuleFromSourceString("axle_graph",description.SourcePath.c_str(),description.Source.c_str(),diagnostics.writeRef());
        if(!module)throw std::runtime_error(Diagnostics(diagnostics));
        retainWarnings();
        Slang::ComPtr<slang::IEntryPoint> entry;
        if(SLANG_FAILED(module->findAndCheckEntryPoint(description.EntryPoint.c_str(),description.Stage==RHIShaderStage::Vertex?SLANG_STAGE_VERTEX:SLANG_STAGE_FRAGMENT,
            entry.writeRef(),diagnostics.writeRef())))throw std::runtime_error(Diagnostics(diagnostics));
        retainWarnings();
        slang::IComponentType* components[]{module,entry.get()};Slang::ComPtr<slang::IComponentType> composite,linked;
        if(SLANG_FAILED(session->createCompositeComponentType(components,2,composite.writeRef(),diagnostics.writeRef())))throw std::runtime_error(Diagnostics(diagnostics));
        retainWarnings();
        if(SLANG_FAILED(composite->link(linked.writeRef(),diagnostics.writeRef())))throw std::runtime_error(Diagnostics(diagnostics));
        retainWarnings();
        Slang::ComPtr<slang::IBlob> code;
        if(SLANG_FAILED(linked->getEntryPointCode(0,0,code.writeRef(),diagnostics.writeRef())) || !code || !code->getBufferSize())throw std::runtime_error(Diagnostics(diagnostics));
        retainWarnings();
        result.Bytecode.Format=description.Format;result.Bytecode.Stage=description.Stage;result.Bytecode.EntryPoint=description.EntryPoint;
        const auto* first=static_cast<const uint8_t*>(code->getBufferPointer());result.Bytecode.Data.assign(first,first+code->getBufferSize());
        Slang::ComPtr<slang::IMetadata> metadata;
        if(SLANG_FAILED(linked->getEntryPointMetadata(0,0,metadata.writeRef(),diagnostics.writeRef())))throw std::runtime_error("Slang output metadata unavailable: "+Diagnostics(diagnostics));
        retainWarnings();
        auto* layout=linked->getLayout(0,diagnostics.writeRef());if(!layout)throw std::runtime_error("Slang output layout unavailable: "+Diagnostics(diagnostics));
        retainWarnings();
        result.Reflection=ReflectSlang(layout,metadata,result.Bytecode);
        if(description.Format==RHIShaderFormat::DXIL)ValidateDXIL(result.Bytecode,result.Reflection);
        result.WarningMessage=std::move(warnings);
        result.Success=true;
    }catch(const std::exception& failure){result.Success=false;result.Bytecode={};result.Reflection={};result.ErrorMessage=failure.what();}
#endif
    return result;
}
}
