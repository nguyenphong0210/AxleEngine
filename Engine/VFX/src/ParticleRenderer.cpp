#include <Axle/ParticleRenderer.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Axle {

    ParticleRenderer::ParticleRenderer() {
    }

    ParticleRenderer::~ParticleRenderer() {
        Shutdown();
    }

    void ParticleRenderer::Init() {
        if (m_Initialized) return;

        LoadShaders();
        CreateBuffers();

        m_InstanceStagingBuffer.reserve(MAX_PARTICLES_PER_BATCH);
        m_Initialized = true;
        AXLE_CORE_INFO("[ParticleRenderer] GPU Instancing Pipeline initialized successfully (Capacity: {0} particles/batch)", MAX_PARTICLES_PER_BATCH);
    }

    void ParticleRenderer::Shutdown() {
        m_Shader.reset();
        m_VAO.reset();
        m_QuadVBO.reset();
        m_InstanceVBO.reset();
        m_QuadIBO.reset();
        m_InstanceStagingBuffer.clear();
        m_Initialized = false;
    }

    void ParticleRenderer::CreateBuffers() {
        // 1. Static Unit Quad Geometry [-0.5, 0.5]
        float quadVertices[] = {
            -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
             0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
             0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f,  0.0f, 1.0f
        };

        m_QuadVBO = std::make_shared<VertexBuffer>(quadVertices, static_cast<u32>(sizeof(quadVertices)));
        m_QuadVBO->SetLayout({
            { ShaderDataType::Float3, "a_QuadPos" },
            { ShaderDataType::Float2, "a_QuadTexCoord" }
        });

        uint32_t quadIndices[] = { 0, 1, 2, 2, 3, 0 };
        m_QuadIBO = std::make_shared<IndexBuffer>(quadIndices, 6);

        // 2. Dynamic Instance Vertex Buffer
        m_InstanceVBO = std::make_shared<VertexBuffer>(static_cast<u32>(MAX_PARTICLES_PER_BATCH * sizeof(ParticleInstanceVertex)));
        m_InstanceVBO->SetLayout({
            { ShaderDataType::Float3, "a_InstancePosition" },
            { ShaderDataType::Float,  "a_InstanceSize" },
            { ShaderDataType::Float4, "a_InstanceColor" },
            { ShaderDataType::Float,  "a_InstanceRotation" },
            { ShaderDataType::Int,    "a_InstanceOrientation" },
            { ShaderDataType::Float3, "a_InstanceVelocity" },
            { ShaderDataType::Float4, "a_InstanceUVOffsetScale" }
        });

        // 3. Assemble VertexArray
        m_VAO = std::make_shared<VertexArray>();
        m_VAO->AddVertexBuffer(m_QuadVBO);
        m_VAO->AddInstancedVertexBuffer(m_InstanceVBO, 1);
        m_VAO->SetIndexBuffer(m_QuadIBO);
    }

    void ParticleRenderer::LoadShaders() {
        std::string vertSrc;
        std::string fragSrc;

        auto vertPath = FileSystem::ResolveAssetPath("shaders/Particle.vert");
        auto fragPath = FileSystem::ResolveAssetPath("shaders/Particle.frag");

        if (FileSystem::Exists(vertPath) && FileSystem::Exists(fragPath)) {
            vertSrc = FileSystem::ReadTextFile(vertPath);
            fragSrc = FileSystem::ReadTextFile(fragPath);
        }

        if (vertSrc.empty() || fragSrc.empty()) {
            vertSrc = R"(
                #version 450 core
                layout(location = 0) in vec3 a_QuadPos;
                layout(location = 1) in vec2 a_QuadTexCoord;

                layout(location = 2) in vec3 a_InstancePosition;
                layout(location = 3) in float a_InstanceSize;
                layout(location = 4) in vec4 a_InstanceColor;
                layout(location = 5) in float a_InstanceRotation;
                layout(location = 6) in int a_InstanceOrientation;
                layout(location = 7) in vec3 a_InstanceVelocity;
                layout(location = 8) in vec4 a_InstanceUVOffsetScale;

                uniform mat4 u_View;
                uniform mat4 u_Projection;
                uniform mat4 u_ViewProjection;
                uniform vec3 u_CameraPosition;
                uniform vec3 u_CameraRight;
                uniform vec3 u_CameraUp;

                out vec2 v_TexCoord;
                out vec4 v_Color;
                out vec3 v_WorldPos;
                out vec3 v_ViewPos;

                void main() {
                    v_TexCoord = a_InstanceUVOffsetScale.xy + (a_QuadTexCoord * a_InstanceUVOffsetScale.zw);
                    v_Color = a_InstanceColor;

                    float c = cos(a_InstanceRotation);
                    float s = sin(a_InstanceRotation);
                    vec2 rotPos = vec2(
                        a_QuadPos.x * c - a_QuadPos.y * s,
                        a_QuadPos.x * s + a_QuadPos.y * c
                    ) * a_InstanceSize;

                    vec3 worldRight = u_CameraRight;
                    vec3 worldUp = u_CameraUp;

                    if (a_InstanceOrientation == 0) {
                        worldRight = u_CameraRight;
                        worldUp = u_CameraUp;
                    } else if (a_InstanceOrientation == 1) {
                        worldUp = vec3(0.0, 1.0, 0.0);
                        vec3 toCam = u_CameraPosition - a_InstancePosition;
                        toCam.y = 0.0;
                        if (length(toCam) > 0.0001) {
                            toCam = normalize(toCam);
                            worldRight = normalize(cross(worldUp, toCam));
                        } else {
                            worldRight = vec3(1.0, 0.0, 0.0);
                        }
                    } else if (a_InstanceOrientation == 2) {
                        vec3 vel = a_InstanceVelocity;
                        float speed = length(vel);
                        if (speed > 0.001) {
                            vec3 dir = vel / speed;
                            vec3 toCam = normalize(u_CameraPosition - a_InstancePosition);
                            worldRight = normalize(cross(dir, toCam));
                            worldUp = dir;
                            rotPos.y *= max(1.0, speed * 0.25);
                        } else {
                            worldRight = u_CameraRight;
                            worldUp = u_CameraUp;
                        }
                    }

                    vec3 worldPos = a_InstancePosition + (worldRight * rotPos.x) + (worldUp * rotPos.y);
                    v_WorldPos = worldPos;

                    vec4 viewPos = u_View * vec4(worldPos, 1.0);
                    v_ViewPos = viewPos.xyz;

                    gl_Position = u_Projection * viewPos;
                }
            )";

            fragSrc = R"(
                #version 450 core
                layout(location = 0) out vec4 o_Color;

                in vec2 v_TexCoord;
                in vec4 v_Color;
                in vec3 v_WorldPos;
                in vec3 v_ViewPos;

                uniform sampler2D u_ParticleTexture;
                uniform int u_HasTexture;

                uniform sampler2D u_SceneDepthBuffer;
                uniform int u_SoftParticlesEnabled;
                uniform float u_SoftnessDistance;
                uniform vec2 u_ScreenSize;
                uniform float u_CameraNear;
                uniform float u_CameraFar;

                float LinearizeDepth(float depth, float near, float far) {
                    return (2.0 * near * far) / (far + near - (depth * 2.0 - 1.0) * (far - near));
                }

                void main() {
                    vec4 texColor = vec4(1.0);
                    if (u_HasTexture == 1) {
                        texColor = texture(u_ParticleTexture, v_TexCoord);
                    } else {
                        vec2 centerCoord = (v_TexCoord - vec2(0.5)) * 2.0;
                        float distSq = dot(centerCoord, centerCoord);
                        if (distSq > 1.0) {
                            discard;
                        }
                        float alphaMask = smoothstep(1.0, 0.15, distSq);
                        texColor = vec4(1.0, 1.0, 1.0, alphaMask);
                    }

                    vec4 finalColor = v_Color * texColor;
                    if (finalColor.a <= 0.001) {
                        discard;
                    }

                    if (u_SoftParticlesEnabled == 1 && u_ScreenSize.x > 0.0 && u_ScreenSize.y > 0.0) {
                        vec2 screenUV = gl_FragCoord.xy / u_ScreenSize;
                        float sceneDepthRaw = texture(u_SceneDepthBuffer, screenUV).r;
                        float sceneDepthLinear = LinearizeDepth(sceneDepthRaw, u_CameraNear, u_CameraFar);
                        float particleDepthLinear = -v_ViewPos.z;

                        float depthDiff = sceneDepthLinear - particleDepthLinear;
                        if (depthDiff > 0.0) {
                            float softFactor = clamp(depthDiff / max(0.001, u_SoftnessDistance), 0.0, 1.0);
                            finalColor.a *= softFactor;
                        }
                    }

                    o_Color = finalColor;
                }
            )";
        }

        m_Shader = std::make_shared<Shader>(vertSrc, fragSrc);
    }

    void ParticleRenderer::Begin(const PerspectiveCamera& camera,
                                 const std::shared_ptr<Framebuffer>& sceneFBO,
                                 float cameraNear,
                                 float cameraFar) {
        if (!m_Initialized) Init();

        m_InBatch = true;
        m_DrawCallCount = 0;
        m_RenderedParticleCount = 0;
        m_SceneFBO = sceneFBO;
        m_CameraNear = cameraNear;
        m_CameraFar = cameraFar;

        m_CameraPos = camera.GetPosition();
        m_ViewMatrix = camera.GetViewMatrix();
        m_ProjectionMatrix = camera.GetProjectionMatrix();
        m_ViewProjection = camera.GetViewProjectionMatrix();

        glm::mat4 invView = glm::inverse(m_ViewMatrix);
        m_CameraRight = glm::normalize(glm::vec3(invView[0]));
        m_CameraUp = glm::normalize(glm::vec3(invView[1]));

        if (m_SceneFBO) {
            m_ScreenSize = glm::vec2(static_cast<float>(m_SceneFBO->GetWidth()), static_cast<float>(m_SceneFBO->GetHeight()));
        }

        m_Shader->Bind();
        m_Shader->SetMat4("u_View", m_ViewMatrix);
        m_Shader->SetMat4("u_Projection", m_ProjectionMatrix);
        m_Shader->SetMat4("u_ViewProjection", m_ViewProjection);
        m_Shader->SetFloat3("u_CameraPosition", m_CameraPos);
        m_Shader->SetFloat3("u_CameraRight", m_CameraRight);
        m_Shader->SetFloat3("u_CameraUp", m_CameraUp);

        // Rendering state setup for transparent instanced particles
        RenderCommand::SetBlendEnabled(true);
        RenderCommand::SetDepthMask(false); // Do not write to depth buffer to prevent z-fighting
        RenderCommand::SetCullFace(false);  // Billboard quads face both sides
    }

    void ParticleRenderer::End() {
        if (!m_InBatch) return;

        // Restore default OpenGL states
        RenderCommand::SetDepthMask(true);
        RenderCommand::SetBlendAlpha();
        RenderCommand::SetCullFace(true);

        if (m_Shader) {
            m_Shader->Unbind();
        }

        m_InBatch = false;
    }

    void ParticleRenderer::ApplyBlendMode(ParticleBlendMode mode) {
        switch (mode) {
            case ParticleBlendMode::Additive:
                RenderCommand::SetBlendAdditive();
                break;
            case ParticleBlendMode::AlphaBlend:
                RenderCommand::SetBlendAlpha();
                break;
            case ParticleBlendMode::Multiply:
                RenderCommand::SetBlendMultiply();
                break;
        }
    }

    void ParticleRenderer::Render(const ParticleSystem& system,
                                 const std::shared_ptr<Texture2D>& texture,
                                 float softnessDistance) {
        Render(system.GetPool(), system.GetConfig(), texture, softnessDistance);
    }

    void ParticleRenderer::Render(const ParticlePool& pool,
                                 const ParticleEmitterConfig& config,
                                 const std::shared_ptr<Texture2D>& texture,
                                 float softnessDistance) {
        uint32_t activeCount = pool.GetActiveCount();
        if (activeCount == 0 || !m_Shader || !m_VAO) {
            return;
        }

        ApplyBlendMode(config.BlendMode);

        // Texture binding
        if (texture) {
            texture->Bind(0);
            m_Shader->SetInt("u_ParticleTexture", 0);
            m_Shader->SetInt("u_HasTexture", 1);
        } else {
            m_Shader->SetInt("u_HasTexture", 0);
        }

        // Soft Particles depth buffer setup
        if (m_SceneFBO && glad_glBindTexture && glad_glActiveTexture) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_SceneFBO->GetDepthAttachmentRendererID());
            m_Shader->SetInt("u_SceneDepthBuffer", 1);
            m_Shader->SetInt("u_SoftParticlesEnabled", 1);
            m_Shader->SetFloat("u_SoftnessDistance", softnessDistance);
            m_Shader->SetFloat2("u_ScreenSize", m_ScreenSize);
            m_Shader->SetFloat("u_CameraNear", m_CameraNear);
            m_Shader->SetFloat("u_CameraFar", m_CameraFar);
        } else {
            m_Shader->SetInt("u_SoftParticlesEnabled", 0);
        }

        // Compact instance data packing
        uint32_t toRender = std::min(activeCount, MAX_PARTICLES_PER_BATCH);
        if (m_InstanceStagingBuffer.size() < toRender) {
            m_InstanceStagingBuffer.resize(toRender);
        }

        const auto& positions = pool.GetPositions();
        const auto& sizes = pool.GetSizes();
        const auto& colors = pool.GetColors();
        const auto& rotations = pool.GetRotations();
        const auto& velocities = pool.GetVelocities();
        const auto& uvs = pool.GetUVOffsetScales();
        int orientation = static_cast<int>(config.Orientation);

        for (uint32_t i = 0; i < toRender; ++i) {
            m_InstanceStagingBuffer[i].Position = positions[i];
            m_InstanceStagingBuffer[i].Size = sizes[i];
            m_InstanceStagingBuffer[i].Color = colors[i];
            m_InstanceStagingBuffer[i].Rotation = rotations[i];
            m_InstanceStagingBuffer[i].Orientation = orientation;
            m_InstanceStagingBuffer[i].Velocity = velocities[i];
            m_InstanceStagingBuffer[i].UVOffsetScale = uvs[i];
        }

        // Upload instance buffer
        m_InstanceVBO->SetData(m_InstanceStagingBuffer.data(), static_cast<u32>(toRender * sizeof(ParticleInstanceVertex)));

        // Submit hardware instanced draw call
        RenderCommand::DrawIndexedInstanced(m_VAO, toRender);

        m_DrawCallCount++;
        m_RenderedParticleCount += toRender;
    }

}
