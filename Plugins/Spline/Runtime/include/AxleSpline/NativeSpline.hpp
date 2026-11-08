#pragma once
#include <Axle/PluginSDK.hpp>
#include <array>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <locale>
#include <cmath>
#include <utility>
namespace AxleSpline {
// DLL-owned authored state. No Engine/vendor types or runtime handles cross SDK.
struct NativeSpline {
    float Width=8, Step=1;
    std::vector<std::array<float,3>> Points{{0,0,0},{10,0,0}};
    bool Running=false;
    bool Valid() const {
        if(!std::isfinite(Width)||Width<=0||Width>100000||!std::isfinite(Step)||Step<0.001f||Step>10000||Points.size()<2||Points.size()>4096) return false;
        for(const auto& p:Points) for(float v:p) if(!std::isfinite(v)||std::abs(v)>1000000) return false;
        return true;
    }
    bool Read(const std::string& text) {
        std::istringstream in(text); in.imbue(std::locale::classic()); std::string tag; std::size_t count=0;
        NativeSpline parsed;
        if(!(in>>tag>>parsed.Width>>parsed.Step>>count)||tag!="spline-native-v1"||count<2||count>4096) return false;
        parsed.Points.resize(count);
        for(auto& p:parsed.Points) for(auto& v:p) if(!(in>>v)) return false;
        in>>std::ws; if(!in.eof()||!parsed.Valid()) return false;
        *this=std::move(parsed); return true;
    }
    std::string Write() const {
        if(!Valid()) return {};
        std::ostringstream out; out.imbue(std::locale::classic()); out<<std::setprecision(9)<<"spline-native-v1 "<<Width<<' '<<Step<<' '<<Points.size();
        for(const auto& p:Points) for(float v:p) out<<' '<<v;
        return out.str();
    }
};
}
