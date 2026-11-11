#include <Axle/MaterialGraphResources.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Path.hpp>
#include <Axle/Package.hpp>
#include <Axle/TextureDescriptor.hpp>
#include "MaterialGraphInternal.hpp"
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>
#include <limits>

namespace Axle {
namespace {
    [[noreturn]] void Fail(const char* code, std::string message) {throw MaterialGraphDiagnostic{code,std::move(message),{},0,"texturePath"};}
    std::filesystem::path Rooted(const std::string& reference) {
        if(!MaterialGraphInternal::CanonicalAssetPath(reference))Fail("MG_RESOURCE_PATH","Expected canonical root-relative reference: "+reference);
        std::error_code error;
        const auto absoluteRoot=std::filesystem::absolute(FileSystem::GetAssetDirectory(),error);
        if(error)Fail("MG_RESOURCE_PATH","Cannot make the active asset root absolute");
        const auto root=std::filesystem::weakly_canonical(absoluteRoot,error);
        if(error)Fail("MG_RESOURCE_PATH","Cannot resolve the active asset root");
        const auto path=std::filesystem::weakly_canonical(root/PathFromUtf8(reference),error);
        const auto relative=path.lexically_relative(root);
        if(error || relative.empty() || relative.is_absolute() || *relative.begin()=="..")
            Fail("MG_RESOURCE_PATH","Reference escapes the active asset root: "+reference);
        return path;
    }
    std::vector<uint8_t> Read(const std::string& reference) {
        const auto path=Rooted(reference);
        // Exact VFS entries take precedence. Disk reads accept only project-root
        // regular files, without general resource/CWD discovery or device paths.
        if(const auto* package=FileSystem::GetMountedPackage()) {
            // The general Package string API applies legacy prefix aliases. Graph
            // references must keep their exact project-relative identity.
            const auto entry=package->GetEntries().find(reference);
            if(entry!=package->GetEntries().end())return package->ReadEntry(entry->second);
        }
        std::error_code error;
        if(!std::filesystem::is_regular_file(path,error) || error)return {};
        std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return {};
        const auto size=file.tellg();if(size<=0)return {};
        if(size>(std::numeric_limits<int>::max)())Fail("MG_TEXTURE_READ","Image source exceeds decoder size limits: "+reference);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(bytes.data()),size))return {};
        return bytes;
    }
    std::string Extension(const std::string& path) {
        auto extension=PathFromUtf8(path).extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return extension;
    }
    TextureDescriptor Descriptor(std::span<const uint8_t> bytes) {
        const std::string source(bytes.begin(),bytes.end());AssetDocument document;std::string error;
        if(!AssetDSL::ParseSource(source,document,&error) || document.Kind!=AssetKind::Texture || !document.Root || !document.Root->Children.empty())
            Fail("MG_TEXTURE_DESCRIPTOR","Invalid Texture descriptor: "+error);
        const std::set<std::string> strings{"name","source","type","filter","wrapU","wrapV","compression"};
        std::set<std::string> seen;
        for(const auto& property:document.Root->Properties) {
            const auto& value=property.RawValue;
            if(!seen.insert(property.Key).second)Fail("MG_TEXTURE_DESCRIPTOR","Duplicate descriptor field: "+property.Key);
            if(strings.contains(property.Key)) {
                if(value.size()<2 || value.front()!='"' || value.back()!='"')Fail("MG_TEXTURE_DESCRIPTOR","Expected quoted descriptor field: "+property.Key);
            } else if(property.Key=="generateMips" || property.Key=="generatePrefilter") {
                if(value!="true" && value!="false")Fail("MG_TEXTURE_DESCRIPTOR","Expected Boolean descriptor field: "+property.Key);
            } else if(property.Key=="maxResolution") {
                uint32_t size=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),size);
                if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size())Fail("MG_TEXTURE_DESCRIPTOR","Invalid maxResolution");
            } else if(property.Key=="exposure") {
                float exposure=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),exposure);
                if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || !std::isfinite(exposure) || exposure!=1)
                    Fail("MG_TEXTURE_DESCRIPTOR","2D graph textures require exposure 1");
            } else Fail("MG_TEXTURE_DESCRIPTOR","Unsupported descriptor field: "+property.Key);
        }
        TextureDescriptor descriptor;
        if(!seen.contains("source") || !TextureDescriptor::Deserialize(source,descriptor))Fail("MG_TEXTURE_DESCRIPTOR","Texture descriptor requires a source");
        if((descriptor.Type!="Color" && descriptor.Type!="Data" && descriptor.Type!="NormalMap") || descriptor.GeneratePrefilter)
            Fail("MG_TEXTURE_DESCRIPTOR","Graph sampling requires a 2D Color, Data or NormalMap descriptor without prefiltering");
        if((descriptor.Filter!="Point" && descriptor.Filter!="Linear" && descriptor.Filter!="Anisotropic16x") ||
           (descriptor.WrapU!="Repeat" && descriptor.WrapU!="Clamp" && descriptor.WrapU!="Mirror") ||
           (descriptor.WrapV!="Repeat" && descriptor.WrapV!="Clamp" && descriptor.WrapV!="Mirror"))
            Fail("MG_TEXTURE_DESCRIPTOR","Unsupported descriptor filter/wrap metadata");
        Rooted(descriptor.Source);
        if(Extension(descriptor.Source)==".axleasset")Fail("MG_TEXTURE_DESCRIPTOR","Texture descriptor source must be a raw image, not another descriptor");
        return descriptor;
    }
    void Field(std::string& key,std::string_view value) {key+=std::to_string(value.size())+":";key+=value;}
    std::shared_ptr<const DecodedImage> Fallback(const MaterialHLSLTexture& texture,const ImageDecodeOptions& options) {
        auto image=std::make_shared<DecodedImage>();image->SourceChannels=4;image->Options=options;
        std::vector<uint8_t> rgba=texture.Fallback=="White"?std::vector<uint8_t>{255,255,255,255}:
            texture.Fallback=="FlatNormal"?std::vector<uint8_t>{128,128,255,255}:std::vector<uint8_t>{255,0,255,255};
        image->Mips.push_back({1,1,std::move(rgba)});return image;
    }
}

