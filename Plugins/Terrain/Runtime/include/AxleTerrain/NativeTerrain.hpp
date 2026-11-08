#pragma once
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <locale>
#include <cmath>
#include <cstdint>
#include <utility>
namespace AxleTerrain {
// DLL-owned height samples; advanced mesh/physics prototypes remain separate.
struct NativeTerrain {
    std::uint32_t Width=16, Depth=16;
    float Spacing=1, Elevation=0, Friction=0.85f, Restitution=0.05f;
    std::vector<float> Heights=std::vector<float>(Width*Depth,0);
    bool Running=false;
    bool Valid() const {
        if(Width<2||Depth<2||Width>256||Depth>256||Heights.size()!=std::size_t(Width)*Depth) return false;
        if(!std::isfinite(Spacing)||Spacing<0.001f||Spacing>10000||!std::isfinite(Elevation)||std::abs(Elevation)>1000000||
            !std::isfinite(Friction)||Friction<0||!std::isfinite(Restitution)||Restitution<0||Restitution>1) return false;
        for(float v:Heights) if(!std::isfinite(v)||std::abs(v)>1000000) return false;
        return true;
    }
    bool Read(const std::string& text) {
        std::istringstream in(text); in.imbue(std::locale::classic()); std::string tag; NativeTerrain parsed;
        if(!(in>>tag>>parsed.Width>>parsed.Depth>>parsed.Spacing>>parsed.Elevation>>parsed.Friction>>parsed.Restitution)||tag!="terrain-native-v1"||
            parsed.Width<2||parsed.Depth<2||parsed.Width>256||parsed.Depth>256) return false;
        parsed.Heights.resize(std::size_t(parsed.Width)*parsed.Depth);
        for(auto& v:parsed.Heights) if(!(in>>v)) return false;
        in>>std::ws; if(!in.eof()||!parsed.Valid()) return false;
        *this=std::move(parsed); return true;
    }
    std::string Write() const {
        if(!Valid()) return {};
        std::ostringstream out; out.imbue(std::locale::classic()); out<<std::setprecision(9)<<"terrain-native-v1 "<<Width<<' '<<Depth<<' '
            <<Spacing<<' '<<Elevation<<' '<<Friction<<' '<<Restitution;
        for(float v:Heights) out<<' '<<v;
        return out.str();
    }
};
}
