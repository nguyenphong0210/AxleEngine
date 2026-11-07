#include <Axle/UI/CanvasRenderer.hpp>
#include <Axle/UI/Canvas2D.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Window.hpp>
#include <Axle/RHI/RHIFrameContext.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHISampler.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/Log.hpp>
#include "CanvasShaderSource.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace Axle {
    using namespace RHI;
    struct CanvasRenderer::Storage {
        uint64_t Epoch = 0; void* Context = nullptr;
        RHIFrameContext Frame;
        RHIPipelineLayout Layout;
        RHISampler Sampler;
        RHIShaderBytecode VS, PS;
        RHIBuffer Vertices, Indices;
        RHIDescriptorPool Descriptors;
        std::vector<std::shared_ptr<RHITexture>> Images;
        std::unique_ptr<Shader> GLShader;
        GLuint VAO=0, VBO=0, IBO=0;
        ~Storage() {
            if (Context && Window::HasActiveContext() && glfwGetCurrentContext()==Context) {
                if (VAO) glDeleteVertexArrays(1,&VAO);
                if (VBO) glDeleteBuffers(1,&VBO);
                if (IBO) glDeleteBuffers(1,&IBO);
            }
        }
        bool InitNative() {
            auto& device=RHIDevice::Get(); Epoch=device.GetEpoch();
            if (!Frame.Init() || !Sampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge))) return false;
            nri::RootDescriptorDesc root{}; root.registerIndex=0; root.descriptorType=nri::DescriptorType::CONSTANT_BUFFER; root.shaderStages=nri::StageBits::VERTEX_SHADER;
            nri::DescriptorRangeDesc range{}; range.baseRegisterIndex=0; range.descriptorNum=1;
            range.descriptorType=nri::DescriptorType::TEXTURE; range.shaderStages=nri::StageBits::FRAGMENT_SHADER;
            nri::DescriptorSetDesc set{}; set.registerSpace=1; set.ranges=&range; set.rangeNum=1;
            nri::RootSamplerDesc sampler{}; sampler.registerIndex=0; sampler.shaderStages=nri::StageBits::FRAGMENT_SHADER;
            sampler.desc=RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge).ToNRIDesc();
            // Root samplers share root register space 0; texture set remains space1.
            nri::PipelineLayoutDesc desc{}; desc.rootDescriptors=&root; desc.rootDescriptorNum=1;
            desc.descriptorSets=&set; desc.descriptorSetNum=1; desc.rootSamplers=&sampler; desc.rootSamplerNum=1;
            desc.shaderStages=nri::StageBits::VERTEX_SHADER|nri::StageBits::FRAGMENT_SHADER;
            if (!Layout.Init(desc)) return false;
            RHIShaderCompileDesc shader; shader.Source=CanvasShaders::HLSL;
            shader.Format=device.GetBackend()==RHIType::Vulkan?RHIShaderFormat::SPIRV:RHIShaderFormat::DXIL;
            shader.Stage=RHIShaderStage::Vertex; shader.EntryPoint="VSMain";
            auto vertex=RHIShaderCompiler::Get().Compile(shader); if(!vertex.Success)return false; VS=std::move(vertex.Bytecode);
            shader.Stage=RHIShaderStage::Fragment; shader.EntryPoint="PSMain";
            auto pixel=RHIShaderCompiler::Get().Compile(shader); if(!pixel.Success)return false; PS=std::move(pixel.Bytecode);
            return true;
        }
    };
    CanvasRenderer::CanvasRenderer() = default;
    CanvasRenderer::~CanvasRenderer() = default;
    static UIRect Clip(const UIRect& rect, uint32_t width, uint32_t height) {
        return rect.Intersect({0,0,float(width),float(height)});
    }
    uint32_t CanvasRenderer::Submit(const Canvas2D& canvas, RHITexture* target) {
        const auto extent=canvas.GetViewportSize();
        if (extent.x<=0 || extent.y<=0 || !std::isfinite(extent.x) || !std::isfinite(extent.y)) return 0;
        const auto width=static_cast<uint32_t>(extent.x), height=static_cast<uint32_t>(extent.y);
        const auto& vertices=canvas.GetVertexBuffer(); const auto& indices=canvas.GetIndexBuffer();
        const auto quads=canvas.GetQuadClips().size();
        if (!quads || indices.size()!=quads*6 || vertices.size()!=quads*4) return 0;
        auto& device=RHIDevice::Get();
        if (device.IsNativeReady()) {
            if (!device.CanSubmit() || !target || !target->IsValid() || !target->GetRTV() || target->GetWidth()!=width || target->GetHeight()!=height) return 0;
            if (!m_Storage || m_Storage->Epoch!=device.GetEpoch()) {
                auto candidate=std::make_unique<Storage>(); if(!candidate->InitNative())return 0; m_Storage=std::move(candidate);
            }
            auto& state=*m_Storage;
            if (!state.Frame.Begin()) return 0;
            bool submitted=false;
            auto cleanup=std::unique_ptr<Storage,std::function<void(Storage*)>>(&state,[&](auto*){if(!submitted)state.Frame.Abandon();});
            if (!state.Vertices.Init(RHIBufferDesc::Vertex(vertices.size()*sizeof(UIVertex),RHIMemoryLocation::HostUpload,"Canvas vertices"),vertices.data()) ||
                !state.Indices.Init(RHIBufferDesc::Index(indices.size()*4,RHIMemoryLocation::HostUpload,"Canvas indices"),indices.data())) return 0;
            RHIDescriptorPoolDesc pool; pool.MaxDescriptorSets=static_cast<uint32_t>(quads); pool.MaxTextures=static_cast<uint32_t>(quads); pool.MaxSamplers=0;
            if (!state.Descriptors.Init(pool)) return 0;
            state.Images.clear(); state.Images.push_back(RHIMaterial::GetDefaultWhiteTexture());
            for (const auto& source:canvas.GetImages()) { auto image=ImageTextureCache::GetNative(source); if(!image)return 0; state.Images.push_back(std::move(image)); }
            std::vector<nri::DescriptorSet*> sets;
            for(size_t q=0;q<quads;++q) {
                const auto index=vertices[q*4].TextureIndex;
                if(index>=state.Images.size() || !state.Images[index])return 0;
                auto* set=state.Descriptors.AllocateDescriptorSet(state.Layout,0);
                if(!set || !state.Descriptors.UpdateTextureRange(set,0,0,{state.Images[index]->GetSRV()}))return 0;
                sets.push_back(set);
            }
            auto& commands=*state.Frame.GetCommands();
            for(const auto& image:state.Images)commands.TransitionTexture(image->GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ShaderResource);
            commands.TransitionTexture(target->GetNative(),RHIResourceLayout::Undefined,RHIResourceLayout::ColorAttachment);
            auto constants=glm::vec4(width,height,canvas.GetDPIScale(),0);
            auto uniform=state.Frame.WriteUniform(&constants,sizeof(constants)); if(!uniform.Descriptor)return 0;
            RHIGraphicsPipelineDesc pipeline; pipeline.Layout=&state.Layout; pipeline.Shaders={state.VS,state.PS};
            pipeline.Rasterizer.CullMode=nri::CullMode::NONE; pipeline.DepthStencil.DepthTestEnable=false; pipeline.DepthStencil.DepthWriteEnable=false;
            pipeline.DepthStencil.DepthStencilFormat=nri::Format::UNKNOWN;
            pipeline.ColorAttachmentFormats={ToNRIFormat(target->GetFormat())}; pipeline.ColorAttachments={RHIBlendAttachmentDesc::AlphaBlend()};
            pipeline.VertexInput.Streams={{0,sizeof(UIVertex),nri::VertexStreamStepRate::PER_VERTEX}};
            pipeline.VertexInput.Attributes={{"POSITION",0,0,0,nri::Format::RG32_SFLOAT,0},{"TEXCOORD",0,1,8,nri::Format::RG32_SFLOAT,0},
                {"COLOR",0,2,16,nri::Format::RGBA32_SFLOAT,0},{"COLOR",1,3,32,nri::Format::RGBA32_SFLOAT,0},
                {"TEXCOORD",1,4,48,nri::Format::RG32_SFLOAT,0},{"RADIUS",0,5,56,nri::Format::R32_SFLOAT,0},
                {"BORDER",0,6,60,nri::Format::R32_SFLOAT,0},{"ANGLERANGE",0,7,64,nri::Format::RG32_SFLOAT,0},
                {"TEXINDEX",0,8,72,nri::Format::R32_UINT,0},{"PRIMTYPE",0,9,76,nri::Format::R32_UINT,0}};
            auto* native=RHIPipelineCache::Get().GetOrCreateGraphicsPipeline(pipeline); if(!native)return 0;
            commands.SetPipelineLayout(nri::BindPoint::GRAPHICS,state.Layout); commands.SetPipeline(native);
            state.Descriptors.Bind(commands.GetNative()); commands.SetRootDescriptor(0,uniform.Descriptor);
            commands.SetVertexBuffer(0,state.Vertices,0,sizeof(UIVertex)); commands.SetIndexBuffer(state.Indices,0,nri::IndexType::UINT32);
            nri::AttachmentDesc color{}; color.descriptor=target->GetRTV(); color.loadOp=nri::LoadOp::LOAD; color.storeOp=nri::StoreOp::STORE;
            nri::RenderingDesc rendering{}; rendering.colors=&color; rendering.colorNum=1;
            commands.BeginRendering(rendering); commands.SetViewport({0,0,float(width),float(height),0,1});
            uint32_t draws=0;
            for(size_t q=0;q<quads;++q) {
                auto clip=Clip(canvas.GetQuadClips()[q],width,height); if(clip.Width<=0 || clip.Height<=0)continue;
                commands.SetScissor({static_cast<int16_t>(clip.X),static_cast<int16_t>(clip.Y),static_cast<uint16_t>(clip.Width),static_cast<uint16_t>(clip.Height)});
                RHIDescriptorPool::BindDescriptorSet(commands.GetNative(),0,sets[q]);
                commands.DrawIndexed(6,1,static_cast<uint32_t>(q*6),0,0); ++draws;
            }
            commands.EndRendering(); commands.TransitionTexture(target->GetNative(),RHIResourceLayout::ColorAttachment,RHIResourceLayout::ShaderResource);
            if(!state.Frame.End() || !state.Frame.Submit())return 0;
            submitted=true; return draws;
        }
        if (!Window::HasActiveContext() || target) return 0;
        auto* context=glfwGetCurrentContext();
        if (!m_Storage || m_Storage->Context!=context) {
            auto candidate=std::make_unique<Storage>(); candidate->Context=context;
            candidate->GLShader=std::make_unique<Shader>(CanvasShaders::Vertex,CanvasShaders::Fragment);
            if(!candidate->GLShader->GetRendererID())return 0;
            glCreateVertexArrays(1,&candidate->VAO); glCreateBuffers(1,&candidate->VBO); glCreateBuffers(1,&candidate->IBO);
            m_Storage=std::move(candidate);
        }
        auto& state=*m_Storage;
        std::vector<std::shared_ptr<Texture2D>> images{Texture2D::GetWhiteTexture()};
        TextureSpecification specification; specification.FlipVertically=false; specification.GenerateMipmaps=false;
        for(const auto& source:canvas.GetImages()){auto image=ImageTextureCache::GetGL(source,specification);if(!image)return 0;images.push_back(std::move(image));}
        GLint oldVAO=0,oldProgram=0,oldViewport[4],oldScissor[4];
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVAO); glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
        glGetIntegerv(GL_VIEWPORT,oldViewport); glGetIntegerv(GL_SCISSOR_BOX,oldScissor);
        const bool depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND),scissor=glIsEnabled(GL_SCISSOR_TEST),cull=glIsEnabled(GL_CULL_FACE);
        glBindVertexArray(state.VAO); glBindBuffer(GL_ARRAY_BUFFER,state.VBO); glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(UIVertex),vertices.data(),GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,state.IBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*4,indices.data(),GL_STREAM_DRAW);
        const GLint sizes[]={2,2,4,4,2,1,1,2,1,1}; const size_t offsets[]={0,8,16,32,48,56,60,64,72,76};
        for(GLuint a=0;a<10;++a){glEnableVertexAttribArray(a);if(a>=8)glVertexAttribIPointer(a,1,GL_UNSIGNED_INT,sizeof(UIVertex),reinterpret_cast<void*>(offsets[a]));
            else glVertexAttribPointer(a,sizes[a],GL_FLOAT,GL_FALSE,sizeof(UIVertex),reinterpret_cast<void*>(offsets[a]));}
        state.GLShader->Bind(); state.GLShader->SetFloat2("viewport",extent); state.GLShader->SetInt("image",0);
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glEnable(GL_SCISSOR_TEST);
        glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA); glViewport(0,0,width,height);
        uint32_t draws=0;
        for(size_t q=0;q<quads;++q){auto clip=Clip(canvas.GetQuadClips()[q],width,height);if(clip.Width<=0||clip.Height<=0)continue;
            const auto index=vertices[q*4].TextureIndex;if(index>=images.size())continue;images[index]->Bind(0);
            glScissor(static_cast<int>(clip.X),static_cast<int>(height-clip.Y-clip.Height),static_cast<int>(clip.Width),static_cast<int>(clip.Height));
            glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,reinterpret_cast<void*>(q*6*sizeof(uint32_t)));++draws;}
        glBindVertexArray(oldVAO);glUseProgram(oldProgram);glViewport(oldViewport[0],oldViewport[1],oldViewport[2],oldViewport[3]);glScissor(oldScissor[0],oldScissor[1],oldScissor[2],oldScissor[3]);
        if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
        if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
        return draws;
    }
}
