#pragma once

#include <Axle/Types.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/ParticlePool.hpp>
#include <Axle/ParticleEmitter.hpp>
#include <Axle/ParticleSystem.hpp>
#include <Axle/ParticleMaterialAdapter.hpp>
#include <Axle/ParticleRenderer.hpp>

#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHISampler.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHIShader.hpp>

#include <memory>
#include <algorithm>
#include <vector>
#include <glm/glm.hpp>

namespace Axle {

    // =========================================================================
    // RHIParticleConstants: hardware-aligned constant buffer (CBV)
    // =========================================================================
    struct alignas(256) RHIParticleConstants {
        glm::mat4 ViewProjection = glm::mat4(1.0f);        // 64 bytes (offset 0)
        glm::mat4 View = glm::mat4(1.0f);                  // 64 bytes (offset 64)
        glm::mat4 Projection = glm::mat4(1.0f);            // 64 bytes (offset 128)
        glm::vec4 CameraPosition = glm::vec4(0.0f);        // 16 bytes (xyz = CamPos, w = CameraNear) (offset 192)
        glm::vec4 CameraRight = glm::vec4(1.0f, 0.0f, 0.0f, 1000.0f); // 16 bytes (xyz = CamRight, w = CameraFar) (offset 208)
        glm::vec4 CameraUp = glm::vec4(0.0f, 1.0f, 0.0f, 0.5f);       // 16 bytes (xyz = CamUp, w = SoftnessDistance) (offset 224)
        glm::vec4 SunDirection = glm::vec4(0.5f, 1.0f, 0.3f, 0.2f);   // 16 bytes (xyz = SunDir, w = WrapLighting) (offset 240)

        glm::vec4 SunColor = glm::vec4(1.0f, 0.95f, 0.9f, 0.5f);       // 16 bytes (rgb = SunColor, w = ScatteringFactor) (offset 256)
        glm::vec4 AmbientColor = glm::vec4(0.25f, 0.28f, 0.32f, 1.0f); // 16 bytes (rgb = AmbientColor, w = NormalMapStrength) (offset 272)
        glm::vec4 ScreenSize = glm::vec4(1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f); // 16 bytes (offset 288)
        glm::vec4 FadeParams = glm::vec4(0.5f, 2.0f, 50.0f, 100.0f);   // 16 bytes (x=NearStart, y=NearEnd, z=FarStart, w=FarEnd) (offset 304)
        glm::uvec4 Flags = glm::uvec4(0);                  // 16 bytes (x=HasTex, y=SubFrame, z=HasNormal, w=Lighting) (offset 320)
        glm::uvec4 Flags2 = glm::uvec4(0);                 // 16 bytes (x=NearFade, y=FarFade, z=SoftParticles, w=LightCount) (offset 336)
        glm::vec4 PointLightPositions[4]{};                // 64 bytes (xyz = Position, w = Radius) (offset 352..416)
        glm::vec4 PointLightColors[4]{};                   // 64 bytes (rgb = Color, w = Intensity) (offset 416..480)
        glm::vec4 CutoutAndMaterial = glm::vec4(0.0f);     // 16 bytes (x=AlphaCutoff, y=AuthoredSoftness, z=Reserved, w=IsCutoutMode) (offset 480..496)
        glm::vec4 UVTransform{1,1,0,0}; // offset 496
        glm::vec4 MaterialTint{1}; // offset 512
        glm::vec4 MaterialEmission{0}; // RGB and intensity, offset 528
        glm::vec4 Reserved = glm::vec4(0.0f);              // 16 bytes reserved (offset 544..560); allocation padded to 768 bytes
    };
    static_assert(sizeof(RHIParticleConstants) == 768, "RHIParticleConstants must be exactly 768 bytes (256-byte aligned)!");

