#include <Axle/RHISceneRenderer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/RHI/RHIVertexLayout.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>

#include <fstream>
#include <sstream>

namespace Axle {

    using namespace Axle::RHI;

    // Embedded fallback HLSL shader in case shader file is not on disk
    static const char* s_FallbackForwardShader = R"(
        cbuffer ObjectConstants : register(b0, space0) {
            float4x4 u_ModelViewProjection;
            float4   u_ColorPrimary;
            float4   u_ColorSecondary;
            float4   u_Params;
            float4   u_SunDirAndIntensity;
        };

        struct VSInput {
            float3 Position   : POSITION;
            float3 Normal     : NORMAL;
            float4 Tangent    : TANGENT;
            float2 TexCoord   : TEXCOORD0;
            float4 Color      : COLOR0;
        };

        struct PSInput {
            float4 PositionCS : SV_Position;
            float3 NormalWS   : NORMAL;
            float2 TexCoord   : TEXCOORD0;
            float4 VertexColor: COLOR0;
            float3 LocalPos   : TEXCOORD1;
        };

        PSInput VSMain(VSInput input) {
            PSInput output;
            output.PositionCS  = mul(u_ModelViewProjection, float4(input.Position, 1.0));
            output.NormalWS    = input.Normal;
            output.TexCoord    = input.TexCoord;
            output.VertexColor = input.Color;
            output.LocalPos    = input.Position;
            return output;
        }

