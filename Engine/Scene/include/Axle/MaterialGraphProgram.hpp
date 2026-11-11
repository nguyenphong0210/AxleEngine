#pragma once
#include <Axle/MaterialGraphHLSL.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <Axle/ShaderPropertyReflection.hpp>
#include <Axle/MaterialBindingPlan.hpp>

namespace Axle {
    enum class MaterialGraphCompilerBackend : uint8_t { DXC, Slang };
    struct CompiledMaterialGraphProgram {
        MaterialGraphCompilerBackend Compiler = MaterialGraphCompilerBackend::DXC;
        MaterialHLSLSource Graph;
        std::string HLSL;
        RHI::RHIShaderBytecode Vertex;
        RHI::RHIShaderBytecode Fragment;
        RHI::RHIShaderReflection Reflection;
        std::vector<ShaderPropertyInstance> DefaultProperties;
        std::vector<MaterialGraphParameter> ParameterSchema;
        MaterialBindingPlan BindingPlan;
        std::string ShaderKey;
        std::string LayoutKey;
        std::string CullMode;
        bool DepthWrite = true;
    };

    class MaterialGraphProgram {
    public:
        // Compiles both stages with existing DXC. No GPU device is needed. Formats
        // are explicitly DXIL or SPIR-V; GL/DXBC are not implicit fallbacks.
        // Failed lower/compile attempts preserve the previous output artifact.
        static bool Compile(const MaterialGraphDocument& document, RHI::RHIShaderFormat format,
                            CompiledMaterialGraphProgram& outProgram, MaterialGraphDiagnostic* error = nullptr,
                            const MaterialNodeRegistry& definitions = MaterialNodeRegistry::Builtins(),
                            const MaterialHLSLRegistry& lowerings = MaterialHLSLRegistry::Builtins(),
                            MaterialGraphCompilerBackend compiler = MaterialGraphCompilerBackend::DXC);
        // Validates graph-specific reflected storage/resources, returning a canonical
        // backend-independent descriptor/storage compatibility key and packing plan.
        // All outputs remain unchanged on failure. No synthetic legacy metadata.
        static bool BuildLayout(const MaterialHLSLSource& graph, const RHI::RHIShaderReflection& reflection,
                                MaterialBindingPlan& plan, std::string& key, MaterialGraphDiagnostic* error = nullptr);
    private:
        friend class MaterialGraphRuntime;
        static bool Prepare(const MaterialGraphDocument&, RHI::RHIShaderFormat, CompiledMaterialGraphProgram&,
                            MaterialGraphDiagnostic*, const MaterialNodeRegistry&, const MaterialHLSLRegistry&, MaterialGraphCompilerBackend);
        static bool CompilePrepared(const MaterialGraphDocument&, CompiledMaterialGraphProgram&, MaterialGraphDiagnostic*);
        static bool Finalize(CompiledMaterialGraphProgram&, MaterialGraphDiagnostic*);
    };
}
