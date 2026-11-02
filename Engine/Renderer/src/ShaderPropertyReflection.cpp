#include <Axle/ShaderPropertyReflection.hpp>
#include <Axle/MaterialBindingPlan.hpp>
#include <algorithm>
#include <sstream>
#include <regex>

namespace Axle {

    const char* ShaderPropertyTypeToString(ShaderPropertyType type) {
        switch (type) {
            case ShaderPropertyType::Float:     return "Float";
            case ShaderPropertyType::Float2:    return "Float2";
            case ShaderPropertyType::Float3:    return "Float3";
            case ShaderPropertyType::Float4:    return "Float4";
            case ShaderPropertyType::Color:     return "Color";
            case ShaderPropertyType::Int:       return "Int";
            case ShaderPropertyType::Bool:      return "Bool";
            case ShaderPropertyType::Texture2D: return "Texture2D";
            default:                            return "Unknown";
        }
    }

    ShaderPropertyType StringToShaderPropertyType(const std::string& str) {
        if (str == "Float")     return ShaderPropertyType::Float;
        if (str == "Float2")    return ShaderPropertyType::Float2;
        if (str == "Float3")    return ShaderPropertyType::Float3;
        if (str == "Float4")    return ShaderPropertyType::Float4;
        if (str == "Color")     return ShaderPropertyType::Color;
        if (str == "Int")       return ShaderPropertyType::Int;
        if (str == "Bool")      return ShaderPropertyType::Bool;
        if (str == "Texture2D") return ShaderPropertyType::Texture2D;
        return ShaderPropertyType::Float;
    }

    std::string ShaderPropertyParser::FormatDisplayName(const std::string& identifier) {
        std::string name = identifier;
        if (name.rfind("u_", 0) == 0 || name.rfind("m_", 0) == 0 || name.rfind("g_", 0) == 0) {
            name = name.substr(2);
        } else if (name.rfind("_", 0) == 0) {
            name = name.substr(1);
        }

        if (name.empty()) return identifier;

        std::string result;
        result.reserve(name.size() + 8);
        result += static_cast<char>(std::toupper(name[0]));

        for (size_t i = 1; i < name.size(); ++i) {
            char curr = name[i];
            char prev = name[i - 1];

            if (curr == '_') {
                result += ' ';
            } else if (std::isupper(curr) && !std::isupper(prev) && prev != '_') {
                result += ' ';
                result += curr;
            } else {
                result += curr;
            }
        }
        return result;
    }

    static std::string ExtractQuotedString(const std::string& str) {
        size_t firstQuote = str.find('"');
        if (firstQuote == std::string::npos) return "";
        size_t secondQuote = str.find('"', firstQuote + 1);
        if (secondQuote == std::string::npos) return "";
        return str.substr(firstQuote + 1, secondQuote - firstQuote - 1);
    }

