#include <Axle/RHI/RHIPresentationContext.hpp>
#include <Axle/RHI/RHIFrameContext.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHISampler.hpp>

namespace Axle::RHI {
    struct RHIPresentationContext::Storage {
        RHISurface Surface;
        RHISwapChain SwapChain;
        RHISwapChainDesc Desc;
        std::vector<RHIFrameContext> Frames;
        std::vector<nri::Descriptor*> Views;
        std::vector<RHIResourceLease> ViewLeases;
        RHIPipelineLayout ImageLayout;
        RHIDescriptorPool ImagePool;
        RHIShaderBytecode ImageVS, ImagePS;
        nri::Pipeline* ImagePipeline = nullptr;
        RHIResourceLease ImagePipelineOwner;
        nri::Format ImageFormat = nri::Format::UNKNOWN;
        bool PrepareImage(nri::Texture* source, nri::Format format, RHICommandBuffer& commands) {
            auto& device = RHIDevice::Get();
            auto owner = device.GetResources().AcquireNative(source);
            if (!owner.IsLive()) return false;
            if (!ImageLayout.IsValid()) {
                nri::DescriptorRangeDesc range{}; range.descriptorNum = 1; range.descriptorType=nri::DescriptorType::TEXTURE;
                range.shaderStages=nri::StageBits::FRAGMENT_SHADER;
                nri::DescriptorSetDesc set{}; set.registerSpace=1; set.ranges=&range; set.rangeNum=1;
                nri::RootSamplerDesc sampler{}; sampler.shaderStages=nri::StageBits::FRAGMENT_SHADER;
                sampler.desc=RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge).ToNRIDesc();
                nri::PipelineLayoutDesc desc{}; desc.descriptorSets=&set; desc.descriptorSetNum=1;
                desc.rootSamplers=&sampler; desc.rootSamplerNum=1; desc.shaderStages=nri::StageBits::VERTEX_SHADER|nri::StageBits::FRAGMENT_SHADER;
                if (!ImageLayout.Init(desc)) return false;
                constexpr const char* shader=R"(
                    Texture2D image:register(t0,space1); SamplerState linearSampler:register(s0,space0);
                    struct Output { float4 position:SV_Position; float2 uv:TEXCOORD0; };
                    Output VSMain(uint id:SV_VertexID){Output o; o.uv=float2((id<<1)&2,id&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
                    float4 PSMain(Output i):SV_Target0{return image.Sample(linearSampler,i.uv);}
                )";
                RHIShaderCompileDesc compile; compile.Source=shader;
                compile.Format=device.GetBackend()==RHIType::Vulkan?RHIShaderFormat::SPIRV:RHIShaderFormat::DXIL;
                compile.Stage=RHIShaderStage::Vertex; compile.EntryPoint="VSMain";
                auto vs=RHIShaderCompiler::Get().Compile(compile); if(!vs.Success)return false; ImageVS=std::move(vs.Bytecode);
                compile.Stage=RHIShaderStage::Fragment; compile.EntryPoint="PSMain";
                auto ps=RHIShaderCompiler::Get().Compile(compile); if(!ps.Success)return false; ImagePS=std::move(ps.Bytecode);
            }
            if (!ImagePipeline || ImageFormat!=format) {
                RHIGraphicsPipelineDesc desc; desc.Layout=&ImageLayout;desc.Shaders={ImageVS,ImagePS};
                desc.Rasterizer.CullMode=nri::CullMode::NONE;desc.DepthStencil.DepthTestEnable=false;desc.DepthStencil.DepthWriteEnable=false;
                desc.DepthStencil.DepthStencilFormat=nri::Format::UNKNOWN;desc.ColorAttachmentFormats={format};desc.ColorAttachments={RHIBlendAttachmentDesc::Opaque()};
                ImagePipeline=RHIPipelineCache::Get().GetOrCreateGraphicsPipeline(desc);ImageFormat=format;
                if(!ImagePipeline)return false;
                ImagePipelineOwner=device.GetResources().AcquireNative(ImagePipeline);
            }
            // New descriptors retain both source and native view through submission.
            if(!ImagePool.Init())return false;
            nri::TextureViewDesc view{};view.texture=source;view.type=nri::TextureView::TEXTURE;
            view.format=device.GetNRICore().GetTextureDesc(*source).format;view.mipNum=view.layerNum=1;
            nri::Descriptor* descriptor=nullptr;auto core=device.GetNRICore();
            if(core.CreateTextureView(view,descriptor)!=nri::Result::SUCCESS)return false;
            auto lease=device.GetResources().Publish(RHIResourceKind::DescriptorSet,"Present source view",{descriptor},
                [core,descriptor]{core.DestroyDescriptor(descriptor);},{owner});
            auto* set=ImagePool.AllocateDescriptorSet(ImageLayout,0);
            if(!lease || !set || !ImagePool.UpdateTextureRange(set,0,0,{descriptor}) || !commands.KeepAlive(lease))return false;
            ImageSet=set; commands.TransitionTexture(source,RHIResourceLayout::Undefined,RHIResourceLayout::ShaderResource);
            return true;
        }
        nri::DescriptorSet* ImageSet=nullptr;
        uint64_t Epoch = 0, Presented = 0;
        uint32_t Slot = 0;
        bool BuildFrameStorage() {
            auto& device = RHIDevice::Get(); const auto core = device.GetNRICore();
            std::vector<RHIFrameContext> frames(Desc.FramesInFlight);
            for (auto& frame : frames) if (!frame.Init()) return false;
            std::vector<nri::Descriptor*> views;
            std::vector<RHIResourceLease> leases;
            for (uint32_t i = 0; i < SwapChain.GetBufferCount(); ++i) {
                auto* texture = SwapChain.GetTexture(i);
                nri::TextureViewDesc desc{}; desc.texture = texture; desc.type = nri::TextureView::COLOR_ATTACHMENT;
                desc.format = core.GetTextureDesc(*texture).format; desc.mipNum = desc.layerNum = 1;
                nri::Descriptor* view = nullptr;
                if (ScopedRHIFault::Invoke("CreatePresentView", [&] { return core.CreateTextureView(desc, view); }) != nri::Result::SUCCESS || !view) return false;
                auto lease = device.GetResources().Publish(RHIResourceKind::DescriptorSet, "Presentation attachment", {view},
                    [core, view] { core.DestroyDescriptor(view); }, {device.GetResources().AcquireNative(texture)});
                if (!lease) { core.DestroyDescriptor(view); return false; }
                views.push_back(view); leases.push_back(std::move(lease));
            }
            Frames = std::move(frames); Views = std::move(views); ViewLeases = std::move(leases); Slot = 0; return true;
        }
        bool Reconfigure(uint32_t width, uint32_t height, bool vsync) {
            auto& device = RHIDevice::Get();
            if (!device.WaitIdle()) return false;
            for (auto& frame : Frames) if (!frame.Abandon()) return false;
            Frames.clear(); Views.clear(); ViewLeases.clear(); device.GetResources().CollectAfterDrain();
            Surface.Resize(width, height);
            if (!SwapChain.Configure(width, height, vsync)) return false;
            return BuildFrameStorage();
        }
    };
    RHIPresentationContext::RHIPresentationContext() = default;
    RHIPresentationContext::~RHIPresentationContext() { Shutdown(); }
    bool RHIPresentationContext::Init(GLFWwindow* window, const RHISwapChainDesc& desc) {
        auto& device = RHIDevice::Get();
        if (m_Storage || !device.CanCreateResources() || !window) return false;
        auto candidate = std::make_unique<Storage>(); candidate->Desc = desc; candidate->Epoch = device.GetEpoch();
        if (!candidate->Surface.Init(window) || !candidate->SwapChain.Init(candidate->Surface, desc) || !candidate->BuildFrameStorage()) return false;
        m_Storage = std::move(candidate); return true;
    }
    bool RHIPresentationContext::Shutdown() {
        if (!m_Storage) return true;
        auto& device = RHIDevice::Get();
        if (m_Storage->Epoch == device.GetEpoch() && device.IsNativeReady() && !device.WaitIdle()) return false;
        m_Storage.reset(); return true;
    }
    RHIPresentOutcome RHIPresentationContext::RenderFrame(uint32_t width, uint32_t height, bool vsync,
        const Recorder& prepare, const Recorder& draw, nri::Texture* blitSource) {
        auto& device = RHIDevice::Get();
        if (!m_Storage || m_Storage->Epoch != device.GetEpoch()) return RHIPresentOutcome::Failed;
        auto& state = *m_Storage;
        if (!width || !height) { state.SwapChain.Resize(0, 0); return RHIPresentOutcome::Skipped; }
        if (state.SwapChain.NeedsResize() || state.SwapChain.IsMinimized() || width != state.SwapChain.GetWidth() ||
            height != state.SwapChain.GetHeight() || vsync != state.SwapChain.IsVSync())
            if (!state.Reconfigure(width, height, vsync)) return RHIPresentOutcome::Failed;
        if (!device.CanSubmit() || state.Frames.empty()) return RHIPresentOutcome::Failed;
        auto& frame = state.Frames[state.Slot];
        if (!frame.Begin()) return RHIPresentOutcome::Failed;
        if (!state.SwapChain.BeginFrame()) {
            frame.Abandon();
            return state.SwapChain.NeedsResize() ? RHIPresentOutcome::RebuildRequired : RHIPresentOutcome::Failed;
        }
        auto abandon = [&] {
            frame.Abandon(); state.SwapChain.RequestRebuild(); return RHIPresentOutcome::Failed;
        };
        auto* texture = state.SwapChain.GetCurrentTexture();
        const auto format = device.GetNRICore().GetTextureDesc(*texture).format;
        auto& commands = *frame.GetCommands();
        if (prepare && !prepare(commands, format)) return abandon();

        if (blitSource && !state.PrepareImage(blitSource,format,commands)) return abandon();
        const auto index = state.SwapChain.GetCurrentTextureIndex();
        if (index >= state.Views.size() || !commands.KeepAlive(state.ViewLeases[index])) return abandon();
        commands.TransitionTexture(texture,RHIResourceLayout::Undefined,RHIResourceLayout::ColorAttachment);
        nri::AttachmentDesc color{};color.descriptor=state.Views[index];color.loadOp=nri::LoadOp::CLEAR;color.storeOp=nri::StoreOp::STORE;
        color.clearValue.color.f={0.04f,0.05f,0.07f,1};
        nri::RenderingDesc rendering{};rendering.colors=&color;rendering.colorNum=1;
        commands.BeginRendering(rendering);
        if (blitSource) {
            commands.SetPipelineLayout(nri::BindPoint::GRAPHICS,state.ImageLayout);commands.SetPipeline(state.ImagePipeline);
            state.ImagePool.Bind(commands.GetNative());RHIDescriptorPool::BindDescriptorSet(commands.GetNative(),0,state.ImageSet);
            commands.SetViewport({0,0,float(state.SwapChain.GetWidth()),float(state.SwapChain.GetHeight()),0,1});
            commands.SetScissor({0,0,static_cast<uint16_t>(state.SwapChain.GetWidth()),static_cast<uint16_t>(state.SwapChain.GetHeight())});
            commands.Draw(3);
        }
        if (draw && !draw(commands,format)) return abandon();
        commands.EndRendering();commands.TransitionTexture(texture,RHIResourceLayout::ColorAttachment,RHIResourceLayout::Present);

        if (!frame.End()) return abandon();
        const nri::CommandBuffer* native = commands.GetNative();
        const auto before = device.GetLastSubmittedToken();
        const bool presented = state.SwapChain.PresentFrame(&native, 1);
        const auto completion = device.GetLastSubmittedToken();
        if (completion != before) {
            if (!frame.AdoptSubmission(completion)) return RHIPresentOutcome::Failed;
            state.Slot = (state.Slot + 1) % static_cast<uint32_t>(state.Frames.size());
        }
        if (presented) { ++state.Presented; return RHIPresentOutcome::Presented; }
        return state.SwapChain.NeedsResize() ? RHIPresentOutcome::RebuildRequired : RHIPresentOutcome::Failed;
    }
    uint64_t RHIPresentationContext::GetPresentCount() const { return m_Storage ? m_Storage->Presented : 0; }
    bool RHIPresentationContext::IsVSync() const { return m_Storage && m_Storage->SwapChain.IsVSync(); }
}
