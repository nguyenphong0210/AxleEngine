#include <Axle/MaterialGraph.hpp>
#include "MaterialGraphInternal.hpp"
#include <cctype>
#include <set>

namespace Axle {
namespace {
    bool Identifier(std::string_view name, bool qualified = false) {
        if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_')) return false;
        for (unsigned char c : name)
            if (!std::isalnum(c) && c != '_' && !(qualified && c == '.')) return false;
        return true;
    }

    bool PrintableString(const std::string& value) {
        return std::all_of(value.begin(), value.end(), [](unsigned char c){
            return c >= 32 || c == '\n' || c == '\r' || c == '\t' || c == '\b' || c == '\f';
        });
    }

    bool PropertyDefault(const MaterialNodePropertyDefinition& property) {
        if (!property.Default) return true;
        switch (property.Kind) {
            case MaterialPropertyKind::String: {
                const auto* value = std::get_if<std::string>(&*property.Default);
                return value && PrintableString(*value) && (!property.AssetPath || MaterialGraphInternal::CanonicalAssetPath(*value)) &&
                    (property.AllowedStrings.empty() || std::find(property.AllowedStrings.begin(), property.AllowedStrings.end(), *value) != property.AllowedStrings.end());
            }
            case MaterialPropertyKind::Boolean: return std::holds_alternative<bool>(*property.Default);
            case MaterialPropertyKind::Number: {
                const auto* value = std::get_if<MaterialValue>(&*property.Default);
                return value && value->Type == property.NumberType && MaterialGraphInternal::ValidValue(*value);
            }
        }
        return false;
    }
}

    bool MaterialNodeRegistry::Register(MaterialNodeDefinition definition, MaterialGraphDiagnostic* error) {
        if (error) *error = {};
        auto fail = [&](std::string message) {
            if (error) *error = {"MG_DEFINITION", std::move(message), {}, 0, definition.Type};
            return false;
        };
        if (!Identifier(definition.Type, true) || definition.Revision == 0 || definition.Signatures.empty() ||
            (definition.Role != MaterialNodeRole::Operation && definition.Role != MaterialNodeRole::SurfaceOutput))
            return fail("Node definition requires a type, nonzero revision, valid role and signatures");
        if (m_Definitions.contains(definition.Type)) return fail("Node type is already registered");
        for (const auto* properties : {&definition.Attributes, &definition.Properties}) {
            std::set<std::string> names;
            for (const auto& property : *properties) {
                if (!Identifier(property.Name) || !names.insert(property.Name).second || property.DiagnosticCode.empty() ||
                    (property.Kind != MaterialPropertyKind::String && property.Kind != MaterialPropertyKind::Boolean && property.Kind != MaterialPropertyKind::Number) ||
                    (property.Kind == MaterialPropertyKind::Number && !MaterialGraphInternal::Width(property.NumberType)) ||
                    (property.Kind != MaterialPropertyKind::String && (!property.AllowedStrings.empty() || property.AssetPath)) ||
                    (property.Required && property.Default) || !PropertyDefault(property))
                    return fail("Property schema has invalid names, kinds, constraints or defaults");
                if (properties == &definition.Attributes && (property.Name == "id" || property.Name == "type" || property.Name == "editorPos"))
                    return fail("Node attributes cannot override reserved fields");
                if (!std::all_of(property.AllowedStrings.begin(), property.AllowedStrings.end(), PrintableString))
                    return fail("String choices contain unsupported control characters");
            }
        }
        if (definition.ParameterAttribute) {
            const auto it = std::find_if(definition.Attributes.begin(), definition.Attributes.end(), [&](const auto& p){return p.Name == *definition.ParameterAttribute;});
            if (it == definition.Attributes.end() || it->Kind != MaterialPropertyKind::String || !it->Required || it->AssetPath)
                return fail("ParameterAttribute must name a required string attribute");
        }
        // Match by pin names, just as validation does. Check every parameter binding
        // so a wildcard signature cannot overlap a concrete overload either.
        std::array<std::set<std::string>, 4> signatures;
        for (const auto& signature : definition.Signatures) {
            for (const auto* pins : {&signature.Inputs, &signature.Outputs}) {
                std::set<std::string> names;
                for (const auto& pin : *pins) {
                    if (!Identifier(pin.Name) || !names.insert(pin.Name).second ||
                        (pin.Type ? !MaterialGraphInternal::Width(*pin.Type) : !definition.ParameterAttribute) ||
                        (pin.Default && (!pin.Type || pin.Default->Type != *pin.Type || !MaterialGraphInternal::ValidValue(*pin.Default))) ||
                        (pins == &signature.Outputs && pin.Default))
                        return fail("Pin schema has invalid names, types or defaults");
                }
            }
            for (size_t binding = 0; binding < signatures.size(); ++binding) {
                std::string key;
                for (const auto* pins : {&signature.Inputs, &signature.Outputs}) {
                    std::vector<std::string> canonical;
                    for (const auto& pin : *pins)
                        canonical.push_back(pin.Name + ":" + std::to_string(pin.Type ? static_cast<unsigned>(*pin.Type) : binding));
                    std::sort(canonical.begin(), canonical.end());
                    for (const auto& pin : canonical) key += pin + ";";
                    key += "|";
                }
                if (!signatures[binding].insert(key).second) return fail("Duplicate or overlapping node signature");
            }
            if (definition.Role == MaterialNodeRole::SurfaceOutput && !signature.Outputs.empty())
                return fail("Surface output signatures cannot expose output pins");
        }
        const auto key = definition.Type;
        m_Definitions.emplace(key, std::move(definition));
        return true;
    }

    const MaterialNodeDefinition* MaterialNodeRegistry::Find(std::string_view type) const {
        const auto found = m_Definitions.find(type);
        return found == m_Definitions.end() ? nullptr : &found->second;
    }
}