    std::vector<ShaderPropertyDesc> ShaderPropertyParser::ParseFromHLSL(
        const std::string& hlslSource,
        const RHI::RHIShaderReflection* reflection)
    {
        std::vector<ShaderPropertyDesc> properties;
        std::unordered_map<std::string, size_t> propIndices;

        std::istringstream stream(hlslSource);
        std::string line;

        std::string currentHeader;
        std::string pendingTooltip;
        bool pendingRange = false;
        float rangeMin = 0.0f;
        float rangeMax = 1.0f;
        bool pendingColor = false;
        bool pendingTexture = false;

        // Regular expression patterns for annotations and declarations
        static const std::regex headerRegex(R"(\[\s*Header\s*\(\s*\"([^\"]*)\"\s*\)\s*\])");
        static const std::regex tooltipRegex(R"(\[\s*Tooltip\s*\(\s*\"([^\"]*)\"\s*\)\s*\])");
        static const std::regex rangeRegex(R"(\[\s*Range\s*\(\s*([+-]?\d+(?:\.\d+)?f?)\s*,\s*([+-]?\d+(?:\.\d+)?f?)\s*\)\s*\])");
        static const std::regex colorRegex(R"(\[\s*Color\s*\])");
        static const std::regex textureRegex(R"(\[\s*Texture\s*\])");

        // Variable declaration matching
        // e.g. "float u_Dissolve;" or "float4 u_Color;" or "Texture2D u_Noise : register(t0, space1);"
        static const std::regex declRegex(R"((?:float4|float3|float2|float|int|uint|bool|Texture2D)\s+([a-zA-Z_][a-zA-Z0-9_]*)(?:\s*:\s*[^=;]+)?\s*(?:=\s*([^;]+))?;)");

        bool inCbuffer = false;
        int braceDepth = 0;

        while (std::getline(stream, line)) {
            // Trim whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                continue;
            }
            std::string trimmed = line.substr(start);

            // Track cbuffer and brace scopes to distinguish uniforms from function-local variables
            if (trimmed.find("cbuffer ") != std::string::npos) {
                inCbuffer = true;
            }
            for (char ch : trimmed) {
                if (ch == '{') {
                    braceDepth++;
                } else if (ch == '}') {
                    if (braceDepth > 0) braceDepth--;
                    if (braceDepth == 0) inCbuffer = false;
                }
            }

            // Check for Header annotation
            std::smatch match;
            if (std::regex_search(trimmed, match, headerRegex)) {
                currentHeader = match[1].str();
            }

            // Check for Tooltip annotation
            if (std::regex_search(trimmed, match, tooltipRegex)) {
                pendingTooltip = match[1].str();
            }

            // Check for Range annotation
            if (std::regex_search(trimmed, match, rangeRegex)) {
                try {
                    std::string minStr = match[1].str();
                    std::string maxStr = match[2].str();
                    if (!minStr.empty() && minStr.back() == 'f') minStr.pop_back();
                    if (!maxStr.empty() && maxStr.back() == 'f') maxStr.pop_back();
                    rangeMin = std::stof(minStr);
                    rangeMax = std::stof(maxStr);
                    pendingRange = true;
                } catch (...) {
                    pendingRange = false;
                }
            }

            // Check for Color annotation
            if (std::regex_search(trimmed, match, colorRegex)) {
                pendingColor = true;
            }

            // Check for Texture annotation
            if (std::regex_search(trimmed, match, textureRegex)) {
                pendingTexture = true;
            }

            // Check for Variable Declaration
            if (std::regex_search(trimmed, match, declRegex)) {
                std::string varName = match[1].str();
                std::string defaultValStr = match[2].matched ? match[2].str() : "";

                // Exclude function-local variables (inside non-cbuffer scopes without explicit property annotations)
                if (braceDepth > 0 && !inCbuffer && !pendingRange && !pendingColor && !pendingTexture) {
                    continue;
                }

                // Ignore standard engine uniforms
                if (IsEngineUniform(varName)) {
                    // Reset pending annotations
                    pendingRange = false;
                    pendingColor = false;
                    pendingTexture = false;
                    pendingTooltip.clear();
                    continue;
                }

                ShaderPropertyDesc desc;
                desc.Name = varName;
                desc.DisplayName = FormatDisplayName(varName);
                desc.Header = currentHeader;
                desc.Tooltip = pendingTooltip;
                desc.HasRange = pendingRange;
                desc.MinValue = rangeMin;
                desc.MaxValue = rangeMax;

                // Determine type
                if (trimmed.find("Texture2D") != std::string::npos || pendingTexture) {
                    desc.Type = ShaderPropertyType::Texture2D;
                } else if (pendingColor || trimmed.find("float4") != std::string::npos && (varName.find("Color") != std::string::npos || varName.find("Tint") != std::string::npos)) {
                    desc.Type = ShaderPropertyType::Color;
                    desc.DefaultVec4 = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                } else if (trimmed.find("float4") != std::string::npos) {
                    desc.Type = ShaderPropertyType::Float4;
                } else if (trimmed.find("float3") != std::string::npos) {
                    desc.Type = ShaderPropertyType::Float3;
                } else if (trimmed.find("float2") != std::string::npos) {
                    desc.Type = ShaderPropertyType::Float2;
                } else if (trimmed.find("int") != std::string::npos || trimmed.find("uint") != std::string::npos) {
                    desc.Type = ShaderPropertyType::Int;
                } else if (trimmed.find("bool") != std::string::npos) {
                    desc.Type = ShaderPropertyType::Bool;
                } else {
                    desc.Type = ShaderPropertyType::Float;
                    if (pendingRange) {
                        desc.DefaultFloat = (rangeMin + rangeMax) * 0.5f;
                    }
                }

                // If default value string is present, try parsing
                if (!defaultValStr.empty()) {
                    try {
                        if (desc.Type == ShaderPropertyType::Float) {
                            if (defaultValStr.back() == 'f') defaultValStr.pop_back();
                            desc.DefaultFloat = std::stof(defaultValStr);
                        } else if (desc.Type == ShaderPropertyType::Int) {
                            desc.DefaultInt = std::stoi(defaultValStr);
                        }
                    } catch (...) {}
                }

                if (propIndices.find(varName) == propIndices.end()) {
                    propIndices[varName] = properties.size();
                    properties.push_back(desc);
                }

                // Reset pending flags for next declaration
                pendingRange = false;
                pendingColor = false;
                pendingTexture = false;
                pendingTooltip.clear();
            }
        }

