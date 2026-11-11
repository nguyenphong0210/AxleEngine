#include <Axle/EnvironmentBake.hpp>
#include <Axle/JobSystem.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Axle {
namespace {
constexpr double Pi=glm::pi<double>();
bool Fail(std::string* error,const char* message) {if(error)*error=message;return false;}
bool PowerOfTwo(uint32_t v) {return v && !(v&(v-1));}
uint32_t MipCount(uint32_t v) {uint32_t n=0;for(;v;v>>=1)++n;return n;}
double RadicalInverse(uint32_t bits) {
    bits=(bits<<16)|(bits>>16);bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);
    bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);
    bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);
    bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);
    return bits*2.3283064365386963e-10;
}
glm::dvec3 LocalToWorld(const glm::dvec3& n,const glm::dvec3& local) {
    const auto up=std::abs(n.z)<.999?glm::dvec3(0,0,1):glm::dvec3(1,0,0);
    const auto t=glm::normalize(glm::cross(up,n));
    return t*local.x+glm::cross(n,t)*local.y+n*local.z;
}
glm::dvec3 GGX(double x,double y,const glm::dvec3& n,double roughness) {
    const double a=roughness*roughness;
    const double cosTheta=std::sqrt((1-y)/(1+(a*a-1)*y));
    const double sinTheta=std::sqrt(std::max(0.0,1-cosTheta*cosTheta)),phi=2*Pi*x;
    return LocalToWorld(n,{std::cos(phi)*sinTheta,std::sin(phi)*sinTheta,cosTheta});
}
template<class F> void Each(uint32_t count,F&& f) {
    if(JobSystem::IsInitialized() && JobSystem::IsValidSchedulerContext()) JobSystem::ParallelFor(count,f,32);
    else for(uint32_t i=0;i<count;++i) f(i);
}
struct Source {
    const DecodedImage& Image;
    glm::dvec3 Pixel(const ImageMipData& mip,int x,int y) const {
        x=(x%int(mip.Width)+int(mip.Width))%int(mip.Width);y=std::clamp(y,0,int(mip.Height)-1);
        float rgba[4];std::memcpy(rgba,mip.Pixels.data()+(size_t(y)*mip.Width+x)*16,16);
        return {rgba[0],rgba[1],rgba[2]};
    }
    glm::dvec3 Bilinear(const ImageMipData& mip,double u,double v) const {
        const double x=u*mip.Width-.5,y=v*mip.Height-.5;
        const int ix=int(std::floor(x)),iy=int(std::floor(y));
        return glm::mix(glm::mix(Pixel(mip,ix,iy),Pixel(mip,ix+1,iy),x-ix),
                        glm::mix(Pixel(mip,ix,iy+1),Pixel(mip,ix+1,iy+1),x-ix),y-iy);
    }
    glm::dvec3 Sample(const glm::dvec3& d,double lod=0) const {
        const double u=std::atan2(d.z,d.x)/(2*Pi)+.5,v=.5-std::asin(std::clamp(d.y,-1.0,1.0))/Pi;
        lod=std::clamp(lod,0.0,double(Image.Mips.size()-1));const auto level=size_t(lod);
        return glm::mix(Bilinear(Image.Mips[level],u,v),Bilinear(Image.Mips[std::min(level+1,Image.Mips.size()-1)],u,v),lod-level);
    }
};
EnvironmentMip MakeMip(uint32_t size) {return {size,std::vector<glm::vec4>(size_t(size)*size,glm::vec4(0,0,0,1))};}
}

