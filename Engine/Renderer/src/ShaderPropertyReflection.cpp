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

        // Keep character positions while removing comments and strings from code.
        // Annotations are read from the original lines; declarations/scopes never are.
        std::string clean=hlslSource;
        bool block=false, quoted=false; char quote=0;
        for(size_t i=0;i<clean.size();++i) {
            const char c=hlslSource[i], next=i+1<clean.size()?hlslSource[i+1]:0;
            if(block) {
                if(c=='*' && next=='/') {clean[i]=clean[i+1]=' ';++i;block=false;}
                else if(c!='\n') clean[i]=' ';
            } else if(quoted) {
                if(c=='\\' && next) {clean[i]=clean[i+1]=' ';++i;}
                else {if(c==quote)quoted=false;if(c!='\n')clean[i]=' ';}
            } else if(c=='/' && next=='*') {clean[i]=clean[i+1]=' ';++i;block=true;}
            else if(c=='/' && next=='/') {while(i<clean.size() && hlslSource[i]!='\n')clean[i++]=' ';if(i==clean.size())break;}
            else if(c=='"' || c=='\'') {quoted=true;quote=c;clean[i]=' ';}
        }
        static const std::regex headerRegex(R"anno(\[\s*Header\s*\(\s*"([^"]*)"\s*\)\s*\])anno");
        static const std::regex tooltipRegex(R"anno(\[\s*Tooltip\s*\(\s*"([^"]*)"\s*\)\s*\])anno");
        static const std::regex rangeRegex(R"(\[\s*Range\s*\(\s*([^,]+),\s*([^\)]+)\)\s*\])");
        static const std::regex defaultRegex(R"(\[\s*Default\s*\(([^\)]*)\)\s*\])");
        static const std::regex colorRegex(R"(\[\s*Color\s*\])");
        static const std::regex declRegex(R"(\b(float4|float3|float2|float|int|uint|bool|Texture2D)\s+([a-zA-Z_][a-zA-Z0-9_]*)(?:\s*:\s*[^=;]+)?\s*(?:=\s*([^;]+))?;)");
        static const std::regex numberRegex(R"([+-]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][+-]?\d+)?)");
        std::istringstream original(hlslSource), codeStream(clean);
        std::string line,code,header,tooltip,defaultValue;
        bool hasRange=false,color=false,cbufferPending=false;
        float rangeMin=0,rangeMax=1;
        std::vector<bool> scopes;
        while(std::getline(original,line) && std::getline(codeStream,code)) {
            std::smatch annotation;
            if(std::regex_search(line,annotation,headerRegex))header=annotation[1];
            if(std::regex_search(line,annotation,tooltipRegex))tooltip=annotation[1];
            if(std::regex_search(line,annotation,colorRegex))color=true;
            if(std::regex_search(line,annotation,defaultRegex))defaultValue=annotation[1];
            if(std::regex_search(line,annotation,rangeRegex)) {
                try {rangeMin=std::stof(annotation[1]);rangeMax=std::stof(annotation[2]);hasRange=true;}catch(...) {hasRange=false;}
            }
            size_t cursor=0;
            auto advance=[&](size_t end) {
                while(cursor<end) {
                    if(code.compare(cursor,7,"cbuffer")==0)cbufferPending=true;
                    if(code[cursor]=='{') {scopes.push_back(cbufferPending);cbufferPending=false;}
                    else if(code[cursor]=='}' && !scopes.empty())scopes.pop_back();
                    ++cursor;
                }
            };
            const auto first=code.find_first_not_of(" \t");
            if(first!=code.npos && code[first]=='#')continue;
            for(std::sregex_iterator it(code.begin(),code.end(),declRegex),last;it!=last;++it) {
                advance(static_cast<size_t>(it->position()));
                const auto& match=*it;
                if(!scopes.empty() && !scopes.back())continue;
                const std::string type=match[1],name=match[2];
                if(IsEngineUniform(name) || name=="Reserved" || name=="u_BaseMap") {
                    hasRange=false;color=false;tooltip.clear();defaultValue.clear();continue;
                }
                ShaderPropertyDesc desc;
                desc.Name=name;desc.DisplayName=FormatDisplayName(name);desc.Header=header;desc.Tooltip=tooltip;
                desc.HasRange=hasRange;desc.MinValue=rangeMin;desc.MaxValue=rangeMax;
                if(type=="Texture2D")desc.Type=ShaderPropertyType::Texture2D;
                else if(type=="float4" && (color || name.find("Color")!=name.npos || name.find("Tint")!=name.npos)) {
                    desc.Type=ShaderPropertyType::Color;desc.DefaultVec4=glm::vec4(1);
                } else if(type=="float4")desc.Type=ShaderPropertyType::Float4;
                else if(type=="float3")desc.Type=ShaderPropertyType::Float3;
                else if(type=="float2")desc.Type=ShaderPropertyType::Float2;
                else if(type=="int" || type=="uint")desc.Type=ShaderPropertyType::Int;
                else if(type=="bool")desc.Type=ShaderPropertyType::Bool;
                else {desc.Type=ShaderPropertyType::Float;desc.DefaultFloat=hasRange?rangeMin:0.0f;}
                std::string value=defaultValue.empty()?(match[3].matched?match[3].str():""):defaultValue;
                if(!value.empty())try {
                    if(desc.Type==ShaderPropertyType::Float)desc.DefaultFloat=std::stof(value);
                    else if(desc.Type==ShaderPropertyType::Int)desc.DefaultInt=std::stoi(value);
                    else if(desc.Type==ShaderPropertyType::Bool)desc.DefaultBool=value.find("true")!=value.npos || std::stoi(value)!=0;
                    else if(desc.Type!=ShaderPropertyType::Texture2D) {
                        auto open=value.find('(');
                        if(open!=value.npos)value=value.substr(open+1);
                        std::vector<float> components;
                        for(std::sregex_iterator n(value.begin(),value.end(),numberRegex),last;n!=last;++n)components.push_back(std::stof(n->str()));
                        const int count=type=="float2"?2:type=="float3"?3:4;
                        if(components.size()==1)for(int i=0;i<count;++i)desc.DefaultVec4[i]=components[0];
                        else if(components.size()==count)for(int i=0;i<count;++i)desc.DefaultVec4[i]=components[i];
                    }
                }catch(...) {}
                if(!propIndices.contains(name)) {propIndices[name]=properties.size();properties.push_back(desc);}
                hasRange=false;color=false;tooltip.clear();defaultValue.clear();
            }
            advance(code.size());
        }

        // If reflection metadata is provided, enrich properties with byte offsets & constant buffer info
        if (reflection) {
            // Reflection is authoritative for active fields and storage types.
            std::vector<ShaderPropertyDesc> active;
            for (const auto& cb : reflection->ConstantBuffers) {
                for (const auto& var : cb.Variables) {
                    if (IsEngineUniform(var.Name) || var.Name == "Reserved" || var.Elements) continue;
                    ShaderPropertyDesc desc;
                    if (auto it=propIndices.find(var.Name); it!=propIndices.end()) desc=properties[it->second];
                    desc.Name=var.Name; desc.DisplayName=FormatDisplayName(var.Name);
                    desc.Offset=var.Offset; desc.Size=var.Size; desc.ConstantBufferName=cb.Name;
                    using Scalar=RHI::RHIShaderScalarType;
                    if (var.ScalarType==Scalar::Float && var.Rows==1 && var.Columns>=1 && var.Columns<=4) {
                        if (var.Columns==1) desc.Type=ShaderPropertyType::Float;
                        else if (var.Columns==2) desc.Type=ShaderPropertyType::Float2;
                        else if (var.Columns==3) desc.Type=ShaderPropertyType::Float3;
                        else if (desc.Type!=ShaderPropertyType::Color) desc.Type=ShaderPropertyType::Float4;
                    } else if ((var.ScalarType==Scalar::Int || var.ScalarType==Scalar::UInt) && var.Rows==1 && var.Columns==1) desc.Type=ShaderPropertyType::Int;
                    else if (var.ScalarType==Scalar::Bool && var.Rows==1 && var.Columns==1) desc.Type=ShaderPropertyType::Bool;
                    else if (var.ScalarType!=Scalar::Unknown || !propIndices.contains(var.Name)) continue;
                    active.push_back(desc);
                }
            }
            for (const auto& resource : reflection->Resources) {
                if (resource.Type!=RHI::RHIResourceType::Texture || resource.Name=="u_BaseMap" || resource.BindCount!=1 ||
                    (resource.TextureDimension && resource.TextureDimension!=2)) continue;
                ShaderPropertyDesc desc;
                if (auto it=propIndices.find(resource.Name); it!=propIndices.end()) desc=properties[it->second];
                desc.Name=resource.Name; desc.DisplayName=FormatDisplayName(resource.Name); desc.Type=ShaderPropertyType::Texture2D;
                active.push_back(desc);
            }
            return active;
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
                    inst.FloatVal = desc.DefaultFloat;
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

