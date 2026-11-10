#include <Axle/MaterialGraph.hpp>
#include "MaterialGraphInternal.hpp"
#include <Axle/FileSystem.hpp>
#include <Axle/UUID.hpp>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace Axle {
namespace {
    using Location = AssetSourceLocation;
    using ValueType = MaterialValueType;

    [[noreturn]] void Fail(std::string code, std::string message, Location source = {},
                           uint64_t node = 0, std::string field = {}) {
        throw MaterialGraphDiagnostic{std::move(code), std::move(message), source, node, std::move(field)};
    }

    template<class F> bool Checked(MaterialGraphDiagnostic* error, F&& operation) {
        if (error) *error = {};
        try { operation(); return true; }
        catch (const MaterialGraphDiagnostic& diagnostic) { if (error) *error = diagnostic; return false; }
        catch (const std::filesystem::filesystem_error& failure) {
            if (error) *error = {"MG_IO", failure.what(), {}, 0, {}};
            return false;
        }
    }

    size_t Width(ValueType type) {
        switch (type) {
            case ValueType::Float: return 1;
            case ValueType::Float2: return 2;
            case ValueType::Float3: return 3;
            case ValueType::Float4: return 4;
        }
        Fail("MG_TYPE", "Unsupported value type");
    }

    std::string TypeName(ValueType type) {
        static constexpr const char* names[] = {"Float", "Float2", "Float3", "Float4"};
        return names[Width(type)-1];
    }

    ValueType ParseType(const std::string& text, Location source) {
        for (auto type : {ValueType::Float, ValueType::Float2, ValueType::Float3, ValueType::Float4})
            if (text == TypeName(type)) return type;
        Fail("MG_TYPE", "Unsupported value type '" + text + "'", source);
    }

    std::string_view TrimValue(std::string_view value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
        return value;
    }

    uint64_t Unsigned(std::string_view text, Location source) {
        uint64_t value = 0;
        auto [end, ec] = std::from_chars(text.data(), text.data()+text.size(), value);
        if (ec != std::errc{} || end != text.data()+text.size() || text.empty())
            Fail("MG_NUMBER", "Expected an unsigned 64-bit integer", source);
        return value;
    }

    float Number(std::string_view text, Location source) {
        text = TrimValue(text);
        float value = 0;
        auto [end, ec] = std::from_chars(text.data(), text.data()+text.size(), value);
        if (ec != std::errc{} || end != text.data()+text.size() || text.empty() || !std::isfinite(value))
            Fail("MG_NUMBER", "Expected a finite Float value", source);
        return value;
    }

    MaterialValue Value(std::string_view text, ValueType type, Location source) {
        text = TrimValue(text);
        MaterialValue value{type, {}};
        if (type == ValueType::Float) value.Components.push_back(Number(text, source));
        else {
            if (text.size() < 2 || text.front() != '[' || text.back() != ']')
                Fail("MG_VALUE", "Expected a vector for " + TypeName(type), source);
            text.remove_prefix(1); text.remove_suffix(1);
            while (true) {
                const auto comma = text.find(',');
                value.Components.push_back(Number(text.substr(0, comma), source));
                if (comma == std::string_view::npos) break;
                text.remove_prefix(comma+1);
            }
        }
        if (value.Components.size() != Width(type)) Fail("MG_VALUE", "Incorrect vector width", source);
        return value;
    }

    void CheckValue(const MaterialValue& value, ValueType expected, Location source) {
        if (value.Type != expected || value.Components.size() != Width(expected) ||
            !std::all_of(value.Components.begin(), value.Components.end(), [](float v){return std::isfinite(v);}))
            Fail("MG_VALUE", "Default value must match its declared finite type", source);
    }

    void CheckString(const std::string& value, Location source) {
        for (unsigned char c : value) if (c < 32 && c != '\n' && c != '\r' && c != '\t' && c != '\b' && c != '\f')
            Fail("MG_STRING", "String contains an unsupported control character", source);
    }

