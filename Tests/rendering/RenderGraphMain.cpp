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
    for (int test=0; test<6; ++test) {
        RHICommandBuffer commands;
        RENDER_CHECK(commands.Init() && commands.Begin(), "negative recording begins");
        RenderGraph graph;
        graph.AddPass("consumer", {});
        if (test==1 || test==5) RENDER_CHECK(graph.DeclareRead("consumer", image.GetSRV()), "sampled declaration");
        if (test==4) RENDER_CHECK(graph.DeclareBuffer("consumer", vertices.AcquireLease(), RHIAccess::VertexBuffer), "read-only vertex declaration");
        RENDER_CHECK(graph.Compile(true) && graph.BeginPass("consumer", commands), "compile exact negative input set");
        if (test==0) device.GetResources().RecordNativeUse(commands.GetNative(), image.GetSRV());
        if (test==1) device.GetResources().RecordNativeUse(commands.GetNative(), image.GetUAV());
        if (test==2) commands.SetRootDescriptor(0, constants.GetCBV());
        if (test==3) commands.SetVertexBuffer(0, vertices.GetNative());
        if (test==4) commands.TransitionBuffer(vertices.GetNative(), RHIAccess::None, RHIAccess::CopyDest);
        if (test==5) {
            const auto prior=image.GetHandle();
            RENDER_CHECK(image.Init(desc) && image.GetHandle()!=prior, "replace a declared texture generation");
            device.GetResources().RecordNativeUse(commands.GetNative(),image.GetSRV());
        }
        RENDER_CHECK(!commands.IsRecording() && !graph.GetError().empty() && !commands.End() && !commands.Submit(),
            "undeclared texture/root/mesh or wrong storage/copy access cannot submit");
    }
    upload.texture=image.GetNative();
    RENDER_CHECK(device.UploadData(&upload,1,nullptr,0)==nri::Result::SUCCESS, "replacement source pixels");
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
