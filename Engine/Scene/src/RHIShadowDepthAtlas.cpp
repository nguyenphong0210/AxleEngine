#include <Axle/RHIShadowDepthAtlas.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/Log.hpp>
#include "CascadeDepthSource.hpp"
#include <algorithm>
#include <cmath>

namespace Axle {
using namespace RHI;
    bool RHIShadowDepthAtlas::Init() {
        auto& device=RHIDevice::Get();
        const auto support=device.GetTextureFormatSupport(nri::Format::D32_SFLOAT);
        const auto required=nri::FormatSupportBits::TEXTURE|nri::FormatSupportBits::DEPTH_STENCIL_ATTACHMENT;
        m_Supported=(static_cast<uint32_t>(support)&static_cast<uint32_t>(required))==static_cast<uint32_t>(required);
        if (!m_Supported) return true;
        auto atlas=std::make_unique<RHITexture>();
        if (!atlas->Init(RHITextureDesc::DepthStencil2D(2,2,RHITextureFormat::D32_FLOAT,"CascadeAtlasDisabled"))) return false;
        // Initialize a tiny defined fallback once. Disabled views never clear
        // a retained full-resolution atlas, including after an Ultra view.
        RHICommandBuffer initialize;
        if (!initialize.Init() || !initialize.Begin()) return false;
        nri::RenderingDesc clear{};clear.depth.descriptor=atlas->GetDSV();
        clear.depth.loadOp=nri::LoadOp::CLEAR;clear.depth.storeOp=nri::StoreOp::STORE;
        clear.depth.clearValue.depthStencil.depth=1;
        initialize.BeginRendering(clear);initialize.EndRendering();
        initialize.TransitionTexture(atlas->GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ShaderResource);
        if (!initialize.End() || !initialize.Submit() || !device.WaitIdle()) return false;
        m_DisabledAtlas=std::move(atlas);
        auto& compiler=RHIShaderCompiler::Get();
        if (!compiler.IsInitialized() && !compiler.Init()) return false;
        RHIShaderCompileDesc desc; desc.Source=kCascadeDepthSource; desc.SourcePath="CascadedShadowDepth.hlsl";
        desc.Format=device.GetBackend()==RHIType::Vulkan?RHIShaderFormat::SPIRV:RHIShaderFormat::DXIL;
        desc.EntryPoint="VSMain"; desc.Stage=RHIShaderStage::Vertex;
        const auto vs=compiler.Compile(desc);
        desc.EntryPoint="PSMain"; desc.Stage=RHIShaderStage::Fragment;
        const auto ps=compiler.Compile(desc);
        if (!vs.Success || !ps.Success) { AXLE_CORE_ERROR("Cascade depth shader: {0} {1}",vs.ErrorMessage,ps.ErrorMessage); return false; }
        nri::RootConstantDesc object{}; object.registerIndex=0; object.size=sizeof(RHIObjectPushConstants); object.shaderStages=nri::StageBits::ALL;
        nri::RootDescriptorDesc roots[2]{};
        for (auto& root:roots) {root.descriptorType=nri::DescriptorType::CONSTANT_BUFFER;root.shaderStages=nri::StageBits::ALL;}
        roots[0].registerIndex=1; roots[1].registerIndex=3;
        nri::DescriptorRangeDesc range{};range.baseRegisterIndex=0;range.descriptorNum=1;
        range.descriptorType=nri::DescriptorType::TEXTURE;range.shaderStages=nri::StageBits::FRAGMENT_SHADER;
        nri::DescriptorSetDesc set{};set.registerSpace=1;set.ranges=&range;set.rangeNum=1;
        nri::RootSamplerDesc sampler{};sampler.registerIndex=0;sampler.shaderStages=nri::StageBits::FRAGMENT_SHADER;
        sampler.desc=RHISamplerDesc::Anisotropic16x().ToNRIDesc();
        nri::PipelineLayoutDesc layout{};layout.rootRegisterSpace=0;layout.rootConstants=&object;layout.rootConstantNum=1;
        layout.rootDescriptors=roots;layout.rootDescriptorNum=2;layout.descriptorSets=&set;layout.descriptorSetNum=1;
        layout.rootSamplers=&sampler;layout.rootSamplerNum=1;layout.shaderStages=nri::StageBits::VERTEX_SHADER|nri::StageBits::FRAGMENT_SHADER;
        if (!m_Layout.Init(layout)) return false;
        RHIDescriptorPoolDesc pool;pool.MaxDescriptorSets=4096;pool.MaxTextures=4096;pool.MaxSamplers=0;
        if (!m_Pool.Init(pool)) return false;
        for (int c=0;c<3;++c) {
            RHIGraphicsPipelineDesc pipeline;pipeline.Layout=&m_Layout;pipeline.Shaders={vs.Bytecode,ps.Bytecode};
            pipeline.VertexInput=VertexLayoutContract::GetCanonicalVertexInputDesc();
            pipeline.Rasterizer.FrontCounterClockwise=true;
            pipeline.Rasterizer.CullMode=c==0?nri::CullMode::NONE:c==1?nri::CullMode::BACK:nri::CullMode::FRONT;
            pipeline.DepthStencil.DepthStencilFormat=nri::Format::D32_SFLOAT;
            m_Pipelines[c]=RHIPipelineCache::Get().GetOrCreateGraphicsPipeline(pipeline);
            if (!m_Pipelines[c]) return false;
        }
        for (auto& camera:m_Cameras)
            if (!camera.Init(RHIBufferDesc::Constant(256,RHIMemoryLocation::HostUpload,"CascadeCamera"))) return false;
        return true;
    }
    void RHIShadowDepthAtlas::Shutdown() {
        m_Draws.clear();m_Atlas.reset();m_DisabledAtlas.reset();m_Pool.Shutdown();m_Layout.Shutdown();
        for (auto& camera:m_Cameras) camera.Shutdown();
        for (auto& pipeline:m_Pipelines) pipeline=nullptr;
        m_Views.clear();m_LastKey.clear();m_PendingKey.clear();m_Supported=false;m_Diagnostic.clear();m_DrawCount=0;m_CasterCount=0;m_NeedsRender=false;
    }
    nri::Descriptor* RHIShadowDepthAtlas::GetSRV() const {
        const auto* atlas=IsActive()?m_Atlas.get():m_DisabledAtlas.get();
        if (atlas) return atlas->GetSRV();
        const auto fallback=RHIMaterial::GetDefaultWhiteTexture();
        return fallback?fallback->GetSRV():nullptr;
    }
    bool RHIShadowDepthAtlas::ReleaseUnusedOwners() {
        for (size_t i=m_CasterCount;i<m_Draws.size();++i) {
            auto& draw=m_Draws[i];draw.Mesh.reset();
            if (draw.Albedo) {
                auto white=RHIMaterial::GetDefaultWhiteTexture();
                if (draw.Set && (!white || !m_Pool.UpdateTexture(draw.Set,0,0,white->GetSRV()))) return false;
                draw.Albedo.reset();
            }
        }
        return true;
    }
    bool RHIShadowDepthAtlas::Prepare(const RenderWorld& world,MaterialTextureCache& textures,
        const std::vector<ShadowDepthView>& views,uint32_t size,bool cullCasters,bool cache) {
        m_DrawCount=0;m_CasterCount=0;m_Diagnostic.clear();m_Views=views;m_NeedsRender=false;
        for(auto& visible:m_Visible)visible.clear();
        if(views.size()>16||size>8192){m_Diagnostic="SHADOW_VIEWS: invalid atlas/view budget";return false;}
        if(!IsActive()||!m_Supported){m_Views.clear();return ReleaseUnusedOwners();}
        if(!m_Atlas||!m_Atlas->IsValid()||m_Atlas->GetWidth()!=size) {
            auto candidate=std::make_unique<RHITexture>();
            if(!candidate->Init(RHITextureDesc::DepthStencil2D(size,size,RHITextureFormat::D32_FLOAT,"ShadowDepthAtlas"))){m_Diagnostic="SHADOW_ALLOCATION: atlas replacement failed; old generation retained";return false;}
            m_Atlas=std::move(candidate);m_LastKey.clear();
        }
        m_PendingKey.clear();bool cacheable=cache;
        auto append=[&](const auto& value){m_PendingKey.append(reinterpret_cast<const char*>(&value),sizeof(value));};
        append(size);append(views.size());
        for(size_t v=0;v<views.size();++v) {
            const auto& view=views[v];
            if(!view.Size||uint64_t(view.X)+view.Size>size||uint64_t(view.Y)+view.Size>size)return false;
            append(view.Matrix);append(view.X);append(view.Y);append(view.Size);
            if(!m_Cameras[v].UpdateData(&view.Matrix,sizeof(glm::mat4)))return false;
        }
        for (const auto& instance:world.Meshes) {
            if (!instance.NativeView || !instance.NativeView->IsValid()) continue;
            if (instance.Material.UseCustomShader) {
                m_Diagnostic="SHADOW_CUSTOM_SHADER: arbitrary custom HLSL has no depth-caster interface; built-in PBR shadows remain active.";continue;
            }
            if (!instance.CastsShadows) continue;
            const auto& material=instance.Material.RHIMaterialAsset;
            if (material && (material->GetSurfaceType()==SurfaceType::Transparent || material->GetSurfaceType()==SurfaceType::Additive)) continue;
            std::array<bool,16> visible{};bool any=false;
            for(size_t v=0;v<m_Views.size();++v) {
                visible[v]=!cullCasters||ShadowIntersects(m_Views[v].Matrix,instance.WorldTransform,instance.NativeView->GetBoundingMin(),instance.NativeView->GetBoundingMax());
                any|=visible[v];
            }
            if(!any)continue;
            if(m_CasterCount>=4096){m_Diagnostic="SHADOW_CASTERS: descriptor budget exhausted";return false;}
            if (m_CasterCount==m_Draws.size()) m_Draws.emplace_back();
            auto& draw=m_Draws[m_CasterCount++];
            draw.Albedo.reset();draw.Object=RHIObjectPushConstants{};
            draw.Mesh=instance.NativeView;draw.Object.Model=instance.WorldTransform;
            draw.Object.ColorPrimary=instance.Material.AlbedoColor;draw.Object.ColorSecondary=instance.Material.SecondaryColor;
            draw.Object.Params=glm::vec4(instance.Material.TileScale.x,instance.Material.PatternMode,.5f,0);
            // The push-constant union defaults to a legacy sun vector. With no
            // native material this is still an opaque caster, not surface 1.
            draw.Object.MaterialExtra=glm::vec4(0,0,.5f,float(SurfaceType::Opaque));
            RHIMaterialConstants constants;
            auto cull=CullMode::None;
            if (material) {material->ApplyToPushConstants(draw.Object);constants=material->BuildConstants();cull=material->GetCullMode();}
            // ForwardPBR samples albedo only for its textured shading modes.
            const bool textured=(int(draw.Object.Params.y)==5 || int(draw.Object.Params.y)==6) && (constants.Flags.x&1u);
            if (textured) {
                draw.Albedo=material->GetTexture(RHITextureSlot::Albedo);
                if (!draw.Albedo) draw.Albedo=textures.Get(material->GetTexturePath(RHITextureSlot::Albedo),true);
            }
            if (!draw.Albedo) {constants.Flags.x&=~1u;draw.Albedo=RHIMaterial::GetDefaultWhiteTexture();}
            if (!draw.Albedo || !draw.Albedo->GetSRV() ||
                (!draw.Material.IsValid() && !draw.Material.Init(RHIBufferDesc::Constant(sizeof(constants),RHIMemoryLocation::HostUpload,"CascadeMaterial")))) return false;
            if (!draw.Material.UpdateData(&constants,sizeof(constants))) return false;
            if (!draw.Set) draw.Set=m_Pool.AllocateDescriptorSet(m_Layout,0);
            if (!draw.Set || !m_Pool.UpdateTexture(draw.Set,0,0,draw.Albedo->GetSRV())) return false;
            draw.Pipeline=m_Pipelines[cull==CullMode::Back?1:cull==CullMode::Front?2:0];
            const size_t drawIndex=m_CasterCount-1;
            for(size_t v=0;v<m_Views.size();++v)if(visible[v])m_Visible[v].push_back(drawIndex);
            append(draw.Object);append(constants);append(cull);append(draw.Mesh->GetVertexHandle());append(draw.Mesh->GetIndexHandle());
            // Cutout textures may be written in place without a generation change.
            if(textured&&int(draw.Object.MaterialExtra.w)==1)cacheable=false;

        }
        append(m_CasterCount);
        if(!ReleaseUnusedOwners())return false;
        m_NeedsRender=!cacheable||m_PendingKey!=m_LastKey;return true;
    }
    bool RHIShadowDepthAtlas::Record(RHICommandBuffer& commands) {
        if(!NeedsRender())return true;
        nri::RenderingDesc rendering{};rendering.depth.descriptor=m_Atlas->GetDSV();
        rendering.depth.loadOp=nri::LoadOp::CLEAR;rendering.depth.storeOp=nri::StoreOp::STORE;
        rendering.depth.clearValue.depthStencil.depth=1;
        for(size_t i=0;i<m_CasterCount;++i)commands.TransitionTexture(m_Draws[i].Albedo->GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ShaderResource);
        commands.BeginRendering(rendering);
        commands.SetPipelineLayout(nri::BindPoint::GRAPHICS,m_Layout);m_Pool.Bind(commands.GetNative());
        for(size_t v=0;v<m_Views.size();++v) {
            const auto& view=m_Views[v];
            commands.SetViewport({float(view.X),float(view.Y),float(view.Size),float(view.Size),0,1});
            commands.SetScissor({static_cast<int16_t>(view.X),static_cast<int16_t>(view.Y),static_cast<nri::Dim_t>(view.Size),static_cast<nri::Dim_t>(view.Size)});
            commands.SetRootDescriptor(0,m_Cameras[v].GetCBV());
            for(auto i:m_Visible[v]) {
                const auto& draw=m_Draws[i];commands.SetPipeline(draw.Pipeline);commands.SetRootDescriptor(1,draw.Material.GetCBV());
                RHIDescriptorPool::BindDescriptorSet(commands.GetNative(),0,draw.Set);
                commands.SetRootConstants(0,&draw.Object,sizeof(draw.Object));draw.Mesh->Bind(commands);draw.Mesh->Draw(commands);
                m_DrawCount+=static_cast<uint32_t>(draw.Mesh->GetSubmeshCount());
            }
        }
        commands.EndRendering();return commands.IsRecording();
    }
}
