#include <Axle/MaterialGraphProgram.hpp>
#include <Axle/MaterialSurfaceShader.hpp>
#include <Axle/CustomShaderInterface.hpp>
#include <Axle/MaterialBindingPlan.hpp>
#include <algorithm>
#include <charconv>

namespace Axle {
namespace {
    MaterialGraphDiagnostic CompileError(const MaterialGraphDocument& document, std::string message, const char* code) {
        MaterialGraphDiagnostic error{code, std::move(message), document.Source, 0, {}};
        constexpr std::string_view prefix="mg_node_";
        const auto position=error.Message.find(prefix);
        if (position!=std::string::npos) {
            const char* first=error.Message.data()+position+prefix.size();
            uint64_t id=0; const auto parsed=std::from_chars(first,error.Message.data()+error.Message.size(),id);
            if (parsed.ec==std::errc{}) for(const auto& node:document.Nodes) if(node.ID==id) {error.NodeID=id;error.Source=node.Source;break;}
        }
        return error;
    }

    std::vector<ShaderPropertyInstance> Defaults(const MaterialHLSLSource& source, const RHI::RHIShaderReflection& reflection) {
        std::vector<ShaderPropertyInstance> properties;
        for(const auto& parameter:source.Parameters) {
            ShaderPropertyInstance property;
            property.Desc.Name=parameter.Symbol; property.Desc.DisplayName=parameter.Name;
            property.Desc.ConstantBufferName="MaterialParameters";
            switch(parameter.Default.Type) {
                case MaterialValueType::Float: property.Desc.Type=ShaderPropertyType::Float; break;
                case MaterialValueType::Float2: property.Desc.Type=ShaderPropertyType::Float2; break;
                case MaterialValueType::Float3: property.Desc.Type=ShaderPropertyType::Float3; break;
                case MaterialValueType::Float4: property.Desc.Type=ShaderPropertyType::Float4; break;
            }
            property.FloatVal=parameter.Default.Components[0]; property.Desc.DefaultFloat=property.FloatVal;
            property.Vec4Val=glm::vec4(0);
            for(size_t i=0;i<parameter.Default.Components.size();++i) property.Vec4Val[static_cast<glm::length_t>(i)]=parameter.Default.Components[i];
            property.Desc.DefaultVec4=property.Vec4Val;
            if(const auto* buffer=reflection.FindConstantBuffer("MaterialParameters"))
                for(const auto& variable:buffer->Variables) if(variable.Name==parameter.Symbol) {property.Desc.Offset=variable.Offset;property.Desc.Size=variable.Size;break;}
            properties.push_back(std::move(property));
        }
        for(const auto& texture:source.Textures) {
            ShaderPropertyInstance property; property.Desc.Name=texture.TextureSymbol;
            property.Desc.Type=ShaderPropertyType::Texture2D; property.Desc.DisplayName=texture.Path;
            property.TexturePath=texture.Path; property.Desc.DefaultTexturePath=texture.Path;
            property.Desc.TextureColorSpace=texture.ColorSpace=="sRGB"?ShaderTextureColorSpace::SRGB:ShaderTextureColorSpace::Linear;
            property.Desc.SamplerName=texture.SamplerSymbol;property.Desc.SamplerPolicy=ShaderSamplerPolicy::LinearRepeat;
            properties.push_back(std::move(property));
        }
        return properties;
    }
}

    bool MaterialGraphProgram::Compile(const MaterialGraphDocument& document, RHI::RHIShaderFormat format,
                                      CompiledMaterialGraphProgram& outProgram, MaterialGraphDiagnostic* error,
                                      const MaterialNodeRegistry& definitions, const MaterialHLSLRegistry& lowerings) {
        CompiledMaterialGraphProgram candidate;
        if (!Prepare(document,format,candidate,error,definitions,lowerings) ||
            !CompilePrepared(document,candidate,error) || !Finalize(candidate,error)) return false;
        outProgram=std::move(candidate); return true;
    }