        // If reflection metadata is provided, enrich properties with byte offsets & constant buffer info
        if (reflection) {
            for (const auto& cb : reflection->ConstantBuffers) {
                // Skip engine internal global buffers
                if (cb.Name == "SceneConstants" || cb.Name == "EngineScene" || cb.Name == "EngineObject") continue;

                for (const auto& var : cb.Variables) {
                    if (IsEngineUniform(var.Name)) continue;

                    auto it = propIndices.find(var.Name);
                    if (it != propIndices.end()) {
                        properties[it->second].Offset = var.Offset;
                        properties[it->second].Size = var.Size;
                        properties[it->second].ConstantBufferName = cb.Name;
                    } else {
                        // Unannotated variable discovered in DXC reflection
                        if (IsEngineUniform(var.Name)) continue;

                        ShaderPropertyDesc desc;
                        desc.Name = var.Name;
                        desc.DisplayName = FormatDisplayName(var.Name);
                        desc.Offset = var.Offset;
                        desc.Size = var.Size;
                        desc.ConstantBufferName = cb.Name;

                        if (var.Size == sizeof(float)) {
                            desc.Type = ShaderPropertyType::Float;
                            desc.HasRange = true;
                            desc.MinValue = 0.0f;
                            desc.MaxValue = 1.0f;
                            desc.DefaultFloat = 0.5f;
                        } else if (var.Size == sizeof(glm::vec4)) {
                            if (var.Name.find("Color") != std::string::npos || var.Name.find("Tint") != std::string::npos) {
                                desc.Type = ShaderPropertyType::Color;
                            } else {
                                desc.Type = ShaderPropertyType::Float4;
                            }
                        } else if (var.Size == sizeof(int)) {
                            desc.Type = ShaderPropertyType::Int;
                        }

                        propIndices[var.Name] = properties.size();
                        properties.push_back(desc);
                    }
                }
            }

            // Also check reflection texture resource bindings
            for (const auto& res : reflection->Resources) {
                if (res.Type == RHI::RHIResourceType::Texture) {
                    if (res.Name == "u_BaseMap" || res.Name == "u_NormalMap" ||
                        res.Name == "u_RoughnessMap" || res.Name == "u_MetallicMap") {
                        continue;
                    }
                    if (propIndices.find(res.Name) == propIndices.end()) {
                        ShaderPropertyDesc desc;
                        desc.Name = res.Name;
                        desc.DisplayName = FormatDisplayName(res.Name);
                        desc.Type = ShaderPropertyType::Texture2D;
                        propIndices[res.Name] = properties.size();
                        properties.push_back(desc);
                    }
                }
            }
        }

        return properties;
    }

    std::vector<ShaderPropertyInstance> SynchronizeCustomProperties(
        const std::vector<ShaderPropertyDesc>& newProperties,
        const std::vector<ShaderPropertyInstance>& existingInstances)
    {
        std::unordered_map<std::string, const ShaderPropertyInstance*> existingMap;
        for (const auto& inst : existingInstances) {
            existingMap[inst.Desc.Name] = &inst;
        }

        std::vector<ShaderPropertyInstance> result;
        for (const auto& desc : newProperties) {
            if (IsEngineUniform(desc.Name)) {
                continue;
            }

            ShaderPropertyInstance inst;
            inst.Desc = desc;

            auto it = existingMap.find(desc.Name);
            if (it != existingMap.end() && it->second->Desc.Type == desc.Type) {
                // Compatible! Preserve existing saved values
                inst.FloatVal = it->second->FloatVal;
                inst.Vec4Val = it->second->Vec4Val;
                inst.IntVal = it->second->IntVal;
                inst.BoolVal = it->second->BoolVal;
                inst.TexturePath = it->second->TexturePath;
            } else {
                // Initialize with descriptor defaults
                if (desc.Type == ShaderPropertyType::Float) {
                    inst.FloatVal = (desc.DefaultFloat != 0.0f) ? desc.DefaultFloat : (desc.HasRange ? desc.DefaultFloat : desc.MinValue);
                } else if (desc.Type == ShaderPropertyType::Color ||
                           desc.Type == ShaderPropertyType::Float2 ||
                           desc.Type == ShaderPropertyType::Float3 ||
                           desc.Type == ShaderPropertyType::Float4) {
                    inst.Vec4Val = desc.DefaultVec4;
                } else if (desc.Type == ShaderPropertyType::Int) {
                    inst.IntVal = desc.DefaultInt;
                } else if (desc.Type == ShaderPropertyType::Bool) {
                    inst.BoolVal = desc.DefaultBool;
                } else if (desc.Type == ShaderPropertyType::Texture2D) {
                    inst.TexturePath = desc.DefaultTexturePath;
                }
            }
            result.push_back(std::move(inst));
        }

        return result;
    }

} // namespace Axle