bool MaterialGraphResources::Resolve(const MaterialGraphDocument& document,const MaterialHLSLSource& graph,
                                    MaterialGraphResourceSet& out,MaterialGraphDiagnostic* error,
                                    const std::span<const std::string>* liveSymbols) {
    if(error)*error={};MaterialGraphResourceSet candidate;candidate.Key="MaterialGraphResources:1";
    uint64_t activeNode=0;std::string activePath;
    auto locate=[&](MaterialGraphDiagnostic& diagnostic) {
        diagnostic.NodeID=activeNode;diagnostic.Source=document.Source;
        for(const auto& node:document.Nodes)if(node.ID==activeNode)diagnostic.Source=node.Source;
        diagnostic.Message="Texture '"+activePath+"': "+diagnostic.Message;
    };
    try {
        for(const auto& texture:graph.Textures) {
            if(liveSymbols && std::find(liveSymbols->begin(),liveSymbols->end(),texture.TextureSymbol)==liveSymbols->end())continue;
            activeNode=texture.NodeID;activePath=texture.Path;
            if((texture.ColorSpace!="sRGB" && texture.ColorSpace!="Linear") ||
               (texture.Sampler!="LinearRepeat" && texture.Sampler!="LinearClamp" && texture.Sampler!="PointRepeat" && texture.Sampler!="PointClamp") ||
               (texture.Fallback!="Error" && texture.Fallback!="White" && texture.Fallback!="FlatNormal" && texture.Fallback!="Magenta") ||
               (texture.Fallback=="FlatNormal" && texture.ColorSpace!="Linear"))Fail("MG_RESOURCE","Invalid sampling/fallback contract");
            ImageDecodeOptions options;options.FlipVertically=true;options.SRGB=texture.ColorSpace=="sRGB";options.GenerateMips=true;
            auto bytes=Read(texture.Path);const bool descriptor=Extension(texture.Path)==".axleasset";
            const auto authoredRevision=ImageContentRevision(bytes);
            candidate.Dependencies.push_back({texture.NodeID,texture.Path,descriptor,!bytes.empty(),authoredRevision});
            std::string source=texture.Path;
            if(descriptor && !bytes.empty()) {
                const auto desc=Descriptor(bytes);
                if((desc.Type=="Data" || desc.Type=="NormalMap") && options.SRGB)Fail("MG_TEXTURE_COLORSPACE","Data/NormalMap descriptors require Linear sampling");
                options.GenerateMips=desc.GenerateMips;options.MaxResolution=desc.MaxResolution;
                source=desc.Source;bytes=Read(source);
                candidate.Dependencies.push_back({texture.NodeID,source,false,!bytes.empty(),ImageContentRevision(bytes)});
            }
            const auto revision=ImageContentRevision(bytes);
            std::string decodeError;auto image=DecodeImage(bytes,options,&decodeError);
            bool fallback=false;
            if(!image) {
                if(texture.Fallback=="Error")Fail(bytes.empty()?"MG_TEXTURE_MISSING":"MG_TEXTURE_DECODE",source+": "+decodeError);
                MaterialGraphDiagnostic warning{"MG_TEXTURE_FALLBACK",source+": "+decodeError+"; using authored "+texture.Fallback,{},0,"texturePath"};
                locate(warning);candidate.Warnings.push_back(std::move(warning));fallback=true;image=Fallback(texture,options);
            }
            if(image->HDR && options.SRGB)Fail("MG_TEXTURE_COLORSPACE","Floating-point HDR images require Linear sampling");
            if(image->HDR)for(const auto& mip:image->Mips)for(size_t index=0;index<mip.Pixels.size();index+=sizeof(float)) {
                float value;std::memcpy(&value,mip.Pixels.data()+index,sizeof(value));
                if(!std::isfinite(value))Fail("MG_TEXTURE_VALUE","HDR image values must be finite");
            }
            std::string key="GraphImage:1";Field(key,std::to_string(FileSystem::GetSourceGeneration()));
            const auto sourcePath=Rooted(source);
            Field(key,PathToUtf8(Rooted(texture.Path)));Field(key,PathToUtf8(sourcePath));Field(key,texture.Path);Field(key,source);
            Field(key,std::to_string(authoredRevision));Field(key,std::to_string(revision));Field(key,ImageInterpretationKey(options));
            Field(key,texture.Fallback);Field(key,std::to_string(fallback));
            ImageSourceData data;data.SourceKey=source;data.SourcePath=sourcePath;data.Revision=revision;
            data.RequestKey=key;data.Image=std::move(image);
            candidate.Images.push_back({texture,std::move(data),key,fallback});
            Field(candidate.Key,key);Field(candidate.Key,texture.TextureSymbol);Field(candidate.Key,texture.Sampler);
        }
        out=std::move(candidate);return true;
    }catch(MaterialGraphDiagnostic diagnostic) {locate(diagnostic);if(error)*error=std::move(diagnostic);return false;}
    catch(const std::exception& failure) {MaterialGraphDiagnostic diagnostic{"MG_TEXTURE_RESOURCE",failure.what(),{},0,"texturePath"};locate(diagnostic);if(error)*error=std::move(diagnostic);return false;}
}
}