    // =========================================================================
    // ParticleRenderPlanner: CPU Instance Packing, Transform & Depth Sorting
    // =========================================================================
    struct ParticleDrawPacket {
        std::string EmitterName;
        ParticleBlendMode BlendMode = ParticleBlendMode::AlphaBlend;
        nri::Format ColorFormat = nri::Format::RGBA8_UNORM;
        uint32_t InstanceOffset = 0;
        uint32_t InstanceCount = 0;
        std::vector<ParticleInstanceVertex> Instances;
        RHIParticleConstants Constants{};
    };

    class ParticleRenderPlanner {
    public:
        static void BuildDrawPackets(
            const ParticlePool& pool,
            const ParticleEmitterConfig& config,
            const PerspectiveCamera& camera,
            const glm::vec3& emitterWorldPos,
            const glm::quat& emitterWorldRot,
            std::vector<ParticleDrawPacket>& outPackets,
            uint32_t maxBatchSize = 20000,
            const glm::vec3& emitterWorldScale = glm::vec3(1)
        );
    };

    struct ParticleDrawResources {
        RHI::RHIBuffer Instances;
        RHI::RHIBuffer Constants;
        RHI::RHIDescriptorPool Pool;
        nri::DescriptorSet* Set = nullptr;
    };

    // =========================================================================
    // RHIParticleRenderer: Multi-Backend GPU Instanced Particle VFX Renderer
    // =========================================================================
    class RHIParticleRenderer {
    public:
        static constexpr uint32_t MAX_PARTICLES_PER_BATCH = 20000;

        RHIParticleRenderer();
        ~RHIParticleRenderer();

        RHIParticleRenderer(const RHIParticleRenderer&) = delete;
        RHIParticleRenderer& operator=(const RHIParticleRenderer&) = delete;

        // Initialize shader compilation, pipeline layouts, and graphics pipelines
        bool Init(RHI::RHIDevice& device = RHI::RHIDevice::Get());

        // Release GPU resources
        void Shutdown();

        bool IsInitialized() const { return m_Initialized; }
        bool IsValid() const { return m_Initialized; }

        // Begin batch rendering pass with camera & target settings
        void Begin(RHI::RHICommandBuffer& cmd,
                   const PerspectiveCamera& camera,
                   nri::Format colorFormat = nri::Format::RGBA8_UNORM,
                   nri::Format depthFormat = nri::Format::D32_SFLOAT,
                   uint8_t sampleCount = 1);

        void SetSceneDepth(RHI::RHITexture* depth, uint32_t width, uint32_t height) {
            m_SceneDepth = depth && depth->IsValid() ? depth->GetSRV() : nullptr;
            m_ActiveConstants.ScreenSize = glm::vec4(width, height, 1.0f/std::max(width,1u), 1.0f/std::max(height,1u));
        }

        // End batch rendering pass
        void End();

        // Render an active ParticleSystem
        void Render(RHI::RHICommandBuffer& cmd,
                    const ParticleSystem& system,
                    RHI::RHITexture* texture = nullptr,
                    RHI::RHITexture* normalMap = nullptr,
                    float softnessDistance = 0.5f,
                    const glm::vec3& emitterWorldPos = glm::vec3(0.0f),
                    const glm::quat& emitterWorldRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                    const glm::vec3& emitterWorldScale = glm::vec3(1),
                    const EffectiveParticleRenderSettings* material = nullptr);

        // Render directly from ParticlePool & ParticleEmitterConfig
        void Render(RHI::RHICommandBuffer& cmd,
                    const ParticlePool& pool,
                    const ParticleEmitterConfig& config,
                    RHI::RHITexture* texture = nullptr,
                    RHI::RHITexture* normalMap = nullptr,
                    float softnessDistance = 0.5f,
                    const glm::vec3& emitterWorldPos = glm::vec3(0.0f),
                    const glm::quat& emitterWorldRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                    const glm::vec3& emitterWorldScale = glm::vec3(1),
                    const EffectiveParticleRenderSettings* material = nullptr);

