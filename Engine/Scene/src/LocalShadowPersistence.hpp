#pragma once
#include <Axle/LocalShadowSettings.hpp>
#include <ostream>
#include <string>
#include <exception>

namespace Axle {
    inline void WriteLocalShadows(std::ostream& out,const LocalShadowSettings& s) {
        out << "      CastShadows: " << (s.CastShadows?"true":"false") << "\n";
        out << "      ShadowBias: " << s.Bias << "\n";
        out << "      ShadowSlopeBias: " << s.SlopeBias << "\n";
        out << "      ShadowNormalBias: " << s.NormalBias << "\n";
        out << "      ShadowFilterRadius: " << s.FilterRadius << "\n";
        out << "      ShadowNearPlane: " << s.NearPlane << "\n";
    }
    inline bool ReadLocalShadows(const std::string& text,LocalShadowSettings& s) {
        const auto colon=text.find(':');if(colon==std::string::npos)return false;
        const auto key=text.substr(0,colon);auto value=text.substr(colon+1);
        const auto first=value.find_first_not_of(" \t\r\n");
        value=first==std::string::npos?std::string{}:value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
        if(key=="CastShadows"){s.CastShadows=value=="true";return true;}
        float* target=key=="ShadowBias"?&s.Bias:key=="ShadowSlopeBias"?&s.SlopeBias:
            key=="ShadowNormalBias"?&s.NormalBias:key=="ShadowFilterRadius"?&s.FilterRadius:
            key=="ShadowNearPlane"?&s.NearPlane:nullptr;
        if(!target)return false;
        // Match the surrounding light fields: malformed text keeps defaults.
        try{*target=std::stof(value);}catch(const std::exception&){}
        return true;
    }

}
