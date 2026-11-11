#pragma once
#include <Axle/MaterialGraph.hpp>
#include <functional>
#include <map>

namespace Axle {
    struct MaterialHLSLParameter {
        std::string Name;
        std::string Symbol;
        MaterialValue Default;
    };

    struct MaterialHLSLTexture {
        uint64_t NodeID = 0;
        std::string Path;
        std::string ColorSpace;
        std::string Sampler;
        std::string TextureSymbol;
        std::string SamplerSymbol;
        uint32_t Binding = 0;
        std::string Fallback = "Error";
    };

    struct MaterialHLSLSource {
        std::string Declarations;
        std::string SurfaceFunction;
        std::vector<MaterialHLSLParameter> Parameters;
        std::vector<MaterialHLSLTexture> Textures;
        std::string SemanticKey;
        // Only participating definition/emitter revisions; excludes values and paths.
        std::string CompilerKey;
        std::vector<std::pair<uint64_t, std::string>> NodeSources;
    };

    struct MaterialHLSLNodeContext {
        const MaterialIRNode& Node;
        const MaterialNodeDefinition& Definition;
        const std::map<std::string, std::string>& Inputs;
        const std::map<std::string, std::string>& Parameters;
        const std::string* TextureSymbol = nullptr;
        const std::string* SamplerSymbol = nullptr;
        // Inputs and symbols are compiler-generated identifiers/expressions.
        std::string Temporary;
    };

    struct MaterialHLSLNodeEmission {
        std::string Statements;
        std::map<std::string, std::string> Outputs;
        // The surface-output lowerer supplies expressions for the six surface fields.
        std::map<std::string, std::string> Surface;
    };

    using MaterialHLSLEmit = std::function<bool(const MaterialHLSLNodeContext&, MaterialHLSLNodeEmission&, MaterialGraphDiagnostic&)>;

    struct MaterialHLSLNodeLowering {
        std::string Type;
        uint32_t DefinitionRevision = 1;
        uint32_t LoweringRevision = 1;
        MaterialHLSLEmit Emit;
        // A node can request one texture/sampler pair using schema property keys.
        std::optional<std::string> TexturePathProperty;
        std::string ColorSpaceProperty;
        std::string SamplerProperty;
        std::optional<std::string> FallbackProperty;
    };

    // Trusted compiler extensions. Emit callbacks run synchronously; their owning
    // code/modules and any captures must remain alive for the duration of Lower.
    class MaterialHLSLRegistry {
    public:
        bool Register(MaterialHLSLNodeLowering lowering, MaterialGraphDiagnostic* error = nullptr);
        const MaterialHLSLNodeLowering* Find(std::string_view type) const;
        static const MaterialHLSLRegistry& Builtins();
    private:
        std::map<std::string, MaterialHLSLNodeLowering, std::less<>> m_Lowerings;
    };

    class MaterialGraphHLSL {
    public:
        // Revalidates the document; no public mutable IR is trusted as compiler input.
        // Authored parameter names never become HLSL identifiers or source directives.
        // Publishes output only after every node has lowered successfully.
        static bool Lower(const MaterialGraphDocument& document, MaterialHLSLSource& outSource,
                          MaterialGraphDiagnostic* error = nullptr,
                          const MaterialNodeRegistry& definitions = MaterialNodeRegistry::Builtins(),
                          const MaterialHLSLRegistry& lowerings = MaterialHLSLRegistry::Builtins());
    };
}
