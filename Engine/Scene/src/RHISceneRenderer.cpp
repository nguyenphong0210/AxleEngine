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

        // Initialize 16x Anisotropic Trilinear Sampler
        if (!m_AnisoSampler.Init(RHISamplerDesc::Anisotropic16x())) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize 16x Anisotropic Sampler, using defaults.");
        }

        // Initialize Tier 2 Descriptor Pool (2048 textures, 64 sets)
        if (!m_DescriptorPool.Init(RHIDescriptorPoolDesc::Tier2Bindless(2048, 64))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize RHIDescriptorPool!");
        } else {
            // Allocate Default PBR Material Descriptor Set (Space 1)
            m_DefaultDescriptorSet = m_DescriptorPool.AllocateDescriptorSet(m_PipelineLayout, 0);
            if (m_DefaultDescriptorSet) {
                // Bind 16x Anisotropic Sampler to slot 0 of Range 1
                if (m_AnisoSampler.IsValid()) {
                    m_DescriptorPool.UpdateSampler(m_DefaultDescriptorSet, 1, 0, m_AnisoSampler.GetNative());
                }

                // Bind Fallback GPU resident textures to Range 0 (t0..t8: PBR material + IBL cubemaps + BRDF LUT)
                auto whiteTex = RHIMaterial::GetDefaultWhiteTexture();
                auto normalTex = RHIMaterial::GetDefaultFlatNormalTexture();
                auto roughTex = RHIMaterial::GetDefaultRoughnessTexture();
                auto blackTex = RHIMaterial::GetDefaultBlackTexture();
                auto irradianceTex = m_IrradianceMap ? m_IrradianceMap : RHIMaterial::GetDefaultIrradianceCubemap();
                auto prefilterTex = m_PrefilterMap ? m_PrefilterMap : RHIMaterial::GetDefaultPrefilterCubemap();
                auto brdfLUTTex = m_BRDFLUT ? m_BRDFLUT : RHIMaterial::GetDefaultBRDFLUT();

                if (whiteTex && normalTex && roughTex && blackTex && irradianceTex && prefilterTex && brdfLUTTex) {
                    std::vector<nri::Descriptor*> defaultSRVs = {
                        whiteTex->GetSRV(),       // t0: Albedo (1x1 White)
                        normalTex->GetSRV(),      // t1: Normal (1x1 Flat Normal)
                        roughTex->GetSRV(),       // t2: Roughness (1x1 0.5 Rough)
                        blackTex->GetSRV(),       // t3: Metallic (1x1 Black)
                        whiteTex->GetSRV(),       // t4: AO (1x1 White)
                        blackTex->GetSRV(),       // t5: Emissive (1x1 Black)
                        irradianceTex->GetSRV(),  // t6: Irradiance Cubemap
                        prefilterTex->GetSRV(),   // t7: Specular Prefilter Cubemap
                        brdfLUTTex->GetSRV()      // t8: BRDF Integration 2D LUT
                    };
                    m_DescriptorPool.UpdateTextureRange(m_DefaultDescriptorSet, 0, 0, defaultSRVs);
                }
            }
        }

        // Create Graphics Pipeline
        if (!CreateGraphicsPipeline(device)) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to create graphics pipeline!");
            return false;
        }

        // Initialize Scene Constants and Forward+ Multi-Light Constant Buffers
        m_SceneConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHISceneConstants), RHIMemoryLocation::HostUpload, "SceneConstantsBuffer"));
        if (!m_LightBuffer.Init(device)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Forward+ Multi-Light GPU Buffer!");
        }

        // Initialize Atmospheric Skybox Pipeline & Constant Buffer
        if (CompileSkyboxShaders(device) && CreateSkyboxPipelineLayout(device)) {
            if (!CreateSkyboxPipeline(device)) {
                AXLE_CORE_WARN("RHISceneRenderer: Failed to create Atmospheric Skybox graphics pipeline!");
            }
        }
        m_SkyboxConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHISkyboxConstants), RHIMemoryLocation::HostUpload, "SkyboxConstantsBuffer"));

        m_Initialized = true;
        AXLE_CORE_INFO("RHISceneRenderer: Successfully initialized with PushConstant transform streaming, 16x Anisotropic Filtering, Descriptor Pools & Forward+ Multi-Light Buffer.");
        return true;
    }

    void RHISceneRenderer::Shutdown() {
        if (!m_Initialized) return;

        m_Pipeline = nullptr;
        m_PipelineHDR = nullptr;
        m_SkyboxPipeline = nullptr;
        m_SkyboxPipelineHDR = nullptr;
        m_SkyboxPipelineLayout.Shutdown();
        m_SkyboxConstantBuffer.Shutdown();

        m_LightBuffer.Shutdown();
        m_SceneConstantBuffer.Shutdown();

        m_DefaultDescriptorSet = nullptr;
        m_IrradianceMap = nullptr;
        m_PrefilterMap = nullptr;
        m_BRDFLUT = nullptr;
        m_DescriptorPool.Shutdown();
        m_AnisoSampler.Shutdown();

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

        // Root Descriptors (space 0):
        // [0] b1, space 0: 256-byte Scene Constants
        // [1] b2, space 0: 2304-byte Forward+ Multi-Light Buffer
        nri::RootDescriptorDesc rootDescs[2] = {};
        rootDescs[0].registerIndex = 1;
        rootDescs[0].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[0].shaderStages = nri::StageBits::ALL;

        rootDescs[1].registerIndex = 2;
        rootDescs[1].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[1].shaderStages = nri::StageBits::ALL;

        // Descriptor Set (space 1): 6 PBR textures (t0..t5) + 3 IBL textures (t6..t8) and 1 Sampler (s0)
        nri::DescriptorRangeDesc ranges[2] = {};
        ranges[0].baseRegisterIndex = 0;
        ranges[0].descriptorNum = 9; // t0..t5 PBR + t6 Irradiance + t7 Prefilter + t8 BRDF LUT
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
        layoutDesc.rootDescriptors = rootDescs;
        layoutDesc.rootDescriptorNum = 2;
        layoutDesc.descriptorSets = &descSet;
        layoutDesc.descriptorSetNum = 1;
        layoutDesc.shaderStages = nri::StageBits::ALL;

        return m_PipelineLayout.Init(layoutDesc);
    }

    void RHISceneRenderer::SetIBLMaps(const std::shared_ptr<RHI::RHITexture>& irradiance,
                                      const std::shared_ptr<RHI::RHITexture>& prefilter,
                                      const std::shared_ptr<RHI::RHITexture>& brdfLUT) {
        m_IrradianceMap = irradiance;
        m_PrefilterMap = prefilter;
        m_BRDFLUT = brdfLUT;
        m_UseIBL = (irradiance != nullptr || prefilter != nullptr || brdfLUT != nullptr);

        if (m_DefaultDescriptorSet && m_DescriptorPool.IsValid()) {
            auto irrTex = m_IrradianceMap ? m_IrradianceMap : RHIMaterial::GetDefaultIrradianceCubemap();
            auto prefTex = m_PrefilterMap ? m_PrefilterMap : RHIMaterial::GetDefaultPrefilterCubemap();
            auto lutTex = m_BRDFLUT ? m_BRDFLUT : RHIMaterial::GetDefaultBRDFLUT();

            if (irrTex && prefTex && lutTex) {
                std::vector<nri::Descriptor*> iblSRVs = {
                    irrTex->GetSRV(),
                    prefTex->GetSRV(),
                    lutTex->GetSRV()
                };
                m_DescriptorPool.UpdateTextureRange(m_DefaultDescriptorSet, 0, 6, iblSRVs);
            }
        }
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreatePipeline(nri::Format colorFormat, nri::Format depthFormat) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_PipelineLayout;
        gfxDesc.Shaders = { m_VertexShader, m_PixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::BACK;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = true;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS;
        gfxDesc.DepthStencil.DepthStencilFormat = depthFormat;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };

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
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateGraphicsPipeline(RHIDevice& device) {
        m_Pipeline = GetOrCreatePipeline(nri::Format::RGBA8_UNORM, nri::Format::D32_SFLOAT);
        m_PipelineHDR = GetOrCreatePipeline(nri::Format::RGBA16_SFLOAT, nri::Format::D32_SFLOAT);
        return m_Pipeline != nullptr && m_PipelineHDR != nullptr;
    }

    bool RHISceneRenderer::CompileSkyboxShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        if (!compiler.IsInitialized()) {
            compiler.Init();
        }

        std::string shaderSource;
        auto shaderPath = FileSystem::ResolveAssetPath("shaders/AtmosphericSkybox.hlsl");
        if (!std::filesystem::exists(shaderPath)) {
            shaderPath = std::filesystem::current_path() / "Engine" / "Resources" / "Shaders" / "AtmosphericSkybox.hlsl";
        }
        if (!std::filesystem::exists(shaderPath)) {
            shaderPath = "Engine/Resources/Shaders/AtmosphericSkybox.hlsl";
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
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to locate AtmosphericSkybox.hlsl!");
            return false;
        }

        RHIShaderCompileDesc vsDesc;
        vsDesc.Source = shaderSource;
        vsDesc.SourcePath = shaderPath.string();
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        vsDesc.HLSLVersion = "2021";
        vsDesc.ShaderModel = "6_0";
        vsDesc.Format = (device.GetCapabilities().ActiveBackend == RHIType::Vulkan) ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        vsDesc.Optimization = true;

        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile AtmosphericSkybox VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_SkyboxVertexShader = vsRes.Bytecode;

        RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;

        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile AtmosphericSkybox PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_SkyboxPixelShader = psRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreateSkyboxPipelineLayout(RHIDevice& device) {
        // Root Descriptor [0] b0, space 0: 256-byte SkyboxConstants
        nri::RootDescriptorDesc rootDesc = {};
        rootDesc.registerIndex = 0;
        rootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDesc.shaderStages = nri::StageBits::ALL;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootDescriptors = &rootDesc;
        layoutDesc.rootDescriptorNum = 1;
        layoutDesc.shaderStages = nri::StageBits::ALL;

        return m_SkyboxPipelineLayout.Init(layoutDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateSkyboxPipeline(nri::Format colorFormat, nri::Format depthFormat) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_SkyboxPipelineLayout;
        gfxDesc.Shaders = { m_SkyboxVertexShader, m_SkyboxPixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS_EQUAL;
        gfxDesc.DepthStencil.DepthStencilFormat = depthFormat;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };

        // Empty vertex input layout (full-screen triangle generated via SV_VertexID)
        gfxDesc.VertexInput = RHIVertexInputDesc{};

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateSkyboxPipeline(RHIDevice& device) {
        m_SkyboxPipeline = GetOrCreateSkyboxPipeline(nri::Format::RGBA8_UNORM, nri::Format::D32_SFLOAT);
        m_SkyboxPipelineHDR = GetOrCreateSkyboxPipeline(nri::Format::RGBA16_SFLOAT, nri::Format::D32_SFLOAT);
        return m_SkyboxPipeline != nullptr && m_SkyboxPipelineHDR != nullptr;
    }

    void RHISceneRenderer::SetRenderTargetFormat(RHI::RHITextureFormat colorFormat, RHI::RHITextureFormat depthFormat) {
        m_CurrentColorFormat = RHI::ToNRIFormat(colorFormat);
        m_CurrentDepthFormat = RHI::ToNRIFormat(depthFormat);
    }

    RHI::RHITextureFormat RHISceneRenderer::GetRenderTargetColorFormat() const {
        switch (m_CurrentColorFormat) {
            case nri::Format::RGBA16_SFLOAT: return RHI::RHITextureFormat::RGBA16_FLOAT;
            case nri::Format::RGBA32_SFLOAT: return RHI::RHITextureFormat::RGBA32_FLOAT;
            default:                         return RHI::RHITextureFormat::RGBA8_UNORM;
        }
    }

    RHI::RHITextureFormat RHISceneRenderer::GetRenderTargetDepthFormat() const {
        switch (m_CurrentDepthFormat) {
            case nri::Format::D24_UNORM_S8_UINT: return RHI::RHITextureFormat::D24_UNORM_S8_UINT;
            default:                             return RHI::RHITextureFormat::D32_FLOAT;
        }
    }

    void RHISceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene,
                                       const PerspectiveCamera& camera,
                                       RHITexture* colorTarget,
                                       RHITexture* depthTarget,
                                       const SceneRenderSettings& settings)
    {
        if (!colorTarget || !colorTarget->IsValid()) return;

        m_CurrentColorFormat = RHI::ToNRIFormat(colorTarget->GetFormat());
        if (depthTarget && depthTarget->IsValid()) {
            m_CurrentDepthFormat = RHI::ToNRIFormat(depthTarget->GetFormat());
        }

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

        // 1. Stream Global Scene Constants & Forward+ Multi-Light Buffer
        RHISceneConstants sceneConstants;
        sceneConstants.ViewProjection = camera.GetViewProjectionMatrix();
        sceneConstants.View = camera.GetViewMatrix();
        sceneConstants.Projection = camera.GetProjectionMatrix();
        sceneConstants.CameraPosition = glm::vec4(camera.GetPosition(), 1.0f);
        sceneConstants.AmbientLight = glm::vec4(0.22f, 0.25f, 0.32f, 1.0f);
        sceneConstants.SunDirection = glm::vec4(0.45f, 0.85f, 0.25f, 1.2f);
        sceneConstants.SunColor = glm::vec4(1.0f, 0.98f, 0.92f, 1.0f);

        if (scene) {
            RHIMultiLightBufferData lightData{};
            lightData.CameraPosAndExposure = glm::vec4(camera.GetPosition(), 1.0f);

            auto& registry = scene->GetRegistry();

            // 1. Primary Directional Sun Light
            auto dirLightView = registry.view<DirectionalLightComponent, TransformComponent>();
            bool foundSun = false;
            for (auto entityID : dirLightView) {
                const auto& dlc = dirLightView.get<DirectionalLightComponent>(entityID);
                const auto& tc = dirLightView.get<TransformComponent>(entityID);

                glm::vec3 fwd = tc.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 sunDir = -glm::normalize(fwd);
                float lux = (dlc.Intensity > 100.0f) ? dlc.Intensity : (dlc.Intensity * PhysicalIlluminance::DirectSunlight);

                lightData.SunLight.DirectionAndIlluminance = glm::vec4(sunDir, lux);
                lightData.SunLight.ColorAndAmbient = glm::vec4(dlc.Color, dlc.AmbientIntensity);
                lightData.SunLight.AmbientColor = glm::vec4(0.22f, 0.25f, 0.32f, 1.0f);
                lightData.SunLight.ShadowParams = glm::vec4(dlc.ShadowBias, dlc.ShadowFilterRadius, 0.0f, 0.0f);
                lightData.LightCounts.z = 1;
                foundSun = true;
                break;
            }

            if (!foundSun) {
                lightData.SunLight.DirectionAndIlluminance = glm::vec4(glm::normalize(glm::vec3(0.45f, 0.85f, 0.25f)), PhysicalIlluminance::DirectSunlight);
                lightData.SunLight.ColorAndAmbient = glm::vec4(1.0f, 0.98f, 0.92f, 0.25f);
                lightData.SunLight.AmbientColor = glm::vec4(0.22f, 0.25f, 0.32f, 1.0f);
                lightData.SunLight.ShadowParams = glm::vec4(0.0012f, 2.0f, 0.0f, 0.0f);
                lightData.LightCounts.z = 1;
            }

            // 2. Point Lights (up to 16)
            auto pointLightView = registry.view<PointLightComponent, TransformComponent>();
            uint32_t pointIndex = 0;
            for (auto entityID : pointLightView) {
                if (pointIndex >= MAX_POINT_LIGHTS) break;
                const auto& plc = pointLightView.get<PointLightComponent>(entityID);
                const auto& tc = pointLightView.get<TransformComponent>(entityID);

                float intensity = (plc.Intensity > 10.0f) ? plc.Intensity : (plc.Intensity * 1000.0f);
                lightData.PointLights[pointIndex].PositionAndRadius = glm::vec4(tc.Translation, plc.Radius);
                lightData.PointLights[pointIndex].ColorAndIntensity = glm::vec4(plc.Color, intensity);
                lightData.PointLights[pointIndex].Params = glm::vec4(plc.Falloff, 0.05f * 0.05f, 0.0f, 0.0f);
                pointIndex++;
            }
            lightData.LightCounts.x = pointIndex;

            // 3. Spot Lights (up to 16)
            auto spotLightView = registry.view<SpotLightComponent, TransformComponent>();
            uint32_t spotIndex = 0;
            for (auto entityID : spotLightView) {
                if (spotIndex >= MAX_SPOT_LIGHTS) break;
                const auto& slc = spotLightView.get<SpotLightComponent>(entityID);
                if (!slc.Enabled) continue;
                const auto& tc = spotLightView.get<TransformComponent>(entityID);

                glm::vec3 fwd = glm::normalize(tc.Rotation * glm::vec3(0.0f, 0.0f, -1.0f));
                float intensity = (slc.Intensity > 10.0f) ? slc.Intensity : (slc.Intensity * 500.0f);
                float cosInner = std::cos(glm::radians(slc.InnerConeAngle));
                float cosOuter = std::cos(glm::radians(slc.OuterConeAngle));

                lightData.SpotLights[spotIndex].PositionAndRange = glm::vec4(tc.Translation, slc.Range);
                lightData.SpotLights[spotIndex].DirectionAndIntensity = glm::vec4(fwd, intensity);
                lightData.SpotLights[spotIndex].ColorAndInnerAngle = glm::vec4(slc.Color, cosInner);
                lightData.SpotLights[spotIndex].AngleParams = glm::vec4(cosOuter, 1.0f, 0.0f, 0.0f);
                spotIndex++;
            }
            lightData.LightCounts.y = spotIndex;

            // 4. Discover Skybox for IBL environment maps
            SkyboxComponent skyboxComp;
            bool hasSkybox = scene->GetPrimarySkybox(skyboxComp);
            bool useIBL = m_UseIBL || (hasSkybox && skyboxComp.Enabled && skyboxComp.Type == SkyboxType::HDRCubemap);
            lightData.LightCounts.w = useIBL ? 1u : 0u;

            m_LightBuffer.Update(lightData);

            const auto& sun = lightData.SunLight;
            sceneConstants.SunDirection = glm::vec4(sun.DirectionAndIlluminance.x,
                                                    sun.DirectionAndIlluminance.y,
                                                    sun.DirectionAndIlluminance.z,
                                                    sun.DirectionAndIlluminance.w * 0.0005f);
            sceneConstants.SunColor = sun.ColorAndAmbient;
            sceneConstants.AmbientLight = glm::vec4(sun.AmbientColor.r, sun.AmbientColor.g, sun.AmbientColor.b, sun.ColorAndAmbient.w);
        } else {
            m_LightBuffer.ResetToDefaultSun();
        }

        if (m_SceneConstantBuffer.IsValid()) {
            m_SceneConstantBuffer.UpdateData(&sceneConstants, sizeof(RHISceneConstants));
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

        // Bind Graphics Pipeline & Layout matching active color & depth formats
        nri::Pipeline* activePipeline = (m_CurrentColorFormat == nri::Format::RGBA16_SFLOAT && m_PipelineHDR)
                                            ? m_PipelineHDR
                                            : (m_CurrentColorFormat == nri::Format::RGBA8_UNORM && m_Pipeline)
                                                  ? m_Pipeline
                                                  : GetOrCreatePipeline(m_CurrentColorFormat, m_CurrentDepthFormat);
        m_CommandBuffer.SetPipeline(activePipeline ? activePipeline : m_Pipeline);
        m_CommandBuffer.SetPipelineLayout(nri::BindPoint::GRAPHICS, m_PipelineLayout);

        // Bind Root Descriptors: [0] SceneConstants (b1), [1] MultiLightBuffer (b2)
        if (m_SceneConstantBuffer.IsValid() && m_SceneConstantBuffer.GetCBV()) {
            m_CommandBuffer.SetRootDescriptor(0, m_SceneConstantBuffer.GetCBV());
        }
        if (m_LightBuffer.IsValid() && m_LightBuffer.GetCBV()) {
            m_CommandBuffer.SetRootDescriptor(1, m_LightBuffer.GetCBV());
        }

        // Bind Descriptor Pool & Material Descriptor Set (Space 1: 6 PBR textures + 16x Aniso Sampler)
        if (m_DescriptorPool.IsValid()) {
            m_DescriptorPool.Bind(m_CommandBuffer.GetNative());
            if (m_DefaultDescriptorSet) {
                RHIDescriptorPool::BindDescriptorSet(m_CommandBuffer.GetNative(), 0, m_DefaultDescriptorSet);
            }
        }

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

        // 5. Procedural Atmospheric Skybox Pass (Rayleigh & Mie scattering at infinity)
        if (settings.EnableSkybox && m_SkyboxPipeline) {
            SkyboxComponent activeSkybox = m_SkyboxParams;
            if (scene) {
                SkyboxComponent sceneSkybox;
                if (scene->GetPrimarySkybox(sceneSkybox)) {
                    activeSkybox = sceneSkybox;
                }
            }

            if (activeSkybox.Enabled && activeSkybox.Type == SkyboxType::ProceduralAtmospheric) {
                // 1. Determine normalized Sun direction
                glm::vec3 sunDir(0.45f, 0.85f, 0.25f);
                if (activeSkybox.SyncWithSun) {
                    sunDir = glm::vec3(sceneConstants.SunDirection);
                } else {
                    float radAz = glm::radians(activeSkybox.CustomAzimuth);
                    float radEl = glm::radians(std::clamp(activeSkybox.CustomElevation, 1.0f, 89.9f));
                    sunDir = glm::vec3(
                        std::cos(radEl) * std::sin(radAz),
                        std::sin(radEl),
                        std::cos(radEl) * std::cos(radAz)
                    );
                }
                if (glm::length(sunDir) > 1e-4f) {
                    sunDir = glm::normalize(sunDir);
                } else {
                    sunDir = glm::vec3(0.0f, 1.0f, 0.0f);
                }

                // 2. Solar elevation, day factor and sunset horizon warming factors
                float sunElev = sunDir.y;
                float dayFactor = std::clamp(sunElev * 3.0f + 0.5f, 0.0f, 1.0f);
                float sunsetFactor = std::clamp(1.0f - std::abs(sunElev) * 2.5f, 0.0f, 1.0f);

                // 3. Setup 256-byte CBV aligned constant buffer
                RHISkyboxConstants skyboxConstants{};
                skyboxConstants.InvViewProjection = glm::inverse(sceneConstants.ViewProjection);
                skyboxConstants.CameraPosition = sceneConstants.CameraPosition;
                skyboxConstants.SunDirection = glm::vec4(sunDir, sunElev);
                skyboxConstants.SunColor = glm::vec4(glm::vec3(sceneConstants.SunColor) * activeSkybox.Intensity, dayFactor);
                skyboxConstants.ZenithColor = glm::vec4(activeSkybox.ZenithColor, sunsetFactor);
                skyboxConstants.HorizonColor = glm::vec4(activeSkybox.HorizonColor, activeSkybox.Turbidity);
                skyboxConstants.GroundColor = glm::vec4(activeSkybox.GroundColor, activeSkybox.Exposure);
                skyboxConstants.Params = glm::vec4(activeSkybox.SunSize, activeSkybox.SunGlow, 0.0f, 2.2f); // ACES filmic & 2.2 gamma

                if (m_SkyboxConstantBuffer.IsValid()) {
                    m_SkyboxConstantBuffer.UpdateData(&skyboxConstants, sizeof(RHISkyboxConstants));
                }

                // 4. Bind Skybox Graphics Pipeline matching active color & depth formats
                nri::Pipeline* activeSkyboxPipeline = (m_CurrentColorFormat == nri::Format::RGBA16_SFLOAT && m_SkyboxPipelineHDR)
                                                          ? m_SkyboxPipelineHDR
                                                          : (m_CurrentColorFormat == nri::Format::RGBA8_UNORM && m_SkyboxPipeline)
                                                                ? m_SkyboxPipeline
                                                                : GetOrCreateSkyboxPipeline(m_CurrentColorFormat, m_CurrentDepthFormat);
                m_CommandBuffer.SetPipeline(activeSkyboxPipeline ? activeSkyboxPipeline : m_SkyboxPipeline);
                m_CommandBuffer.SetPipelineLayout(nri::BindPoint::GRAPHICS, m_SkyboxPipelineLayout);
                if (m_SkyboxConstantBuffer.IsValid() && m_SkyboxConstantBuffer.GetCBV()) {
                    m_CommandBuffer.SetRootDescriptor(0, m_SkyboxConstantBuffer.GetCBV());
                }

                // 5. Draw full-screen triangle on far plane (SV_VertexID unproject)
                m_CommandBuffer.Draw(3, 1, 0, 0);
                m_LastDrawCallCount++;
            }
        }

        // 6. End Rendering Pass
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
