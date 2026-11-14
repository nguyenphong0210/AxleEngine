#pragma once
#include <Axle/MaterialGraphRuntime.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <cstring>

namespace Axle::Tests {
using namespace RHI;
// Returned-bytecode fixture shared in shape with C12-03, now populating actual
// texture/sampler descriptors through the production resource writer.
inline std::vector<uint8_t> DrawMaterialGraph(std::shared_ptr<const MaterialGraphReadyGeneration> generation,
                          const MaterialGraphInstance& instance={},std::function<void()> beforeSubmit={}, const EngineMaterialValues* lighting=nullptr, bool minify=false) {
    auto& device=RHIDevice::Get();
    EngineMaterialValues engine;engine.CameraPosition={0,0,4,1};engine.SunDirection.w=0;engine.SunColor=glm::vec4(0);
    if(lighting)engine=*lighting;
    auto resolved=MaterialGraphRuntime::ResolveSurface(*generation,instance,engine);
    auto* surface=std::get_if<ResolvedShaderSurface>(&resolved);if(!surface)return {};
    const auto* buffers=&surface->Constants;
    constexpr uint32_t width=64;
    RHITexture color,depth;RHIBuffer readback;RHIBufferDesc read;read.Size=width*width*4;read.MemoryLocation=RHIMemoryLocation::HostReadback;
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
    std::vector<nri::DescriptorSet*> sets;
    for(size_t i=0;i<native.LayoutDescription.DescriptorSets.size();++i) {
        auto* set=pool.AllocateDescriptorSet(*native.PipelineLayout,static_cast<uint32_t>(i));if(!set)return {};sets.push_back(set);
        const auto& desc=native.LayoutDescription.DescriptorSets[i];
        for(size_t r=0;r<desc.Ranges.size();++r) {
            if(desc.Ranges[r].descriptorType!=nri::DescriptorType::CONSTANT_BUFFER)continue;
            nri::Descriptor* descriptor=nullptr;
            for(size_t b=0;b<constants.size();++b)if(buffers->Buffers[b].Address==ShaderAddress{desc.RegisterSpace,desc.Ranges[r].baseRegisterIndex})descriptor=constants[b].GetCBV();
            if(!descriptor||!pool.UpdateTexture(set,static_cast<uint32_t>(r),0,descriptor))return {};
        }
    }
    if(!MaterialGraphRuntime::WriteResourceDescriptors(*generation,pool,sets))return {};
    auto* pipeline=MaterialGraphRuntime::GetPipeline(*generation);if(!pipeline)return {};
    RHICommandBuffer commands;if(!commands.Init()||!commands.Begin(pool.GetNative()))return {};
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
