#pragma once

#include <Axle/Types.hpp>
#include <Axle/Shader.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/ParticlePool.hpp>
#include <Axle/ParticleEmitter.hpp>
#include <Axle/ParticleSystem.hpp>
#include <memory>
#include <vector>

namespace Axle {

    struct ParticleInstanceVertex {
        glm::vec3 Position = glm::vec3(0.0f);
        float Size = 1.0f;
        glm::vec4 Color = glm::vec4(1.0f);
        float Rotation = 0.0f;
        int Orientation = 0; // 0=CameraFacing, 1=AxialY, 2=VelocityAligned
        glm::vec3 Velocity = glm::vec3(0.0f);
        glm::vec4 UVOffsetScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    };

    class ParticleRenderer {
    public:
        static constexpr uint32_t MAX_PARTICLES_PER_BATCH = 20000;

        ParticleRenderer();
        ~ParticleRenderer();

        void Init();
        void Shutdown();

        // Begin batch rendering pass with camera
        void Begin(const PerspectiveCamera& camera,
                   const std::shared_ptr<Framebuffer>& sceneFBO = nullptr,
                   float cameraNear = 0.1f,
                   float cameraFar = 1000.0f);

        // End batch rendering pass and restore OpenGL state
        void End();

        // Render an active ParticleSystem
        void Render(const ParticleSystem& system,
                    const std::shared_ptr<Texture2D>& texture = nullptr,
                    float softnessDistance = 0.5f);

        // Render directly from ParticlePool & ParticleEmitterConfig
        void Render(const ParticlePool& pool,
                    const ParticleEmitterConfig& config,
                    const std::shared_ptr<Texture2D>& texture = nullptr,
                    float softnessDistance = 0.5f);

        // Accessors
        std::shared_ptr<Shader> GetShader() const { return m_Shader; }
        std::shared_ptr<VertexArray> GetVAO() const { return m_VAO; }
        bool IsInitialized() const { return m_Initialized; }
        uint32_t GetDrawCallCount() const { return m_DrawCallCount; }
        uint32_t GetRenderedParticleCount() const { return m_RenderedParticleCount; }

    private:
        void CreateBuffers();
        void LoadShaders();
        void ApplyBlendMode(ParticleBlendMode mode);

    private:
        std::shared_ptr<Shader> m_Shader;
        std::shared_ptr<VertexArray> m_VAO;
        std::shared_ptr<VertexBuffer> m_QuadVBO;
        std::shared_ptr<VertexBuffer> m_InstanceVBO;
        std::shared_ptr<IndexBuffer> m_QuadIBO;

        std::vector<ParticleInstanceVertex> m_InstanceStagingBuffer;

        // Active pass context
        glm::mat4 m_ViewMatrix = glm::mat4(1.0f);
        glm::mat4 m_ProjectionMatrix = glm::mat4(1.0f);
        glm::mat4 m_ViewProjection = glm::mat4(1.0f);
        glm::vec3 m_CameraPos = glm::vec3(0.0f);
        glm::vec3 m_CameraRight = glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 m_CameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

        std::shared_ptr<Framebuffer> m_SceneFBO;
        float m_CameraNear = 0.1f;
        float m_CameraFar = 1000.0f;
        glm::vec2 m_ScreenSize = glm::vec2(1280.0f, 720.0f);

        bool m_InBatch = false;
        bool m_Initialized = false;
        uint32_t m_DrawCallCount = 0;
        uint32_t m_RenderedParticleCount = 0;
    };

}
