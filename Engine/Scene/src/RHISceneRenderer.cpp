#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHIParticleRenderer.hpp>
#include <Axle/RHIPhysicsDebugRenderer.hpp>
#include <Axle/RHIObjectPicker.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/Vehicle.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/RHI/RHIVertexLayout.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>

#include <iostream>
#include <fstream>
#include <sstream>

namespace Axle {

    using namespace Axle::RHI;

    // Embedded fallback HLSL shader in case shader file is not on disk
    static const char* s_FallbackForwardShader = R"(
        cbuffer ObjectConstants : register(b0, space0) {
            float4x4 u_Model;
            float4   u_ColorPrimary;
            float4   u_ColorSecondary;
            float4   u_Params;
            float4   u_SunDirAndIntensity;
        };

        cbuffer SceneConstants : register(b1, space0) {
            float4x4 u_ViewProjection;
            float4x4 u_View;
            float4x4 u_Projection;
            float4   u_CameraPosition;
            float4   u_AmbientLight;
            float4   u_SunDirection;
            float4   u_SunColor;
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
            float3 WorldPos   : POSITION;
            float3 NormalWS   : NORMAL;
            float2 TexCoord   : TEXCOORD0;
            float4 VertexColor: COLOR0;
            float3 LocalPos   : TEXCOORD1;
        };

        PSInput VSMain(VSInput input) {
            PSInput output;
            float4 worldPos    = mul(u_Model, float4(input.Position, 1.0));
            output.PositionCS  = mul(u_ViewProjection, worldPos);
            output.WorldPos    = worldPos.xyz;
            output.NormalWS    = normalize(mul((float3x3)u_Model, input.Normal));
            output.TexCoord    = input.TexCoord;
            output.VertexColor = input.Color;
            output.LocalPos    = input.Position;
            return output;
        }

        float glsl_mod(float x, float y) {
            return x - y * floor(x / y);
        }