    bool MaterialGraphProgram::Prepare(const MaterialGraphDocument& document, RHI::RHIShaderFormat format,
                                     CompiledMaterialGraphProgram& candidate, MaterialGraphDiagnostic* error,
                                     const MaterialNodeRegistry& definitions, const MaterialHLSLRegistry& lowerings) {
        if(error) *error={};
        auto fail=[&](std::string message,const char* code) {if(error)*error=CompileError(document,std::move(message),code);return false;};
        if(format!=RHI::RHIShaderFormat::DXIL && format!=RHI::RHIShaderFormat::SPIRV)
            return fail("Material graphs require the DXIL or SPIR-V compiler backend","MG_SHADER_FORMAT");
        if(!MaterialGraphHLSL::Lower(document,candidate.Graph,error,definitions,lowerings)) return false;
        candidate.HLSL=AssembleMaterialSurfaceShader(candidate.Graph.Declarations,candidate.Graph.SurfaceFunction);
        candidate.ParameterSchema=document.Parameters;
        candidate.CullMode=document.CullMode; candidate.DepthWrite=document.DepthWrite;
        candidate.Vertex.Format=candidate.Fragment.Format=format;
        // Collision-free, process-local content identity. The fixed compiler contract
        // is part of this version: SM6_0, HLSL2021, O3, VSMain/PSMain, Vulkan1.3 DX layout.
        candidate.ShaderKey="MaterialGraphShader:1:"+std::to_string(static_cast<unsigned>(format))+":"+
            std::to_string(candidate.Graph.CompilerKey.size())+":"+candidate.Graph.CompilerKey+candidate.HLSL;
        return true;
    }

    bool MaterialGraphProgram::CompilePrepared(const MaterialGraphDocument& document, CompiledMaterialGraphProgram& candidate,
                                             MaterialGraphDiagnostic* error) {
        auto fail=[&](std::string message,const char* code) {if(error)*error=CompileError(document,std::move(message),code);return false;};
        auto& compiler=RHI::RHIShaderCompiler::Get();
        if(!compiler.IsInitialized() && !compiler.Init()) return fail("DXC initialization failed","MG_DXC_INIT");
        RHI::RHIShaderCompileDesc description; description.Source=candidate.HLSL;
        description.SourcePath="mg_generated_material.hlsl"; description.Format=candidate.Vertex.Format;
        description.Stage=RHI::RHIShaderStage::Vertex; description.EntryPoint="VSMain";
        auto vertex=compiler.Compile(description);
        if(!vertex.Success || !vertex.Bytecode.IsValid()) return fail("Vertex compilation failed: "+vertex.ErrorMessage,"MG_DXC_VERTEX");
        description.Stage=RHI::RHIShaderStage::Fragment; description.EntryPoint="PSMain";
        auto fragment=compiler.Compile(description);
        if(!fragment.Success || !fragment.Bytecode.IsValid()) return fail("Fragment compilation failed: "+fragment.ErrorMessage,"MG_DXC_FRAGMENT");
        candidate.Vertex=std::move(vertex.Bytecode); candidate.Fragment=std::move(fragment.Bytecode);
        candidate.Reflection=RHI::RHIShaderReflection::Merge({vertex.Reflection,fragment.Reflection});
        return true;
    }

    bool MaterialGraphProgram::Finalize(CompiledMaterialGraphProgram& candidate, MaterialGraphDiagnostic* error) {
        // Native descriptor range indices must agree when compatible layouts are
        // reused, even if reflection enumeration order changes after recompilation.
        std::sort(candidate.Reflection.Resources.begin(),candidate.Reflection.Resources.end(),[](const auto& a,const auto& b){
            return std::tie(a.Space,a.Type,a.BindPoint,a.Name)<std::tie(b.Space,b.Type,b.BindPoint,b.Name);
        });
        std::sort(candidate.Reflection.ConstantBuffers.begin(),candidate.Reflection.ConstantBuffers.end(),[](const auto& a,const auto& b){
            return std::tie(a.Space,a.BindPoint,a.Name)<std::tie(b.Space,b.BindPoint,b.Name);
        });
        if(!BuildLayout(candidate.Graph,candidate.Reflection,candidate.BindingPlan,candidate.LayoutKey,error)) return false;
        candidate.DefaultProperties=Defaults(candidate.Graph,candidate.Reflection);
        for(auto& schema:candidate.BindingPlan.PropertySchema)
            for(const auto& property:candidate.DefaultProperties)if(property.Desc.Name==schema.Name) schema=property.Desc;
        return true;
    }
}
