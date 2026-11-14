#include "RenderTestCommon.hpp"
#include <Axle/RenderGraph.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>

using namespace Axle;
using namespace Axle::RHI;

static bool Run() {
    auto& device = RHIDevice::Get();
    {
        RHICommandBuffer commands; RENDER_CHECK(commands.Init()&&commands.Begin(),"scoped recording begins");
        {
            RenderGraph graph; graph.AddPass("producer",{}); graph.AddPass("consumer",{});
            RENDER_CHECK(graph.Compile(true)&&graph.BeginPass("producer",commands),"producer owns recording scope");
            RENDER_CHECK(!commands.Reset()&&commands.IsRecording(),"active graph prevents discarding producer work via Reset");
            RENDER_CHECK(graph.BeginPass("consumer",commands)&&graph.IsComplete(),"refused reset preserves original recording");
        }
        RENDER_CHECK(commands.End()&&commands.Submit()&&device.WaitIdle()&&commands.Reset(),"normal reset succeeds after graph detaches");
    }
    for (bool otherOwned : {false, true}) {
        RHICommandBuffer a, b;
        RENDER_CHECK(a.Init() && b.Init() && a.Begin() && b.Begin(), "two independent command recordings");
        {
            RenderGraph other; other.AddPass("owned", {});
            if (otherOwned) RENDER_CHECK(other.Compile(true) && other.BeginPass("owned",b), "another graph owns B");
            {
                RenderGraph graph; graph.AddPass("first",{}); graph.AddPass("second",{});
                RENDER_CHECK(graph.Compile(true) && graph.BeginPass("first",a), "first exact command generation");
                RENDER_CHECK(!graph.BeginPass("second",b) && !a.IsRecording(), "switching command generations is rejected without detaching A");
            }
            RENDER_CHECK(!device.GetResources().HasAccessValidator(a.GetNative()) &&
                device.GetResources().HasAccessValidator(b.GetNative())==otherOwned,
                "failed switch detaches original scope and preserves another graph owner");
        }
        RENDER_CHECK(!device.GetResources().HasAccessValidator(b.GetNative()) && !a.End() && !a.Submit() && b.End(),
            "both scopes detach and failed original recording cannot submit");
    }
    RHITexture image;
    auto desc = RHITextureDesc::RenderTarget2D(1, 1);
    desc.Usage = desc.Usage | RHITextureUsage::Storage;
    RENDER_CHECK(image.Init(desc), "real sampled/storage texture");
    const uint8_t pixel[]{64, 128, 192, 255};
    nri::TextureSubresourceUploadDesc sub{pixel, 1, 4, 4};
    nri::TextureUploadDesc upload{}; upload.texture=image.GetNative(); upload.subresources=&sub;
    upload.after={nri::AccessBits::SHADER_RESOURCE,nri::Layout::SHADER_RESOURCE,nri::StageBits::ALL};
    RENDER_CHECK(device.UploadData(&upload,1,nullptr,0)==nri::Result::SUCCESS, "defined external input pixels");
    RHIBuffer constants, vertices;
    RENDER_CHECK(constants.Init(RHIBufferDesc::Constant(256, RHIMemoryLocation::HostUpload)) &&
        vertices.Init(RHIBufferDesc::Vertex(256, RHIMemoryLocation::HostUpload)), "real buffer inputs");
    {
        RHITexture attachmentOnly; auto attachmentDesc=desc; attachmentDesc.Usage=RHITextureUsage::ColorAttachment;
        RENDER_CHECK(attachmentOnly.Init(attachmentDesc), "attachment-only physical texture");
        RenderGraph graph; const auto texture=graph.Import("no SRV usage",attachmentOnly.GetNative(),true);
        graph.AddPass("unsupported",{{texture,RHIResourceLayout::ShaderResource}});
        RENDER_CHECK(!graph.Compile(true), "strict compile rejects incompatible physical texture usage");
        RenderGraph bufferGraph; bufferGraph.AddPass("unsupported",{});
        RENDER_CHECK(bufferGraph.DeclareBuffer("unsupported",constants.AcquireLease(),RHIAccess::VertexBuffer) &&
            !bufferGraph.Compile(true), "strict compile rejects incompatible physical buffer usage");
        RenderGraph undefined; undefined.AddPass("read",{});
        RENDER_CHECK(undefined.DeclareBuffer("read",vertices.AcquireLease(),RHIAccess::VertexBuffer,RenderGraph::Access::Read,false) &&
            !undefined.Compile(true), "undefined buffer contents cannot be read before a producer");
        RenderGraph produced; produced.AddPass("write",{}); produced.AddPass("read",{});
        RENDER_CHECK(produced.DeclareBuffer("write",vertices.AcquireLease(),RHIAccess::CopyDest,RenderGraph::Access::Write,false) &&
            produced.DeclareBuffer("read",vertices.AcquireLease(),RHIAccess::VertexBuffer,RenderGraph::Access::Read,false) &&
            produced.Compile(true) && produced.GetDependencies(1)==std::vector<uint32_t>{0}, "buffer producer establishes dataflow and contents");
    }
    for (int test=0; test<8; ++test) {
        RHICommandBuffer commands;
        RENDER_CHECK(commands.Init() && commands.Begin(), "negative recording begins");
        RenderGraph graph;
        graph.AddPass("consumer", {});
        if (test==1 || test>=5) RENDER_CHECK(graph.DeclareRead("consumer", image.GetSRV()), "sampled declaration");
        if (test==4) RENDER_CHECK(graph.DeclareBuffer("consumer", vertices.AcquireLease(), RHIAccess::VertexBuffer), "read-only vertex declaration");
        RENDER_CHECK(graph.Compile(true) && graph.BeginPass("consumer", commands), "compile exact negative input set");
        if (test==0) device.GetResources().RecordNativeUse(commands.GetNative(), image.GetSRV());
        if (test==1) {
            device.GetResources().DefineViewAccess(image.GetUAV(),nri::AccessBits::SHADER_RESOURCE,nri::Layout::SHADER_RESOURCE);
            device.GetResources().RecordNativeUse(commands.GetNative(), image.GetUAV());
        }
        if (test==2) commands.SetRootDescriptor(0, constants.GetCBV());
        if (test==3) commands.SetVertexBuffer(0, vertices.GetNative());
        if (test==4) commands.TransitionBuffer(vertices.GetNative(), RHIAccess::None, RHIAccess::CopyDest);
        if (test==5) {
            const auto prior=image.GetHandle();
            RENDER_CHECK(image.Init(desc) && image.GetHandle()!=prior, "replace a declared texture generation");
            device.GetResources().RecordNativeUse(commands.GetNative(),image.GetSRV());
        }
        if (test==6) {
            RHITextureBarrier barrier(image.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResource);
            barrier.AfterAccess=RHIAccess::CopyDest;
            RHIBarrierGroup group; group.AddTexture(barrier); commands.PipelineBarrier(group);
        }
        if (test==7) {
            nri::AttachmentDesc color{}; color.descriptor=image.GetRTV();
            color.loadOp=nri::LoadOp::CLEAR; color.storeOp=nri::StoreOp::STORE;
            nri::RenderingDesc rendering{}; rendering.colors=&color; rendering.colorNum=1;
            commands.BeginRendering(rendering);
        }
        RENDER_CHECK(!commands.IsRecording() && !graph.GetError().empty() && !commands.End() && !commands.Submit(),
            "undeclared texture/root/mesh or wrong storage/copy access cannot submit");
    }
    RHIPipelineLayout layout;
    nri::DescriptorRangeDesc range{}; range.descriptorType=nri::DescriptorType::TEXTURE;
    range.descriptorNum=1; range.shaderStages=nri::StageBits::FRAGMENT_SHADER;
    nri::DescriptorSetDesc setDesc{}; setDesc.ranges=&range; setDesc.rangeNum=1;
    nri::PipelineLayoutDesc layoutDesc{}; layoutDesc.descriptorSets=&setDesc; layoutDesc.descriptorSetNum=1;
    layoutDesc.shaderStages=nri::StageBits::FRAGMENT_SHADER;
    RHIDescriptorPool pool;
    RENDER_CHECK(layout.Init(layoutDesc) && pool.Init(RHIDescriptorPoolDesc::Tier2Bindless(4,4)), "real descriptor arena");
    auto* set=pool.AllocateDescriptorSet(layout,0);
    RENDER_CHECK(set && pool.UpdateTexture(set,0,0,image.GetSRV()), "real descriptor dependency");
    {
        RHICommandBuffer commands;
        RENDER_CHECK(commands.Init() && commands.Begin(), "descriptor negative begins");
        RenderGraph graph; graph.AddPass("missing",{});
        RENDER_CHECK(graph.Compile(true) && graph.BeginPass("missing",commands), "compile without transitive texture input");
        RHIDescriptorPool::BindDescriptorSet(commands.GetNative(),0,set);
        RENDER_CHECK(!commands.IsRecording() && !commands.End() && !commands.Submit(), "raw descriptor helper cannot bypass graph declaration");
    }
    {
        RHITexture replacement;
        RENDER_CHECK(replacement.Init(desc), "different descriptor dependency");
        RHICommandBuffer commands;
        RENDER_CHECK(commands.Init() && commands.Begin(), "descriptor replacement recording");
        RenderGraph graph; graph.AddPass("changed",{});
        RENDER_CHECK(graph.DeclareRead("changed",set) && graph.Compile(true), "freeze original descriptor inputs");
        RENDER_CHECK(pool.UpdateTexture(set,0,0,replacement.GetSRV()), "change a not-yet-recorded descriptor set");
        RENDER_CHECK(graph.BeginPass("changed",commands), "begin original compiled input set");
        RHIDescriptorPool::BindDescriptorSet(commands.GetNative(),0,set);
        RENDER_CHECK(!commands.IsRecording() && !commands.End() && !commands.Submit(), "descriptor changes cannot introduce an undeclared generation");
    }
    RENDER_CHECK(pool.UpdateTexture(set,0,0,image.GetSRV()), "restore declared descriptor dependency");
    {
        // Imported views must identify their access on the exact backing generation.
        RHITexture backing; RENDER_CHECK(backing.Init(desc), "import backing generation");
        auto data=upload; data.texture=backing.GetNative();
        RENDER_CHECK(device.UploadData(&data,1,nullptr,0)==nri::Result::SUCCESS,"import backing pixels");
        struct Views {
            nri::Descriptor* SRV=nullptr; nri::Descriptor* UAV=nullptr;
            ~Views() { const auto& core=RHIDevice::Get().GetNRICore(); if(SRV)core.DestroyDescriptor(SRV); if(UAV)core.DestroyDescriptor(UAV); }
        } views;
        const auto& core=device.GetNRICore();
        nri::TextureViewDesc view{}; view.texture=backing.GetNative(); view.format=nri::Format::RGBA8_UNORM;
        view.type=nri::TextureView::TEXTURE; view.mipNum=view.layerNum=1;
        RENDER_CHECK(core.CreateTextureView(view,views.SRV)==nri::Result::SUCCESS,"external SRV");
        view.type=nri::TextureView::STORAGE_TEXTURE;
        RENDER_CHECK(core.CreateTextureView(view,views.UAV)==nri::Result::SUCCESS,"external UAV");
        auto& resources=device.GetResources();
        resources.AddAlias(backing.AcquireLease(),views.SRV); resources.AddAlias(backing.AcquireLease(),views.UAV);
        for(auto* alias : {views.SRV,views.UAV}) {
            RENDER_CHECK(!pool.UpdateTexture(set,0,0,alias),"untyped external view cannot enter a descriptor range");
            RHICommandBuffer c; RENDER_CHECK(c.Init()&&c.Begin(),"untyped view recording");
            RenderGraph g; g.AddPass("untyped",{});
            RENDER_CHECK(g.DeclareRead("untyped",backing.GetSRV())&&g.Compile(true)&&g.BeginPass("untyped",c),"declare backing without alias role");
            RENDER_CHECK(!resources.RecordNativeUse(c.GetNative(),alias)&&!c.End()&&!c.Submit(),"direct untyped view binding cannot bypass metadata");
        }
        for(auto kind : {RHIResourceKind::Surface,RHIResourceKind::DescriptorSet}) {
            nri::Descriptor* standalone=nullptr; view.type=nri::TextureView::TEXTURE;
            RENDER_CHECK(core.CreateTextureView(view,standalone)==nri::Result::SUCCESS,"separate imported view");
            auto owner=resources.Publish(kind,"standalone view",{standalone},[core,standalone]{core.DestroyDescriptor(standalone);},{backing.AcquireLease()});
            RENDER_CHECK(owner.IsLive(),"standalone view publication");
            resources.DefineViewAccess(standalone,nri::AccessBits::SHADER_RESOURCE,nri::Layout::SHADER_RESOURCE);
            RENDER_CHECK(!pool.UpdateTexture(set,0,0,standalone),"standalone view without exact backing association is rejected");
            RHICommandBuffer c; RENDER_CHECK(c.Init()&&c.Begin(),"standalone view recording");
            RenderGraph g; g.AddPass("standalone",{});
            RENDER_CHECK(g.DeclareRead("standalone",backing.GetSRV())&&g.Compile(true)&&g.BeginPass("standalone",c),"declared physical backing");
            RENDER_CHECK(!resources.RecordNativeUse(c.GetNative(),standalone)&&!c.End()&&!c.Submit(),"standalone metadata cannot hide view access");
        }
        resources.DefineViewAccess(views.SRV,nri::AccessBits::SHADER_RESOURCE,nri::Layout::SHADER_RESOURCE);
        resources.DefineViewAccess(views.UAV,nri::AccessBits::SHADER_RESOURCE_STORAGE,nri::Layout::SHADER_RESOURCE_STORAGE);
        RENDER_CHECK(pool.UpdateTexture(set,0,0,views.SRV),"annotated view publishes exact access metadata");
        {
            RHICommandBuffer c; RENDER_CHECK(c.Init()&&c.Begin(),"annotated import recording");
            RenderGraph g; g.AddPass("import",{});
            RENDER_CHECK(g.DeclareRead("import",set)&&g.Compile(true)&&g.BeginPass("import",c),"compile annotated import");
            c.SetPipelineLayout(nri::BindPoint::GRAPHICS,layout); pool.Bind(c.GetNative());
            RHIDescriptorPool::BindDescriptorSet(c.GetNative(),0,set);
            g.EndRecording();
            RENDER_CHECK(c.End()&&c.Submit()&&device.WaitIdle(),"annotated imported SRV records and submits");
        }
        RENDER_CHECK(pool.UpdateTexture(set,0,0,image.GetSRV()),"release imported descriptor dependency before owner cleanup");
        RENDER_CHECK(device.WaitIdle(),"imported view cleanup drain");
    }
    RHICommandBuffer commands;
    RENDER_CHECK(commands.Init() && commands.Begin(), "positive recording begins");
    const auto handle=image.GetHandle();
    {
        RenderGraph graph; graph.AddPass("declared",{});
        RENDER_CHECK(graph.DeclareRead("declared",set) && graph.DeclareRead("declared",constants.GetCBV()) &&
            graph.DeclareBuffer("declared",vertices.AcquireLease(),RHIAccess::VertexBuffer), "exact pass input declarations");
        RENDER_CHECK(graph.Compile(true) && graph.BeginPass("declared",commands), "declared input transitions before recording");
        commands.SetPipelineLayout(nri::BindPoint::GRAPHICS,layout);
        pool.Bind(commands.GetNative()); RHIDescriptorPool::BindDescriptorSet(commands.GetNative(),0,set);
        commands.SetVertexBuffer(0,vertices.GetNative());
        RENDER_CHECK(graph.IsComplete() && commands.IsRecording(), "declared resources record successfully");
    }
    RENDER_CHECK(!device.GetResources().HasAccessValidator(commands.GetNative()) && commands.End(), "scope detaches before graph destruction");
    image.Shutdown(); pool.Shutdown();
    RENDER_CHECK(device.GetResources().Acquire(handle).IsLive() && commands.Submit() && device.WaitIdle(), "recorded exact generations survive owner/graph destruction");
    RENDER_CHECK(!device.GetResources().Acquire(handle).IsLive(), "completion retires descriptor transitive inputs");
    return true;
}

int main(int argc,char** argv) {
    RenderTests::ConfigureFailureReporting(); Log::Init();
    RHIDeviceDesc desc; desc.PreferredBackend=argc>1 && std::string(argv[1])=="vulkan" ? RHIType::Vulkan : RHIType::Direct3D12;
    desc.AllowFallback=false; desc.EnableNRIValidation=true;
    desc.EnableValidation=RHIDevice::IsGraphicsAPIValidationAvailable(desc.PreferredBackend);
    bool ok=RHIDevice::Get().Init(desc) && Run();
    RHIDevice::Get().Shutdown();
    ok=RenderTests::Check(GetRHIValidationErrorCount()==0,"native graph validation including teardown",__FILE__,__LINE__) && ok;
    std::cout << "RenderGraph authority " << (ok?"PASS":"FAIL") << '\n'; return ok?0:1;
}
