#pragma once
#include <Axle/MaterialGraphRuntime.hpp>
#include <Axle/MaterialSurfaceShader.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <algorithm>
#include <cstring>

namespace Axle::Tests {
using namespace RHI;
// Returned-bytecode fixture shared across MaterialGraph GPU tests, populating
// authored descriptors via MaterialGraphRuntime and binding neutral engine inputs.
inline std::vector<uint8_t> DrawMaterialGraph(std::shared_ptr<const MaterialGraphReadyGeneration> generation,
                          const MaterialGraphInstance& instance={},std::function<void()> beforeSubmit={}, const EngineMaterialValues* lighting=nullptr, bool minify=false) {
    auto& device=RHIDevice::Get();
    EngineMaterialValues engine;engine.CameraPosition={0,0,4,1};engine.SunDirection.w=0;engine.SunColor=glm::vec4(0);
    if(lighting)engine=*lighting;
    auto resolved=MaterialGraphRuntime::ResolveSurface(*generation,instance,engine);
    auto* surface=std::get_if<ResolvedShaderSurface>(&resolved);if(!surface)return {};
    auto packed=surface->Constants;
    // Standalone bytecode draws provide neutral renderer-owned light, shadow,
    // probe, occlusion and LTC bindings when referenced by MaterialSurface.
    for(const auto& reflected:generation->Program.Reflection.ConstantBuffers) {
        if(!IsMaterialSurfaceEngineBuffer(reflected))continue;
        const bool alreadyPacked=std::any_of(packed.Buffers.begin(),packed.Buffers.end(),[&](const auto& entry) {
            return entry.Address==ShaderAddress{reflected.Space,reflected.BindPoint};
        });
        if(alreadyPacked)continue;
        packed.Buffers.push_back({reflected.Name,{reflected.Space,reflected.BindPoint},
                                  std::vector<uint8_t>((reflected.Size+255u)&~255u,0)});
    }
    const auto* buffers=&packed;
    constexpr uint32_t width=64;
    RHITexture color,depth,shadowDepth,volumeFallback;RHIBuffer readback;RHIBufferDesc read;read.Size=width*width*4;read.MemoryLocation=RHIMemoryLocation::HostReadback;
    if(!color.Init(RHITextureDesc::RenderTarget2D(width,width))||!depth.Init(RHITextureDesc::DepthStencil2D(width,width))||!readback.Init(read))return {};
    std::vector<RHIVertex3D> vertices(3);
    vertices[0].Position={-1,-1,0.5f};vertices[1].Position={0,1,0.5f};vertices[2].Position={1,-1,0.5f};
    for(auto& vertex:vertices){vertex.Normal={0,0,1};vertex.Tangent={1,0,0,1};}
    if(minify){vertices[0].TexCoord={0,0};vertices[1].TexCoord={0,128};vertices[2].TexCoord={128,0};}
    RHIMesh mesh;if(!mesh.Init(vertices,{0,1,2}))return {};
    RHIDescriptorPool pool;if(!pool.Init())return {};
    std::vector<RHIBuffer> constants(buffers->Buffers.size());
    for(size_t i=0;i<constants.size();++i)if(!constants[i].Init(RHIBufferDesc::Constant(buffers->Buffers[i].Bytes.size()))||
        !constants[i].UpdateData(buffers->Buffers[i].Bytes.data(),buffers->Buffers[i].Bytes.size()))return {};
    const auto& native=*generation->Native;
    std::vector<nri::DescriptorSet*> sets(native.LayoutDescription.DescriptorSets.size(),nullptr);
    for(size_t i=0;i<sets.size();++i) {
        sets[i]=pool.AllocateDescriptorSet(*native.PipelineLayout,static_cast<uint32_t>(i));
        if(!sets[i])return {};
    }
    if(!MaterialGraphRuntime::WriteResourceDescriptors(*generation,pool,sets))return {};
    RHISampler linear,shadow;
    const auto& engineResources=generation->Program.Reflection.Resources;
    const bool needLinearSampler=std::any_of(engineResources.begin(),engineResources.end(),[](const auto& value) {
        return IsMaterialSurfaceEngineResource(value)&&value.Type==RHIResourceType::Sampler&&value.Name!="u_ShadowSampler";
    });
    const bool needShadowSampler=std::any_of(engineResources.begin(),engineResources.end(),[](const auto& value) {
        return IsMaterialSurfaceEngineResource(value)&&value.Type==RHIResourceType::Sampler&&value.Name=="u_ShadowSampler";
    });
    const bool needShadowDepth=std::any_of(engineResources.begin(),engineResources.end(),[](const auto& value) {
        return IsMaterialSurfaceEngineResource(value)&&value.Type==RHIResourceType::Texture&&
            (value.Name=="u_CascadeDepth"||value.Name=="u_LocalShadowDepth");
    });
    const bool needVolumeFallback=std::any_of(engineResources.begin(),engineResources.end(),[](const auto& value) {
        return IsMaterialSurfaceEngineResource(value)&&value.Type==RHIResourceType::Texture&&
            (value.Name=="u_FroxelIntegrated"||value.Name=="u_FroxelMedia");
    });
    if((needLinearSampler&&!linear.Init(RHISamplerDesc::Trilinear(RHIAddressMode::ClampToEdge)))||
       (needShadowSampler&&!shadow.Init(RHISamplerDesc::ShadowPCF()))||
       (needShadowDepth&&!shadowDepth.Init(RHITextureDesc::DepthStencil2D(2,2,RHITextureFormat::D32_FLOAT,"MaterialGraphTestShadowDepth")))||
       (needVolumeFallback&&!volumeFallback.Init(RHITextureDesc::StorageTexture3D(1,1,1,RHITextureFormat::RGBA16_FLOAT,"MaterialGraphTestVolume3D"))))return {};
    for(size_t i=0;i<native.LayoutDescription.DescriptorSets.size();++i) {
        const auto& desc=native.LayoutDescription.DescriptorSets[i];
        for(size_t r=0;r<desc.Ranges.size();++r) {
            const auto& range=desc.Ranges[r];
            nri::Descriptor* descriptor=nullptr;
            if(range.descriptorType==nri::DescriptorType::CONSTANT_BUFFER) {
                for(size_t b=0;b<constants.size();++b)if(buffers->Buffers[b].Address==ShaderAddress{desc.RegisterSpace,range.baseRegisterIndex})descriptor=constants[b].GetCBV();
            } else {
                const auto resource=std::find_if(engineResources.begin(),engineResources.end(),[&](const auto& value) {
                    return value.Space==desc.RegisterSpace&&value.BindPoint==range.baseRegisterIndex&&
                        ((range.descriptorType==nri::DescriptorType::TEXTURE&&value.Type==RHIResourceType::Texture)||
                         (range.descriptorType==nri::DescriptorType::SAMPLER&&value.Type==RHIResourceType::Sampler));
                });
                if(resource==engineResources.end())return {};
                if(!IsMaterialSurfaceEngineResource(*resource))continue;
                if(range.descriptorType==nri::DescriptorType::SAMPLER)
                    descriptor=resource->Name=="u_ShadowSampler"?shadow.GetNative():linear.GetNative();
                else if(resource->Name=="u_CascadeDepth"||resource->Name=="u_LocalShadowDepth")
                    descriptor=shadowDepth.GetSRV();
                else if(resource->Name=="u_FroxelIntegrated"||resource->Name=="u_FroxelMedia")
                    descriptor=volumeFallback.GetSRV();
                else {
                    const bool cube=resource->Name=="u_IrradianceMap"||resource->Name=="u_PrefilterMap"||
                                    resource->Name.starts_with("u_ReflectionProbeCube");
                    auto texture=cube?RHIMaterial::GetDefaultPrefilterCubemap():RHIMaterial::GetDefaultWhiteTexture();
                    if(!texture)return {};
                    descriptor=texture->GetSRV();
                }
            }
            if(!descriptor||!pool.UpdateTexture(sets[i],static_cast<uint32_t>(r),0,descriptor))return {};
        }
    }
    auto* pipeline=MaterialGraphRuntime::GetPipeline(*generation);if(!pipeline)return {};
    RHICommandBuffer commands;if(!commands.Init()||!commands.Begin(pool.GetNative()))return {};
    if(shadowDepth.IsValid()) {
        commands.TransitionTexture(shadowDepth.GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::DepthStencilWrite);
        nri::RenderingDesc shadowClear{};shadowClear.depth.descriptor=shadowDepth.GetDSV();
        shadowClear.depth.loadOp=nri::LoadOp::CLEAR;shadowClear.depth.storeOp=nri::StoreOp::STORE;
        shadowClear.depth.clearValue.depthStencil.depth=1.0f;
        commands.BeginRendering(shadowClear);commands.EndRendering();
        commands.TransitionTexture(shadowDepth.GetNative(),RHIResourceLayout::DepthStencilWrite,RHIResourceLayout::ShaderResource);
    }
    if(volumeFallback.IsValid()) {
        commands.TransitionTexture(volumeFallback.GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ShaderResource);
    }
    commands.TransitionTexture(color.GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ColorAttachment);
    commands.TransitionTexture(depth.GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::DepthStencilWrite);
    nri::AttachmentDesc attachment{};attachment.descriptor=color.GetRTV();attachment.loadOp=nri::LoadOp::CLEAR;attachment.storeOp=nri::StoreOp::STORE;
    nri::AttachmentDesc depthAttachment{};depthAttachment.descriptor=depth.GetDSV();depthAttachment.loadOp=nri::LoadOp::CLEAR;depthAttachment.storeOp=nri::StoreOp::STORE;depthAttachment.clearValue.depthStencil.depth=1;
    nri::RenderingDesc rendering{};rendering.colors=&attachment;rendering.colorNum=1;rendering.depth=depthAttachment;
    commands.BeginRendering(rendering);commands.SetViewport({0,0,float(width),float(width),0,1});commands.SetScissor({0,0,width,width});
    commands.SetPipelineLayout(nri::BindPoint::GRAPHICS,*native.PipelineLayout);
    for(size_t i=0;i<sets.size();++i)commands.SetDescriptorSet(nri::BindPoint::GRAPHICS,static_cast<uint32_t>(i),sets[i]);
    commands.SetPipeline(pipeline);mesh.Bind(commands);mesh.Draw(commands);commands.EndRendering();
    commands.TransitionTexture(color.GetNative(),RHIResourceLayout::ColorAttachment,RHIResourceLayout::CopySource);
    nri::TextureDataLayoutDesc layout{};layout.rowPitch=width*4;layout.slicePitch=width*width*4;
    nri::TextureRegionDesc region{};region.width=region.height=width;region.depth=1;
    commands.ReadbackTextureToBuffer(*readback.GetNative(),layout,*color.GetNative(),region);
    if(!commands.End())return {};
    resolved=MaterialBindingError{}; // Drop wrapper owners; recorded command leases must be sufficient.
    generation.reset();if(beforeSubmit)beforeSubmit();
    if(!commands.Submit()||!device.WaitIdle())return {};
    const auto* memory=readback.Map();if(!memory)return {};
    std::vector<uint8_t> pixels(read.Size);std::memcpy(pixels.data(),memory,pixels.size());readback.Unmap();return pixels;
}
}