bool ValidateEnvironmentBakeSettings(const EnvironmentBakeSettings& s,std::string* error) {
    if(error)error->clear();
    if(!PowerOfTwo(s.EnvironmentSize)||s.EnvironmentSize<16||s.EnvironmentSize>512||
       !PowerOfTwo(s.IrradianceSize)||s.IrradianceSize<4||s.IrradianceSize>64||
       !PowerOfTwo(s.PrefilterSize)||s.PrefilterSize<16||s.PrefilterSize>256||
       !PowerOfTwo(s.BRDFSize)||s.BRDFSize<16||s.BRDFSize>256||
       !s.PrefilterMips||s.PrefilterMips>MipCount(s.PrefilterSize)||s.Samples<16||s.Samples>2048)
        return Fail(error,"ENV_BAKE_SETTINGS: invalid power-of-two dimensions, mip count or samples");
    uint64_t work=uint64_t(s.IrradianceSize)*s.IrradianceSize*6+uint64_t(s.BRDFSize)*s.BRDFSize;
    for(uint32_t i=1;i<s.PrefilterMips;++i)work+=uint64_t(s.PrefilterSize>>i)*(s.PrefilterSize>>i)*6;
    if(work*s.Samples>128000000) return Fail(error,"ENV_BAKE_BUDGET: sample budget exceeds 128 million");
    return true;
}
glm::vec3 EnvironmentCubeDirection(uint32_t face,float u,float v) {
    const float x=2*u-1,y=2*v-1;
    const std::array<glm::vec3,6> directions={glm::vec3(1,-y,-x),glm::vec3(-1,-y,x),glm::vec3(x,1,y),glm::vec3(x,-1,-y),glm::vec3(x,-y,1),glm::vec3(-x,-y,-1)};
    return face<6?glm::normalize(directions[face]):glm::vec3(0);
}
glm::vec2 IntegrateEnvironmentBRDF(float nDotV,float roughness,uint32_t samples) {
    if(!samples||!std::isfinite(nDotV)||!std::isfinite(roughness))return {};
    const double nv=std::clamp(double(nDotV),1e-5,1.0),r=std::clamp(double(roughness),0.0,1.0);
    const glm::dvec3 n(0,0,1),v(std::sqrt(1-nv*nv),0,nv);
    double a=0,b=0;
    for(uint32_t i=0;i<samples;++i) {
        const auto h=GGX(double(i)/samples,RadicalInverse(i),n,r);
        const double vh=std::max(glm::dot(v,h),0.0),nh=std::max(h.z,0.0);
        const auto l=2*vh*h-v;const double nl=std::max(l.z,0.0);
        if(nl<=0||nh<=0)continue;
        const double k=r*r*.5;
        const double g=(nv/(nv*(1-k)+k))*(nl/(nl*(1-k)+k));
        const double visible=g*vh/(nh*nv),fc=std::pow(1-vh,5);
        a+=(1-fc)*visible;b+=fc*visible;
    }
    return {a/samples,b/samples};
}
bool BakeEnvironment(const DecodedImage& image,const EnvironmentBakeSettings& s,BakedEnvironment& output,std::string* error) {
    if(!ValidateEnvironmentBakeSettings(s,error))return false;
    if(!image.HDR||image.Options.FlipVertically||image.Options.SRGB||image.Mips.empty())
        return Fail(error,"ENV_BAKE_SOURCE: unflipped linear floating-point image required");
    uint32_t width=0,height=0;
    for(const auto& mip:image.Mips) {
        if(!mip.Width||!mip.Height||mip.Width>8192||mip.Height>8192||mip.Pixels.size()!=uint64_t(mip.Width)*mip.Height*16||
           (width&&(mip.Width!=std::max(1u,width/2)||mip.Height!=std::max(1u,height/2))))
            return Fail(error,"ENV_BAKE_SOURCE: invalid image dimensions or mip chain");
        for(size_t i=0;i<mip.Pixels.size();i+=4) {
            float value;std::memcpy(&value,mip.Pixels.data()+i,4);
            if(!std::isfinite(value)||value<0||value>1e12f)return Fail(error,"ENV_BAKE_VALUE: image values must be finite and in [0, 1e12]");
        }
        width=mip.Width;height=mip.Height;
    }
    try {
        BakedEnvironment candidate;const Source source{image};
        for(uint32_t face=0;face<6;++face) {
            for(uint32_t size=s.EnvironmentSize;size;size>>=1) {
                auto mip=MakeMip(size);
                Each(size*size,[&](uint32_t index){
                    const auto n=glm::dvec3(EnvironmentCubeDirection(face,(index%size+.5f)/size,(index/size+.5f)/size));
                    const double lod=std::max(0.0,std::log2(double(image.Mips[0].Width)/(size*4.0)));
                    mip.Pixels[index]=glm::vec4(source.Sample(n,lod),1);
                });
                candidate.Radiance.Faces[face].push_back(std::move(mip));
            }
            auto diffuse=MakeMip(s.IrradianceSize);
            Each(s.IrradianceSize*s.IrradianceSize,[&](uint32_t index){
                const auto n=glm::dvec3(EnvironmentCubeDirection(face,(index%s.IrradianceSize+.5f)/s.IrradianceSize,(index/s.IrradianceSize+.5f)/s.IrradianceSize));
                glm::dvec3 sum(0);
                for(uint32_t i=0;i<s.Samples;++i) {
                    const double x=(i+.5)/s.Samples,y=RadicalInverse(i),phi=2*Pi*y;
                    const auto l=LocalToWorld(n,{std::sqrt(x)*std::cos(phi),std::sqrt(x)*std::sin(phi),std::sqrt(1-x)});
                    sum+=source.Sample(l);
                }
                diffuse.Pixels[index]=glm::vec4(sum/double(s.Samples),1);
            });
            candidate.Irradiance.Faces[face].push_back(std::move(diffuse));
            for(uint32_t level=0;level<s.PrefilterMips;++level) {
                const uint32_t size=s.PrefilterSize>>level;const double rough=s.PrefilterMips>1?double(level)/(s.PrefilterMips-1):0;
                auto mip=MakeMip(size);
                Each(size*size,[&](uint32_t index){
                    const auto n=glm::dvec3(EnvironmentCubeDirection(face,(index%size+.5f)/size,(index/size+.5f)/size));
                    if(level==0){mip.Pixels[index]=glm::vec4(source.Sample(n),1);return;}
                    glm::dvec3 sum(0);double weight=0;
                    for(uint32_t i=0;i<s.Samples;++i) {
                        const auto h=GGX(double(i)/s.Samples,RadicalInverse(i),n,rough);
                        const double nh=std::max(glm::dot(n,h),0.0);
                        const auto l=2*nh*h-n;const double nl=std::max(glm::dot(n,l),0.0);
                        if(nl<=0)continue;
                        const double a=rough*rough,denom=nh*nh*(a*a-1)+1;
                        const double pdf=std::max(a*a/(Pi*denom*denom)*.25,1e-8);
                        const double sinTheta=std::sqrt(std::max(0.0,1-l.y*l.y));
                        const double texelAngle=2*Pi*Pi*std::max(sinTheta,1e-4)/(image.Mips[0].Width*double(image.Mips[0].Height));
                        const double lod=std::max(0.0,.5*std::log2(1/(s.Samples*pdf*texelAngle)));
                        sum+=source.Sample(l,lod)*nl;weight+=nl;
                    }
                    mip.Pixels[index]=glm::vec4(weight>0?sum/weight:source.Sample(n),1);
                });
                candidate.Prefilter.Faces[face].push_back(std::move(mip));
            }
        }
        candidate.BRDF=MakeMip(s.BRDFSize);
        Each(s.BRDFSize*s.BRDFSize,[&](uint32_t index){
            const auto value=IntegrateEnvironmentBRDF((index%s.BRDFSize+.5f)/s.BRDFSize,(index/s.BRDFSize+.5f)/s.BRDFSize,s.Samples);
            candidate.BRDF.Pixels[index]={value.x,value.y,0,1};
        });
        output=std::move(candidate);return true;
    }catch(const std::bad_alloc&){return Fail(error,"ENV_BAKE_MEMORY: allocation failed; prior result retained");}
}
}
