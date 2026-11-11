#include <Axle/EnvironmentBake.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x "\n";return false;}}while(0)
using namespace Axle;
namespace {
DecodedImage Source(bool directional=false) {
    DecodedImage result;result.HDR=true;result.Mips.push_back({64,32,{}});auto& mip=result.Mips[0];mip.Pixels.resize(64*32*16);
    for(uint32_t y=0;y<32;++y)for(uint32_t x=0;x<64;++x) {
        float direction=std::cos((y+.5f)/32*glm::pi<float>());
        glm::vec4 color=directional?glm::vec4(2+direction,4+direction,8+direction,1):glm::vec4(2,4,8,1);
        std::memcpy(mip.Pixels.data()+(y*64+x)*16,&color,16);
    }
    return result;
}
EnvironmentBakeSettings Small() {EnvironmentBakeSettings s;s.EnvironmentSize=16;s.IrradianceSize=4;s.PrefilterSize=16;s.PrefilterMips=5;s.BRDFSize=16;s.Samples=128;return s;}
bool Constant() {
    BakedEnvironment maps;std::string error;CHECK(BakeEnvironment(Source(),Small(),maps,&error)&&error.empty());
    for(const auto* cube:{&maps.Radiance,&maps.Irradiance,&maps.Prefilter})for(const auto& face:cube->Faces)for(const auto& mip:face)for(const auto& p:mip.Pixels)
        CHECK(glm::length(glm::vec3(p)-glm::vec3(2,4,8))<1e-5f&&p.w==1);
    CHECK(maps.Radiance.Faces[0].size()==5&&maps.Prefilter.Faces[0].size()==5);
    for(float nv:{0.01f,.25f,.5f,.9f,1.f}) {
        auto v=IntegrateEnvironmentBRDF(nv,0,256);const float fc=std::pow(1-nv,5.f);
        CHECK(std::abs(v.x-(1-fc))<1e-5&&std::abs(v.y-fc)<1e-5);
    }
    for(const auto& pixel:maps.BRDF.Pixels)CHECK(std::isfinite(pixel.x)&&std::isfinite(pixel.y)&&pixel.x>=0&&pixel.y>=0&&pixel.x+pixel.y<1.1f&&pixel.z==0&&pixel.w==1);
    BakedEnvironment again;CHECK(BakeEnvironment(Source(),Small(),again));
    CHECK(again.BRDF.Pixels==maps.BRDF.Pixels&&again.Prefilter.Faces[4][3].Pixels==maps.Prefilter.Faces[4][3].Pixels);
    return true;
}
bool Directional() {
    auto s=Small();s.Samples=512;BakedEnvironment maps;CHECK(BakeEnvironment(Source(true),s,maps));
    for(uint32_t face=0;face<6;++face) {
        for(uint32_t y=0;y<s.IrradianceSize;++y)for(uint32_t x=0;x<s.IrradianceSize;++x) {
            auto direction=EnvironmentCubeDirection(face,(x+.5f)/s.IrradianceSize,(y+.5f)/s.IrradianceSize);
            const auto expected=glm::vec3(2,4,8)+glm::vec3(direction.y*(2.f/3));
            CHECK(glm::length(glm::vec3(maps.Irradiance.Faces[face][0].Pixels[y*s.IrradianceSize+x])-expected)<.025f);
        }
        for(const auto& p:maps.Radiance.Faces[face][0].Pixels)CHECK(p.x>=1&&p.x<=3&&p.z>=7&&p.z<=9);
    }
    // Face centers must map to standard native cube axes without vertical flips.
    const glm::vec3 axes[]{ {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
    for(uint32_t f=0;f<6;++f)CHECK(glm::length(EnvironmentCubeDirection(f,.5f,.5f)-axes[f])<1e-6f);
    const auto top=maps.Radiance.Faces[2][0].Pixels[(s.EnvironmentSize/2)*s.EnvironmentSize+s.EnvironmentSize/2];
    const auto bottom=maps.Radiance.Faces[3][0].Pixels[(s.EnvironmentSize/2)*s.EnvironmentSize+s.EnvironmentSize/2];
    CHECK(top.x>2.9&&bottom.x<1.1);
    // GGX roughness reduces the directional contrast while preserving energy.
    const auto sharp=maps.Prefilter.Faces[2][0].Pixels[8*16+8].x;
    const auto rough=maps.Prefilter.Faces[2][4].Pixels[0].x;
    CHECK(sharp>2.9f&&rough<sharp-.15f&&rough>2.4f);
    return true;
}
bool Invalid() {
    BakedEnvironment output;output.BRDF.Size=123;auto s=Small();std::string error;
    auto bad=s;bad.PrefilterMips=10;CHECK(!BakeEnvironment(Source(),bad,output,&error)&&!error.empty()&&output.BRDF.Size==123);
    bad=s;bad.Samples=4096;CHECK(!ValidateEnvironmentBakeSettings(bad));
    auto image=Source();image.Mips[0].Pixels.pop_back();CHECK(!BakeEnvironment(image,s,output,&error)&&output.BRDF.Size==123);
    image=Source();float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(image.Mips[0].Pixels.data(),&nan,4);
    CHECK(!BakeEnvironment(image,s,output,&error)&&error.find("ENV_BAKE_VALUE")!=std::string::npos);
    image=Source();image.HDR=false;CHECK(!BakeEnvironment(image,s,output));
    image=Source();image.Options.FlipVertically=true;CHECK(!BakeEnvironment(image,s,output));
    image=Source();float negative=-1;std::memcpy(image.Mips[0].Pixels.data(),&negative,4);CHECK(!BakeEnvironment(image,s,output));
    return true;
}
}
int main(){if(!Constant()||!Directional()||!Invalid())return 1;std::cout<<"PASS environment constant-energy, analytic diffuse, GGX roughness, cube orientation, DFG and transactional validation\n";return 0;}