        // Render with native NRI descriptors (for bindless / shared textures)
        void Render(RHI::RHICommandBuffer& cmd,
                    const ParticlePool& pool,
                    const ParticleEmitterConfig& config,
                    nri::Descriptor* textureSRV,
                    nri::Descriptor* normalMapSRV,
                    float softnessDistance = 0.5f,
                    const glm::vec3& emitterWorldPos = glm::vec3(0.0f),
                    const glm::quat& emitterWorldRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                    const glm::vec3& emitterWorldScale = glm::vec3(1),
                    const EffectiveParticleRenderSettings* material = nullptr);

        // Environment lighting
        void SetLightEnvironment(const ParticleLightEnvironment& env) { m_LightEnvironment = env; }
        const ParticleLightEnvironment& GetLightEnvironment() const { return m_LightEnvironment; }
        ParticleLightEnvironment& GetLightEnvironment() { return m_LightEnvironment; }

        // Pipeline retrieval & dynamic PSO caching
        nri::Pipeline* GetOrCreatePipeline(ParticleBlendMode blendMode,
                                           nri::Format colorFormat,
                                           nri::Format depthFormat = nri::Format::D32_SFLOAT,
                                           uint8_t sampleCount = 1);

        // Accessors & Telemetry
        RHI::RHIPipelineLayout& GetPipelineLayout() { return m_PipelineLayout; }
        RHI::RHIBuffer& GetConstantBuffer() { return m_ConstantBuffer; }
        RHI::RHIBuffer& GetInstanceBuffer() { return m_InstanceBuffer; }
        uint32_t GetDrawCallCount() const { return m_DrawCallCount; }
        uint32_t GetRenderedParticleCount() const { return m_RenderedParticleCount; }
        void ResetMetrics() { m_DrawCallCount = 0; m_RenderedParticleCount = 0; }

    private:
        bool CompileShaders(RHI::RHIDevice& device);
        bool CreatePipelineLayout(RHI::RHIDevice& device);
        bool CreatePipelines(RHI::RHIDevice& device);
        bool CreateBuffers(RHI::RHIDevice& device);
        ParticleDrawResources *AcquireDrawResources(uint32_t instanceCount);

    private:
        RHI::RHIDevice* m_Device = nullptr;
        bool m_Initialized = false;
        bool m_InBatch = false;
        nri::Descriptor* m_SceneDepth = nullptr;

        RHI::RHIShaderBytecode m_VertexShader;
        RHI::RHIShaderBytecode m_PixelShader;
        RHI::RHIPipelineLayout m_PipelineLayout;

        // Default PSOs (AlphaBlend, Additive, Multiply, Cutout in SDR & HDR formats)
        nri::Pipeline* m_PipelineAlphaBlendSDR = nullptr;
        nri::Pipeline* m_PipelineAlphaBlendHDR = nullptr;
        nri::Pipeline* m_PipelineAdditiveSDR = nullptr;
        nri::Pipeline* m_PipelineAdditiveHDR = nullptr;
        nri::Pipeline* m_PipelineCutoutSDR = nullptr;
        nri::Pipeline* m_PipelineCutoutHDR = nullptr;

        // GPU Buffers
        RHI::RHIBuffer m_ConstantBuffer;
        RHI::RHIBuffer m_InstanceBuffer;
        std::vector<std::unique_ptr<ParticleDrawResources>> m_DrawResources;
        size_t m_DrawResourceCursor = 0;
        RHI::RHISampler m_LinearSampler;
        RHI::RHIDescriptorPool m_DescriptorPool;
        nri::DescriptorSet* m_DescriptorSet = nullptr;

        // Active pass context
        RHIParticleConstants m_ActiveConstants{};
        nri::Format m_ActiveColorFormat = nri::Format::RGBA8_UNORM;
        nri::Format m_ActiveDepthFormat = nri::Format::D32_SFLOAT;
        uint8_t m_ActiveSampleCount = 1;

        std::vector<ParticleInstanceVertex> m_InstanceStagingBuffer;
        ParticleLightEnvironment m_LightEnvironment;

        uint32_t m_DrawCallCount = 0;
        uint32_t m_RenderedParticleCount = 0;
    };

}
