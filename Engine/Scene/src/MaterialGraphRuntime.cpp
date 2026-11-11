#include <Axle/MaterialGraphRuntime.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Axle {
namespace {
    bool Fail(MaterialGraphDiagnostic* error, const char* code, std::string message, std::string field = {}) {
        if(error)*error={code,std::move(message),{},0,std::move(field)};
        return false;
    }
    bool ValidateOverrides(const MaterialGraphInstance& instance, const std::vector<MaterialGraphParameter>& schema,
                           MaterialGraphDiagnostic* error) {
        for(const auto& [name,value]:instance.Overrides) {
            const auto found=std::find_if(schema.begin(),schema.end(),[&](const auto& p){return p.Name==name;});
            if(found==schema.end())return Fail(error,"MG_INSTANCE_PARAMETER","Unknown override parameter: "+name,name);
            if(value.Type!=found->Default.Type || value.Components.size()!=static_cast<size_t>(value.Type)+1 ||
               std::any_of(value.Components.begin(),value.Components.end(),[](float v){return !std::isfinite(v);}))
                return Fail(error,"MG_INSTANCE_TYPE","Override must match the declared finite numeric type: "+name,name);
        }
        return true;
    }
    bool SameSchema(const auto& a, const auto& b) {
        if(a.size()!=b.size())return false;
        for(const auto& parameter:a) {
            const auto found=std::find_if(b.begin(),b.end(),[&](const auto& p){return p.Name==parameter.Name;});
            if(found==b.end() || found->Default!=parameter.Default)return false;
        }
        return true;
    }
    CullMode Cull(const std::string& mode) {return mode=="Back"?CullMode::Back:mode=="Front"?CullMode::Front:CullMode::None;}
    std::string PipelineKey(const CompiledMaterialGraphProgram& program, const MaterialGraphTarget& target) {
        // Length prefixes make even adversarial registered source unambiguous.
        auto field=[](std::string_view s){return std::to_string(s.size())+":"+std::string(s);};
        return "MaterialGraphPipeline:2"+field(program.ShaderKey)+field(program.LayoutKey)+field(program.CullMode)+
            field(std::to_string(program.DepthWrite))+field(std::to_string(static_cast<unsigned>(target.Color)))+
            field(std::to_string(static_cast<unsigned>(target.Depth)))+field(std::to_string(target.Samples))+
            field(std::to_string(static_cast<unsigned>(target.Backend)));
    }
}

bool MaterialGraphInstance::Resolve(const CompiledMaterialGraphProgram& program, std::vector<ShaderPropertyInstance>& out,
                                    MaterialGraphDiagnostic* error) const {
    if(error)*error={};
    if(!ValidateOverrides(*this,program.ParameterSchema,error))return false;
    auto properties=program.DefaultProperties;
    for(const auto& parameter:program.Graph.Parameters) {
        const auto found=Overrides.find(parameter.Name);if(found==Overrides.end())continue;
        for(auto& property:properties)if(property.Desc.Name==parameter.Symbol) {
            property.FloatVal=found->second.Components[0];property.Vec4Val=glm::vec4(0);
            for(size_t i=0;i<found->second.Components.size();++i)property.Vec4Val[static_cast<glm::length_t>(i)]=found->second.Components[i];
        }
    }
    out=std::move(properties);return true;
}

bool RenameMaterialGraphParameter(MaterialGraphDocument& document, std::string_view oldName, std::string_view newName,
                                 std::span<MaterialGraphInstance> instances, MaterialGraphDiagnostic* error,
                                 const MaterialNodeRegistry& definitions) {
    if(error)*error={};
    MaterialGraphIR ir;
    if(!MaterialGraph::Validate(document,ir,error,definitions))return false;
    const std::string oldKey(oldName),newKey(newName);
    if(newKey.empty() || std::none_of(document.Parameters.begin(),document.Parameters.end(),[&](const auto& p){return p.Name==oldKey;}))
        return Fail(error,"MG_RENAME","Rename requires an existing source and a nonempty destination");
    if(oldKey!=newKey && std::any_of(document.Parameters.begin(),document.Parameters.end(),[&](const auto& p){return p.Name==newKey;}))
        return Fail(error,"MG_RENAME","Rename destination already exists",newKey);
    for(const auto& instance:instances)if(!ValidateOverrides(instance,document.Parameters,error))return false;
    auto candidate=document;
    std::vector<MaterialGraphInstance> migrated(instances.begin(),instances.end());
    for(auto& parameter:candidate.Parameters)if(parameter.Name==oldKey)parameter.Name=newKey;
    for(auto& node:candidate.Nodes) {
        const auto* definition=definitions.Find(node.Type);
        if(!definition->ParameterAttribute)continue;
        for(auto& attribute:node.Attributes)if(attribute.Name==*definition->ParameterAttribute && std::get<std::string>(attribute.Value)==oldKey)
            attribute.Value=newKey;
    }
    for(auto& instance:migrated)if(oldKey!=newKey) {
        auto entry=instance.Overrides.extract(oldKey);
        if(!entry.empty()){entry.key()=newKey;instance.Overrides.insert(std::move(entry));}
    }
    if(!MaterialGraph::Validate(candidate,ir,error,definitions))return false;
    document=std::move(candidate);
    for(size_t i=0;i<migrated.size();++i)instances[i]=std::move(migrated[i]);
    return true;
}

MaterialGraphRuntime::MaterialGraphRuntime(uint64_t identity, MaterialGraphTarget target, size_t shaderCapacity)
    : m_Identity(identity),m_Target(target),m_ShaderCapacity(shaderCapacity) {}

