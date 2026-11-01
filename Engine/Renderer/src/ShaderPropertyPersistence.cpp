#include <Axle/ShaderPropertyPersistence.hpp>
#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <limits>

namespace Axle {
    namespace {
        std::string Trim(std::string_view text) {
            auto first=text.find_first_not_of(" \t\r\n");
            return first==text.npos?"":std::string(text.substr(first,text.find_last_not_of(" \t\r\n")-first+1));
        }
        std::string Unquote(std::string value) {
            if (value.size()>=2 && value.front()=='"' && value.back()=='"') value=value.substr(1,value.size()-2);
            std::string result;
            for (size_t i=0;i<value.size();++i) {
                if (value[i]=='\\' && i+1<value.size() && (value[i+1]=='\\' || value[i+1]=='"')) ++i;
                result+=value[i];
            }
            return result;
        }
        const char* Key(ShaderPropertyType type) {
            switch(type) {
                case ShaderPropertyType::Float:return "customFloat";
                case ShaderPropertyType::Float2:return "customVec2";
                case ShaderPropertyType::Float3:return "customVec3";
                case ShaderPropertyType::Float4:return "customVec4";
                case ShaderPropertyType::Color:return "customColor";
                case ShaderPropertyType::Int:return "customInt";
                case ShaderPropertyType::Bool:return "customBool";
                case ShaderPropertyType::Texture2D:return "customTexture";
            }
            return "customFloat";
        }
    }
    void WriteShaderProperties(std::ostream& stream,const std::vector<ShaderPropertyInstance>& properties,std::string_view indent) {
        auto precision=stream.precision();
        stream<<std::setprecision(std::numeric_limits<float>::max_digits10);
        for (const auto& p:properties) {
            stream<<indent<<Key(p.Desc.Type)<<'.'<<p.Desc.Name<<": ";
            if (p.Desc.Type==ShaderPropertyType::Float) stream<<p.FloatVal;
            else if (p.Desc.Type==ShaderPropertyType::Int) stream<<p.IntVal;
            else if (p.Desc.Type==ShaderPropertyType::Bool) stream<<(p.BoolVal?"true":"false");
            else if (p.Desc.Type==ShaderPropertyType::Texture2D) stream<<std::quoted(p.TexturePath);
            else stream<<"[ "<<p.Vec4Val.x<<" , "<<p.Vec4Val.y<<" , "<<p.Vec4Val.z<<" , "<<p.Vec4Val.w<<" ]";
            stream<<'\n';
            if (p.Desc.HasRange) stream<<indent<<"customRange."<<p.Desc.Name<<": [ "<<p.Desc.MinValue<<" , "<<p.Desc.MaxValue<<" ]\n";
        }
        stream.precision(precision);
    }
    bool ReadShaderPropertyLine(std::string_view line,std::vector<ShaderPropertyInstance>& properties) {
        auto dot=line.find('.'), colon=line.find(':');
        if (dot==line.npos || colon==line.npos || dot>=colon) return false;
        auto key=line.substr(0,dot);
        bool range=key=="customRange";
        ShaderPropertyType type=ShaderPropertyType::Float;
        bool known=range;
        for (int i=0;i<=static_cast<int>(ShaderPropertyType::Texture2D);++i)
            if (key==Key(static_cast<ShaderPropertyType>(i))) { type=static_cast<ShaderPropertyType>(i); known=true; break; }
        if (!known) return false;
        auto name=Trim(line.substr(dot+1,colon-dot-1));
        if (name.empty()) return false;
        auto value=Trim(line.substr(colon+1));
        if (value.empty()) return false;
        auto found=std::find_if(properties.begin(),properties.end(),[&](const auto& p){return p.Desc.Name==name;});
        ShaderPropertyInstance parsed=found==properties.end()?ShaderPropertyInstance{}:*found;
        parsed.Desc.Name=name;
        parsed.Desc.DisplayName=ShaderPropertyParser::FormatDisplayName(name);
        if (!range) parsed.Desc.Type=type;
        try {
            if (range) {
                if (std::sscanf(value.c_str(),"[ %f , %f ]",&parsed.Desc.MinValue,&parsed.Desc.MaxValue)!=2) return false;
                parsed.Desc.HasRange=true;
            } else if (type==ShaderPropertyType::Float) parsed.FloatVal=std::stof(value);
            else if (type==ShaderPropertyType::Int) parsed.IntVal=std::stoi(value);
            else if (type==ShaderPropertyType::Bool) {
                if (value!="true" && value!="false" && value!="0" && value!="1") return false;
                parsed.BoolVal=value=="true" || value=="1";
            } else if (type==ShaderPropertyType::Texture2D) parsed.TexturePath=Unquote(value);
            else {
                value=Unquote(value);
                if (type==ShaderPropertyType::Color && value.front()=='#') {
                    auto hex=value.substr(1);
                    if (hex.size()!=6 && hex.size()!=8) return false;
                    auto packed=std::stoul(hex,nullptr,16);
                    if (hex.size()==6) packed=(packed<<8)|255;
                    parsed.Vec4Val=glm::vec4((packed>>24)&255,(packed>>16)&255,(packed>>8)&255,packed&255)/255.0f;
                } else if (std::sscanf(value.c_str(),"[ %f , %f , %f , %f ]",&parsed.Vec4Val.x,&parsed.Vec4Val.y,&parsed.Vec4Val.z,&parsed.Vec4Val.w)<2) return false;
            }
        } catch (...) { return false; }
        if (found==properties.end()) properties.push_back(parsed);
        else *found=parsed;
        return true;
    }
}
