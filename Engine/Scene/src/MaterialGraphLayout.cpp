#include <Axle/MaterialGraphProgram.hpp>
#include <Axle/CustomShaderInterface.hpp>
#include <algorithm>
#include <set>

namespace Axle {
namespace {
    void Field(std::string& key, std::string_view field) {
        key += std::to_string(field.size()) + ':'; key += field;
    }
    template<class T> void Number(std::string& key, T number) { Field(key,std::to_string(static_cast<uint64_t>(number))); }
}

bool MaterialGraphProgram::BuildLayout(const MaterialHLSLSource& graph, const RHI::RHIShaderReflection& reflection,
                                     MaterialBindingPlan& outPlan, std::string& outKey, MaterialGraphDiagnostic* error) {
    using namespace RHI;
    if(error)*error={};
    auto fail=[&](std::string message) {if(error)*error={"MG_LAYOUT",std::move(message),{},0,{}};return false;};
    const auto stages=RHIShaderCompiler::StageToNRI(reflection.Stage)|reflection.AdditionalStages;
    if((reflection.Format!=RHIShaderFormat::DXIL && reflection.Format!=RHIShaderFormat::SPIRV) ||
       stages!=(nri::StageBits::VERTEX_SHADER|nri::StageBits::FRAGMENT_SHADER))return fail("Graph layout requires merged vertex/fragment reflection");
    const auto validation=CustomShaderInterface::Validate(reflection);
    std::string collision;
    if(!validation.IsValid || validation.Type!=CustomInterfaceType::Canonical || CustomShaderInterface::HasVulkanBindingCollision(reflection,collision))
        return fail(validation.IsValid?collision:validation.Diagnostic);
    auto normalized=reflection;
    std::sort(normalized.Resources.begin(),normalized.Resources.end(),[](const auto& a,const auto& b){
        return std::tie(a.Space,a.Type,a.BindPoint,a.Name)<std::tie(b.Space,b.Type,b.BindPoint,b.Name);
    });
    std::sort(normalized.ConstantBuffers.begin(),normalized.ConstantBuffers.end(),[](const auto& a,const auto& b){
        return std::tie(a.Space,a.BindPoint,a.Name)<std::tie(b.Space,b.BindPoint,b.Name);
    });
    std::set<std::string> names;
    std::vector<std::string> records;
    for(const auto& resource:reflection.Resources) {
        const auto visibility=RHIShaderCompiler::StageToNRI(resource.Stage)|resource.AdditionalStages;
        if(!names.insert(resource.Name).second || resource.BindCount!=1 ||
           (static_cast<uint64_t>(visibility)&~static_cast<uint64_t>(stages))!=0 ||
           (resource.Stage!=RHIShaderStage::Vertex && resource.Stage!=RHIShaderStage::Fragment))
            return fail("Duplicate, array or unsupported-stage descriptor: "+resource.Name);
        bool expected=false;
        if(resource.Type==RHIResourceType::ConstantBuffer) {
            expected=resource.Space==0 && ((resource.Name=="EngineObject"&&resource.BindPoint==0)||
                (resource.Name=="EngineScene"&&resource.BindPoint==1)||(resource.Name=="MaterialParameters"&&resource.BindPoint==2));
            if(!reflection.FindConstantBuffer(resource.Name))return fail("Missing reflected cbuffer storage: "+resource.Name);
        } else for(const auto& texture:graph.Textures) {
            if(resource.Type==RHIResourceType::Texture && resource.Name==texture.TextureSymbol)
                expected=resource.Space==1 && resource.BindPoint==texture.Binding && resource.TextureDimension==2;
            if(resource.Type==RHIResourceType::Sampler && resource.Name==texture.SamplerSymbol)
                expected=resource.Space==1 && resource.BindPoint==texture.Binding;
        }
        if(!expected)return fail("Descriptor differs from the graph interface: "+resource.Name);
        std::string record="resource";Field(record,resource.Name);Number(record,resource.Type);Number(record,resource.Space);
        Number(record,resource.BindPoint);Number(record,resource.BindCount);Number(record,resource.TextureDimension);
        // Merge represents one stage in Stage and remaining stages in AdditionalStages.
        Number(record,RHIShaderCompiler::StageToNRI(resource.Stage)|resource.AdditionalStages);
        records.push_back(std::move(record));
    }
    std::set<std::string> bufferNames;
    for(auto& buffer:normalized.ConstantBuffers) {
        if(!bufferNames.insert(buffer.Name).second)return fail("Duplicate cbuffer storage: "+buffer.Name);
        const auto* resource=reflection.FindResource(buffer.Name);
        if(!resource || resource->Type!=RHIResourceType::ConstantBuffer || resource->BindPoint!=buffer.BindPoint || resource->Space!=buffer.Space)
            return fail("Cbuffer storage and descriptor disagree: "+buffer.Name);
        std::string record="buffer";Field(record,buffer.Name);Number(record,buffer.Space);Number(record,buffer.BindPoint);Number(record,buffer.Size);
        records.push_back(std::move(record));
        std::sort(buffer.Variables.begin(),buffer.Variables.end(),[](const auto& a,const auto& b){return a.Offset<b.Offset;});
        if((buffer.Name=="EngineObject"&&buffer.Variables.size()!=1)||
           (buffer.Name=="EngineScene"&&buffer.Variables.size()!=4))return fail("Incomplete engine cbuffer storage: "+buffer.Name);
        std::set<std::string> variables;
        for(const auto& variable:buffer.Variables) {
            if(!variables.insert(variable.Name).second || variable.ScalarType!=RHIShaderScalarType::Float || variable.Elements || variable.RowMajor)
                return fail("Unsupported or duplicate reflected variable: "+variable.Name);
            uint32_t rows=1, columns=0, offset=variable.Offset;
            if(buffer.Name=="EngineObject" && variable.Name=="u_Model") {rows=columns=4;offset=0;}
            else if(buffer.Name=="EngineScene") {
                if(variable.Name=="u_ViewProjection") {rows=columns=4;offset=0;}
                else if(variable.Name=="u_CameraPosition") {columns=4;offset=64;}
                else if(variable.Name=="u_SunDirection") {columns=4;offset=80;}
                else if(variable.Name=="u_SunColor") {columns=4;offset=96;}
            } else if(buffer.Name=="MaterialParameters") {
                for(const auto& parameter:graph.Parameters) if(parameter.Symbol==variable.Name)columns=static_cast<uint32_t>(parameter.Default.Type)+1;
                if(variable.Offset%4 || variable.Offset%16+variable.Size>16)return fail("Parameter crosses a DX cbuffer register: "+variable.Name);
            }
            if(!columns || variable.Rows!=rows || variable.Columns!=columns || variable.Size!=rows*columns*4 || variable.Offset!=offset)
                return fail("Reflected type/offset differs from the graph declaration: "+variable.Name);
            record="variable";Field(record,buffer.Name);Field(record,variable.Name);Number(record,variable.Offset);Number(record,variable.Size);
            Number(record,variable.ScalarType);Number(record,variable.Rows);Number(record,variable.Columns);
            records.push_back(std::move(record));
        }
    }
    std::vector<ShaderPropertyDesc> properties;
    for(const auto& texture:graph.Textures) {
        ShaderPropertyDesc desc;desc.Name=texture.TextureSymbol;desc.Type=ShaderPropertyType::Texture2D;
        desc.TextureColorSpace=texture.ColorSpace=="sRGB"?ShaderTextureColorSpace::SRGB:ShaderTextureColorSpace::Linear;
        desc.SamplerName=texture.SamplerSymbol;desc.SamplerPolicy=ShaderSamplerPolicy::LinearRepeat;
        properties.push_back(std::move(desc));
    }
    ValidatedShaderInterface interface;interface.AllowLegacyTemplate=false;
    auto plan=BuildMaterialBindingPlan(normalized,interface,properties);
    if(const auto* issue=std::get_if<MaterialBindingError>(&plan))return fail(issue->Code+": "+issue->Message);
    // Physical Vulkan vertex locations are backend-specific; semantic storage is portable.
    for(const auto& vertex:reflection.VertexInputs) {
        std::string record="vertex";Field(record,vertex.SemanticName);Number(record,vertex.SemanticIndex);Number(record,vertex.ComponentCount);
        records.push_back(std::move(record));
    }
    std::sort(records.begin(),records.end());
    std::string key="MaterialGraphLayout:1";
    for(const auto& record:records)Field(key,record);
    outPlan=std::get<MaterialBindingPlan>(std::move(plan));outKey=std::move(key);return true;
}
}