bool MaterialGraphRuntime::Reload(std::string_view source, const MaterialNodeRegistry& definitions, const MaterialHLSLRegistry& lowerings) {
    MaterialGraphDocument document;
    if(!MaterialGraph::Parse(source,document,&m_Diagnostic,definitions))return false;
    return Reload(document,definitions,lowerings);
}

bool MaterialGraphRuntime::Reload(const MaterialGraphDocument& document, const MaterialNodeRegistry& definitions,
                                 const MaterialHLSLRegistry& lowerings) {
    m_Diagnostic={};
    if(!m_Identity || document.Identity!=m_Identity)return Fail(&m_Diagnostic,"MG_RELOAD_IDENTITY","Document identity differs from this material runtime");
    try {
        auto candidate=std::make_shared<MaterialGraphReadyGeneration>();
        candidate->Target=m_Target;
        auto& program=candidate->Program;
        if(!MaterialGraphProgram::Prepare(document,m_Target.Format,program,&m_Diagnostic,definitions,lowerings,m_Target.Compiler,m_Target.Backend))return false;
        const auto code=m_Shaders.find(program.ShaderKey);
        if(code!=m_Shaders.end()) {
            ++m_Stats.ShaderCacheHits;
            program.Vertex=code->second.Vertex;program.Fragment=code->second.Fragment;program.Reflection=code->second.Reflection;
        } else {
            ++m_Stats.ShaderCompiles;
            if(!MaterialGraphProgram::CompilePrepared(document,program,&m_Diagnostic))return false;
        }
        if(!MaterialGraphProgram::Finalize(program,&m_Diagnostic))return false;
        candidate->PipelineKey=PipelineKey(program,m_Target);
        candidate->ID=CustomProgramCache::AllocateGenerationID();
        if(m_Target.Native) {
            auto& device=RHI::RHIDevice::Get();
            const auto backend=device.GetBackend();
            if(!device.CanCreateResources() || (backend!=RHI::RHIType::Direct3D12 && backend!=RHI::RHIType::Vulkan) ||
                (m_Target.Backend!=RHI::RHIType::None && m_Target.Backend!=backend) ||
                (backend==RHI::RHIType::Vulkan)!=(m_Target.Format==RHI::RHIShaderFormat::SPIRV))
                return Fail(&m_Diagnostic,"MG_RELOAD_DEVICE","Native material target requires the matching active backend");
            candidate->DeviceEpoch=device.GetEpoch();
            auto native=std::make_shared<CustomReadyGeneration>();
            native->GenerationID=candidate->ID;
            native->BindingPlan=program.BindingPlan;native->Reflection=program.Reflection;
            native->VSBytecode=program.Vertex;native->PSBytecode=program.Fragment;
            uint32_t rootSpace=0;for(const auto& r:program.Reflection.Resources)rootSpace=std::max(rootSpace,r.Space+1);
            native->LayoutDescription=program.Reflection.BuildPipelineLayoutDesc(rootSpace);
            if(!native->LayoutDescription.RootConstants.empty())return Fail(&m_Diagnostic,"MG_RELOAD_LAYOUT","Graph cbuffers must use descriptors");
            if(m_Ready && m_Ready->DeviceEpoch==candidate->DeviceEpoch && m_Ready->Program.LayoutKey==program.LayoutKey &&
               m_Ready->Native && m_Ready->Native->PipelineLayout->IsValid())native->PipelineLayout=m_Ready->Native->PipelineLayout;
            else {
                native->PipelineLayout=std::make_shared<RHI::RHIPipelineLayout>();
                ++m_Stats.LayoutBuilds;
                if(!native->PipelineLayout->Init(native->LayoutDescription))return Fail(&m_Diagnostic,"MG_RELOAD_LAYOUT","Native graph layout creation failed");
            }
            candidate->Native=native;
            if(!GetPipeline(*candidate))return Fail(&m_Diagnostic,"MG_RELOAD_PIPELINE","Native graph pipeline validation failed");
            if(!PrepareResources(document,*candidate))return false;
        }
        // Cache only successful complete candidates; stale value/resource metadata is
        // never reused from the code cache. A zero capacity explicitly disables it.
        if(code==m_Shaders.end() && m_ShaderCapacity) {
            while(m_Shaders.size()>=m_ShaderCapacity){m_Shaders.erase(m_ShaderOrder.front());m_ShaderOrder.pop_front();}
            m_ShaderOrder.push_back(program.ShaderKey);
            m_Shaders.emplace(program.ShaderKey,ShaderCode{program.Vertex,program.Fragment,program.Reflection});
        }
        if(m_Ready && m_Ready->PipelineKey==candidate->PipelineKey && m_Ready->DeviceEpoch==candidate->DeviceEpoch &&
           m_Ready->Resources.Key==candidate->Resources.Key &&
           m_Ready->Program.Graph.SemanticKey==program.Graph.SemanticKey && SameSchema(m_Ready->Program.ParameterSchema,program.ParameterSchema))return true;
        // No candidate is mutable through the published interface.
        m_Ready=std::move(candidate);return true;
    } catch(const std::exception& failure) {
        return Fail(&m_Diagnostic,"MG_RELOAD",failure.what());
    }
}

nri::Pipeline* MaterialGraphRuntime::GetPipeline(const MaterialGraphReadyGeneration& generation) {
    const auto& device=RHI::RHIDevice::Get();
    if(!generation.Native || !device.CanSubmit() || generation.DeviceEpoch!=device.GetEpoch())return nullptr;
    return CreateCustomMaterialPipeline(*generation.Native,generation.Target.Color,generation.Target.Depth,generation.Target.Samples,
        SurfaceType::Opaque,Cull(generation.Program.CullMode),generation.Program.DepthWrite);
}

void MaterialGraphRuntime::Clear() {m_Ready.reset();m_Shaders.clear();m_ShaderOrder.clear();m_Diagnostic={};}
}
