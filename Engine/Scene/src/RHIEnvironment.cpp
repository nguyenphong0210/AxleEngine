#include <Axle/RHIEnvironment.hpp>
#include <Axle/ImageSource.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Path.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <array>

namespace Axle {
using namespace RHI;
namespace {
std::string SettingsKey(const EnvironmentBakeSettings& s) {
    return std::to_string(s.EnvironmentSize)+":"+std::to_string(s.IrradianceSize)+":"+std::to_string(s.PrefilterSize)+":"+
        std::to_string(s.PrefilterMips)+":"+std::to_string(s.BRDFSize)+":"+std::to_string(s.Samples);
}
bool Upload(const BakedEnvironment& data,RHIEnvironmentGeneration& generation) {
    const EnvironmentCube* cubes[]{&data.Radiance,&data.Irradiance,&data.Prefilter};
    std::shared_ptr<RHITexture>* targets[]{&generation.Radiance,&generation.Irradiance,&generation.Prefilter};
    std::array<std::vector<nri::TextureSubresourceUploadDesc>,4> subresources;
    std::array<nri::TextureUploadDesc,4> uploads{};
    for(size_t i=0;i<4;++i) {
        auto texture=std::make_shared<RHITexture>();RHITextureDesc description;
        if(i<3)description=RHITextureDesc::Cubemap(cubes[i]->Faces[0][0].Size,static_cast<uint32_t>(cubes[i]->Faces[0].size()),RHITextureFormat::RGBA32_FLOAT,"EnvironmentCube");
        else {description.Width=description.Height=data.BRDF.Size;description.Format=RHITextureFormat::RGBA32_FLOAT;description.Usage=RHITextureUsage::ShaderResource;description.DebugName="EnvironmentBRDF";}
        if(!texture->Init(description))return false;
        auto add=[&](const EnvironmentMip& mip) {
            nri::TextureSubresourceUploadDesc sub{};sub.slices=mip.Pixels.data();sub.sliceNum=1;
            sub.rowPitch=mip.Size*16;sub.slicePitch=mip.Size*mip.Size*16;subresources[i].push_back(sub);
        };
        if(i<3)for(const auto& face:cubes[i]->Faces)for(const auto& mip:face)add(mip);
        else add(data.BRDF);
        uploads[i].texture=texture->GetNative();uploads[i].subresources=subresources[i].data();uploads[i].planes=nri::PlaneBits::COLOR;
        uploads[i].after={nri::AccessBits::SHADER_RESOURCE,nri::Layout::SHADER_RESOURCE,nri::StageBits::ALL};
        if(i<3)*targets[i]=std::move(texture);else generation.BRDF=std::move(texture);
    }
    return RHIDevice::Get().UploadData(uploads.data(),4,nullptr,0)==nri::Result::SUCCESS;
}
}
bool RHIEnvironmentGeneration::IsValid() const {
    return Radiance&&Irradiance&&Prefilter&&BRDF&&Radiance->IsValid()&&Irradiance->IsValid()&&Prefilter->IsValid()&&BRDF->IsValid();
}
bool RHIEnvironment::SetBakeSettings(const EnvironmentBakeSettings& settings,std::string* error) {
    if(!ValidateEnvironmentBakeSettings(settings,error))return false;
    if(settings!=m_Settings){Clear();m_Settings=settings;}
    return true;
}
void RHIEnvironment::Clear() {
    m_Selected.reset();m_Cache.clear();m_Diagnostic.clear();m_FailedBakeKey.clear();m_FailedBakeDiagnostic.clear();m_SourceExposure=1;
}
bool RHIEnvironment::Fallback(const std::string& request,const std::string& message) {
    m_Diagnostic=message;
    for(const auto& entry:m_Cache)if(entry->Request==request&&entry->IsValid()&&(!m_Selected||entry->Sequence>m_Selected->Sequence))m_Selected=entry;
    if(m_Selected){m_Selected->Used=++m_Clock;m_Diagnostic+="; retained same-source last-good environment";}
    return true; // An authored ambient fallback is a successful, diagnosed view.
}
bool RHIEnvironment::Prepare(const RenderSkyboxData& sky) {
    m_Selected.reset();m_Diagnostic.clear();m_SourceExposure=1;
    std::erase_if(m_Cache,[](const auto& generation){return !generation->IsValid();});
    if(!sky.Authored||!sky.Enabled||sky.Type!=1)return true;
    if(sky.HDRPath.empty()){m_Diagnostic="ENV_SOURCE: empty authored HDR reference; using diffuse ambient";return true;}
    if(!RHIDevice::Get().CanCreateResources()){m_Diagnostic="ENV_DEVICE: native render owner is unavailable";return false;}
    if(!std::isfinite(sky.Intensity)||sky.Intensity<0||!std::isfinite(sky.Exposure)||sky.Exposure<0||double(sky.Intensity)*sky.Exposure>1e12) {
        m_Diagnostic="ENV_SETTINGS: intensity/exposure must be finite, non-negative and bounded";return true;
    }
    const auto path=FileSystem::ResolveAssetPath(PathFromUtf8(sky.HDRPath));
    std::string request=std::to_string(FileSystem::GetSourceGeneration())+":"+PathToUtf8(path.lexically_normal());
    auto sourcePath=path;
    ImageDecodeOptions options;options.GenerateMips=true;options.MaxResolution=2048;
    auto extension=PathToUtf8(path.extension());std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(extension==".axleasset") {
        TextureDescriptor descriptor;
        if(!TextureDescriptor::Deserialize(FileSystem::ReadTextFile(path),descriptor)||descriptor.Type!="CubemapHDR"||descriptor.Source.empty()||
           !std::isfinite(descriptor.Exposure)||descriptor.Exposure<0||double(descriptor.Exposure)*sky.Intensity*sky.Exposure>1e12)
            return Fallback(request,"ENV_DESCRIPTOR: valid CubemapHDR source and exposure required");
        m_SourceExposure=descriptor.Exposure;
        sourcePath=FileSystem::ResolveAssetPath(PathFromUtf8(descriptor.Source));
        request+="|"+PathToUtf8(sourcePath.lexically_normal());
        options.GenerateMips=descriptor.GenerateMips;
        if(descriptor.MaxResolution)options.MaxResolution=std::min(options.MaxResolution,descriptor.MaxResolution);
    }
    // Resolve descriptors here so a newly authored missing source cannot reuse
    // a different source from the generic descriptor last-good cache.
    const auto source=LoadRawImageSource(sourcePath,options);
    if(!source.Image)return Fallback(request,"ENV_SOURCE: "+source.Error);
    if(!source.Image->HDR)return Fallback(request,"ENV_FORMAT: floating-point HDR source required");
    const auto key=request+"|"+source.SourceKey+"|"+std::to_string(source.Revision)+"|"+ImageInterpretationKey(source.Image->Options)+"|"+SettingsKey(m_Settings);
    for(const auto& generation:m_Cache)if(generation->Key==key) {
        m_Selected=generation;generation->Used=++m_Clock;
        if(source.UsedLastGood)m_Diagnostic="ENV_SOURCE_LAST_GOOD: "+source.Error;
        return true;
    }
    if(key==m_FailedBakeKey)return Fallback(request,m_FailedBakeDiagnostic);
    BakedEnvironment baked;std::string error;++m_BakeCount;
    if(!BakeEnvironment(*source.Image,m_Settings,baked,&error)) {
        m_FailedBakeKey=key;m_FailedBakeDiagnostic=error;return Fallback(request,error);
    }
    auto candidate=std::make_shared<RHIEnvironmentGeneration>();candidate->Key=key;candidate->Request=request;
    if(!Upload(baked,*candidate))return Fallback(request,"ENV_UPLOAD: native allocation/upload failed");
    ++m_UploadCount;candidate->Used=++m_Clock;candidate->Sequence=++m_Sequence;
    m_Selected=candidate;m_Cache.push_back(std::move(candidate));
    while(m_Cache.size()>4) {
        auto oldest=std::min_element(m_Cache.begin(),m_Cache.end(),[](const auto& a,const auto& b){return a->Used<b->Used;});
        m_Cache.erase(oldest);
    }
    if(source.UsedLastGood)m_Diagnostic="ENV_SOURCE_LAST_GOOD: "+source.Error;
    return true;
}
}
