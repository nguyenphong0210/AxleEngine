#include <Axle/MaterialGraphRuntime.hpp>
#include <algorithm>

namespace Axle {
bool MaterialGraphRuntime::PrepareResources(const MaterialGraphDocument& document,MaterialGraphReadyGeneration& candidate) {
    std::vector<std::string> symbols;
    for(const auto& texture:candidate.Program.BindingPlan.Textures)symbols.push_back(texture.ResourceName);
    const std::span<const std::string> live(symbols);
    if(!MaterialGraphResources::Resolve(document,candidate.Program.Graph,candidate.Resources,&m_Diagnostic,&live))return false;
    auto& device=RHI::RHIDevice::Get();
    auto fail=[&](const char* code,const std::string& message,const MaterialHLSLTexture* binding=nullptr) {
        m_Diagnostic={code,message,document.Source,binding?binding->NodeID:0,"texturePath"};
        if(std::string_view(code)=="MG_SAMPLER_GPU")m_Diagnostic.Field="sampler";
        if(binding)for(const auto& node:document.Nodes)if(node.ID==binding->NodeID)m_Diagnostic.Source=node.Source;
        return false;
    };
    for(const auto& image:candidate.Resources.Images) {
        MaterialGraphNativeTexture entry;entry.Symbol=image.Binding.TextureSymbol;entry.Key=image.Key;
        const auto reflected=std::find_if(candidate.Program.BindingPlan.Textures.begin(),candidate.Program.BindingPlan.Textures.end(),[&](const auto& b){return b.ResourceName==entry.Symbol;});
        if(reflected==candidate.Program.BindingPlan.Textures.end())return fail("MG_TEXTURE_BINDING","Missing graph texture binding",&image.Binding);
        entry.Address=reflected->Address;
        if(m_Ready && m_Ready->DeviceEpoch==candidate.DeviceEpoch)
            for(const auto& previous:m_Ready->Textures)if(previous.Key==entry.Key && previous.Texture->IsValid()){entry.Texture=previous.Texture;break;}
        if(!entry.Texture)for(const auto& previous:candidate.Textures)if(previous.Key==entry.Key){entry.Texture=previous.Texture;break;}
        if(!entry.Texture) {
            const auto& decoded=*image.Source.Image;
            RHI::RHITextureDesc description;description.Width=decoded.Mips[0].Width;description.Height=decoded.Mips[0].Height;
            description.MipLevels=static_cast<uint32_t>(decoded.Mips.size());description.Usage=RHI::RHITextureUsage::ShaderResource;
            description.Format=decoded.HDR?RHI::RHITextureFormat::RGBA32_FLOAT:decoded.Options.SRGB?RHI::RHITextureFormat::RGBA8_SRGB:RHI::RHITextureFormat::RGBA8_UNORM;
            description.DebugName=image.Binding.Path;
            auto texture=std::make_shared<RHI::RHITexture>();
            if(!texture->Init(description))return fail("MG_TEXTURE_GPU","Texture creation failed: "+image.Binding.Path,&image.Binding);
            std::vector<nri::TextureSubresourceUploadDesc> levels;
            for(const auto& mip:decoded.Mips) {
                if(mip.Pixels.size()>UINT32_MAX || uint64_t(mip.Width)*decoded.BytesPerPixel()>UINT32_MAX)
                    return fail("MG_TEXTURE_GPU","Texture upload exceeds supported subresource sizes",&image.Binding);
                levels.push_back({mip.Pixels.data(),1,mip.Width*decoded.BytesPerPixel(),static_cast<uint32_t>(mip.Pixels.size())});
            }
            nri::TextureUploadDesc upload{};upload.texture=texture->GetNative();upload.subresources=levels.data();upload.planes=nri::PlaneBits::COLOR;
            upload.after={nri::AccessBits::SHADER_RESOURCE,nri::Layout::SHADER_RESOURCE,nri::StageBits::ALL};
            if(device.UploadData(&upload,1,nullptr,0)!=nri::Result::SUCCESS)
                return fail("MG_TEXTURE_GPU","Texture upload failed: "+image.Binding.Path,&image.Binding);
            entry.Texture=std::move(texture);
        }
        candidate.Textures.push_back(std::move(entry));
    }
    for(const auto& binding:candidate.Program.BindingPlan.Samplers) {
        const MaterialHLSLTexture* authored=nullptr;
        for(const auto& texture:candidate.Program.Graph.Textures)if(texture.SamplerSymbol==binding.ResourceName){authored=&texture;break;}
        const auto description=MaterialSamplerDescription(binding.Policy);
        if(!description)return fail("MG_SAMPLER_GPU","Graph sampler has no explicit policy: "+binding.ResourceName,authored);
        MaterialGraphNativeSampler entry{binding.ResourceName,binding.Address,binding.Policy,{}};
        if(m_Ready && m_Ready->DeviceEpoch==candidate.DeviceEpoch)
            for(const auto& previous:m_Ready->Samplers)if(previous.Policy==entry.Policy && previous.Sampler->IsValid()){entry.Sampler=previous.Sampler;break;}
        if(!entry.Sampler)for(const auto& previous:candidate.Samplers)if(previous.Policy==entry.Policy){entry.Sampler=previous.Sampler;break;}
        if(!entry.Sampler) {
            auto sampler=std::make_shared<RHI::RHISampler>();
            if(!sampler->Init(*description))return fail("MG_SAMPLER_GPU","Sampler creation failed: "+binding.ResourceName,authored);
            entry.Sampler=std::move(sampler);
        }
        candidate.Samplers.push_back(std::move(entry));
    }
    return true;
}

bool MaterialGraphRuntime::WriteResourceDescriptors(const MaterialGraphReadyGeneration& generation,RHI::RHIDescriptorPool& pool,
                                                    std::span<nri::DescriptorSet*> sets) {
    const auto& device=RHI::RHIDevice::Get();
    if(!device.CanCreateResources() || generation.DeviceEpoch!=device.GetEpoch() || !generation.Native ||
       !pool.IsValid() || sets.size()!=generation.Native->LayoutDescription.DescriptorSets.size())return false;
    for(size_t setIndex=0;setIndex<sets.size();++setIndex) {
        if(!sets[setIndex])return false;
        const auto& set=generation.Native->LayoutDescription.DescriptorSets[setIndex];
        for(size_t rangeIndex=0;rangeIndex<set.Ranges.size();++rangeIndex) {
            const auto& range=set.Ranges[rangeIndex];const ShaderAddress address{set.RegisterSpace,range.baseRegisterIndex};
            if(range.descriptorType==nri::DescriptorType::CONSTANT_BUFFER)continue;
            nri::Descriptor* descriptor=nullptr;
            if(range.descriptorType==nri::DescriptorType::TEXTURE)
                for(const auto& texture:generation.Textures)if(texture.Address==address && texture.Texture->IsValid())descriptor=texture.Texture->GetSRV();
            if(range.descriptorType==nri::DescriptorType::SAMPLER)
                for(const auto& sampler:generation.Samplers)if(sampler.Address==address && sampler.Sampler->IsValid())descriptor=sampler.Sampler->GetNative();
            if(!descriptor || range.descriptorNum!=1 || !pool.UpdateTexture(sets[setIndex],static_cast<uint32_t>(rangeIndex),0,descriptor))return false;
        }
    }
    return true;
}
}