        float4 PSMain(PSInput input) : SV_Target0 {
            float3 N = normalize(input.NormalWS);
            float3 L = normalize(u_SunDirAndIntensity.xyz);
            int patternMode = (int)u_Params.y;
            float tileScale = max(0.01, u_Params.x);
            float4 baseColor = u_ColorPrimary;
            
            if (patternMode == 0 || patternMode == 1) {
                float3 checkCoords = floor(input.LocalPos / tileScale);
                float check = fmod(abs(checkCoords.x + checkCoords.y + checkCoords.z), 2.0);
                baseColor = (check > 0.5) ? u_ColorPrimary : u_ColorSecondary;
            } else if (patternMode == 2) {
                baseColor = u_ColorPrimary * input.VertexColor;
            }

            float NdotL = max(0.0, dot(N, L));
            float sunIntensity = max(0.2, u_SunDirAndIntensity.w);
            float3 diffuse = baseColor.rgb * (NdotL * sunIntensity);
            float3 ambient = baseColor.rgb * float3(0.22, 0.25, 0.32);
            float3 V = float3(0.0, 0.707, 0.707);
            float3 H = normalize(L + V);
            float NdotH = max(0.0, dot(N, H));
            float3 specular = float3(0.9, 0.9, 0.9) * pow(NdotH, 32.0) * (1.0 - u_Params.z) * 0.35;
            
            return float4(ambient + diffuse + specular, baseColor.a);
        }
    )";

    RHISceneRenderer::RHISceneRenderer() = default;

    RHISceneRenderer::~RHISceneRenderer() {
        Shutdown();
    }

    bool RHISceneRenderer::Init(RHIDevice& device) {
        if (m_Initialized) return true;
        if (!device.IsInitialized()) {
            AXLE_CORE_ERROR("RHISceneRenderer::Init failed: RHIDevice is not initialized!");
            return false;
        }

        m_Device = &device;

        // Initialize mesh registry
        RHIMeshRegistry::Get().Init();

        // Initialize constant streamer
        m_Streamer = std::make_unique<RHIStreamer>();
        if (!m_Streamer->Init(device)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize RHIStreamer");
        }

        // Initialize Command Buffer
        if (!m_CommandBuffer.Init(device.GetGraphicsQueue())) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to initialize RHICommandBuffer");
            return false;
        }

        // Compile Forward Shaders
        if (!CompileShaders(device)) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile forward scene shaders!");
            return false;
        }

        // Create Pipeline Layout
        if (!CreatePipelineLayout(device)) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to create pipeline layout!");
            return false;
        }

        // Create Graphics Pipeline
        if (!CreateGraphicsPipeline(device)) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to create graphics pipeline!");
            return false;
        }

        m_Initialized = true;
        AXLE_CORE_INFO("RHISceneRenderer: Successfully initialized with PushConstant transform streaming.");
        return true;
    }

    void RHISceneRenderer::Shutdown() {
        if (!m_Initialized) return;

        if (m_Device && m_Pipeline) {
            m_Device->GetNRICore().DestroyPipeline(m_Pipeline);
            m_Pipeline = nullptr;
        }

        m_PipelineLayout.Shutdown();
        m_CommandBuffer.Shutdown();
        if (m_Streamer) {
            m_Streamer->Shutdown();
            m_Streamer.reset();
        }

        m_Device = nullptr;
        m_Initialized = false;
    }

    bool RHISceneRenderer::CompileShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        if (!compiler.IsInitialized()) {
            compiler.Init();
        }

        std::string shaderSource;
        auto shaderPath = FileSystem::ResolveAssetPath("shaders/ForwardPBR.hlsl");
        if (!std::filesystem::exists(shaderPath)) {
            shaderPath = std::filesystem::current_path() / "Engine" / "Resources" / "Shaders" / "ForwardPBR.hlsl";
        }
        if (!std::filesystem::exists(shaderPath)) {
            shaderPath = FileSystem::ResolveAssetPath("shaders/ForwardScene.hlsl");
        }
        if (!std::filesystem::exists(shaderPath)) {
            shaderPath = std::filesystem::current_path() / "Engine" / "Resources" / "Shaders" / "ForwardScene.hlsl";
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
            shaderSource = s_FallbackForwardShader;
        }

        RHIShaderFormat format = (device.GetBackend() == RHIType::Vulkan) ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;

        // Compile Vertex Shader
        RHIShaderCompileDesc vsDesc;
        vsDesc.Source = shaderSource;
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        vsDesc.Format = format;
        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: VS compile failed: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_VertexShader = vsRes.Bytecode;

        // Compile Pixel Shader
        RHIShaderCompileDesc psDesc;
        psDesc.Source = shaderSource;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;
        psDesc.Format = format;
        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: PS compile failed: {0}", psRes.ErrorMessage);
            return false;
        }
        m_PixelShader = psRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreatePipelineLayout(RHIDevice& device) {
        // Root Constant (b0, space 0): 128-byte per-entity push constants
        nri::RootConstantDesc rootConst = {};
        rootConst.registerIndex = 0;
        rootConst.size = sizeof(RHIObjectPushConstants); // 128 bytes
        rootConst.shaderStages = nri::StageBits::ALL;

        // Root Descriptor (b1, space 0): 256-byte scene constants
        nri::RootDescriptorDesc rootDesc = {};
        rootDesc.registerIndex = 1;
        rootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDesc.shaderStages = nri::StageBits::ALL;

        // Descriptor Set (space 1): 6 PBR textures (t0..t5) and 1 Sampler (s0)
        nri::DescriptorRangeDesc ranges[2] = {};
        ranges[0].baseRegisterIndex = 0;
        ranges[0].descriptorNum = 6;
        ranges[0].descriptorType = nri::DescriptorType::TEXTURE;
        ranges[0].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        ranges[1].baseRegisterIndex = 0;
        ranges[1].descriptorNum = 1;
        ranges[1].descriptorType = nri::DescriptorType::SAMPLER;
        ranges[1].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc descSet = {};
        descSet.registerSpace = 1;
        descSet.ranges = ranges;
        descSet.rangeNum = 2;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootConstants = &rootConst;
        layoutDesc.rootConstantNum = 1;
        layoutDesc.rootDescriptors = &rootDesc;
        layoutDesc.rootDescriptorNum = 1;
        layoutDesc.descriptorSets = &descSet;
        layoutDesc.descriptorSetNum = 1;
        layoutDesc.shaderStages = nri::StageBits::ALL;

        return m_PipelineLayout.Init(layoutDesc);
    }

    bool RHISceneRenderer::CreateGraphicsPipeline(RHIDevice& device) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_PipelineLayout;
        gfxDesc.Shaders = { m_VertexShader, m_PixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::BACK;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = true;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS;
        gfxDesc.DepthStencil.DepthStencilFormat = nri::Format::D32_SFLOAT;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { nri::Format::RGBA8_UNORM };

        // Configure 64-byte RHIVertex3D input layout
        RHIVertexInputDesc inputDesc;
        inputDesc.Streams.push_back({ 0, sizeof(RHIVertex3D), nri::VertexStreamStepRate::PER_VERTEX });
        inputDesc.Attributes.push_back({ "POSITION", 0, 0, static_cast<uint32_t>(offsetof(RHIVertex3D, Position)), nri::Format::RGB32_SFLOAT, 0 });
        inputDesc.Attributes.push_back({ "NORMAL", 0, 1, static_cast<uint32_t>(offsetof(RHIVertex3D, Normal)), nri::Format::RGB32_SFLOAT, 0 });
        inputDesc.Attributes.push_back({ "TANGENT", 0, 2, static_cast<uint32_t>(offsetof(RHIVertex3D, Tangent)), nri::Format::RGBA32_SFLOAT, 0 });
        inputDesc.Attributes.push_back({ "TEXCOORD", 0, 3, static_cast<uint32_t>(offsetof(RHIVertex3D, TexCoord)), nri::Format::RG32_SFLOAT, 0 });
        inputDesc.Attributes.push_back({ "COLOR", 0, 4, static_cast<uint32_t>(offsetof(RHIVertex3D, Color)), nri::Format::RGBA32_SFLOAT, 0 });
        gfxDesc.VertexInput = inputDesc;

        auto& cache = RHIPipelineCache::Get();
        m_Pipeline = cache.GetOrCreateGraphicsPipeline(gfxDesc);
        return m_Pipeline != nullptr;
    }

    void RHISceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene,
                                       const PerspectiveCamera& camera,
                                       RHITexture* colorTarget,
                                       RHITexture* depthTarget,
                                       const SceneRenderSettings& settings)
    {
        if (!colorTarget || !colorTarget->IsValid()) return;

        RenderScene(scene, camera,
                    colorTarget->GetNative(),
                    colorTarget->GetRTV(),
                    depthTarget ? depthTarget->GetNative() : nullptr,
                    depthTarget ? depthTarget->GetDSV() : nullptr,
                    colorTarget->GetWidth(), colorTarget->GetHeight(),
                    settings);
    }

    void RHISceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene,
                                       const PerspectiveCamera& camera,
                                       nri::Texture* colorTarget,
                                       nri::Descriptor* colorRTV,
                                       nri::Texture* depthTarget,
                                       nri::Descriptor* depthDSV,
                                       uint32_t width, uint32_t height,
                                       const SceneRenderSettings& settings)
    {
        if (!m_Initialized || !colorTarget || !colorRTV || width == 0 || height == 0) return;

        m_LastDrawCallCount = 0;
        m_LastRenderedEntityCount = 0;
        m_LastStreamedTransformBytes = 0;

        // 1. Stream Global Scene Constants (View-Projection, Camera Pos, Sun Light)
        RHISceneConstants sceneConstants;
        sceneConstants.ViewProjection = camera.GetViewProjectionMatrix();
        sceneConstants.View = camera.GetViewMatrix();
        sceneConstants.Projection = camera.GetProjectionMatrix();
        sceneConstants.CameraPosition = glm::vec4(camera.GetPosition(), 1.0f);
        sceneConstants.AmbientLight = glm::vec4(0.22f, 0.25f, 0.32f, 1.0f);
        sceneConstants.SunDirection = glm::vec4(0.45f, 0.85f, 0.25f, 1.2f);
        sceneConstants.SunColor = glm::vec4(1.0f, 0.98f, 0.92f, 1.0f);

        if (scene) {
            auto dirLightView = scene->GetRegistry().view<DirectionalLightComponent, TransformComponent>();
            for (auto entityID : dirLightView) {
                const auto& tc = dirLightView.get<TransformComponent>(entityID);
                glm::vec3 fwd = tc.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
                sceneConstants.SunDirection = glm::vec4(-glm::normalize(fwd), 1.2f);
                break;
            }
        }

        if (m_Streamer && m_Streamer->IsValid()) {
            m_Streamer->StreamConstant(sceneConstants);
            m_LastStreamedTransformBytes += sizeof(RHISceneConstants);
        }

        // 2. Begin Command Recording
        if (!m_CommandBuffer.Begin()) return;

        // Transition Color & Depth Attachments for rendering
        m_CommandBuffer.TransitionTexture(colorTarget, RHIResourceLayout::ShaderResource, RHIResourceLayout::ColorAttachment);
        if (depthTarget) {
            m_CommandBuffer.TransitionTexture(depthTarget, RHIResourceLayout::DepthStencilRead, RHIResourceLayout::DepthStencilWrite);
        }

        // 3. Begin Rendering Pass
        nri::AttachmentDesc colorAttachment = {};
        colorAttachment.descriptor = colorRTV;
        colorAttachment.loadOp = nri::LoadOp::CLEAR;
        colorAttachment.storeOp = nri::StoreOp::STORE;
        colorAttachment.clearValue.color.f = { 0.12f, 0.14f, 0.18f, 1.0f };

        nri::AttachmentDesc depthAttachment = {};
        if (depthDSV) {
            depthAttachment.descriptor = depthDSV;
            depthAttachment.loadOp = nri::LoadOp::CLEAR;
            depthAttachment.storeOp = nri::StoreOp::STORE;
            depthAttachment.clearValue.depthStencil.depth = 1.0f;
            depthAttachment.clearValue.depthStencil.stencil = 0;
        }

        nri::RenderingDesc renderingDesc = {};
        renderingDesc.colors = &colorAttachment;
        renderingDesc.colorNum = 1;
        if (depthDSV) {
            renderingDesc.depth = depthAttachment;
        }

        m_CommandBuffer.BeginRendering(renderingDesc);

        // Viewport & Scissor Setup
        nri::Viewport vp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        m_CommandBuffer.SetViewport(vp);

        nri::Rect scissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
        m_CommandBuffer.SetScissor(scissor);

        // Bind Graphics Pipeline & Layout
        m_CommandBuffer.SetPipeline(m_Pipeline);
        m_CommandBuffer.SetPipelineLayout(nri::BindPoint::GRAPHICS, m_PipelineLayout);

        // 4. Iterate Scene Entities & Stream Transforms via Root Constants
        if (scene) {
            auto& registry = scene->GetRegistry();
            auto meshView = registry.view<TransformComponent, MeshInstance3D>();

            for (auto entityID : meshView) {
                const auto& tc = meshView.get<TransformComponent>(entityID);
                const auto& mc = meshView.get<MeshInstance3D>(entityID);

                // Determine GPU mesh to render
                std::shared_ptr<RHIMesh> mesh = mc.RHIMeshAsset;
                if (!mesh) {
                    if (!mc.AssetPath.empty()) {
                        mesh = RHIMeshRegistry::Get().GetOrCreateMesh(mc.AssetPath);
                    } else {
                        // Check if entity has standard primitive name or fallback to Cube
                        Entity ent(entityID, scene.get());
                        std::string name = ent.GetName();
                        if (name.find("Sphere") != std::string::npos) {
                            mesh = RHIMeshRegistry::Get().GetSphere();
                        } else if (name.find("Plane") != std::string::npos || name.find("Ground") != std::string::npos || name.find("Floor") != std::string::npos) {
                            mesh = RHIMeshRegistry::Get().GetPlane();
                        } else if (name.find("Cylinder") != std::string::npos) {
                            mesh = RHIMeshRegistry::Get().GetCylinder();
                        } else if (name.find("Cone") != std::string::npos) {
                            mesh = RHIMeshRegistry::Get().GetCone();
                        } else {
                            mesh = RHIMeshRegistry::Get().GetCube();
                        }
                    }
                }

                if (mesh && mesh->IsValid()) {
                    glm::mat4 worldMat = scene->GetWorldTransform(entityID);
                    glm::mat4 mvp = sceneConstants.ViewProjection * worldMat;

                    RHIObjectPushConstants pushConsts;
                    pushConsts.ModelViewProjection = mvp;
                    pushConsts.ColorPrimary = mc.Color1;
                    pushConsts.ColorSecondary = mc.Color2;
                    pushConsts.Params = glm::vec4(mc.TileScale, static_cast<float>(mc.PatternMode), 0.5f, 0.0f);
                    pushConsts.SunDirAndIntensity = sceneConstants.SunDirection;

                    // Apply PBR Material parameters if present
                    if (mc.RHIMaterialAsset) {
                        mc.RHIMaterialAsset->ApplyToPushConstants(pushConsts);
                        pushConsts.ModelViewProjection = mvp;
                        pushConsts.SunDirAndIntensity = sceneConstants.SunDirection;
                    } else if (registry.all_of<MaterialComponent>(entityID)) {
                        const auto& mat = registry.get<MaterialComponent>(entityID);
                        if (mat.RHIMaterialAsset) {
                            mat.RHIMaterialAsset->ApplyToPushConstants(pushConsts);
                            pushConsts.ModelViewProjection = mvp;
                            pushConsts.SunDirAndIntensity = sceneConstants.SunDirection;
                        } else {
                            pushConsts.ColorPrimary = mat.GetAlbedoColor();
                            pushConsts.ColorSecondary = glm::vec4(mat.GetEmissionColor(), mat.GetEmissionIntensity());
                            pushConsts.Params = glm::vec4(mat.GetUVScale().x, 0.0f, mat.GetRoughness(), mat.GetMetallic());
                        }
                    }

                    // Bind per-entity transform directly to Root Constants (Zero CPU-GPU stalls!)
                    m_CommandBuffer.SetRootConstants(0, &pushConsts, sizeof(pushConsts));
                    m_LastStreamedTransformBytes += sizeof(pushConsts);

                    // Bind vertex/index buffers and dispatch draw
                    mesh->Bind(m_CommandBuffer);
                    mesh->Draw(m_CommandBuffer);

                    m_LastDrawCallCount += static_cast<uint32_t>(mesh->GetSubmeshCount());
                    m_LastRenderedEntityCount++;
                }
            }
        }

        // 5. End Rendering Pass
        m_CommandBuffer.EndRendering();

        // Transition back to ShaderResource for ImGui presentation
        m_CommandBuffer.TransitionTexture(colorTarget, RHIResourceLayout::ColorAttachment, RHIResourceLayout::ShaderResource);
        if (depthTarget) {
            m_CommandBuffer.TransitionTexture(depthTarget, RHIResourceLayout::DepthStencilWrite, RHIResourceLayout::DepthStencilRead);
        }

        // 6. End and Submit to Queue
        m_CommandBuffer.End();
        auto* queue = m_Device->GetGraphicsQueue();
        m_CommandBuffer.Submit(queue);
        m_Device->GetNRICore().QueueWaitIdle(queue);

        if (m_Streamer) {
            m_Streamer->EndFrame();
        }
    }

}