        float4 PSMain(PSInput input) : SV_Target0 {
            int patternMode = (int)u_Params.y;
            float tileScale = max(0.01, u_Params.x);
            float4 baseColor = u_ColorPrimary;
            
            if (patternMode == 0) {
                float2 coord = floor(input.WorldPos.xz * tileScale);
                float check = glsl_mod(coord.x + coord.y, 2.0);
                baseColor = lerp(u_ColorPrimary, u_ColorSecondary, check);
            } else if (patternMode == 1) {
                float3 coord = floor((input.LocalPos + float3(0.5, 0.5, 0.5)) * tileScale + float3(0.0001, 0.0001, 0.0001));
                float check = glsl_mod(coord.x + coord.y + coord.z, 2.0);
                baseColor = lerp(u_ColorPrimary, u_ColorSecondary, check);
            } else if (patternMode == 2) {
                float3 nLocal = normalize(input.LocalPos);
                float uSph = atan2(nLocal.z, nLocal.x) / (2.0 * 3.14159265) + 0.5;
                float vSph = asin(clamp(nLocal.y, -1.0, 1.0)) / 3.14159265 + 0.5;
                float2 coord = floor(float2(uSph * tileScale, vSph * (tileScale * 0.5)));
                float check = glsl_mod(coord.x + coord.y, 2.0);
                baseColor = lerp(u_ColorPrimary, u_ColorSecondary, check);
            } else if (patternMode == 3) {
                baseColor = u_ColorPrimary;
            } else if (patternMode == 4) {
                baseColor = u_ColorPrimary * input.VertexColor;
            }

            float3 camPos = (length(u_CameraPosition.xyz) > 0.001) ? u_CameraPosition.xyz : float3(0.0, 5.0, 10.0);
            float3 V = normalize(camPos - input.WorldPos);

            float3 N = normalize(input.NormalWS);
            if (patternMode == 0 || patternMode == 1) {
                float3 dX = ddx(input.WorldPos);
                float3 dY = ddy(input.WorldPos);
                float3 crossN = cross(dX, dY);
                if (length(crossN) > 1e-6) {
                    N = normalize(crossN);
                }
            }
            if (dot(N, V) < 0.0) {
                N = -N;
            }

            float3 L = normalize(u_SunDirAndIntensity.xyz);
            float NdotL = max(0.0, dot(N, L));
            float sunIntensity = max(0.2, u_SunDirAndIntensity.w);
            float3 diffuse = baseColor.rgb * (NdotL * sunIntensity);
            float3 ambient = baseColor.rgb * float3(0.22, 0.25, 0.32);
            float3 H = normalize(L + V);
            float NdotH = max(0.0, dot(N, H));
            float3 specular = float3(0.9, 0.9, 0.9) * pow(NdotH, 32.0) * (1.0 - u_Params.z) * 0.35;
            
            return float4(ambient + diffuse + specular, baseColor.a);
        }
    )";

    static std::filesystem::path FindShaderPath(const std::string& shaderFileName) {
        // 1. Try Asset Path resolution
        auto path = FileSystem::ResolveAssetPath("shaders/" + shaderFileName);
        if (std::filesystem::exists(path)) return path;

        path = FileSystem::ResolveAssetPath(shaderFileName);
        if (std::filesystem::exists(path)) return path;

        // 2. Try candidate root directories
        std::vector<std::filesystem::path> roots = {
            std::filesystem::current_path(),
            std::filesystem::current_path().parent_path(),
            std::filesystem::current_path().parent_path().parent_path(),
            std::filesystem::current_path().parent_path().parent_path().parent_path(),
            std::filesystem::path(".."),
            std::filesystem::path("../.."),
            std::filesystem::path("../../.."),
            FileSystem::GetAssetDirectory(),
            FileSystem::GetAssetDirectory().parent_path()
        };

        std::vector<std::string> subdirs = {
            "Engine/Resources/Shaders",
            "Resources/Shaders",
            "shaders",
            "Editor/assets/shaders",
            "assets/shaders"
        };

        for (const auto& root : roots) {
            if (root.empty()) continue;
            for (const auto& sub : subdirs) {
                std::error_code ec;
                auto testPath = root / sub / shaderFileName;
                if (std::filesystem::exists(testPath, ec) && !std::filesystem::is_directory(testPath, ec)) {
                    return testPath;
                }
            }
        }

        return {};
    }

    RHISceneRenderer::RHISceneRenderer() = default;

    RHISceneRenderer::~RHISceneRenderer() {
        Shutdown();
    }

    bool RHISceneRenderer::Init(RHIDevice& device, bool enablePhysicsDebug) {
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

        // Per-draw resource descriptors; the immutable sampler uses no heap slots.
        auto materialPoolDesc = RHIDescriptorPoolDesc::Tier2Bindless(65536, 4096);
        materialPoolDesc.MaxSamplers = 0;
        if (!m_DescriptorPool.Init(materialPoolDesc)) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to initialize material descriptors!");
            return false;
        } else {
            // Allocate Default PBR Material Descriptor Set (Space 1)
            m_DefaultDescriptorSet = m_DescriptorPool.AllocateDescriptorSet(m_PipelineLayout, 0);
            if (m_DefaultDescriptorSet) {
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
                    for (uint32_t slot=6;slot<static_cast<uint32_t>(RHITextureSlot::Count);++slot)
                        defaultSRVs.push_back(RHIMaterial::GetFallbackTexture(static_cast<RHITextureSlot>(slot))->GetSRV());
                    defaultSRVs.push_back(blackTex->GetSRV());
                    defaultSRVs.push_back(whiteTex->GetSRV());
                    m_DescriptorPool.UpdateTextureRange(m_DefaultDescriptorSet, 0, 0, defaultSRVs);
                }
            }
        }

        RHIDescriptorPoolDesc customPool;
        customPool.MaxDescriptorSets=4096; customPool.MaxConstantBuffers=16384;
        customPool.MaxTextures=16384; customPool.MaxSamplers=1024;
        if (!m_CustomDescriptorPool.Init(customPool) || !m_CustomSampler.Init(RHISamplerDesc::Anisotropic16x())) return false;

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

        // Initialize Film-Grade Tone Mapping Post-Processing Pipeline & Resources
        if (CompileToneMappingShaders(device) && CreateToneMappingPipelineLayout(device)) {
            if (!CreateToneMappingPipeline(device)) {
                AXLE_CORE_WARN("RHISceneRenderer: Failed to create Tone Mapping graphics pipeline!");
            }
        }

        if (!m_ToneMappingSampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Bilinear Clamp Sampler for Tone Mapping.");
        }

        if (!m_ToneMappingDescriptorPool.Init(RHIDescriptorPoolDesc::Tier2Bindless(64, 16))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Tone Mapping Descriptor Pool!");
        } else if (m_ToneMappingPipelineLayout.IsValid()) {
            m_ToneMappingDescriptorSet = m_ToneMappingDescriptorPool.AllocateDescriptorSet(m_ToneMappingPipelineLayout, 0);
            if (m_ToneMappingDescriptorSet && m_ToneMappingSampler.IsValid()) {
                m_ToneMappingDescriptorPool.UpdateSampler(m_ToneMappingDescriptorSet, 1, 0, m_ToneMappingSampler.GetNative());
            }
        }

        m_ToneMappingConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIToneMappingConstants), RHIMemoryLocation::HostUpload, "ToneMappingConstantsBuffer"));

        // Initialize Fast Approximate Anti-Aliasing (FXAA 3.11 Quality) Pipeline & Resources
        if (CompileFXAAShaders(device) && CreateFXAAPipelineLayout(device)) {
            if (!CreateFXAAPipeline(device)) {
                AXLE_CORE_WARN("RHISceneRenderer: Failed to create FXAA graphics pipeline!");
            }
        }

        if (!m_FXAASampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Bilinear Clamp Sampler for FXAA.");
        }

        if (!m_FXAADescriptorPool.Init(RHIDescriptorPoolDesc::Tier2Bindless(64, 16))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize FXAA Descriptor Pool!");
        } else if (m_FXAAPipelineLayout.IsValid()) {
            m_FXAADescriptorSet = m_FXAADescriptorPool.AllocateDescriptorSet(m_FXAAPipelineLayout, 0);
            if (m_FXAADescriptorSet && m_FXAASampler.IsValid()) {
                m_FXAADescriptorPool.UpdateSampler(m_FXAADescriptorSet, 1, 0, m_FXAASampler.GetNative());
            }
        }

        m_FXAAConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIFXAAConstants), RHIMemoryLocation::HostUpload, "FXAAConstantsBuffer"));

        // Initialize GPU Instanced Particle VFX Renderer
        m_ParticleRenderer = std::make_unique<RHIParticleRenderer>();
        if (!m_ParticleRenderer->Init(device)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize GPU Instanced Particle VFX Renderer!");
        }

        // Initialize RHI Physics Debug Wireframe Line Renderer
        if (enablePhysicsDebug) {
            if (JPH::DebugRenderer::sInstance != nullptr) {
                AXLE_CORE_WARN("RHISceneRenderer: JPH::DebugRenderer singleton already active, skipping embedded physics debug renderer.");
            } else {
                if (!PhysicsEngine::IsInitialized()) {
                    PhysicsEngine::Init();
                }
                m_PhysicsDebugRenderer = std::make_unique<RHIPhysicsDebugRenderer>();
                if (!m_PhysicsDebugRenderer->Init(device)) {
                    AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize RHI Physics Debug Wireframe Line Renderer!");
                }
            }
        }

        // Initialize GPU Integer Object ID Picker
        m_ObjectPicker = std::make_unique<RHIObjectPicker>();
        if (!m_ObjectPicker->Init(device)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize GPU Integer Object ID Picker!");
        }

        m_Initialized = true;
        AXLE_CORE_INFO("RHISceneRenderer: Successfully initialized with PushConstant transform streaming, 16x Anisotropic Filtering, Descriptor Pools, Forward+ Multi-Light Buffer, Film-Grade Tone Mapping & Anti-Aliasing (FXAA/MSAA).");
        return true;
    }

    void RHISceneRenderer::Shutdown() {
        if (!m_Initialized) return;

        if (m_ObjectPicker) {
            m_ObjectPicker->Shutdown();
            m_ObjectPicker.reset();
        }

        if (m_PhysicsDebugRenderer) {
            m_PhysicsDebugRenderer->Shutdown();
            m_PhysicsDebugRenderer.reset();
        }

        if (m_ParticleRenderer) {
            m_ParticleRenderer->Shutdown();
            m_ParticleRenderer.reset();
        }

        m_FXAAPipelineSDR = nullptr;
        m_FXAAPipelineHDR = nullptr;
        m_FXAAPipelineLayout.Shutdown();
        m_FXAAConstantBuffer.Shutdown();
        m_FXAADescriptorSet = nullptr;
        m_FXAADescriptorPool.Shutdown();
        m_FXAASampler.Shutdown();
        m_IntermediateFXAATarget.reset();
        m_MSAAColorTarget.reset();
        m_MSAADepthTarget.reset();
        m_CurrentMSAASamples = 1;

        m_ToneMappingPipelineSDR = nullptr;
        m_ToneMappingPipelineHDR = nullptr;
        m_ToneMappingPipelineLayout.Shutdown();
        m_ToneMappingConstantBuffer.Shutdown();
        m_ToneMappingDescriptorSet = nullptr;
        m_ToneMappingDescriptorPool.Shutdown();
        m_ToneMappingSampler.Shutdown();
        m_IntermediateHDRTarget.reset();
        m_IntermediateHDRDepthTarget.reset();

        m_Pipeline = nullptr;
        m_PipelineHDR = nullptr;
        m_SkyboxPipeline = nullptr;
        m_SkyboxPipelineHDR = nullptr;
        m_SkyboxPipelineLayout.Shutdown();
        m_SkyboxConstantBuffer.Shutdown();

        m_LightBuffer.Shutdown();
        m_SceneConstantBuffer.Shutdown();

        m_MaterialDraws.clear();
        m_CustomDraws.clear();
        m_CustomDescriptorPool.Shutdown();
        m_CustomSampler.Shutdown();
        m_CustomProgramCache.Clear();
        m_OpaqueSceneColor.reset();
        m_OpaqueSceneDepth.reset();
        m_MaterialTextures.Clear();
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
        auto shaderPath = FindShaderPath("ForwardPBR.hlsl");
        if (shaderPath.empty()) {
            shaderPath = FindShaderPath("ForwardScene.hlsl");
        }

        if (!shaderPath.empty() && std::filesystem::exists(shaderPath)) {
            std::ifstream file(shaderPath);
            if (file.is_open()) {
                std::stringstream ss;
                ss << file.rdbuf();
                shaderSource = ss.str();
                AXLE_CORE_INFO("RHISceneRenderer: Loaded ForwardPBR shader from {0}", shaderPath.string());
            }
        }

        if (shaderSource.empty()) {
            AXLE_CORE_WARN("RHISceneRenderer: Using fallback forward shader!");
            shaderSource = s_FallbackForwardShader;
        }

        RHIShaderFormat format = (device.GetBackend() == RHIType::Vulkan) ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;

        // Compile Vertex Shader
        RHIShaderCompileDesc vsDesc;
        vsDesc.Source = shaderSource;
        vsDesc.SourcePath = shaderPath.empty() ? "ForwardPBR.hlsl" : shaderPath.string();
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        vsDesc.HLSLVersion = "2021";
        vsDesc.ShaderModel = "6_0";
        vsDesc.Format = format;
        vsDesc.Optimization = true;
        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: VS compile failed: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_VertexShader = vsRes.Bytecode;

        // Compile Pixel Shader
        RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;
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
        nri::RootDescriptorDesc rootDescs[3] = {};
        rootDescs[0].registerIndex = 1;
        rootDescs[0].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[0].shaderStages = nri::StageBits::ALL;

        rootDescs[1].registerIndex = 2;
        rootDescs[1].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[1].shaderStages = nri::StageBits::ALL;

        rootDescs[2].registerIndex = 3;
        rootDescs[2].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[2].shaderStages = nri::StageBits::ALL;

        // Descriptor Set (space 1): 6 PBR textures (t0..t5) + 3 IBL textures (t6..t8) and 1 Sampler (s0)
        nri::DescriptorRangeDesc ranges[2] = {};
        ranges[0].baseRegisterIndex = 0;
        ranges[0].descriptorNum = static_cast<uint32_t>(RHITextureSlot::Count)+5; // t0..t5 PBR + t6 Irradiance + t7 Prefilter + t8 BRDF LUT
        ranges[0].descriptorType = nri::DescriptorType::TEXTURE;
        ranges[0].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::RootSamplerDesc samplers[2]{};
        auto& sampler=samplers[0];
        sampler.registerIndex = 0;
        sampler.desc = RHISamplerDesc::Anisotropic16x().ToNRIDesc();
        sampler.shaderStages = nri::StageBits::FRAGMENT_SHADER;

        samplers[1].registerIndex=1;
        samplers[1].desc=RHISamplerDesc::Trilinear(RHIAddressMode::ClampToEdge).ToNRIDesc();
        samplers[1].shaderStages=nri::StageBits::FRAGMENT_SHADER;

        nri::DescriptorSetDesc descSet = {};
        descSet.registerSpace = 1;
        descSet.ranges = ranges;
        descSet.rangeNum = 1;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootConstants = &rootConst;
        layoutDesc.rootConstantNum = 1;
        layoutDesc.rootDescriptors = rootDescs;
        layoutDesc.rootDescriptorNum = 3;
        layoutDesc.rootSamplers = samplers;
        layoutDesc.rootSamplerNum = 2;
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

    nri::Pipeline* RHISceneRenderer::GetOrCreatePipeline(nri::Format colorFormat, nri::Format depthFormat, uint8_t sampleCount,
                                                         Axle::SurfaceType surfaceType,
                                                         Axle::CullMode cullMode,
                                                         bool depthWrite) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_PipelineLayout;
        gfxDesc.Shaders = { m_VertexShader, m_PixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        if (cullMode == Axle::CullMode::Back) {
            gfxDesc.Rasterizer.CullMode = nri::CullMode::BACK;
        } else if (cullMode == Axle::CullMode::Front) {
            gfxDesc.Rasterizer.CullMode = nri::CullMode::FRONT;
        } else {
            gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        }
        gfxDesc.Rasterizer.FrontCounterClockwise = true;

        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = depthWrite;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS;
        gfxDesc.DepthStencil.DepthStencilFormat = depthFormat;

        if (surfaceType == Axle::SurfaceType::Transparent) {
            gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::AlphaBlend() };
        } else if (surfaceType == Axle::SurfaceType::Additive) {
            gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Additive() };
        } else {
            gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        }
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        gfxDesc.SampleCount = sampleCount;

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

    nri::Pipeline* RHISceneRenderer::GetOrCreateCustomPipeline(const CustomReadyGeneration& generation,
        nri::Format colorFormat, nri::Format depthFormat, uint8_t samples,
        SurfaceType surface, CullMode cull, bool depthWrite) {
        return CreateCustomMaterialPipeline(generation,colorFormat,depthFormat,samples,surface,cull,depthWrite);
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
        auto shaderPath = FindShaderPath("AtmosphericSkybox.hlsl");

        if (!shaderPath.empty() && std::filesystem::exists(shaderPath)) {
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

    nri::Pipeline* RHISceneRenderer::GetOrCreateSkyboxPipeline(nri::Format colorFormat, nri::Format depthFormat, uint8_t sampleCount) {
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
        gfxDesc.SampleCount = sampleCount;

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

    bool RHISceneRenderer::CompileToneMappingShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        if (!compiler.IsInitialized()) {
            compiler.Init();
        }

        std::string shaderSource;
        auto shaderPath = FindShaderPath("ToneMapping.hlsl");

        if (!shaderPath.empty() && std::filesystem::exists(shaderPath)) {
            std::ifstream file(shaderPath);
            if (file.is_open()) {
                std::stringstream ss;
                ss << file.rdbuf();
                shaderSource = ss.str();
            }
        }

        if (shaderSource.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to locate ToneMapping.hlsl!");
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
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile ToneMapping VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_ToneMappingVertexShader = vsRes.Bytecode;

        RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;

        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile ToneMapping PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_ToneMappingPixelShader = psRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreateToneMappingPipelineLayout(RHIDevice& device) {
        // Root Descriptor [0] b0, space 0: 256-byte ToneMappingConstants
        nri::RootDescriptorDesc rootDesc = {};
        rootDesc.registerIndex = 0;
        rootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDesc.shaderStages = nri::StageBits::ALL;

        // Descriptor Set (space 1):
        // Range 0: t0 (HDR Texture)
        // Range 1: s0 (Linear/Bilinear Sampler)
        nri::DescriptorRangeDesc ranges[2] = {};
        ranges[0].baseRegisterIndex = 0;
        ranges[0].descriptorNum = 1;
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
        layoutDesc.rootDescriptors = &rootDesc;
        layoutDesc.rootDescriptorNum = 1;
        layoutDesc.descriptorSets = &descSet;
        layoutDesc.descriptorSetNum = 1;
        layoutDesc.shaderStages = nri::StageBits::ALL;

        return m_ToneMappingPipelineLayout.Init(layoutDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateToneMappingPipeline(nri::Format colorFormat) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_ToneMappingPipelineLayout;
        gfxDesc.Shaders = { m_ToneMappingVertexShader, m_ToneMappingPixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = false;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };

        // Empty vertex input layout (full-screen triangle generated via SV_VertexID)
        gfxDesc.VertexInput = RHIVertexInputDesc{};

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateToneMappingPipeline(RHIDevice& device) {
        m_ToneMappingPipelineSDR = GetOrCreateToneMappingPipeline(nri::Format::RGBA8_UNORM);
        m_ToneMappingPipelineHDR = GetOrCreateToneMappingPipeline(nri::Format::RGBA16_SFLOAT);
        return m_ToneMappingPipelineSDR != nullptr && m_ToneMappingPipelineHDR != nullptr;
    }

    bool RHISceneRenderer::CompileFXAAShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        if (!compiler.IsInitialized()) {
            compiler.Init();
        }

        std::string shaderSource;
        auto shaderPath = FindShaderPath("FXAA.hlsl");

        if (!shaderPath.empty() && std::filesystem::exists(shaderPath)) {
            std::ifstream file(shaderPath);
            if (file.is_open()) {
                std::stringstream ss;
                ss << file.rdbuf();
                shaderSource = ss.str();
            }
        }

        if (shaderSource.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to locate FXAA.hlsl!");
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
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile FXAA VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_FXAAVertexShader = vsRes.Bytecode;

        RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;

        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile FXAA PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_FXAAPixelShader = psRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreateFXAAPipelineLayout(RHIDevice& device) {
        // Root Descriptor [0] b0, space 0: 256-byte RHIFXAAConstants
        nri::RootDescriptorDesc rootDesc = {};
        rootDesc.registerIndex = 0;
        rootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDesc.shaderStages = nri::StageBits::ALL;

        // Descriptor Set (space 1):
        // Range 0: t0 (Input Texture)
        // Range 1: s0 (Linear Sampler)
        nri::DescriptorRangeDesc ranges[2] = {};
        ranges[0].baseRegisterIndex = 0;
        ranges[0].descriptorNum = 1;
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
        layoutDesc.rootDescriptors = &rootDesc;
        layoutDesc.rootDescriptorNum = 1;
        layoutDesc.descriptorSets = &descSet;
        layoutDesc.descriptorSetNum = 1;
        layoutDesc.shaderStages = nri::StageBits::ALL;

        return m_FXAAPipelineLayout.Init(layoutDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateFXAAPipeline(nri::Format colorFormat) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_FXAAPipelineLayout;
        gfxDesc.Shaders = { m_FXAAVertexShader, m_FXAAPixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = false;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };

        // Empty vertex input layout (full-screen triangle generated via SV_VertexID)
        gfxDesc.VertexInput = RHIVertexInputDesc{};

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateFXAAPipeline(RHIDevice& device) {
        m_FXAAPipelineSDR = GetOrCreateFXAAPipeline(nri::Format::RGBA8_UNORM);
        m_FXAAPipelineHDR = GetOrCreateFXAAPipeline(nri::Format::RGBA16_SFLOAT);
        return m_FXAAPipelineSDR != nullptr && m_FXAAPipelineHDR != nullptr;
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
                                       const SceneRenderSettings& settings,
                                       PhysicsScene* physicsScene)
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
                    settings,
                    physicsScene);
    }

    void RHISceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene,
                                       const PerspectiveCamera& camera,
                                       nri::Texture* colorTarget,
                                       nri::Descriptor* colorRTV,
                                       nri::Texture* depthTarget,
                                       nri::Descriptor* depthDSV,
                                       uint32_t width, uint32_t height,
                                       const SceneRenderSettings& settings,
                                       PhysicsScene* physicsScene)
    {
        if (!m_Initialized || !colorTarget || !colorRTV || width == 0 || height == 0) return;

        m_LastDrawCallCount = 0;
        m_CustomDescriptorPool.Reset(); // The preceding RenderScene submission completed before reuse.
        m_LastRenderedEntityCount = 0;
        m_LastStreamedTransformBytes = 0;

        // 1. Stream Global Scene Constants & Forward+ Multi-Light Buffer
        RHISceneConstants sceneConstants;
        sceneConstants.ViewProjection = camera.GetViewProjectionMatrixZO();
        sceneConstants.View = camera.GetViewMatrix();
        sceneConstants.Projection = camera.GetProjectionMatrixZO();
        sceneConstants.CameraPosition = glm::vec4(camera.GetPosition(), settings.EnableToneMapping ? 1.0f : 0.0f);
        sceneConstants.AmbientLight = glm::vec4(0.22f, 0.25f, 0.32f, 1.0f);
        sceneConstants.SunDirection = glm::vec4(0.45f, 0.85f, 0.25f, 1.2f);
        sceneConstants.SunColor = glm::vec4(1.0f, 0.98f, 0.92f, 1.0f);

        if (scene) {
            RHIMultiLightBufferData lightData{};
            lightData.CameraPosAndExposure = glm::vec4(camera.GetPosition(), 1.0f);
            lightData.ReservedHeader[0].x = settings.EnableStudioLightLobes ? 0.0f : 1.0f;

            auto& registry = scene->GetRegistry();

            // 1. Primary Directional Sun Light
            auto dirLightView = registry.view<DirectionalLightComponent, TransformComponent>();
            bool foundSun = false;
            entt::entity primarySun = entt::null;
            for (auto entityID : dirLightView) {
                const auto& dlc = dirLightView.get<DirectionalLightComponent>(entityID);
                const auto& tc = dirLightView.get<TransformComponent>(entityID);

                glm::vec3 sunDir = dlc.GetDirection();
                float lux = (dlc.Intensity > 100.0f) ? dlc.Intensity : (dlc.Intensity * PhysicalIlluminance::DirectSunlight);

                lightData.SunLight.DirectionAndIlluminance = glm::vec4(sunDir, lux);
                lightData.SunLight.ColorAndAmbient = glm::vec4(dlc.Color, dlc.AmbientIntensity);
                lightData.SunLight.AmbientColor = glm::vec4(0.22f, 0.25f, 0.32f, 1.0f);
                lightData.SunLight.ShadowParams = glm::vec4(dlc.ShadowBias, dlc.ShadowFilterRadius, 0.0f, 0.0f);
                lightData.LightCounts.z = 1;
                foundSun = true;
                primarySun = entityID;
                break;
            }

            if (!foundSun) {
                lightData.SunLight.DirectionAndIlluminance = glm::vec4(glm::normalize(glm::vec3(0.45f, 0.85f, 0.25f)), PhysicalIlluminance::DirectSunlight);
                lightData.SunLight.ColorAndAmbient = glm::vec4(1.0f, 0.98f, 0.92f, 0.25f);
                lightData.SunLight.AmbientColor = glm::vec4(0.22f, 0.25f, 0.32f, 1.0f);
                lightData.SunLight.ShadowParams = glm::vec4(0.0012f, 2.0f, 0.0f, 0.0f);
                lightData.LightCounts.z = 1;
            }

            // 2. Additional directional lights share the local-light array.
            // Params.z marks a direction and radiance instead of a point and falloff.
            uint32_t pointIndex = 0;
            for (auto entityID : dirLightView) {
                if (entityID == primarySun) continue;
                if (pointIndex >= MAX_POINT_LIGHTS) break;
                const auto& dlc = dirLightView.get<DirectionalLightComponent>(entityID);
                float lux = dlc.Intensity > 100.0f ? dlc.Intensity : dlc.Intensity * PhysicalIlluminance::DirectSunlight;
                auto& light = lightData.PointLights[pointIndex++];
                light.PositionAndRadius = glm::vec4(dlc.GetDirection(), 0.0f);
                light.ColorAndIntensity = glm::vec4(dlc.Color, lux * 0.000035f);
                light.Params = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
            }

            // Point lights use the remaining entries (up to 16 combined).
            auto pointLightView = registry.view<PointLightComponent, TransformComponent>();
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

        // Determine whether intermediate targets are used for MSAA, Tone Mapping & FXAA
        bool useMSAA = (settings.MSAASamples > 1);
        uint8_t activeSampleCount = useMSAA ? settings.MSAASamples : 1;
        bool useToneMap = settings.EnableToneMapping;
        bool useFXAA = settings.EnableFXAA;

        nri::Texture* sceneTargetTexture = colorTarget;
        nri::Descriptor* sceneTargetRTV = colorRTV;
        nri::Texture* sceneDepthTexture = depthTarget;
        nri::Descriptor* sceneDepthDSV = depthDSV;
        nri::Format sceneTargetFormat = m_CurrentColorFormat;

        if (useMSAA) {
            sceneTargetFormat = useToneMap ? nri::Format::RGBA16_SFLOAT : m_CurrentColorFormat;
            if (!m_MSAAColorTarget || m_MSAAColorTarget->GetWidth() != width || m_MSAAColorTarget->GetHeight() != height ||
                m_CurrentMSAASamples != activeSampleCount || m_MSAAColorTarget->GetFormat() != RHI::FromNRIFormat(sceneTargetFormat) ||
                !m_MSAADepthTarget || m_MSAADepthTarget->GetFormat() != RHI::FromNRIFormat(m_CurrentDepthFormat)) {
                m_MSAAColorTarget = std::make_unique<RHITexture>();
                RHITextureDesc colorDesc = RHITextureDesc::RenderTarget2D(width, height, RHI::FromNRIFormat(sceneTargetFormat), "SceneRenderer_MSAAColor");
                colorDesc.SampleCount = activeSampleCount;
                m_MSAAColorTarget->Init(colorDesc);

                m_MSAADepthTarget = std::make_unique<RHITexture>();
                RHITextureDesc depthDesc = RHITextureDesc::DepthStencil2D(width, height, RHI::FromNRIFormat(m_CurrentDepthFormat), "SceneRenderer_MSAADepth");
                depthDesc.SampleCount = activeSampleCount;
                m_MSAADepthTarget->Init(depthDesc);

                m_CurrentMSAASamples = activeSampleCount;
            }
            if (m_MSAAColorTarget && m_MSAAColorTarget->IsValid()) {
                sceneTargetTexture = m_MSAAColorTarget->GetNative();
                sceneTargetRTV = m_MSAAColorTarget->GetRTV();
            }
            if (m_MSAADepthTarget && m_MSAADepthTarget->IsValid()) {
                sceneDepthTexture = m_MSAADepthTarget->GetNative();
                sceneDepthDSV = m_MSAADepthTarget->GetDSV();
            }
        } else if (useToneMap) {
            if (!m_IntermediateHDRTarget || m_IntermediateHDRTarget->GetWidth() != width || m_IntermediateHDRTarget->GetHeight() != height) {
                m_IntermediateHDRTarget = std::make_unique<RHITexture>();
                m_IntermediateHDRTarget->Init(RHITextureDesc::RenderTarget2D(width, height, RHITextureFormat::RGBA16_FLOAT, "SceneRenderer_IntermediateHDR"));
            }
            if (m_IntermediateHDRTarget && m_IntermediateHDRTarget->IsValid()) {
                sceneTargetTexture = m_IntermediateHDRTarget->GetNative();
                sceneTargetRTV = m_IntermediateHDRTarget->GetRTV();
                sceneTargetFormat = nri::Format::RGBA16_SFLOAT;
            }
        } else if (useFXAA) {
            if (!m_IntermediateFXAATarget || m_IntermediateFXAATarget->GetWidth() != width || m_IntermediateFXAATarget->GetHeight() != height) {
                m_IntermediateFXAATarget = std::make_unique<RHITexture>();
                m_IntermediateFXAATarget->Init(RHITextureDesc::RenderTarget2D(width, height, RHI::FromNRIFormat(m_CurrentColorFormat), "SceneRenderer_IntermediateFXAA"));
            }
            if (m_IntermediateFXAATarget && m_IntermediateFXAATarget->IsValid()) {
                sceneTargetTexture = m_IntermediateFXAATarget->GetNative();
                sceneTargetRTV = m_IntermediateFXAATarget->GetRTV();
                sceneTargetFormat = m_CurrentColorFormat;
            }
        }

        // 2. Begin Command Recording
        if (!m_CommandBuffer.Begin()) return;

        // Transition Color & Depth Attachments for rendering
        m_CommandBuffer.TransitionTexture(sceneTargetTexture, RHIResourceLayout::ShaderResource, RHIResourceLayout::ColorAttachment);
        if (sceneDepthTexture) {
            m_CommandBuffer.TransitionTexture(sceneDepthTexture, RHIResourceLayout::DepthStencilRead, RHIResourceLayout::DepthStencilWrite);
        }

        // 3. Begin Rendering Pass
        nri::AttachmentDesc colorAttachment = {};
        colorAttachment.descriptor = sceneTargetRTV;
        colorAttachment.loadOp = nri::LoadOp::CLEAR;
        colorAttachment.storeOp = nri::StoreOp::STORE;
        colorAttachment.clearValue.color.f = { settings.ClearColor.r, settings.ClearColor.g, settings.ClearColor.b, settings.ClearColor.a };

        nri::AttachmentDesc depthAttachment = {};
        if (sceneDepthDSV) {
            depthAttachment.descriptor = sceneDepthDSV;
            depthAttachment.loadOp = nri::LoadOp::CLEAR;
            depthAttachment.storeOp = nri::StoreOp::STORE;
            depthAttachment.clearValue.depthStencil.depth = 1.0f;
            depthAttachment.clearValue.depthStencil.stencil = 0;
        }

        nri::RenderingDesc renderingDesc = {};
        renderingDesc.colors = &colorAttachment;
        renderingDesc.colorNum = 1;
        if (sceneDepthDSV) {
            renderingDesc.depth = depthAttachment;
        }

        m_CommandBuffer.BeginRendering(renderingDesc);

        // Viewport & Scissor Setup
        nri::Viewport vp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        m_CommandBuffer.SetViewport(vp);

        nri::Rect scissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
        m_CommandBuffer.SetScissor(scissor);

        // Bind Graphics Pipeline & Layout matching active color & depth formats and sample count
        nri::Pipeline* activePipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_PipelineHDR && activeSampleCount == 1)
                                            ? m_PipelineHDR
                                            : (sceneTargetFormat == nri::Format::RGBA8_UNORM && m_Pipeline && activeSampleCount == 1)
                                                  ? m_Pipeline
                                                  : GetOrCreatePipeline(sceneTargetFormat, m_CurrentDepthFormat, activeSampleCount);
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

        auto RenderSkybox=[&]() {
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
                // If Tone Mapping is enabled, output linear HDR sky radiance (-1.0f mode)
                float skyboxToneMode = settings.EnableToneMapping ? -1.0f : static_cast<float>(settings.ToneMappingMode);
                skyboxConstants.Params = glm::vec4(activeSkybox.SunSize, activeSkybox.SunGlow, skyboxToneMode, settings.EnableToneMapping ? 1.0f : settings.Gamma);

                if (m_SkyboxConstantBuffer.IsValid()) {
                    m_SkyboxConstantBuffer.UpdateData(&skyboxConstants, sizeof(RHISkyboxConstants));
                }

                // 4. Bind Skybox Graphics Pipeline matching active color & depth formats
                nri::Pipeline* activeSkyboxPipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_SkyboxPipelineHDR && activeSampleCount == 1)
                                                          ? m_SkyboxPipelineHDR
                                                          : (sceneTargetFormat == nri::Format::RGBA8_UNORM && m_SkyboxPipeline && activeSampleCount == 1)
                                                                ? m_SkyboxPipeline
                                                                : GetOrCreateSkyboxPipeline(sceneTargetFormat, m_CurrentDepthFormat, activeSampleCount);
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

        };

        // 4. Iterate Scene Entities & Stream Transforms via Root Constants (Multi-Pass Forward Pipeline)
        if (scene) {
            auto& registry = scene->GetRegistry();
            auto meshView = registry.view<TransformComponent, MeshInstance3D>();

            struct DrawItem {
                std::shared_ptr<RHIMesh> Mesh;
                RHIObjectPushConstants PushConsts;
                Axle::SurfaceType SurfaceType = Axle::SurfaceType::Opaque;
                Axle::CullMode CullMode = Axle::CullMode::None;
                bool DepthWrite = true;
                float DistanceSq = 0.0f;
                int RenderQueue = 2000;
                std::shared_ptr<RHIMaterial> Material;
                bool UseCustomShader = false;
                std::shared_ptr<const CustomReadyGeneration> CustomGeneration;
                PackedMaterialResultData PackedConstants;
                std::vector<TextureRequest> TextureRequests;
                entt::entity DiagnosticEntity = entt::null;
            };

            std::vector<DrawItem> opaqueQueue;
            std::vector<DrawItem> transparentQueue;
            std::unordered_map<CustomProgramKey,std::string,CustomProgramKeyHash> customFingerprints;

            glm::vec3 camPos = camera.GetPosition();

            for (auto entityID : meshView) {
                const auto& tc = meshView.get<TransformComponent>(entityID);
                const auto& mc = meshView.get<MeshInstance3D>(entityID);

                // Determine GPU mesh to render
                std::shared_ptr<RHIMesh> mesh = mc.RHIMeshAsset;
                if (!mesh) {
                    if (!mc.AssetPath.empty()) {
                        mesh = RHIMeshRegistry::Get().GetOrCreateMesh(mc.AssetPath);
                    }
                    if (!mesh) {
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
                    if (mesh) {
                        const_cast<MeshInstance3D&>(mc).RHIMeshAsset = mesh;
                    }
                }

                if (mesh && mesh->IsValid()) {
                    glm::mat4 worldMat = scene->GetWorldTransform(entityID);

                    DrawItem item;
                    item.DiagnosticEntity=entityID;
                    item.Mesh = mesh;
                    item.PushConsts.Model = worldMat;
                    item.PushConsts.ColorPrimary = mc.Color1;
                    item.PushConsts.ColorSecondary = mc.Color2;
                    item.PushConsts.Params = glm::vec4(mc.TileScale, static_cast<float>(mc.PatternMode), 0.5f, 0.0f);
                    item.PushConsts.SunDirAndIntensity = sceneConstants.SunDirection;
                    glm::vec3 diff = glm::vec3(worldMat[3]) - camPos;
                    item.DistanceSq = glm::dot(diff, diff);

                    // Apply PBR Material parameters if present
                    // Entity material overrides the mesh resource material.
                    if (registry.all_of<MaterialComponent>(entityID)) {
                        auto& mat = registry.get<MaterialComponent>(entityID);
                        if (mat.HasAsset() && mat.MaterialAsset) mat.SyncFromAsset();
                        else mat.SyncToRHI();
                        item.Material = mat.RHIMaterialAsset;

                        auto diagnostic=[&](std::string message) {
                            mat.CustomShaderDiagnostic=message;
                            if (mat.MaterialAsset) mat.MaterialAsset->SetCustomShaderDiagnostic(std::move(message));
                        };
                        diagnostic("");
                        if (mat.UseCustomShader) {
                            CustomProgramKey key;
                            key.ShaderPath=mat.CustomShaderPath.empty()?"":FileSystem::ResolveAssetPath(mat.CustomShaderPath).lexically_normal().string();
                            key.Backend=m_Device->GetBackend();
                            auto [fingerprint,inserted]=customFingerprints.try_emplace(key);
                            if(inserted && !key.ShaderPath.empty())fingerprint->second=m_CustomProgramCache.ComputeFingerprint(key);
                            auto fp=fingerprint->second;
                            auto selected=m_CustomProgramCache.SelectProgram(true,key.ShaderPath,key,fp);
                            if(const auto* entry=m_CustomProgramCache.FindEntry(key))fingerprint->second=entry->LastAttemptFingerprint;
                            diagnostic(selected.Diagnostic);
                            if (selected.Kind==ProgramSelectionKind::Custom && selected.Generation) {
                                const auto& plan=selected.Generation->BindingPlan;
                                auto matchesSchema=[&](const auto& properties) {
                                    return properties.size()==plan.PropertySchema.size() &&
                                        std::equal(properties.begin(),properties.end(),plan.PropertySchema.begin(),
                                            [](const auto& property,const auto& desc){return property.Desc==desc;});
                                };
                                if (mat.CustomShaderGeneration!=selected.Generation->GenerationID || !matchesSchema(mat.CustomProperties) ||
                                    (mat.MaterialAsset && !matchesSchema(mat.MaterialAsset->GetCustomProperties()))) {
                                    mat.CustomProperties=SynchronizeCustomProperties(plan.PropertySchema,mat.CustomProperties);
                                    mat.CustomShaderGeneration=selected.Generation->GenerationID;
                                    if (mat.MaterialAsset) mat.MaterialAsset->SetCustomProperties(mat.CustomProperties);
                                }
                                EngineMaterialValues engine;
                                engine.Model=worldMat; engine.ViewProjection=sceneConstants.ViewProjection;
                                engine.View=sceneConstants.View; engine.Projection=sceneConstants.Projection;
                                engine.CameraPosition=sceneConstants.CameraPosition;
                                engine.SunDirection=sceneConstants.SunDirection; engine.SunColor=sceneConstants.SunColor;
                                engine.BaseColor=mat.AlbedoColor; engine.BaseUVTransform=glm::vec4(mat.UVScale,mat.UVOffset);
                                engine.LegacySurfaceParams=glm::vec4(mat.Roughness,mat.Metallic,mat.AOStrength,mat.UVScale.x);
                                auto resolved=ResolveMaterialProperties(plan,mat.CustomProperties);
                                if (auto* properties=std::get_if<ResolvedMaterialProperties>(&resolved)) {
                                    auto packed=PackMaterialUniforms(plan,engine,*properties);
                                    if (auto* buffers=std::get_if<PackedMaterialResultData>(&packed)) {
                                        EffectiveMaterialValues values;
                                        values.BaseMapPath=mat.AlbedoPath; values.UseBaseMap=mat.UseAlbedoMap;
                                        values.Properties=mat.CustomProperties;
                                        auto textures=ResolveMaterialTextureRequests(plan,values);
                                        if (auto* requests=std::get_if<TextureRequestResultData>(&textures)) {
                                            item.PackedConstants=std::move(*buffers);
                                            item.TextureRequests=std::move(requests->Requests);
                                            item.CustomGeneration=selected.Generation;
                                            item.UseCustomShader=true;
                                        } else diagnostic(std::get<MaterialBindingError>(textures).Message);
                                    } else diagnostic(std::get<MaterialBindingError>(packed).Message);
                                } else diagnostic(std::get<MaterialBindingError>(resolved).Message);
                            }
                        }
                    } else {
                        item.Material = mc.RHIMaterialAsset;
                    }
                    if (item.Material) {
                        item.Material->ApplyToPushConstants(item.PushConsts);
                        item.PushConsts.Model = worldMat;
                        item.SurfaceType = item.Material->GetSurfaceType();
                        item.CullMode = item.Material->GetCullMode();
                        item.DepthWrite = item.Material->GetDepthWrite();
                        item.RenderQueue = item.Material->GetDesc().RenderQueue;
                    }

                    if (item.SurfaceType == Axle::SurfaceType::Transparent || item.SurfaceType == Axle::SurfaceType::Additive) {
                        transparentQueue.push_back(std::move(item));
                    } else {
                        opaqueQueue.push_back(std::move(item));
                    }
                }
            }

            nri::Pipeline* currentPipeline = activePipeline;

            bool refractionReady=false;
            size_t materialDrawIndex = 0;
            size_t customDrawIndex = 0;
            auto RenderDrawItem = [&](const DrawItem& item) {
                if (item.UseCustomShader && item.CustomGeneration) {
                    auto drawCustom=[&]() -> bool {
                        const auto& generation=*item.CustomGeneration;
                        auto* pipeline=GetOrCreateCustomPipeline(generation,sceneTargetFormat,m_CurrentDepthFormat,activeSampleCount,item.SurfaceType,item.CullMode,item.DepthWrite);
                        if (!pipeline || !generation.PipelineLayout || !generation.PipelineLayout->IsValid()) return false;
                        if (customDrawIndex==m_CustomDraws.size()) m_CustomDraws.push_back(std::make_unique<CustomDrawResources>());
                        auto& resource=*m_CustomDraws[customDrawIndex++];
                        resource.Generation=item.CustomGeneration;
                        resource.Textures.clear(); resource.Sets.clear();
                        const auto& buffers=item.PackedConstants.Buffers;
                        resource.ConstantBuffers.resize(buffers.size());
                        for(size_t i=0;i<buffers.size();++i) {
                            const auto size=(buffers[i].Bytes.size()+255u)&~size_t(255u);
                            auto& buffer=resource.ConstantBuffers[i];
                            if ((!buffer.IsValid() || buffer.GetDesc().Size<size) &&
                                !buffer.Init(RHIBufferDesc::Constant(size,RHIMemoryLocation::HostUpload,"CustomMaterialBuffer"))) return false;
                            if (!buffer.UpdateData(buffers[i].Bytes.data(),buffers[i].Bytes.size())) return false;
                        }
                        for(const auto& request:item.TextureRequests) {
                            auto texture=m_MaterialTextures.Get(request.ResolvedPath,request.Role==TextureRole::Color);
                            if (!texture) texture=request.Role==TextureRole::TangentNormal?RHIMaterial::GetDefaultFlatNormalTexture():RHIMaterial::GetDefaultWhiteTexture();
                            if (!texture || !texture->GetSRV()) return false;
                            resource.Textures.push_back(std::move(texture));
                        }
                        for(size_t setIndex=0;setIndex<generation.LayoutDescription.DescriptorSets.size();++setIndex) {
                            auto* set=m_CustomDescriptorPool.AllocateDescriptorSet(*generation.PipelineLayout,static_cast<uint32_t>(setIndex));
                            if (!set) return false;
                            resource.Sets.push_back(set);
                            const auto& description=generation.LayoutDescription.DescriptorSets[setIndex];
                            for(size_t rangeIndex=0;rangeIndex<description.Ranges.size();++rangeIndex) {
                                const auto& range=description.Ranges[rangeIndex];
                                nri::Descriptor* descriptor=nullptr;
                                ShaderAddress address{description.RegisterSpace,range.baseRegisterIndex};
                                if (range.descriptorType==nri::DescriptorType::CONSTANT_BUFFER) {
                                    for(size_t i=0;i<buffers.size();++i) if(buffers[i].Address==address) descriptor=resource.ConstantBuffers[i].GetCBV();
                                } else if(range.descriptorType==nri::DescriptorType::TEXTURE) {
                                    for(size_t i=0;i<item.TextureRequests.size();++i) if(item.TextureRequests[i].Address==address) descriptor=resource.Textures[i]->GetSRV();
                                } else if(range.descriptorType==nri::DescriptorType::SAMPLER) descriptor=m_CustomSampler.GetNative();
                                if (!descriptor || range.descriptorNum!=1) return false;
                                m_CustomDescriptorPool.UpdateTexture(set,static_cast<uint32_t>(rangeIndex),0,descriptor);
                            }
                        }
                        m_CommandBuffer.SetPipelineLayout(nri::BindPoint::GRAPHICS,*generation.PipelineLayout);
                        m_CustomDescriptorPool.Bind(m_CommandBuffer.GetNative());
                        for(size_t i=0;i<resource.Sets.size();++i) RHIDescriptorPool::BindDescriptorSet(m_CommandBuffer.GetNative(),static_cast<uint32_t>(i),resource.Sets[i]);
                        m_CommandBuffer.SetPipeline(pipeline);
                        item.Mesh->Bind(m_CommandBuffer); item.Mesh->Draw(m_CommandBuffer);
                        m_LastDrawCallCount+=static_cast<uint32_t>(item.Mesh->GetSubmeshCount()); ++m_LastRenderedEntityCount;
                        return true;
                    };
                    bool drawn=drawCustom();
                    // Restore the complete built-in binding context, even on preparation failure.
                    m_CommandBuffer.SetPipelineLayout(nri::BindPoint::GRAPHICS,m_PipelineLayout);
                    m_DescriptorPool.Bind(m_CommandBuffer.GetNative());
                    m_CommandBuffer.SetRootDescriptor(0,m_SceneConstantBuffer.GetCBV());
                    m_CommandBuffer.SetRootDescriptor(1,m_LightBuffer.GetCBV());
                    currentPipeline=nullptr;
                    if (drawn) return;
                    if(auto* material=registry.try_get<MaterialComponent>(item.DiagnosticEntity)) {
                        material->CustomShaderDiagnostic="Custom draw pipeline or resource preparation failed; using built-in material.";
                        if(material->MaterialAsset)material->MaterialAsset->SetCustomShaderDiagnostic(material->CustomShaderDiagnostic);
                    }
                }
                nri::Pipeline* needed = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && item.SurfaceType == Axle::SurfaceType::Opaque && item.CullMode == Axle::CullMode::None && item.DepthWrite && activeSampleCount == 1 && m_PipelineHDR)
                    ? m_PipelineHDR
                    : (sceneTargetFormat == nri::Format::RGBA8_UNORM && item.SurfaceType == Axle::SurfaceType::Opaque && item.CullMode == Axle::CullMode::None && item.DepthWrite && activeSampleCount == 1 && m_Pipeline)
                        ? m_Pipeline
                        : GetOrCreatePipeline(sceneTargetFormat, m_CurrentDepthFormat, activeSampleCount, item.SurfaceType, item.CullMode, item.DepthWrite);

                if (needed && needed != currentPipeline) {
                    m_CommandBuffer.SetPipeline(needed);
                    currentPipeline = needed;
                }

                if (materialDrawIndex == m_MaterialDraws.size()) {
                    auto resource = std::make_unique<MaterialDrawResources>();
                    if (!resource->Constants.Init(RHIBufferDesc::Constant(sizeof(RHIMaterialConstants), RHIMemoryLocation::HostUpload, "MaterialConstants"))) return;
                    resource->Set = m_DescriptorPool.AllocateDescriptorSet(m_PipelineLayout, 0);
                    if (!resource->Set) {
                        AXLE_CORE_ERROR("Material descriptor capacity exhausted; draw skipped.");
                        return;
                    }
                    m_MaterialDraws.push_back(std::move(resource));
                }
                auto& resource = *m_MaterialDraws[materialDrawIndex++];
                RHIMaterialConstants materialData;
                if (item.Material) materialData = item.Material->BuildConstants();
                std::vector<nri::Descriptor*> srvs(static_cast<size_t>(RHITextureSlot::Count)+5);
                for (uint32_t slot = 0; slot < static_cast<uint32_t>(RHITextureSlot::Count); ++slot) {
                    auto textureSlot = static_cast<RHITextureSlot>(slot);
                    std::shared_ptr<RHITexture> texture;
                    if (item.Material && (materialData.Flags.x & (1u << slot))) {
                        if (item.Material->HasTexture(textureSlot)) texture = item.Material->GetTexture(textureSlot);
                        else {
                            const auto& desc = item.Material->GetDesc();
                            auto path = slot == 2 && desc.TextureMode == TextureMode::PackedORM
                                ? desc.ORMPath : item.Material->GetTexturePath(textureSlot);
                            texture = m_MaterialTextures.Get(path, slot == 0 || slot == 5);
                        }
                    }
                    if (!texture) {
                        materialData.Flags.x &= ~(1u << slot);
                        texture = RHIMaterial::GetFallbackTexture(textureSlot);
                    }
                    resource.Textures[slot] = texture;
                    srvs[slot<6?slot:slot+3]=texture->GetSRV();
                }
                srvs[6]=(m_IrradianceMap ? m_IrradianceMap : RHIMaterial::GetDefaultIrradianceCubemap())->GetSRV();
                srvs[7]=(m_PrefilterMap ? m_PrefilterMap : RHIMaterial::GetDefaultPrefilterCubemap())->GetSRV();
                srvs[8]=(m_BRDFLUT ? m_BRDFLUT : RHIMaterial::GetDefaultBRDFLUT())->GetSRV();
                srvs[13]=refractionReady?m_OpaqueSceneColor->GetSRV():RHIMaterial::GetDefaultBlackTexture()->GetSRV();
                srvs[14]=refractionReady?m_OpaqueSceneDepth->GetSRV():RHIMaterial::GetDefaultWhiteTexture()->GetSRV();
                materialData.RefractionFrame=glm::vec4(width,height,refractionReady?1.0f:0.0f,0.0f);
                m_DescriptorPool.UpdateTextureRange(resource.Set, 0, 0, srvs);
                if (!resource.Constants.UpdateData(&materialData, sizeof(materialData))) return;
                m_CommandBuffer.SetRootDescriptor(2, resource.Constants.GetCBV());
                RHIDescriptorPool::BindDescriptorSet(m_CommandBuffer.GetNative(), 0, resource.Set);

                m_CommandBuffer.SetRootConstants(0, &item.PushConsts, sizeof(item.PushConsts));
                m_LastStreamedTransformBytes += sizeof(item.PushConsts);

                item.Mesh->Bind(m_CommandBuffer);
                item.Mesh->Draw(m_CommandBuffer);

                m_LastDrawCallCount += static_cast<uint32_t>(item.Mesh->GetSubmeshCount());
                m_LastRenderedEntityCount++;
            };

            std::sort(opaqueQueue.begin(), opaqueQueue.end(), [](const DrawItem& a, const DrawItem& b) {
                return a.RenderQueue < b.RenderQueue;
            });
            // 4a. Render Opaque & Cutout Queue
            for (const auto& item : opaqueQueue) {
                RenderDrawItem(item);
            }

            RenderSkybox();
            bool needsRefraction=std::any_of(transparentQueue.begin(),transparentQueue.end(),[](const DrawItem& item) {
                return item.SurfaceType==SurfaceType::Transparent && item.Material && item.Material->GetDesc().RefractionEnabled;
            });
            if (needsRefraction && activeSampleCount==1 && sceneDepthTexture && sceneDepthDSV) {
                bool newColor=!m_OpaqueSceneColor || m_OpaqueSceneColor->GetWidth()!=width || m_OpaqueSceneColor->GetHeight()!=height || m_OpaqueSceneColor->GetFormat()!=FromNRIFormat(sceneTargetFormat);
                bool newDepth=!m_OpaqueSceneDepth || m_OpaqueSceneDepth->GetWidth()!=width || m_OpaqueSceneDepth->GetHeight()!=height || m_OpaqueSceneDepth->GetFormat()!=FromNRIFormat(m_CurrentDepthFormat);
                if (newColor) {
                    m_OpaqueSceneColor=std::make_unique<RHITexture>();
                    m_OpaqueSceneColor->Init(RHITextureDesc::RenderTarget2D(width,height,FromNRIFormat(sceneTargetFormat),"OpaqueSceneColor"));
                }
                if (newDepth) {
                    m_OpaqueSceneDepth=std::make_unique<RHITexture>();
                    m_OpaqueSceneDepth->Init(RHITextureDesc::DepthStencil2D(width,height,FromNRIFormat(m_CurrentDepthFormat),"OpaqueSceneDepth"));
                }
                if (m_OpaqueSceneColor->IsValid() && m_OpaqueSceneDepth->IsValid()) {
                    m_CommandBuffer.EndRendering();
                    m_CommandBuffer.TransitionTexture(sceneTargetTexture,RHIResourceLayout::ColorAttachment,RHIResourceLayout::CopySource);
                    m_CommandBuffer.TransitionTexture(sceneDepthTexture,RHIResourceLayout::DepthStencilWrite,RHIResourceLayout::CopySource);
                    m_CommandBuffer.TransitionTexture(m_OpaqueSceneColor->GetNative(),newColor?RHIResourceLayout::Undefined:RHIResourceLayout::ShaderResource,RHIResourceLayout::CopyDest);
                    m_CommandBuffer.TransitionTexture(m_OpaqueSceneDepth->GetNative(),newDepth?RHIResourceLayout::Undefined:RHIResourceLayout::ShaderResource,RHIResourceLayout::CopyDest);
                    const auto& core=m_Device->GetNRICore();
                    core.CmdCopyTexture(*m_CommandBuffer.GetNative(),*m_OpaqueSceneColor->GetNative(),nullptr,*sceneTargetTexture,nullptr);
                    core.CmdCopyTexture(*m_CommandBuffer.GetNative(),*m_OpaqueSceneDepth->GetNative(),nullptr,*sceneDepthTexture,nullptr);
                    m_CommandBuffer.TransitionTexture(sceneTargetTexture,RHIResourceLayout::CopySource,RHIResourceLayout::ColorAttachment);
                    m_CommandBuffer.TransitionTexture(sceneDepthTexture,RHIResourceLayout::CopySource,RHIResourceLayout::DepthStencilWrite);
                    m_CommandBuffer.TransitionTexture(m_OpaqueSceneColor->GetNative(),RHIResourceLayout::CopyDest,RHIResourceLayout::ShaderResource);
                    m_CommandBuffer.TransitionTexture(m_OpaqueSceneDepth->GetNative(),RHIResourceLayout::CopyDest,RHIResourceLayout::ShaderResource);
                    colorAttachment.loadOp=nri::LoadOp::LOAD;
                    depthAttachment.loadOp=nri::LoadOp::LOAD;
                    renderingDesc.depth=depthAttachment;
                    m_CommandBuffer.BeginRendering(renderingDesc);
                    refractionReady=true;
                }
            }
            // Sky uses a different layout; restore all material bindings before alpha draws.
            m_CommandBuffer.SetPipelineLayout(nri::BindPoint::GRAPHICS,m_PipelineLayout);
            m_CommandBuffer.SetRootDescriptor(0,m_SceneConstantBuffer.GetCBV());
            m_CommandBuffer.SetRootDescriptor(1,m_LightBuffer.GetCBV());
            m_DescriptorPool.Bind(m_CommandBuffer.GetNative());
            currentPipeline=nullptr;

            // 4b. Render Transparent & Additive Queue (Sorted Back-to-Front)
            if (!transparentQueue.empty()) {
                std::sort(transparentQueue.begin(), transparentQueue.end(), [](const DrawItem& a, const DrawItem& b) {
                    return a.RenderQueue == b.RenderQueue ? a.DistanceSq > b.DistanceSq : a.RenderQueue < b.RenderQueue;
                });
                for (const auto& item : transparentQueue) {
                    RenderDrawItem(item);
                }
            }
        }

        if (!scene) RenderSkybox();

        // 6. GPU Instanced Particle VFX Pass (Tire smoke, drift dust, exhaust backfire flames, impacts)
        if (settings.EnableParticles && m_ParticleRenderer && scene) {
            m_ParticleRenderer->ResetMetrics();
            m_ParticleRenderer->Begin(m_CommandBuffer, camera, sceneTargetFormat, m_CurrentDepthFormat, activeSampleCount);
            scene->RenderParticles(*m_ParticleRenderer, camera, m_CommandBuffer);
            m_ParticleRenderer->End();
            m_LastDrawCallCount += m_ParticleRenderer->GetDrawCallCount();
        }

        // 6.5. Jolt Physics Debug Wireframe Pass (Colliders, wheel suspension raycasts, bounds)
        if (settings.ShowPhysicsDebug && m_PhysicsDebugRenderer) {
            PhysicsScene* activePhysics = physicsScene ? physicsScene : (scene ? scene->GetPhysicsScene() : nullptr);
            if (activePhysics) {
                m_PhysicsDebugRenderer->Begin(camera.GetPosition(),
                                             camera.GetViewProjectionMatrixZO(),
                                             sceneTargetFormat,
                                             m_CurrentDepthFormat,
                                             activeSampleCount,
                                             true,
                                             settings.PhysicsDepthBias,
                                             settings.PhysicsAlphaMultiplier);

                activePhysics->DrawDebug(*m_PhysicsDebugRenderer, settings.PhysicsDrawSettings);

                // Render active vehicle suspension raycasts and chassis orientation if requested
                if (scene) {
                    for (const auto& rvd : scene->GetRuntimeVehicles()) {
                        if (rvd.VehicleInstance && rvd.VehicleInstance->GetConstraint()) {
                            if (settings.PhysicsDrawSettings.DrawBoundingBoxes) {
                                glm::mat4 chassisXform = rvd.VehicleInstance->GetChassisTransform();
                                m_PhysicsDebugRenderer->DrawTransform(chassisXform, 1.5f);
                            }
                        }
                    }
                }

                m_PhysicsDebugRenderer->End(m_CommandBuffer);
                m_LastDrawCallCount += m_PhysicsDebugRenderer->GetDrawCallCount();
            }
        }

        // 7. End Scene Rendering Pass
        m_CommandBuffer.EndRendering();

        // Transition scene color target back to ShaderResource
        m_CommandBuffer.TransitionTexture(sceneTargetTexture, RHIResourceLayout::ColorAttachment, RHIResourceLayout::ShaderResource);
        if (sceneDepthTexture) {
            m_CommandBuffer.TransitionTexture(sceneDepthTexture, RHIResourceLayout::DepthStencilWrite, RHIResourceLayout::DepthStencilRead);
        }

        // 7. Resolve Hardware MSAA if enabled
        if (useMSAA && m_MSAAColorTarget && m_MSAAColorTarget->IsValid()) {
            nri::Texture* resolveDstTexture = colorTarget;
            if (useToneMap) {
                if (!m_IntermediateHDRTarget || m_IntermediateHDRTarget->GetWidth() != width || m_IntermediateHDRTarget->GetHeight() != height) {
                    m_IntermediateHDRTarget = std::make_unique<RHITexture>();
                    m_IntermediateHDRTarget->Init(RHITextureDesc::RenderTarget2D(width, height, RHITextureFormat::RGBA16_FLOAT, "SceneRenderer_IntermediateHDR"));
                }
                resolveDstTexture = m_IntermediateHDRTarget->GetNative();
            } else if (useFXAA) {
                if (!m_IntermediateFXAATarget || m_IntermediateFXAATarget->GetWidth() != width || m_IntermediateFXAATarget->GetHeight() != height) {
                    m_IntermediateFXAATarget = std::make_unique<RHITexture>();
                    m_IntermediateFXAATarget->Init(RHITextureDesc::RenderTarget2D(width, height, RHI::FromNRIFormat(m_CurrentColorFormat), "SceneRenderer_IntermediateFXAA"));
                }
                resolveDstTexture = m_IntermediateFXAATarget->GetNative();
            }

            m_CommandBuffer.TransitionTexture(m_MSAAColorTarget->GetNative(), RHIResourceLayout::ShaderResource, RHIResourceLayout::ResolveSource);
            m_CommandBuffer.TransitionTexture(resolveDstTexture, RHIResourceLayout::ShaderResource, RHIResourceLayout::ResolveDest);
            m_CommandBuffer.ResolveTexture(resolveDstTexture, m_MSAAColorTarget->GetNative(), nri::ResolveOp::AVERAGE);
            m_CommandBuffer.TransitionTexture(resolveDstTexture, RHIResourceLayout::ResolveDest, RHIResourceLayout::ShaderResource);
            m_CommandBuffer.TransitionTexture(m_MSAAColorTarget->GetNative(), RHIResourceLayout::ResolveSource, RHIResourceLayout::ShaderResource);
        }

        // 8. Post-Processing Tone Mapping Pass
        if (useToneMap && m_IntermediateHDRTarget && m_IntermediateHDRTarget->IsValid()) {
            RHIToneMappingConstants toneConstants{};
            toneConstants.Params = glm::vec4(settings.Exposure, settings.Gamma, static_cast<float>(settings.ToneMappingMode), settings.WhitePoint);
            toneConstants.ColorGrading = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);

            nri::Texture* toneOutTexture = colorTarget;
            nri::Descriptor* toneOutRTV = colorRTV;
            if (useFXAA) {
                if (!m_IntermediateFXAATarget || m_IntermediateFXAATarget->GetWidth() != width || m_IntermediateFXAATarget->GetHeight() != height) {
                    m_IntermediateFXAATarget = std::make_unique<RHITexture>();
                    m_IntermediateFXAATarget->Init(RHITextureDesc::RenderTarget2D(width, height, RHI::FromNRIFormat(m_CurrentColorFormat), "SceneRenderer_IntermediateFXAA"));
                }
                toneOutTexture = m_IntermediateFXAATarget->GetNative();
                toneOutRTV = m_IntermediateFXAATarget->GetRTV();
            }

            RenderToneMapping(m_CommandBuffer,
                              m_IntermediateHDRTarget->GetSRV(),
                              toneOutTexture,
                              toneOutRTV,
                              width, height,
                              m_CurrentColorFormat,
                              toneConstants);
        }

        // 9. Post-Processing FXAA Pass
        if (useFXAA && m_IntermediateFXAATarget && m_IntermediateFXAATarget->IsValid()) {
            RHIFXAAConstants fxaaConstants{};
            fxaaConstants.ScreenSize = glm::vec4(
                static_cast<float>(width),
                static_cast<float>(height),
                1.0f / static_cast<float>(width),
                1.0f / static_cast<float>(height)
            );
            fxaaConstants.FXAAParams = glm::vec4(settings.FXAASubpixQuality, settings.FXAAEdgeThreshold, settings.FXAAEdgeThresholdMin, 0.0f);

            RenderFXAA(m_CommandBuffer,
                       m_IntermediateFXAATarget->GetSRV(),
                       colorTarget,
                       colorRTV,
                       width, height,
                       m_CurrentColorFormat,
                       fxaaConstants);
        }

        // 8. End and Submit to Queue
        m_CommandBuffer.End();
        auto* queue = m_Device->GetGraphicsQueue();
        m_CommandBuffer.Submit(queue);
        m_Device->GetNRICore().QueueWaitIdle(queue);

        // Reset command allocator to recycle its memory pages for next frame
        m_CommandBuffer.Reset();

        if (m_Streamer) {
            m_Streamer->EndFrame();
        }
    }

    void RHISceneRenderer::RenderToneMapping(RHI::RHICommandBuffer& cmd,
                                            RHI::RHITexture* inputHDR,
                                            RHI::RHITexture* outputTarget,
                                            const RHIToneMappingConstants& constants)
    {
        if (!inputHDR || !inputHDR->IsValid() || !outputTarget || !outputTarget->IsValid()) return;

        RenderToneMapping(cmd,
                          inputHDR->GetSRV(),
                          outputTarget->GetNative(),
                          outputTarget->GetRTV(),
                          outputTarget->GetWidth(),
                          outputTarget->GetHeight(),
                          RHI::ToNRIFormat(outputTarget->GetFormat()),
                          constants);
    }

    void RHISceneRenderer::RenderToneMapping(RHI::RHICommandBuffer& cmd,
                                            RHI::RHITexture* inputHDR,
                                            RHI::RHITexture* outputTarget,
                                            const SceneRenderSettings& settings)
    {
        RHIToneMappingConstants constants{};
        constants.Params = glm::vec4(settings.Exposure, settings.Gamma, static_cast<float>(settings.ToneMappingMode), settings.WhitePoint);
        constants.ColorGrading = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
        RenderToneMapping(cmd, inputHDR, outputTarget, constants);
    }

    void RHISceneRenderer::RenderToneMapping(RHI::RHICommandBuffer& cmd,
                                            nri::Descriptor* inputHDR_SRV,
                                            nri::Texture* outputTarget,
                                            nri::Descriptor* outputRTV,
                                            uint32_t width, uint32_t height,
                                            nri::Format outFormat,
                                            const RHIToneMappingConstants& constants)
    {
        if (!m_Initialized || !inputHDR_SRV || !outputTarget || !outputRTV || width == 0 || height == 0) return;

        // 1. Update 256-byte Tone Mapping Constants
        if (m_ToneMappingConstantBuffer.IsValid()) {
            m_ToneMappingConstantBuffer.UpdateData(&constants, sizeof(RHIToneMappingConstants));
        }

        // 2. Bind HDR Texture SRV & Sampler in Space 1
        if (m_ToneMappingDescriptorPool.IsValid() && m_ToneMappingDescriptorSet) {
            m_ToneMappingDescriptorPool.UpdateTexture(m_ToneMappingDescriptorSet, 0, 0, inputHDR_SRV);
            if (m_ToneMappingSampler.IsValid()) {
                m_ToneMappingDescriptorPool.UpdateSampler(m_ToneMappingDescriptorSet, 1, 0, m_ToneMappingSampler.GetNative());
            }
        }

        // 3. Transition Output Texture for Color Rendering
        cmd.TransitionTexture(outputTarget, RHIResourceLayout::ShaderResource, RHIResourceLayout::ColorAttachment);

        // 4. Begin Rendering Pass
        nri::AttachmentDesc colorAttachment = {};
        colorAttachment.descriptor = outputRTV;
        colorAttachment.loadOp = nri::LoadOp::CLEAR;
        colorAttachment.storeOp = nri::StoreOp::STORE;

        nri::RenderingDesc renderingDesc = {};
        renderingDesc.colors = &colorAttachment;
        renderingDesc.colorNum = 1;

        cmd.BeginRendering(renderingDesc);

        nri::Viewport vp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        cmd.SetViewport(vp);

        nri::Rect scissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
        cmd.SetScissor(scissor);

        nri::Pipeline* activePipeline = (outFormat == nri::Format::RGBA16_SFLOAT && m_ToneMappingPipelineHDR)
                                            ? m_ToneMappingPipelineHDR
                                            : (outFormat == nri::Format::RGBA8_UNORM && m_ToneMappingPipelineSDR)
                                                  ? m_ToneMappingPipelineSDR
                                                  : GetOrCreateToneMappingPipeline(outFormat);
        cmd.SetPipeline(activePipeline ? activePipeline : m_ToneMappingPipelineSDR);
        cmd.SetPipelineLayout(nri::BindPoint::GRAPHICS, m_ToneMappingPipelineLayout);

        if (m_ToneMappingConstantBuffer.IsValid() && m_ToneMappingConstantBuffer.GetCBV()) {
            cmd.SetRootDescriptor(0, m_ToneMappingConstantBuffer.GetCBV());
        }

        if (m_ToneMappingDescriptorPool.IsValid() && m_ToneMappingDescriptorSet) {
            m_ToneMappingDescriptorPool.Bind(cmd.GetNative());
            RHIDescriptorPool::BindDescriptorSet(cmd.GetNative(), 0, m_ToneMappingDescriptorSet);
        }

        // Draw full-screen triangle via SV_VertexID
        cmd.Draw(3, 1, 0, 0);

        cmd.EndRendering();

        // Transition back to ShaderResource for presentation
        cmd.TransitionTexture(outputTarget, RHIResourceLayout::ColorAttachment, RHIResourceLayout::ShaderResource);

        m_LastDrawCallCount++;
    }

    void RHISceneRenderer::RenderFXAA(RHI::RHICommandBuffer& cmd,
                                      RHI::RHITexture* inputTexture,
                                      RHI::RHITexture* outputTarget,
                                      const RHIFXAAConstants& constants)
    {
        if (!inputTexture || !inputTexture->IsValid() || !outputTarget || !outputTarget->IsValid()) return;

        RenderFXAA(cmd,
                   inputTexture->GetSRV(),
                   outputTarget->GetNative(),
                   outputTarget->GetRTV(),
                   outputTarget->GetWidth(),
                   outputTarget->GetHeight(),
                   RHI::ToNRIFormat(outputTarget->GetFormat()),
                   constants);
    }

    void RHISceneRenderer::RenderFXAA(RHI::RHICommandBuffer& cmd,
                                      RHI::RHITexture* inputTexture,
                                      RHI::RHITexture* outputTarget,
                                      const SceneRenderSettings& settings)
    {
        if (!outputTarget || !outputTarget->IsValid()) return;
        RHIFXAAConstants constants{};
        constants.ScreenSize = glm::vec4(
            static_cast<float>(outputTarget->GetWidth()),
            static_cast<float>(outputTarget->GetHeight()),
            1.0f / static_cast<float>(outputTarget->GetWidth()),
            1.0f / static_cast<float>(outputTarget->GetHeight())
        );
        constants.FXAAParams = glm::vec4(settings.FXAASubpixQuality, settings.FXAAEdgeThreshold, settings.FXAAEdgeThresholdMin, 0.0f);
        RenderFXAA(cmd, inputTexture, outputTarget, constants);
    }

    void RHISceneRenderer::RenderFXAA(RHI::RHICommandBuffer& cmd,
                                      nri::Descriptor* inputSRV,
                                      nri::Texture* outputTarget,
                                      nri::Descriptor* outputRTV,
                                      uint32_t width, uint32_t height,
                                      nri::Format outFormat,
                                      const RHIFXAAConstants& constants)
    {
        if (!m_Initialized || !inputSRV || !outputTarget || !outputRTV || width == 0 || height == 0) return;

        // 1. Update 256-byte FXAA Constants
        if (m_FXAAConstantBuffer.IsValid()) {
            m_FXAAConstantBuffer.UpdateData(&constants, sizeof(RHIFXAAConstants));
        }

        // 2. Bind Texture SRV & Sampler in Space 1
        if (m_FXAADescriptorPool.IsValid() && m_FXAADescriptorSet) {
            m_FXAADescriptorPool.UpdateTexture(m_FXAADescriptorSet, 0, 0, inputSRV);
            if (m_FXAASampler.IsValid()) {
                m_FXAADescriptorPool.UpdateSampler(m_FXAADescriptorSet, 1, 0, m_FXAASampler.GetNative());
            }
        }

        // 3. Transition Output Texture for Color Rendering
        cmd.TransitionTexture(outputTarget, RHIResourceLayout::ShaderResource, RHIResourceLayout::ColorAttachment);

        // 4. Begin Rendering Pass
        nri::AttachmentDesc colorAttachment = {};
        colorAttachment.descriptor = outputRTV;
        colorAttachment.loadOp = nri::LoadOp::CLEAR;
        colorAttachment.storeOp = nri::StoreOp::STORE;

        nri::RenderingDesc renderingDesc = {};
        renderingDesc.colors = &colorAttachment;
        renderingDesc.colorNum = 1;

        cmd.BeginRendering(renderingDesc);

        nri::Viewport vp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        cmd.SetViewport(vp);

        nri::Rect scissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
        cmd.SetScissor(scissor);

        nri::Pipeline* activePipeline = (outFormat == nri::Format::RGBA16_SFLOAT && m_FXAAPipelineHDR)
                                            ? m_FXAAPipelineHDR
                                            : (outFormat == nri::Format::RGBA8_UNORM && m_FXAAPipelineSDR)
                                                  ? m_FXAAPipelineSDR
                                                  : GetOrCreateFXAAPipeline(outFormat);
        cmd.SetPipeline(activePipeline ? activePipeline : m_FXAAPipelineSDR);
        cmd.SetPipelineLayout(nri::BindPoint::GRAPHICS, m_FXAAPipelineLayout);

        if (m_FXAAConstantBuffer.IsValid() && m_FXAAConstantBuffer.GetCBV()) {
            cmd.SetRootDescriptor(0, m_FXAAConstantBuffer.GetCBV());
        }

        if (m_FXAADescriptorPool.IsValid() && m_FXAADescriptorSet) {
            m_FXAADescriptorPool.Bind(cmd.GetNative());
            RHIDescriptorPool::BindDescriptorSet(cmd.GetNative(), 0, m_FXAADescriptorSet);
        }

        // Draw full-screen triangle via SV_VertexID
        cmd.Draw(3, 1, 0, 0);

        cmd.EndRendering();

        // Transition back to ShaderResource for presentation
        cmd.TransitionTexture(outputTarget, RHIResourceLayout::ColorAttachment, RHIResourceLayout::ShaderResource);

        m_LastDrawCallCount++;
    }

    uint32_t RHISceneRenderer::PickEntity(const std::shared_ptr<Scene>& scene, const PerspectiveCamera& camera,
                                          uint32_t width, uint32_t height, uint32_t x, uint32_t y) {
        if (!m_ObjectPicker || !m_ObjectPicker->IsInitialized()) {
            return RHIObjectPicker::InvalidEntityID;
        }
        return m_ObjectPicker->Pick(scene, camera, width, height, x, y);
    }

}
