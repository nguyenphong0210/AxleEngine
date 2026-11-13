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
bool GenerateProceduralSkyImage(const ProceduralSkySettings& settings,uint32_t width,uint32_t height,DecodedImage& output,std::string* error) {
    if(error)error->clear();
    if(width<8||height<4||width>4096||height>2048||!std::isfinite(settings.Intensity)||settings.Intensity<0||
       !std::isfinite(settings.Exposure)||settings.Exposure<0||double(settings.Intensity)*settings.Exposure>1e12||
       !std::isfinite(settings.SunSize)||!std::isfinite(settings.SunGlow)||!std::isfinite(settings.Turbidity))
        return Fail(error,"ENV_PROCEDURAL_SETTINGS: invalid dimensions, exposure or sky parameters");
    auto sun=glm::length(settings.SunDirection)>1e-4f?glm::normalize(settings.SunDirection):glm::vec3(0,1,0);
    const float sunElev=sun.y;
    const float dayFactor=std::clamp(sunElev*3.0f+0.5f,0.0f,1.0f);
    const float sunsetFactor=std::clamp(1.0f-std::abs(sunElev)*2.5f,0.0f,1.0f);
    const glm::vec3 sunsetHorizon(1.0f,0.45f,0.15f),nightZenith(0.015f,0.025f,0.06f),nightHorizon(0.04f,0.05f,0.08f);
    const glm::vec3 dynamicHorizon=glm::mix(settings.HorizonColor,sunsetHorizon,sunsetFactor*0.85f);
    const glm::vec3 dynamicZenith=glm::mix(nightZenith,settings.ZenithColor,dayFactor);
    const glm::vec3 finalHorizon=glm::mix(nightHorizon,dynamicHorizon,dayFactor);
    const float sunRadius=std::max(settings.SunSize,0.005f);
    const float glowExponent=std::max(16.0f/std::max(settings.SunGlow,0.01f),1.0f);
    const glm::vec3 scaledSun=glm::max(settings.SunColor,glm::vec3(0));
    DecodedImage candidate;candidate.HDR=true;candidate.SourceChannels=4;candidate.Options={};
    ImageMipData mip{width,height};mip.Pixels.resize(size_t(width)*height*16);
    auto smooth=[](float value){value=std::clamp(value,0.0f,1.0f);return value*value*(3.0f-2.0f*value);};
    for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
        const float u=(float(x)+0.5f)/float(width),v=(float(y)+0.5f)/float(height);
        const float phi=(u-0.5f)*glm::two_pi<float>(),theta=v*glm::pi<float>();
        const glm::vec3 direction(std::cos(phi)*std::sin(theta),std::cos(theta),std::sin(phi)*std::sin(theta));
        glm::vec3 skyColor;
        if(direction.y>=0.0f) {
            const float t=std::pow(direction.y,std::clamp(0.55f*settings.Turbidity,0.2f,2.0f));
            skyColor=glm::mix(finalHorizon,dynamicZenith,t);
        } else {
            const float t=std::pow(-direction.y,0.5f);
            skyColor=glm::mix(finalHorizon,settings.GroundColor*(0.3f+0.7f*dayFactor),t);
        }
        const float cosTheta=glm::dot(direction,sun);
        if(cosTheta>0.0f&&glm::dot(scaledSun,scaledSun)>0.0f) {
            const float glow=std::pow(std::max(cosTheta,0.0f),glowExponent)*dayFactor;
            skyColor+=glm::mix(scaledSun,sunsetHorizon,sunsetFactor*0.5f)*glow*1.8f;
            const float angle=std::acos(std::clamp(cosTheta,-1.0f,1.0f));
            const float edge0=sunRadius*0.90f,edge1=sunRadius*1.08f;
            const float disk=(1.0f-smooth((angle-edge0)/std::max(edge1-edge0,1e-5f))) * (sunElev+sunRadius>=0.0f?1.0f:0.0f);
            skyColor+=scaledSun*disk*12.0f*dayFactor;
        }
        const glm::vec3 radiance=glm::max(skyColor*settings.Exposure*settings.Intensity,glm::vec3(0));
        float values[4]={radiance.r,radiance.g,radiance.b,1.0f};
        std::memcpy(mip.Pixels.data()+(size_t(y)*width+x)*16,values,16);
    }
    candidate.Mips.push_back(std::move(mip));output=std::move(candidate);return true;
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
