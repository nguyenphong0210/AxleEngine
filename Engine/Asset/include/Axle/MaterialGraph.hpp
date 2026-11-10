#pragma once

#include <Axle/AssetDSL.hpp>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace Axle {

    enum class MaterialValueType : uint8_t { Float, Float2, Float3, Float4 };

    struct MaterialValue {
        MaterialValueType Type = MaterialValueType::Float;
        std::vector<float> Components;
        bool operator==(const MaterialValue&) const = default;
    };

    struct MaterialGraphDiagnostic {
        std::string Code;
        std::string Message;
        AssetSourceLocation Source;
        uint64_t NodeID = 0;
        std::string Field;
    };

    struct MaterialGraphParameter {
        std::string Name;
        MaterialValue Default;
        AssetSourceLocation Source;
    };

    struct MaterialGraphPin {
        std::string Name;
        MaterialValueType Type = MaterialValueType::Float;
        std::optional<MaterialValue> Default;
        AssetSourceLocation Source;
    };

    using MaterialNodePropertyValue = std::variant<std::string, bool, MaterialValue>;

    struct MaterialNodeProperty {
        std::string Name;
        MaterialNodePropertyValue Value;
        AssetSourceLocation Source;
    };

    struct MaterialGraphNode {
        uint64_t ID = 0;
        std::string Type;
        std::array<float, 2> EditorPosition{};
        // Attributes are node-level fields (e.g. ParameterRef.parameter).
        // Properties are fields inside the authored properties block.
        std::vector<MaterialNodeProperty> Attributes;
        std::vector<MaterialNodeProperty> Properties;
        std::vector<MaterialGraphPin> Inputs;
        std::vector<MaterialGraphPin> Outputs;
        AssetSourceLocation Source;
    };

    enum class MaterialPropertyKind : uint8_t { String, Boolean, Number };

    struct MaterialNodePropertyDefinition {
        std::string Name;
        MaterialPropertyKind Kind = MaterialPropertyKind::String;
        MaterialValueType NumberType = MaterialValueType::Float;
        bool Required = true;
        std::optional<MaterialNodePropertyValue> Default;
        std::vector<std::string> AllowedStrings;
        bool AssetPath = false;
        std::string DiagnosticCode = "MG_PROPERTY";
    };

    struct MaterialNodePinDefinition {
        std::string Name;
        // nullopt binds to the parameter selected by ParameterAttribute.
        std::optional<MaterialValueType> Type = MaterialValueType::Float;
        std::optional<MaterialValue> Default; // Initial authoring value when creating nodes.
    };

    struct MaterialNodeSignature {
        std::vector<MaterialNodePinDefinition> Inputs;
        std::vector<MaterialNodePinDefinition> Outputs;
    };

    enum class MaterialNodeRole : uint8_t { Operation, SurfaceOutput };

    struct MaterialNodeDefinition {
        std::string Type;
        uint32_t Revision = 1;
        MaterialNodeRole Role = MaterialNodeRole::Operation;
        std::vector<MaterialNodeSignature> Signatures;
        std::vector<MaterialNodePropertyDefinition> Attributes;
        std::vector<MaterialNodePropertyDefinition> Properties;
        std::optional<std::string> ParameterAttribute;
    };

    // Explicitly owned, copyable schemas: no process-global mutable registration or
    // callbacks into plugin DLLs. Registration rejects duplicate keys and bad schemas.
    class MaterialNodeRegistry {
    public:
        bool Register(MaterialNodeDefinition definition, MaterialGraphDiagnostic* error = nullptr);
        const MaterialNodeDefinition* Find(std::string_view type) const;
        static const MaterialNodeRegistry& Builtins();
    private:
        std::map<std::string, MaterialNodeDefinition, std::less<>> m_Definitions;
    };

    struct MaterialGraphEndpoint {
        uint64_t NodeID = 0;
        std::string Pin;
        bool operator==(const MaterialGraphEndpoint&) const = default;
    };

    struct MaterialGraphConnection {
        MaterialGraphEndpoint From;
        MaterialGraphEndpoint To;
        AssetSourceLocation Source;
    };

    // Authored state. Saving never removes unused nodes, parameters, or defaults.
    struct MaterialGraphDocument {
        uint64_t Identity = 0;
        uint32_t SchemaVersion = 1;
        std::string Name;
        std::string ShadingModel = "DefaultLit";
        std::string SurfaceType = "Opaque";
        std::string CullMode = "Back";
        bool DepthWrite = true;
        std::vector<MaterialGraphParameter> Parameters;
        std::vector<MaterialGraphNode> Nodes;
        std::vector<MaterialGraphConnection> Connections;
        AssetSourceLocation Source;
    };

    enum class MaterialInputConversion : uint8_t { Exact, ScalarSplat };

    struct MaterialIRInput {
        std::string Name;
        MaterialValueType Type = MaterialValueType::Float;
        std::optional<MaterialValue> Default;
        std::optional<MaterialGraphEndpoint> Connection;
        MaterialInputConversion Conversion = MaterialInputConversion::Exact;
    };

    struct MaterialIRNode {
        uint64_t ID = 0;
        std::string Type;
        uint32_t DefinitionRevision = 0;
        std::vector<std::pair<std::string, MaterialNodePropertyValue>> Attributes;
        std::vector<std::pair<std::string, MaterialNodePropertyValue>> Properties;
        std::vector<MaterialIRInput> Inputs;
        // No editor positions, source locations, or authored defaults on outputs.
        std::vector<std::pair<std::string, MaterialValueType>> Outputs;
    };

    // Validated CPU compiler input; independent of RHI, Editor and shader languages.
    struct MaterialGraphIR {
        uint64_t OutputNode = 0;
        std::vector<MaterialIRNode> Nodes; // Reachable nodes, deterministic topological order.
        std::vector<MaterialGraphParameter> Parameters; // Used keys, sorted by name.
        // Full canonical semantic description, not a GPU shader/pipeline cache key.
        // Includes effective defaults and render state; excludes layout and unused data.
        std::string SemanticKey;
    };

    class MaterialGraph {
    public:
        static MaterialGraphDocument Create(std::string name);
        // All output arguments remain unchanged on failure. Diagnostics are first-error.
        static bool Parse(std::string_view source, MaterialGraphDocument& outDocument,
                          MaterialGraphDiagnostic* error = nullptr,
                          const MaterialNodeRegistry& registry = MaterialNodeRegistry::Builtins());
        static bool Validate(const MaterialGraphDocument& document, MaterialGraphIR& outIR,
                             MaterialGraphDiagnostic* error = nullptr,
                             const MaterialNodeRegistry& registry = MaterialNodeRegistry::Builtins());
        static bool Serialize(const MaterialGraphDocument& document, std::string& outSource,
                              MaterialGraphDiagnostic* error = nullptr,
                              const MaterialNodeRegistry& registry = MaterialNodeRegistry::Builtins());
        static bool Load(const std::filesystem::path& path, MaterialGraphDocument& outDocument,
                         MaterialGraphDiagnostic* error = nullptr,
                         const MaterialNodeRegistry& registry = MaterialNodeRegistry::Builtins());
        static bool Save(const std::filesystem::path& path, const MaterialGraphDocument& document,
                         MaterialGraphDiagnostic* error = nullptr,
                         const MaterialNodeRegistry& registry = MaterialNodeRegistry::Builtins());
    };
}
