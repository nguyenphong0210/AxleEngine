#include <Axle/RHIObjectPicker.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Log.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/RHI/RHIBarrier.hpp>
#include <Axle/RHI/RHIVertexLayout.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace Axle {

    RHIObjectPicker::RHIObjectPicker() = default;

    RHIObjectPicker::~RHIObjectPicker() {
        Shutdown();
    }

    bool RHIObjectPicker::Init(RHI::RHIDevice& device) {
        if (m_Initialized) return true;

        m_Device = &device;
        if (!m_CommandBuffer.Init(device.GetGraphicsQueue())) {
            AXLE_CORE_ERROR("RHIObjectPicker: Failed to initialize command buffer!");
            return false;
        }

        if (!CompileShaders(device)) {
            AXLE_CORE_ERROR("RHIObjectPicker: Failed to compile picking shaders!");
            return false;
        }

        if (!CreatePipelineLayout(device)) {
            AXLE_CORE_ERROR("RHIObjectPicker: Failed to create pipeline layout!");
            return false;
        }

        if (!CreateGraphicsPipeline(device)) {
            AXLE_CORE_ERROR("RHIObjectPicker: Failed to create graphics pipeline!");
            return false;
        }

        m_Initialized = true;
        AXLE_CORE_INFO("RHIObjectPicker: Successfully initialized GPU Object ID picking pipeline (R32_UINT).");
        return true;
    }

    void RHIObjectPicker::Shutdown() {
        if (!m_Initialized) return;

        if (m_Device && m_Device->GetGraphicsQueue()) {
            m_Device->GetNRICore().QueueWaitIdle(m_Device->GetGraphicsQueue());
        }

        m_PickingTexture.reset();
        m_DepthTexture.reset();
        m_PipelineLayout.Shutdown();
        m_Pipeline = nullptr;
        m_CommandBuffer.Shutdown();

        m_VertexShader = {};
        m_PixelShader = {};

        m_Width = 0;
        m_Height = 0;
        m_HasRendered = false;
        m_Initialized = false;
    }

    bool RHIObjectPicker::CompileShaders(RHI::RHIDevice& device) {
        auto& compiler = RHI::RHIShaderCompiler::Get();
        if (!compiler.IsInitialized()) {
            compiler.Init();
        }

        std::string shaderSource;
        auto shaderPath = FileSystem::ResolveAssetPath("shaders/ObjectID.hlsl");
        if (!std::filesystem::exists(shaderPath)) {
            shaderPath = std::filesystem::current_path() / "Engine" / "Resources" / "Shaders" / "ObjectID.hlsl";
        }
        if (!std::filesystem::exists(shaderPath)) {
            shaderPath = "Engine/Resources/Shaders/ObjectID.hlsl";
        }

        if (std::filesystem::exists(shaderPath)) {
            std::ifstream file(shaderPath);
            if (file.is_open()) {
                std::stringstream ss;
                ss << file.rdbuf();
                shaderSource = ss.str();
            }
        }

        if (shaderSource.empty()) {
            AXLE_CORE_ERROR("RHIObjectPicker: Failed to locate ObjectID.hlsl at '{0}'", shaderPath.string());
            return false;
        }

        RHI::RHIShaderFormat format = (device.GetCapabilities().ActiveBackend == RHI::RHIType::Vulkan) ? RHI::RHIShaderFormat::SPIRV : RHI::RHIShaderFormat::DXIL;

        // Compile Vertex Shader
        RHI::RHIShaderCompileDesc vsDesc;
        vsDesc.Source = shaderSource;
        vsDesc.SourcePath = shaderPath.string();
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHI::RHIShaderStage::Vertex;
        vsDesc.HLSLVersion = "2021";
        vsDesc.ShaderModel = "6_0";
        vsDesc.Format = format;
        vsDesc.Optimization = true;

        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHIObjectPicker: VS compile failed: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_VertexShader = vsRes.Bytecode;

        // Compile Pixel Shader
        RHI::RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHI::RHIShaderStage::Fragment;

        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHIObjectPicker: PS compile failed: {0}", psRes.ErrorMessage);
            return false;
        }
        m_PixelShader = psRes.Bytecode;

        return true;
    }

    bool RHIObjectPicker::CreatePipelineLayout(RHI::RHIDevice& device) {
        // Root Constant (b0, space 0): 128-byte per-entity push constants
        nri::RootConstantDesc rootConst = {};
        rootConst.registerIndex = 0;
        rootConst.size = sizeof(RHIObjectIDPushConstants); // 128 bytes
        rootConst.shaderStages = nri::StageBits::ALL;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootConstants = &rootConst;
        layoutDesc.rootConstantNum = 1;
        layoutDesc.shaderStages = nri::StageBits::ALL;

        return m_PipelineLayout.Init(layoutDesc);
    }

    bool RHIObjectPicker::CreateGraphicsPipeline(RHI::RHIDevice& device) {
        RHI::RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_PipelineLayout;
        gfxDesc.Shaders = { m_VertexShader, m_PixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = true;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS;
        gfxDesc.DepthStencil.DepthStencilFormat = nri::Format::D32_SFLOAT;

        // Color attachment: R32_UINT with blending disabled
        gfxDesc.ColorAttachments = { RHI::RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { nri::Format::R32_UINT };
        gfxDesc.SampleCount = 1;

        // Configure 64-byte RHIVertex3D input layout
        RHI::RHIVertexInputDesc inputDesc;
        inputDesc.Streams.push_back({ 0, sizeof(RHI::RHIVertex3D), nri::VertexStreamStepRate::PER_VERTEX });
        inputDesc.Attributes.push_back({ "POSITION", 0, 0, static_cast<uint32_t>(offsetof(RHI::RHIVertex3D, Position)), nri::Format::RGB32_SFLOAT, 0 });
        inputDesc.Attributes.push_back({ "NORMAL", 0, 1, static_cast<uint32_t>(offsetof(RHI::RHIVertex3D, Normal)), nri::Format::RGB32_SFLOAT, 0 });
        inputDesc.Attributes.push_back({ "TANGENT", 0, 2, static_cast<uint32_t>(offsetof(RHI::RHIVertex3D, Tangent)), nri::Format::RGBA32_SFLOAT, 0 });
        inputDesc.Attributes.push_back({ "TEXCOORD", 0, 3, static_cast<uint32_t>(offsetof(RHI::RHIVertex3D, TexCoord)), nri::Format::RG32_SFLOAT, 0 });
        inputDesc.Attributes.push_back({ "COLOR", 0, 4, static_cast<uint32_t>(offsetof(RHI::RHIVertex3D, Color)), nri::Format::RGBA32_SFLOAT, 0 });
        gfxDesc.VertexInput = inputDesc;

        auto& cache = RHI::RHIPipelineCache::Get();
        m_Pipeline = cache.GetOrCreateGraphicsPipeline(gfxDesc);
        return m_Pipeline != nullptr;
    }

    void RHIObjectPicker::Resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0) return;
        if (m_PickingTexture && m_PickingTexture->GetWidth() == width && m_PickingTexture->GetHeight() == height) {
            return;
        }

        if (m_Device && m_Device->GetGraphicsQueue()) {
            m_Device->GetNRICore().QueueWaitIdle(m_Device->GetGraphicsQueue());
        }

        m_PickingTexture = std::make_unique<RHI::RHITexture>();
        m_PickingTexture->Init(RHI::RHITextureDesc::IntegerTarget2D(width, height, "RHIObjectPicker_Target"));

        m_DepthTexture = std::make_unique<RHI::RHITexture>();
        m_DepthTexture->Init(RHI::RHITextureDesc::DepthStencil2D(width, height, RHI::RHITextureFormat::D32_FLOAT, "RHIObjectPicker_Depth"));

        m_Width = width;
        m_Height = height;
        m_HasRendered = false;
    }

    void RHIObjectPicker::RenderPickingPass(const std::shared_ptr<Scene>& scene,
                                           const PerspectiveCamera& camera,
                                           uint32_t width, uint32_t height) {
        if (!m_Initialized || !m_Pipeline) return;
        if (width == 0 || height == 0) return;

        Resize(width, height);
        if (!m_PickingTexture || !m_PickingTexture->IsValid()) return;
        if (!m_DepthTexture || !m_DepthTexture->IsValid()) return;

        m_LastDrawCallCount = 0;
        m_LastRenderedEntityCount = 0;

        if (!m_CommandBuffer.Begin()) return;

        // Transition Picking Target to ColorAttachment and Depth to DepthStencilWrite
        m_CommandBuffer.TransitionTexture(m_PickingTexture->GetNative(),
            m_HasRendered ? RHI::RHIResourceLayout::CopySource : RHI::RHIResourceLayout::ShaderResource,
            RHI::RHIResourceLayout::ColorAttachment);
        m_CommandBuffer.TransitionTexture(m_DepthTexture->GetNative(),
            RHI::RHIResourceLayout::DepthStencilRead,
            RHI::RHIResourceLayout::DepthStencilWrite);

        // Begin Rendering Pass
        nri::AttachmentDesc colorAttachment = {};
        colorAttachment.descriptor = m_PickingTexture->GetRTV();
        colorAttachment.loadOp = nri::LoadOp::CLEAR;
        colorAttachment.storeOp = nri::StoreOp::STORE;
        colorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 0.0f };

        nri::AttachmentDesc depthAttachment = {};
        depthAttachment.descriptor = m_DepthTexture->GetDSV();
        depthAttachment.loadOp = nri::LoadOp::CLEAR;
        depthAttachment.storeOp = nri::StoreOp::STORE;
        depthAttachment.clearValue.depthStencil.depth = 1.0f;
        depthAttachment.clearValue.depthStencil.stencil = 0;

        nri::RenderingDesc renderingDesc = {};
        renderingDesc.colors = &colorAttachment;
        renderingDesc.colorNum = 1;
        renderingDesc.depth = depthAttachment;

        m_CommandBuffer.BeginRendering(renderingDesc);

        nri::Viewport vp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        m_CommandBuffer.SetViewport(vp);

        nri::Rect scissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
        m_CommandBuffer.SetScissors(&scissor, 1);

        m_CommandBuffer.SetPipeline(m_Pipeline);
        m_CommandBuffer.SetPipelineLayout(nri::BindPoint::GRAPHICS, m_PipelineLayout);

        if (scene) {
            auto& registry = scene->GetRegistry();
            auto meshView = registry.view<TransformComponent, MeshInstance3D>();

            for (auto entityID : meshView) {
                const auto& tc = meshView.get<TransformComponent>(entityID);
                const auto& mc = meshView.get<MeshInstance3D>(entityID);

                std::shared_ptr<RHI::RHIMesh> mesh = mc.RHIMeshAsset;
                if (!mesh) {
                    if (!mc.AssetPath.empty()) {
                        mesh = RHIMeshRegistry::Get().GetOrCreateMesh(mc.AssetPath);
                    } else {
                        Entity ent(entityID, scene.get());
                        std::string name = ent.GetName();
                        if (name.find("Sphere") != std::string::npos) mesh = RHIMeshRegistry::Get().GetSphere();
                        else if (name.find("Plane") != std::string::npos || name.find("Ground") != std::string::npos || name.find("Floor") != std::string::npos) mesh = RHIMeshRegistry::Get().GetPlane();
                        else if (name.find("Cylinder") != std::string::npos) mesh = RHIMeshRegistry::Get().GetCylinder();
                        else if (name.find("Cone") != std::string::npos) mesh = RHIMeshRegistry::Get().GetCone();
                        else mesh = RHIMeshRegistry::Get().GetCube();
                    }
                }

                if (mesh && mesh->IsValid()) {
                    glm::mat4 worldMat = scene->GetWorldTransform(entityID);
                    glm::mat4 mvp = camera.GetViewProjectionMatrix() * worldMat;

                    RHIObjectIDPushConstants pushConsts;
                    pushConsts.ModelViewProjection = mvp;
                    pushConsts.EntityID = static_cast<uint32_t>(entityID) + 1;

                    m_CommandBuffer.SetRootConstants(0, &pushConsts, sizeof(pushConsts));
                    mesh->Bind(m_CommandBuffer);
                    mesh->Draw(m_CommandBuffer);

                    m_LastDrawCallCount += static_cast<uint32_t>(mesh->GetSubmeshCount());
                    m_LastRenderedEntityCount++;
                }
            }
        }

        m_CommandBuffer.EndRendering();

        // Transition Picking Target to CopySource for Host Readback
        m_CommandBuffer.TransitionTexture(m_PickingTexture->GetNative(),
            RHI::RHIResourceLayout::ColorAttachment, RHI::RHIResourceLayout::CopySource);
        m_CommandBuffer.TransitionTexture(m_DepthTexture->GetNative(),
            RHI::RHIResourceLayout::DepthStencilWrite, RHI::RHIResourceLayout::DepthStencilRead);

        m_CommandBuffer.End();
        auto* queue = m_Device ? m_Device->GetGraphicsQueue() : nullptr;
        m_CommandBuffer.Submit(queue);

        if (m_Device && queue) {
            m_Device->GetNRICore().QueueWaitIdle(queue);
        }

        m_HasRendered = true;
    }

    uint32_t RHIObjectPicker::ReadPixel(uint32_t x, uint32_t y) {
        if (!m_Initialized || !m_HasRendered) return InvalidEntityID;
        if (!m_PickingTexture || !m_PickingTexture->IsValid()) return InvalidEntityID;
        if (x >= m_Width || y >= m_Height) return InvalidEntityID;

        uint32_t pixelValue = InvalidEntityID;

        nri::TextureRegionDesc region = {};
        region.x = static_cast<uint16_t>(x);
        region.y = static_cast<uint16_t>(y);
        region.z = 0;
        region.width = 1;
        region.height = 1;
        region.depth = 1;
        region.mipOffset = 0;
        region.layerOffset = 0;

        nri::ReadbackTextureToHostMemoryDesc readbackDesc = {};
        readbackDesc.srcTexture = m_PickingTexture->GetNative();
        readbackDesc.srcRegion = region;
        readbackDesc.dstData = &pixelValue;
        readbackDesc.dstRowPitch = sizeof(uint32_t);
        readbackDesc.dstSlicePitch = sizeof(uint32_t);

        nri::Queue* queue = m_Device ? m_Device->GetGraphicsQueue() : nullptr;
        if (queue && m_Device) {
            nri::Result res = m_Device->GetNRICore().ReadbackTextureToHostMemory(*queue, &readbackDesc, 1);
            if (res != nri::Result::SUCCESS) {
                AXLE_CORE_ERROR("RHIObjectPicker::ReadPixel: NRI Readback failed with error code {0}", (uint32_t)res);
            }
        }

        if (pixelValue == 0 || pixelValue == InvalidEntityID) {
            return InvalidEntityID;
        }

        return pixelValue - 1;
    }

    uint32_t RHIObjectPicker::Pick(const std::shared_ptr<Scene>& scene,
                                  const PerspectiveCamera& camera,
                                  uint32_t width, uint32_t height,
                                  uint32_t x, uint32_t y) {
        RenderPickingPass(scene, camera, width, height);
        return ReadPixel(x, y);
    }

}