    std::string FormatValue(const MaterialValue& value) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<float>::max_digits10);
        if (value.Type != ValueType::Float) out << '[';
        for (size_t i = 0; i < value.Components.size(); ++i) {
            if (i) out << ", ";
            out << (value.Components[i] == 0 ? 0.0f : value.Components[i]);
        }
        if (value.Type != ValueType::Float) out << ']';
        return out.str();
    }

    void Fields(const AssetBlock& block, std::initializer_list<std::string_view> properties,
                std::initializer_list<std::string_view> children = {}) {
        for (const auto& property : block.Properties)
            if (std::find(properties.begin(), properties.end(), property.Key) == properties.end())
                Fail("MG_FIELD", "Unsupported property '" + property.Key + "' in " + block.Type, property.Source);
        for (const auto& child : block.Children)
            if (std::find(children.begin(), children.end(), child->Type) == children.end())
                Fail("MG_FIELD", "Unsupported block '" + child->Type + "' in " + block.Type, child->Source);
    }

    const AssetProperty& Property(const AssetBlock& block, const std::string& name) {
        const auto* property = block.GetProperty(name);
        if (!property) Fail("MG_FIELD", "Missing property '" + name + "' in " + block.Type, block.Source);
        return *property;
    }

    std::string String(const AssetBlock& block, const std::string& name) {
        const auto& property = Property(block, name);
        if (property.RawValue.size() < 2 || property.RawValue.front() != '"' || property.RawValue.back() != '"')
            Fail("MG_STRING", "Expected quoted string for '" + name + "'", property.Source);
        return property.AsString();
    }

    const AssetBlock* Child(const AssetBlock& block, const std::string& name, bool required = true) {
        const AssetBlock* result = nullptr;
        for (const auto& child : block.Children) if (child->Type == name) {
            if (result) Fail("MG_BLOCK", "Duplicate block '" + name + "'", child->Source);
            result = child.get();
        }
        if (!result && required) Fail("MG_BLOCK", "Missing block '" + name + "'", block.Source);
        return result;
    }

    std::vector<MaterialGraphPin> Pins(const AssetBlock* block) {
        std::vector<MaterialGraphPin> pins;
        if (!block) return pins;
        Fields(*block, {}, {"Pin"});
        for (const auto& child : block->Children) {
            Fields(*child, {"name", "type", "default"});
            MaterialGraphPin pin;
            pin.Name = String(*child, "name"); pin.Source = child->Source;
            pin.Type = ParseType(String(*child, "type"), Property(*child, "type").Source);
            if (const auto* property = child->GetProperty("default")) pin.Default = Value(property->RawValue, pin.Type, property->Source);
            pins.push_back(std::move(pin));
        }
        return pins;
    }

    MaterialGraphEndpoint Endpoint(const AssetProperty& property) {
        const auto text = property.AsString();
        if (property.RawValue.empty() || property.RawValue.front() != '"')
            Fail("MG_ENDPOINT", "Connection endpoints must be quoted nodeId.pin strings", property.Source);
        const auto dot = text.find('.');
        if (dot == std::string::npos || dot+1 == text.size() || text.find('.', dot+1) != std::string::npos)
            Fail("MG_ENDPOINT", "Expected nodeId.pin endpoint", property.Source);
        return {Unsigned(std::string_view(text).substr(0, dot), property.Source), text.substr(dot+1)};
    }

    const MaterialNodePropertyDefinition* FindPropertySchema(const std::vector<MaterialNodePropertyDefinition>& schemas, const std::string& name) {
        const auto found = std::find_if(schemas.begin(), schemas.end(), [&](const auto& p){return p.Name == name;});
        return found == schemas.end() ? nullptr : &*found;
    }

    MaterialNodeProperty DecodeProperty(const AssetProperty& property, const std::vector<MaterialNodePropertyDefinition>& schemas) {
        const auto* schema = FindPropertySchema(schemas, property.Key);
        if (!schema) Fail("MG_FIELD", "Property is not declared by node definition", property.Source, 0, property.Key);
        MaterialNodeProperty result; result.Name = property.Key; result.Source = property.Source;
        switch (schema->Kind) {
            case MaterialPropertyKind::String:
                if (property.RawValue.size() < 2 || property.RawValue.front() != '"' || property.RawValue.back() != '"')
                    Fail("MG_STRING", "Expected quoted node property", property.Source, 0, property.Key);
                result.Value = property.AsString(); break;
            case MaterialPropertyKind::Boolean:
                if (property.RawValue != "true" && property.RawValue != "false") Fail("MG_BOOL", "Expected Boolean node property", property.Source, 0, property.Key);
                result.Value = property.RawValue == "true"; break;
            case MaterialPropertyKind::Number:
                result.Value = Value(property.RawValue, schema->NumberType, property.Source); break;
        }
        return result;
    }

    std::string FormatProperty(const MaterialNodePropertyValue& property) {
        if (const auto* text = std::get_if<std::string>(&property)) return AssetDSL::QuoteString(*text);
        if (const auto* boolean = std::get_if<bool>(&property)) return *boolean ? "true" : "false";
        return FormatValue(std::get<MaterialValue>(property));
    }

    MaterialGraphDocument Decode(const AssetDocument& asset, uint64_t identity, const MaterialNodeRegistry& registry) {
        const auto& root = *asset.Root;
        if (root.Type != "Material") Fail("MG_ROOT", "Expected Material root", root.Source);
        Fields(root, {"schemaVersion", "name", "type", "shadingModel", "surfaceType", "cullMode", "depthWrite"}, {"Parameters", "Graph"});
        MaterialGraphDocument doc;
        doc.Identity = identity; doc.Source = root.Source;
        const auto version = Unsigned(Property(root, "schemaVersion").RawValue, Property(root, "schemaVersion").Source);
        if (version != 1) Fail("MG_VERSION", "Unsupported Material Graph schemaVersion", Property(root, "schemaVersion").Source);
        if (String(root, "type") != "Graph") Fail("MG_ROOT", "Material type must be Graph", Property(root, "type").Source);
        doc.Name = String(root, "name"); doc.ShadingModel = String(root, "shadingModel");
        doc.SurfaceType = String(root, "surfaceType"); doc.CullMode = String(root, "cullMode");
        const auto& depth = Property(root, "depthWrite");
        if (depth.RawValue != "true" && depth.RawValue != "false") Fail("MG_BOOL", "depthWrite must be true or false", depth.Source);
        doc.DepthWrite = depth.RawValue == "true";
        const auto* parameters = Child(root, "Parameters");
        Fields(*parameters, {}, {"Parameter"});
        for (const auto& block : parameters->Children) {
            Fields(*block, {"name", "type", "default"});
            const auto type = ParseType(String(*block, "type"), Property(*block, "type").Source);
            const auto& value = Property(*block, "default");
            doc.Parameters.push_back({String(*block, "name"), Value(value.RawValue, type, value.Source), block->Source});
        }
        const auto* graph = Child(root, "Graph");
        Fields(*graph, {}, {"Node", "Connections"});
        for (const auto& block : graph->Children) if (block->Type == "Node") {
            for (const auto& child : block->Children)
                if (child->Type != "inputs" && child->Type != "outputs" && child->Type != "properties")
                    Fail("MG_FIELD", "Unsupported node block", child->Source);
            MaterialGraphNode node;
            node.Source = block->Source;
            node.ID = Unsigned(Property(*block, "id").RawValue, Property(*block, "id").Source);
            node.Type = String(*block, "type");
            const auto* definition = registry.Find(node.Type);
            if (!definition) Fail("MG_NODE", "Unregistered node type '" + node.Type + "'", node.Source, node.ID);
            const auto& position = Property(*block, "editorPos");
            const auto coordinates = Value(position.RawValue, ValueType::Float2, position.Source);
            std::copy_n(coordinates.Components.begin(), 2, node.EditorPosition.begin());
            for (const auto& property : block->Properties)
                if (property.Key != "id" && property.Key != "type" && property.Key != "editorPos")
                    node.Attributes.push_back(DecodeProperty(property, definition->Attributes));
            node.Inputs = Pins(Child(*block, "inputs", false));
            node.Outputs = Pins(Child(*block, "outputs", false));
            if (const auto* properties = Child(*block, "properties", false)) {
                if (!properties->Children.empty()) Fail("MG_FIELD", "Nested node property blocks are unsupported", properties->Source);
                for (const auto& property : properties->Properties)
                    node.Properties.push_back(DecodeProperty(property, definition->Properties));
            }
            doc.Nodes.push_back(std::move(node));
        }
        const auto* connections = Child(*graph, "Connections");
        Fields(*connections, {}, {"Connection"});
        for (const auto& block : connections->Children) {
            Fields(*block, {"from", "to"});
            doc.Connections.push_back({Endpoint(Property(*block, "from")), Endpoint(Property(*block, "to")), block->Source});
        }
        return doc;
    }

    using PinSpec = std::pair<std::string, ValueType>;
    void PinContract(const MaterialGraphNode& node, const std::vector<MaterialGraphPin>& pins,
                     const std::vector<PinSpec>& expected, bool output) {
        if (pins.size() != expected.size()) Fail("MG_PIN", "Incorrect pin count on " + node.Type, node.Source, node.ID);
        std::set<std::string> names;
        for (const auto& pin : pins) {
            const auto match = std::find_if(expected.begin(), expected.end(), [&](const PinSpec& s){return s.first == pin.Name;});
            if (!names.insert(pin.Name).second || match == expected.end() || match->second != pin.Type)
                Fail("MG_PIN", "Pin name/direction/type does not match node definition", pin.Source, node.ID, pin.Name);
            if (pin.Default) {
                if (output) Fail("MG_PIN", "Output pins cannot have defaults", pin.Source, node.ID, pin.Name);
                CheckValue(*pin.Default, pin.Type, pin.Source);
            }
        }
    }

    const MaterialGraphPin* FindPin(const std::vector<MaterialGraphPin>& pins, const std::string& name) {
        const auto it = std::find_if(pins.begin(), pins.end(), [&](const auto& p){return p.Name == name;});
        return it == pins.end() ? nullptr : &*it;
    }

    using ResolvedProperties = std::vector<std::pair<std::string, MaterialNodePropertyValue>>;

    ResolvedProperties ResolveProperties(const MaterialGraphNode& node, const std::vector<MaterialNodeProperty>& authored,
                                         const std::vector<MaterialNodePropertyDefinition>& schemas, bool attributes) {
        ResolvedProperties result;
        std::set<std::string> names;
        for (const auto& property : authored) {
            const auto* schema = FindPropertySchema(schemas, property.Name);
            if (!schema) Fail(attributes ? "MG_NODE" : "MG_FIELD", "Property is not declared by node definition", property.Source, node.ID, property.Name);
            if (!names.insert(property.Name).second) Fail("MG_PROPERTY", "Duplicate node property", property.Source, node.ID, property.Name);
            auto fail = [&](std::string message) { Fail(schema->DiagnosticCode, std::move(message), property.Source, node.ID, property.Name); };
            switch (schema->Kind) {
                case MaterialPropertyKind::String: {
                    const auto* value = std::get_if<std::string>(&property.Value);
                    if (!value) fail("Expected a string node property");
                    CheckString(*value, property.Source);
                    if (schema->AssetPath && !MaterialGraphInternal::CanonicalAssetPath(*value)) fail("Expected a canonical root-relative asset path");
                    if (!schema->AllowedStrings.empty() && std::find(schema->AllowedStrings.begin(), schema->AllowedStrings.end(), *value) == schema->AllowedStrings.end())
                        fail("Value is not one of the node property's declared choices");
                    break;
                }
                case MaterialPropertyKind::Boolean:
                    if (!std::holds_alternative<bool>(property.Value)) fail("Expected a Boolean node property");
                    break;
                case MaterialPropertyKind::Number: {
                    const auto* value = std::get_if<MaterialValue>(&property.Value);
                    if (!value || value->Type != schema->NumberType || !MaterialGraphInternal::ValidValue(*value)) fail("Numeric node property does not match its declared finite type");
                    break;
                }
            }
            result.emplace_back(property.Name, property.Value);
        }
        for (const auto& schema : schemas) if (!names.contains(schema.Name)) {
            if (schema.Required) Fail(schema.DiagnosticCode, "Missing required node property", node.Source, node.ID, schema.Name);
            if (schema.Default) result.emplace_back(schema.Name, *schema.Default);
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b){return a.first < b.first;});
        return result;
    }

    struct ResolvedNode {
        const MaterialNodeDefinition* Definition = nullptr;
        std::optional<std::string> Parameter;
        ResolvedProperties Attributes;
        ResolvedProperties Properties;
    };

    ResolvedNode NodeContract(const MaterialGraphNode& node,
                              const std::map<std::string, const MaterialGraphParameter*>& parameters,
                              const MaterialNodeRegistry& registry) {
        if (node.ID == 0) Fail("MG_NODE_ID", "Node ID must be nonzero", node.Source);
        if (!std::isfinite(node.EditorPosition[0]) || !std::isfinite(node.EditorPosition[1]))
            Fail("MG_VALUE", "Editor position must be finite", node.Source, node.ID);
        const auto* definition = registry.Find(node.Type);
        if (!definition) Fail("MG_NODE", "Unregistered node type '" + node.Type + "'", node.Source, node.ID);
        ResolvedNode resolved;
        resolved.Definition = definition;
        resolved.Attributes = ResolveProperties(node, node.Attributes, definition->Attributes, true);
        resolved.Properties = ResolveProperties(node, node.Properties, definition->Properties, false);

        std::optional<ValueType> parameterType;
        if (definition->ParameterAttribute) {
            const auto attribute = std::find_if(resolved.Attributes.begin(), resolved.Attributes.end(), [&](const auto& p){return p.first == *definition->ParameterAttribute;});
            resolved.Parameter = std::get<std::string>(attribute->second);
            const auto parameter = parameters.find(*resolved.Parameter);
            if (parameter == parameters.end()) Fail("MG_PARAMETER", "Unknown parameter key '" + *resolved.Parameter + "'", node.Source, node.ID, *definition->ParameterAttribute);
            parameterType = parameter->second->Default.Type;
        }

        auto matches = [&](const std::vector<MaterialGraphPin>& pins, const std::vector<MaterialNodePinDefinition>& expected) {
            if (pins.size() != expected.size()) return false;
            for (const auto& spec : expected) {
                const auto* pin = FindPin(pins, spec.Name);
                const auto type = spec.Type ? *spec.Type : *parameterType;
                if (!pin || pin->Type != type) return false;
            }
            return true;
        };
        const auto signature = std::find_if(definition->Signatures.begin(), definition->Signatures.end(), [&](const auto& s){
            return matches(node.Inputs, s.Inputs) && matches(node.Outputs, s.Outputs);
        });
        if (signature == definition->Signatures.end()) Fail("MG_PIN", "Declared pins do not match a registered node signature", node.Source, node.ID);
        auto resolvedPins = [&](const auto& pins) {
            std::vector<PinSpec> result;
            for (const auto& pin : pins) result.emplace_back(pin.Name, pin.Type ? *pin.Type : *parameterType);
            return result;
        };
        PinContract(node, node.Inputs, resolvedPins(signature->Inputs), false);
        PinContract(node, node.Outputs, resolvedPins(signature->Outputs), true);
        return resolved;
    }

    std::string EndpointText(const MaterialGraphEndpoint& endpoint) { return std::to_string(endpoint.NodeID) + "." + endpoint.Pin; }

    MaterialGraphIR BuildIR(const MaterialGraphDocument& doc, const MaterialNodeRegistry& registry) {
        if (!doc.Identity) Fail("MG_IDENTITY", "Material identity must be nonzero", doc.Source);
        if (doc.SchemaVersion != 1) Fail("MG_VERSION", "Unsupported Material Graph schemaVersion", doc.Source);
        if (doc.Name.empty()) Fail("MG_NAME", "Material name must not be empty", doc.Source);
        CheckString(doc.Name, doc.Source);
        if (doc.ShadingModel != "DefaultLit" || doc.SurfaceType != "Opaque")
            Fail("MG_SURFACE", "C12-01 supports DefaultLit/Opaque surfaces", doc.Source);
        if (doc.CullMode != "Back" && doc.CullMode != "Front" && doc.CullMode != "None")
            Fail("MG_SURFACE", "Unsupported cullMode", doc.Source);
        std::map<std::string, const MaterialGraphParameter*> parameters;
        for (const auto& parameter : doc.Parameters) {
            CheckString(parameter.Name, parameter.Source);
            if (parameter.Name.empty() || !parameters.emplace(parameter.Name, &parameter).second)
                Fail("MG_PARAMETER", "Parameter names must be nonempty unique case-sensitive keys", parameter.Source);
            CheckValue(parameter.Default, parameter.Default.Type, parameter.Source);
        }
        std::map<uint64_t, const MaterialGraphNode*> nodes;
        MaterialGraphIR ir;
        std::map<uint64_t, ResolvedNode> resolvedNodes;
        for (const auto& node : doc.Nodes) {
            if (!nodes.emplace(node.ID, &node).second) Fail("MG_NODE_ID", "Duplicate node ID", node.Source, node.ID);
            auto resolved = NodeContract(node, parameters, registry);
            const auto role = resolved.Definition->Role;
            resolvedNodes.emplace(node.ID, std::move(resolved));
            if (role == MaterialNodeRole::SurfaceOutput) {
                if (ir.OutputNode) Fail("MG_OUTPUT", "Exactly one surface-output node is required", node.Source, node.ID);
                ir.OutputNode = node.ID;
            }
        }
        if (!ir.OutputNode) Fail("MG_OUTPUT", "Exactly one surface-output node is required", doc.Source);
        using InputKey = std::pair<uint64_t, std::string>;
        std::map<InputKey, const MaterialGraphConnection*> incoming;
        std::map<uint64_t, size_t> degree;
        std::map<uint64_t, std::vector<uint64_t>> consumers, dependencies;
        for (const auto& [id, node] : nodes) degree[id] = 0;
        for (const auto& connection : doc.Connections) {
            auto from = nodes.find(connection.From.NodeID), to = nodes.find(connection.To.NodeID);
            if (from == nodes.end() || to == nodes.end()) Fail("MG_ENDPOINT", "Connection references missing node", connection.Source);
            const auto* output = FindPin(from->second->Outputs, connection.From.Pin);
            const auto* input = FindPin(to->second->Inputs, connection.To.Pin);
            if (!output || !input) Fail("MG_ENDPOINT", "Connection requires an existing output-to-input pin pair", connection.Source);
            if (output->Type != input->Type && output->Type != ValueType::Float)
                Fail("MG_CONNECTION_TYPE", "Only exact types and Float-to-vector splats are supported", connection.Source, to->first, input->Name);
            if (!incoming.emplace(InputKey{to->first, input->Name}, &connection).second)
                Fail("MG_CONNECTION", "Input has multiple incoming connections", connection.Source, to->first, input->Name);
            ++degree[to->first]; consumers[from->first].push_back(to->first); dependencies[to->first].push_back(from->first);
        }
        for (const auto& [id, node] : nodes) for (const auto& input : node->Inputs)
            if (!input.Default && !incoming.contains({id, input.Name}))
                Fail("MG_INPUT", "Unconnected input has no default", input.Source, id, input.Name);
        std::set<uint64_t> ready;
        for (const auto& [id, count] : degree) if (!count) ready.insert(id);
        std::vector<uint64_t> order;
        while (!ready.empty()) {
            auto id = *ready.begin(); ready.erase(ready.begin()); order.push_back(id);
            for (auto consumer : consumers[id]) if (--degree[consumer] == 0) ready.insert(consumer);
        }
        if (order.size() != nodes.size()) Fail("MG_CYCLE", "Material graph contains a cycle (including unused nodes)", doc.Source);
        std::set<uint64_t> reachable;
        std::vector<uint64_t> stack{ir.OutputNode};
        while (!stack.empty()) {
            auto id = stack.back(); stack.pop_back();
            if (reachable.insert(id).second) for (auto dependency : dependencies[id]) stack.push_back(dependency);
        }
        std::set<std::string> usedParameters;
        for (auto id : order) if (reachable.contains(id)) {
            const auto& source = *nodes.at(id);
            const auto& resolved = resolvedNodes.at(id);
            MaterialIRNode node;
            node.ID = id; node.Type = source.Type; node.DefinitionRevision = resolved.Definition->Revision;
            node.Attributes = resolved.Attributes; node.Properties = resolved.Properties;
            if (resolved.Parameter) usedParameters.insert(*resolved.Parameter);
            for (const auto& pin : source.Inputs) {
                MaterialIRInput input; input.Name = pin.Name; input.Type = pin.Type;
                if (auto connection = incoming.find({id, pin.Name}); connection != incoming.end()) {
                    input.Connection = connection->second->From;
                    const auto* output = FindPin(nodes.at(input.Connection->NodeID)->Outputs, input.Connection->Pin);
                    if (output->Type != input.Type) input.Conversion = MaterialInputConversion::ScalarSplat;
                } else input.Default = pin.Default;
                node.Inputs.push_back(std::move(input));
            }
            for (const auto& pin : source.Outputs) node.Outputs.emplace_back(pin.Name, pin.Type);
            std::sort(node.Inputs.begin(), node.Inputs.end(), [](const auto& a, const auto& b){return a.Name < b.Name;});
            std::sort(node.Outputs.begin(), node.Outputs.end());
            ir.Nodes.push_back(std::move(node));
        }
        for (const auto& name : usedParameters) {
            auto parameter = *parameters.at(name); parameter.Source = {};
            ir.Parameters.push_back(std::move(parameter));
        }
        std::ostringstream key;
        key.imbue(std::locale::classic());
        key << "MaterialIR:1 " << doc.ShadingModel << ' ' << doc.SurfaceType << ' ' << doc.CullMode << ' ' << doc.DepthWrite << '\n';
        for (const auto& parameter : ir.Parameters)
            key << "P " << AssetDSL::QuoteString(parameter.Name) << ' ' << TypeName(parameter.Default.Type) << ' ' << FormatValue(parameter.Default) << '\n';
        for (const auto& node : ir.Nodes) {
            key << "N " << node.ID << ' ' << node.Type << ' ' << node.DefinitionRevision << '\n';
            for (const auto& attribute : node.Attributes) key << "A " << attribute.first << ' ' << FormatProperty(attribute.second) << '\n';
            for (const auto& property : node.Properties) key << "R " << property.first << ' ' << FormatProperty(property.second) << '\n';
            for (const auto& input : node.Inputs) {
                key << "I " << input.Name << ' ' << TypeName(input.Type) << ' ';
                if (input.Connection) key << EndpointText(*input.Connection) << ' ' << static_cast<unsigned>(input.Conversion);
                else key << FormatValue(*input.Default);
                key << '\n';
            }
            for (const auto& output : node.Outputs) key << "O " << output.first << ' ' << TypeName(output.second) << '\n';
        }
        ir.SemanticKey = key.str();
        return ir;
    }

    std::shared_ptr<AssetBlock> AddBlock(AssetBlock& parent, std::string type) {
        auto block = std::make_shared<AssetBlock>(); block->Type = std::move(type); parent.Children.push_back(block); return block;
    }

    void EncodePins(AssetBlock& node, const std::string& direction, const std::vector<MaterialGraphPin>& pins) {
        if (pins.empty()) return;
        auto block = AddBlock(node, direction);
        for (const auto& pin : pins) {
            auto child = AddBlock(*block, "Pin"); child->SetString("name", pin.Name); child->SetString("type", TypeName(pin.Type));
            if (pin.Default) child->Properties.push_back({"default", FormatValue(*pin.Default)});
        }
    }

    std::string Encode(const MaterialGraphDocument& doc) {
        AssetDocument asset{AssetKind::Material, std::make_shared<AssetBlock>()};
        auto& root = *asset.Root; root.Type = "Material";
        root.SetUInt("schemaVersion", doc.SchemaVersion); root.SetString("name", doc.Name); root.SetString("type", "Graph");
        root.SetString("shadingModel", doc.ShadingModel); root.SetString("surfaceType", doc.SurfaceType);
        root.SetString("cullMode", doc.CullMode); root.SetBool("depthWrite", doc.DepthWrite);
        auto parameters = AddBlock(root, "Parameters");
        for (const auto& parameter : doc.Parameters) {
            auto block = AddBlock(*parameters, "Parameter"); block->SetString("name", parameter.Name);
            block->SetString("type", TypeName(parameter.Default.Type)); block->Properties.push_back({"default", FormatValue(parameter.Default)});
        }
        auto graph = AddBlock(root, "Graph");
        for (const auto& node : doc.Nodes) {
            auto block = AddBlock(*graph, "Node"); block->Properties.push_back({"id", std::to_string(node.ID)}); block->SetString("type", node.Type);
            block->Properties.push_back({"editorPos", FormatValue({ValueType::Float2, {node.EditorPosition[0], node.EditorPosition[1]}})});
            for (const auto& attribute : node.Attributes) block->Properties.push_back({attribute.Name, FormatProperty(attribute.Value)});
            if (!node.Properties.empty()) {
                auto properties = AddBlock(*block, "properties");
                for (const auto& property : node.Properties) properties->Properties.push_back({property.Name, FormatProperty(property.Value)});
            }
            EncodePins(*block, "inputs", node.Inputs); EncodePins(*block, "outputs", node.Outputs);
        }
        auto connections = AddBlock(*graph, "Connections");
        for (const auto& connection : doc.Connections) {
            auto block = AddBlock(*connections, "Connection"); block->SetString("from", EndpointText(connection.From)); block->SetString("to", EndpointText(connection.To));
        }
        return "# AxleFileIdentity: 1 " + std::to_string(doc.Identity) + "\n\n" + AssetDSL::SerializeDocument(asset);
    }
}

    MaterialGraphDocument MaterialGraph::Create(std::string name) {
        MaterialGraphDocument doc; doc.Identity = static_cast<uint64_t>(UUID()); doc.Name = std::move(name);
        MaterialGraphNode output; output.ID = 1; output.Type = "PBRMasterOutput";
        const auto* definition = MaterialNodeRegistry::Builtins().Find(output.Type);
        for (const auto& pin : definition->Signatures.front().Inputs)
            output.Inputs.push_back({pin.Name, *pin.Type, pin.Default, {}});
        doc.Nodes.push_back(std::move(output)); return doc;
    }

    bool MaterialGraph::Parse(std::string_view source, MaterialGraphDocument& outDocument, MaterialGraphDiagnostic* error, const MaterialNodeRegistry& registry) {
        return Checked(error, [&] {
            uint64_t identity = 0; std::string message;
            if (!FileSystem::ParseFileIdentity(source, identity, &message)) Fail("MG_IDENTITY", message);
            AssetDocument asset;
            Location location;
            if (!AssetDSL::ParseSource(source, asset, &message, &location)) Fail("MG_SYNTAX", message, location);
            auto candidate = Decode(asset, identity, registry); BuildIR(candidate, registry);
            outDocument = std::move(candidate);
        });
    }

    bool MaterialGraph::Validate(const MaterialGraphDocument& document, MaterialGraphIR& outIR, MaterialGraphDiagnostic* error, const MaterialNodeRegistry& registry) {
        return Checked(error, [&] { auto candidate = BuildIR(document, registry); outIR = std::move(candidate); });
    }

    bool MaterialGraph::Serialize(const MaterialGraphDocument& document, std::string& outSource, MaterialGraphDiagnostic* error, const MaterialNodeRegistry& registry) {
        return Checked(error, [&] { BuildIR(document, registry); auto candidate = Encode(document); outSource = std::move(candidate); });
    }

    bool MaterialGraph::Load(const std::filesystem::path& path, MaterialGraphDocument& outDocument, MaterialGraphDiagnostic* error, const MaterialNodeRegistry& registry) {
        return Checked(error, [&] {
            const auto source = FileSystem::ReadTextFile(path);
            if (source.empty()) Fail("MG_IO", "Cannot read Material Graph file");
            MaterialGraphDocument candidate; MaterialGraphDiagnostic diagnostic;
            if (!Parse(source, candidate, &diagnostic, registry)) throw diagnostic;
            outDocument = std::move(candidate);
        });
    }

    bool MaterialGraph::Save(const std::filesystem::path& path, const MaterialGraphDocument& document, MaterialGraphDiagnostic* error, const MaterialNodeRegistry& registry) {
        std::string source;
        if (!Serialize(document, source, error, registry)) return false;
        return Checked(error, [&] {
            std::string message;
            if (!FileSystem::WriteIdentifiedTextFile(path, source, &message, document.Identity)) Fail("MG_IO", message);
        });
    }
}
