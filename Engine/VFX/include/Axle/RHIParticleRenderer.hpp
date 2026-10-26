#pragma once

#include <Axle/Types.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/ParticlePool.hpp>
#include <Axle/ParticleEmitter.hpp>
#include <Axle/ParticleSystem.hpp>
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
#include <vector>
#include <glm/glm.hpp>

namespace Axle {

    // =========================================================================
    // RHIParticleConstants: 512-Byte Hardware Aligned Constant Buffer (CBV)
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
        glm::vec4 Reserved[2]{};                           // 32 bytes padding -> Total 512 bytes (offset 480..512)
    };
    static_assert(sizeof(RHIParticleConstants) == 512, "RHIParticleConstants must be exactly 512 bytes (256-byte aligned)!");

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

        // End batch rendering pass
        void End();

        // Render an active ParticleSystem
        void Render(RHI::RHICommandBuffer& cmd,
                    const ParticleSystem& system,
                    RHI::RHITexture* texture = nullptr,
                    RHI::RHITexture* normalMap = nullptr,
                    float softnessDistance = 0.5f);

        // Render directly from ParticlePool & ParticleEmitterConfig
        void Render(RHI::RHICommandBuffer& cmd,
                    const ParticlePool& pool,
                    const ParticleEmitterConfig& config,
                    RHI::RHITexture* texture = nullptr,
                    RHI::RHITexture* normalMap = nullptr,
                    float softnessDistance = 0.5f);

        // Render with native NRI descriptors (for bindless / shared textures)
        void Render(RHI::RHICommandBuffer& cmd,
                    const ParticlePool& pool,
                    const ParticleEmitterConfig& config,
                    nri::Descriptor* textureSRV,
                    nri::Descriptor* normalMapSRV,
                    float softnessDistance = 0.5f);

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

    private:
        RHI::RHIDevice* m_Device = nullptr;
        bool m_Initialized = false;
        bool m_InBatch = false;

        RHI::RHIShaderBytecode m_VertexShader;
        RHI::RHIShaderBytecode m_PixelShader;
        RHI::RHIPipelineLayout m_PipelineLayout;

        // Default PSOs (AlphaBlend, Additive, Multiply in SDR & HDR formats)
        nri::Pipeline* m_PipelineAlphaBlendSDR = nullptr;
        nri::Pipeline* m_PipelineAlphaBlendHDR = nullptr;
        nri::Pipeline* m_PipelineAdditiveSDR = nullptr;
        nri::Pipeline* m_PipelineAdditiveHDR = nullptr;

        // GPU Buffers
        RHI::RHIBuffer m_ConstantBuffer;
        RHI::RHIBuffer m_InstanceBuffer;
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
