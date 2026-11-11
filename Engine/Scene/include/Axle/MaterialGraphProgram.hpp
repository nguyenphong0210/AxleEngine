#pragma once
#include <Axle/MaterialGraphHLSL.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <Axle/ShaderPropertyReflection.hpp>

namespace Axle {
    struct CompiledMaterialGraphProgram {
        MaterialHLSLSource Graph;
        std::string HLSL;
        RHI::RHIShaderBytecode Vertex;
        RHI::RHIShaderBytecode Fragment;
        RHI::RHIShaderReflection Reflection;
        std::vector<ShaderPropertyInstance> DefaultProperties;
    };

    class MaterialGraphProgram {
    public:
        // Compiles both stages with existing DXC. No GPU device is needed. Formats
        // are explicitly DXIL or SPIR-V; GL/DXBC are not implicit fallbacks.
        // Failed lower/compile attempts preserve the previous output artifact.
        static bool Compile(const MaterialGraphDocument& document, RHI::RHIShaderFormat format,
                            CompiledMaterialGraphProgram& outProgram, MaterialGraphDiagnostic* error = nullptr,
                            const MaterialNodeRegistry& definitions = MaterialNodeRegistry::Builtins(),
                            const MaterialHLSLRegistry& lowerings = MaterialHLSLRegistry::Builtins());
    };
}
