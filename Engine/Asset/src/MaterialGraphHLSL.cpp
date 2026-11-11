#include <Axle/MaterialGraphHLSL.hpp>
#include "MaterialGraphInternal.hpp"
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Axle {
namespace {
    [[noreturn]] void Fail(std::string code, std::string message, uint64_t node = 0, std::string field = {}) {
        throw MaterialGraphDiagnostic{std::move(code), std::move(message), {}, node, std::move(field)};
    }

    std::string TypeName(MaterialValueType type) {
        const auto width = MaterialGraphInternal::Width(type);
        if (!width) Fail("MG_HLSL_TYPE", "Unsupported numeric type");
        return width == 1 ? "float" : "float" + std::to_string(width);
    }

    std::string Literal(const MaterialValue& value) {
        std::ostringstream out; out.imbue(std::locale::classic());
        out << std::scientific << std::setprecision(std::numeric_limits<float>::max_digits10);
        out << TypeName(value.Type) << '(';
        for (size_t i=0; i<value.Components.size(); ++i) {
            if (i) out << ", ";
            out << (value.Components[i] == 0 ? 0.0f : value.Components[i]);
        }
        return out.str() + ')';
    }

    std::string StringProperty(const MaterialIRNode& node, const std::string& name) {
        const auto property = std::find_if(node.Properties.begin(), node.Properties.end(), [&](const auto& p){return p.first == name;});
        if (property == node.Properties.end() || !std::holds_alternative<std::string>(property->second))
            Fail("MG_HLSL_RESOURCE", "Lowering requires a declared string resource property", node.ID, name);
        return std::get<std::string>(property->second);
    }

    MaterialHLSLSource LowerValidated(const MaterialGraphIR& ir, const MaterialNodeRegistry& definitions, const MaterialHLSLRegistry& lowerings) {
        MaterialHLSLSource source;
        std::map<std::string, std::string> parameterSymbols;
        std::ostringstream declarations;
        if (!ir.Parameters.empty()) {
            declarations << "cbuffer MaterialParameters : register(b2, space0) {\n";
            for (const auto& parameter : ir.Parameters) {
                const auto symbol = "mg_p" + std::to_string(source.Parameters.size());
                parameterSymbols.emplace(parameter.Name, symbol);
                source.Parameters.push_back({parameter.Name, symbol, parameter.Default});
                declarations << "    " << TypeName(parameter.Default.Type) << ' ' << symbol << ";\n";
            }
            declarations << "};\n";
        }
        std::ostringstream function;
        function << "MaterialSurface AxleEvaluateMaterial(float2 graphUV) {\n";
        std::map<std::pair<uint64_t, std::string>, std::string> outputs;
        std::ostringstream revisionKey;
        bool surfaceEmitted = false;
        for (const auto& node : ir.Nodes) {
            const auto* definition = definitions.Find(node.Type);
            const auto* lowering = lowerings.Find(node.Type);
            if (!lowering) Fail("MG_HLSL_NODE", "Node has no registered HLSL lowering: " + node.Type, node.ID);
            if (lowering->DefinitionRevision != node.DefinitionRevision)
                Fail("MG_HLSL_REVISION", "Node definition revision does not match HLSL lowering", node.ID);
            revisionKey << node.ID << ':' << lowering->DefinitionRevision << ':' << lowering->LoweringRevision << ';';
            std::map<std::string, std::string> inputs;
            for (const auto& input : node.Inputs) {
                std::string expression;
                if (input.Connection) {
                    const auto found = outputs.find({input.Connection->NodeID, input.Connection->Pin});
                    if (found == outputs.end()) Fail("MG_HLSL_INPUT", "Input is unavailable in topological lowering order", node.ID, input.Name);
                    expression = found->second;
                    if (input.Conversion == MaterialInputConversion::ScalarSplat) {
                        std::string components;
                        for (size_t i=0;i<MaterialGraphInternal::Width(input.Type);++i) {
                            if (i) components += ", ";
                            components += expression;
                        }
                        expression = TypeName(input.Type) + '(' + components + ')';
                    }
                } else expression = Literal(*input.Default);
                inputs.emplace(input.Name, std::move(expression));
            }
            std::optional<MaterialHLSLTexture> texture;
            if (lowering->TexturePathProperty) {
                const auto binding = static_cast<uint32_t>(source.Textures.size());
                texture = MaterialHLSLTexture{node.ID, StringProperty(node, *lowering->TexturePathProperty),
                    StringProperty(node, lowering->ColorSpaceProperty), StringProperty(node, lowering->SamplerProperty),
                    "mg_t" + std::to_string(binding), "mg_s" + std::to_string(binding), binding};
                if (!MaterialGraphInternal::CanonicalAssetPath(texture->Path)) Fail("MG_HLSL_RESOURCE", "Texture lowering requires a canonical root-relative path", node.ID, *lowering->TexturePathProperty);
                if (texture->Sampler != "LinearRepeat") Fail("MG_HLSL_SAMPLER", "C12-02 native baseline supports LinearRepeat sampling", node.ID, lowering->SamplerProperty);
                if (texture->ColorSpace != "sRGB" && texture->ColorSpace != "Linear") Fail("MG_HLSL_RESOURCE", "Unsupported sample color space", node.ID);
                declarations << "// [TextureColorSpace(" << texture->ColorSpace << ")] [TextureSampler(" << texture->SamplerSymbol << ", LinearRepeat)]\n";
                declarations << "Texture2D " << texture->TextureSymbol << " : register(t" << binding << ", space1);\n";
                declarations << "SamplerState " << texture->SamplerSymbol << " : register(s" << binding << ", space1);\n";
                source.Textures.push_back(*texture);
            }
            MaterialHLSLNodeContext context{node, *definition, inputs, parameterSymbols,
                texture ? &texture->TextureSymbol : nullptr, texture ? &texture->SamplerSymbol : nullptr,
                "mg_n" + std::to_string(node.ID) + "_temp"};
            MaterialHLSLNodeEmission emission; MaterialGraphDiagnostic error;
            bool emitted=false;
            try { emitted=lowering->Emit(context, emission, error); }
            catch (MaterialGraphDiagnostic diagnostic) { if (!diagnostic.NodeID) diagnostic.NodeID=node.ID; throw diagnostic; }
            catch (const std::exception& failure) { Fail("MG_HLSL_EMIT",failure.what(),node.ID); }
            catch (...) { Fail("MG_HLSL_EMIT","Registered node lowering threw an unknown exception",node.ID); }
            if (!emitted) {
                if (error.Code.empty()) error.Code = "MG_HLSL_EMIT";
                if (error.Message.empty()) error.Message = "Registered node lowering failed";
                if (!error.NodeID) error.NodeID = node.ID;
                throw error;
            }
            const auto virtualSource = "mg_node_" + std::to_string(node.ID);
            source.NodeSources.emplace_back(node.ID, virtualSource);
            function << "#line 1 \"" << virtualSource << "\"\n" << emission.Statements;
            if (!emission.Statements.empty() && emission.Statements.back() != '\n') function << '\n';
            if (emission.Outputs.size() != node.Outputs.size()) Fail("MG_HLSL_OUTPUT", "Lowering output count differs from node signature", node.ID);
            for (const auto& [name, type] : node.Outputs) {
                const auto expression = emission.Outputs.find(name);
                if (expression == emission.Outputs.end() || expression->second.empty()) Fail("MG_HLSL_OUTPUT", "Missing lowering output expression", node.ID, name);
                const auto symbol = "mg_n" + std::to_string(node.ID) + "_o" + std::to_string(outputs.size());
                function << "    " << TypeName(type) << ' ' << symbol << " = " << expression->second << ";\n";
                outputs.emplace(std::pair{node.ID, name}, symbol);
            }
            if (node.ID == ir.OutputNode) {
                const std::vector<std::string> fields{"BaseColor", "Metallic", "Roughness", "Normal", "Emission", "AO"};
                if (emission.Surface.size() != fields.size()) Fail("MG_HLSL_SURFACE", "Surface lowering requires all six surface fields", node.ID);
                function << "    MaterialSurface surface;\n";
                for (const auto& field : fields) {
                    const auto expression = emission.Surface.find(field);
                    if (expression == emission.Surface.end() || expression->second.empty()) Fail("MG_HLSL_SURFACE", "Missing surface field", node.ID, field);
                    function << "    surface." << field << " = " << expression->second << ";\n";
                }
                surfaceEmitted = true;
            } else if (!emission.Surface.empty()) Fail("MG_HLSL_SURFACE", "Only the surface output can emit surface fields", node.ID);
        }
        if (!surfaceEmitted) Fail("MG_HLSL_SURFACE", "No surface was lowered");
        function << "    return surface;\n}\n#line 1 \"mg_surface_template\"\n";
        source.Declarations = declarations.str(); source.SurfaceFunction = function.str();
        source.SemanticKey = "MaterialHLSL:1\n" + ir.SemanticKey + revisionKey.str();
        return source;
    }
}

    bool MaterialHLSLRegistry::Register(MaterialHLSLNodeLowering lowering, MaterialGraphDiagnostic* error) {
        if (error) *error = {};
        if (lowering.Type.empty() || !lowering.DefinitionRevision || !lowering.LoweringRevision || !lowering.Emit ||
            (lowering.TexturePathProperty && (lowering.TexturePathProperty->empty() || lowering.ColorSpaceProperty.empty() || lowering.SamplerProperty.empty())) ||
            (!lowering.TexturePathProperty && (!lowering.ColorSpaceProperty.empty() || !lowering.SamplerProperty.empty())) || m_Lowerings.contains(lowering.Type)) {
            if (error) *error = {"MG_HLSL_DEFINITION", "Invalid or duplicate HLSL lowering registration", {}, 0, lowering.Type};
            return false;
        }
        const auto key = lowering.Type; m_Lowerings.emplace(key, std::move(lowering)); return true;
    }

    const MaterialHLSLNodeLowering* MaterialHLSLRegistry::Find(std::string_view type) const {
        const auto found = m_Lowerings.find(type); return found == m_Lowerings.end() ? nullptr : &found->second;
    }

    bool MaterialGraphHLSL::Lower(const MaterialGraphDocument& document, MaterialHLSLSource& outSource, MaterialGraphDiagnostic* error,
                                  const MaterialNodeRegistry& definitions, const MaterialHLSLRegistry& lowerings) {
        MaterialGraphIR ir;
        if (!MaterialGraph::Validate(document, ir, error, definitions)) return false;
        try {
            auto candidate = LowerValidated(ir, definitions, lowerings); outSource = std::move(candidate); return true;
        } catch (const MaterialGraphDiagnostic& diagnostic) {
            if (error) {
                *error = diagnostic;
                const auto node = std::find_if(document.Nodes.begin(), document.Nodes.end(), [&](const auto& n){return n.ID == error->NodeID;});
                if (node != document.Nodes.end()) error->Source = node->Source;
            }
            return false;
        } catch (const std::exception& failure) {
            if (error) *error = {"MG_HLSL_EMIT", failure.what(), {}, 0, {}};
            return false;
        }
    }
}
