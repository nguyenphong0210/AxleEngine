#include <Axle/ParticleRenderer.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Window.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Axle {

    ParticleRenderer::ParticleRenderer() {
        Init();
    }

    ParticleRenderer::~ParticleRenderer() {
        Shutdown();
    }

    void ParticleRenderer::Init() {
        if (m_Initialized) return;
        if (!Window::HasActiveContext()) return;

        LoadShaders();
        CreateBuffers();

        m_InstanceStagingBuffer.reserve(MAX_PARTICLES_PER_BATCH);
        m_Initialized = true;
        AXLE_CORE_INFO("[ParticleRenderer] GPU Instancing Pipeline initialized successfully (Capacity: {0} particles/batch, Stride: {1} bytes)",
                       MAX_PARTICLES_PER_BATCH, sizeof(ParticleInstanceVertex));
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

        // 2. Dynamic Instance Vertex Buffer (84-byte layout)
        m_InstanceVBO = std::make_shared<VertexBuffer>(static_cast<u32>(MAX_PARTICLES_PER_BATCH * sizeof(ParticleInstanceVertex)));
        m_InstanceVBO->SetLayout({
            { ShaderDataType::Float3, "a_InstancePosition" },
            { ShaderDataType::Float,  "a_InstanceSize" },
            { ShaderDataType::Float4, "a_InstanceColor" },
            { ShaderDataType::Float,  "a_InstanceRotation" },
            { ShaderDataType::Int,    "a_InstanceOrientation" },
            { ShaderDataType::Float3, "a_InstanceVelocity" },
            { ShaderDataType::Float4, "a_InstanceUVOffsetScale" },
            { ShaderDataType::Float4, "a_InstanceUVSubFrame" }
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
                layout(location = 9) in vec4 a_InstanceUVSubFrame;

                uniform mat4 u_View;
                uniform mat4 u_Projection;
                uniform mat4 u_ViewProjection;
                uniform vec3 u_CameraPosition;
                uniform vec3 u_CameraRight;
                uniform vec3 u_CameraUp;

                out vec2 v_TexCoord;
                out vec2 v_TexCoordNext;
                out float v_SubFrameBlend;
                out vec2 v_QuadCoord;
                out vec4 v_Color;
                out vec3 v_WorldPos;
                out vec3 v_ViewPos;
                out vec3 v_Normal;
                out float v_LightingFlag;

                void main() {
                    v_TexCoord = a_InstanceUVOffsetScale.xy + (a_QuadTexCoord * a_InstanceUVOffsetScale.zw);
                    v_TexCoordNext = a_InstanceUVSubFrame.xy + (a_QuadTexCoord * a_InstanceUVOffsetScale.zw);
                    v_SubFrameBlend = a_InstanceUVSubFrame.z;
                    v_LightingFlag = a_InstanceUVSubFrame.w;
                    v_QuadCoord = a_QuadTexCoord;
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

                    vec3 forward = normalize(cross(worldRight, worldUp));
                    vec2 d = (a_QuadTexCoord - vec2(0.5)) * 2.0;
                    float r2 = dot(d, d);
                    vec3 sphereNormalLocal = vec3(d.x, d.y, sqrt(max(0.0, 1.0 - r2)));
                    v_Normal = normalize(worldRight * sphereNormalLocal.x + worldUp * sphereNormalLocal.y + forward * sphereNormalLocal.z);

                    gl_Position = u_Projection * viewPos;
                }
            )";

            fragSrc = R"(
                #version 450 core
                layout(location = 0) out vec4 o_Color;

                in vec2 v_TexCoord;
                in vec2 v_TexCoordNext;
                in float v_SubFrameBlend;
                in vec2 v_QuadCoord;
                in vec4 v_Color;
                in vec3 v_WorldPos;
                in vec3 v_ViewPos;
                in vec3 v_Normal;
                in float v_LightingFlag;

                uniform sampler2D u_ParticleTexture;
                uniform int u_HasTexture;
                uniform int u_SubFrameBlendingEnabled;

                uniform sampler2D u_ParticleNormalMap;
                uniform int u_HasNormalMap;
                uniform float u_NormalMapStrength;
                uniform int u_LightingEnabled;

                uniform vec3 u_CameraPosition;
                uniform vec3 u_SunDirection;
                uniform vec3 u_SunColor;
                uniform vec3 u_AmbientColor;
                uniform float u_WrapLighting;
                uniform float u_ScatteringFactor;

                uniform int u_PointLightCount;
                uniform vec3 u_PointLightPositions[4];
                uniform vec3 u_PointLightColors[4];
                uniform float u_PointLightRadii[4];
                uniform float u_PointLightIntensities[4];

                uniform int u_NearFadeEnabled;
                uniform float u_NearFadeStart;
                uniform float u_NearFadeEnd;

                uniform int u_FarFadeEnabled;
                uniform float u_FarFadeStart;
                uniform float u_FarFadeEnd;

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
                        vec4 colA = texture(u_ParticleTexture, v_TexCoord);
                        if (u_SubFrameBlendingEnabled == 1 && v_SubFrameBlend > 0.0) {
                            vec4 colB = texture(u_ParticleTexture, v_TexCoordNext);
                            texColor = mix(colA, colB, v_SubFrameBlend);
                        } else {
                            texColor = colA;
                        }
                    } else {
                        vec2 centerCoord = (v_QuadCoord - vec2(0.5)) * 2.0;
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

                    if (u_LightingEnabled == 1 && v_LightingFlag > 0.5) {
                        vec3 N = normalize(v_Normal);
                        if (u_HasNormalMap == 1) {
                            vec3 normalTex = texture(u_ParticleNormalMap, v_TexCoord).xyz * 2.0 - 1.0;
                            N = normalize(mix(N, normalTex, clamp(u_NormalMapStrength, 0.0, 1.0)));
                        }

                        float NdotL = dot(N, normalize(u_SunDirection));
                        float wrap = max(0.0, u_WrapLighting);
                        float wrappedDiff = clamp((NdotL + wrap) / (1.0 + wrap), 0.0, 1.0);

                        vec3 viewDir = normalize(u_CameraPosition - v_WorldPos);
                        float VdotL = max(0.0, dot(-viewDir, normalize(u_SunDirection)));
                        float forwardScatter = pow(VdotL, 4.0) * u_ScatteringFactor;

                        vec3 totalLight = u_AmbientColor + (u_SunColor * (wrappedDiff + forwardScatter));

                        for (int i = 0; i < u_PointLightCount && i < 4; ++i) {
                            vec3 lightVec = u_PointLightPositions[i] - v_WorldPos;
                            float dist = length(lightVec);
                            float radius = u_PointLightRadii[i];
                            if (dist < radius) {
                                vec3 L = lightVec / max(0.0001, dist);
                                float atten = clamp(1.0 - (dist / radius), 0.0, 1.0);
                                atten *= atten;
                                float pDiff = clamp((dot(N, L) + wrap) / (1.0 + wrap), 0.0, 1.0);
                                totalLight += u_PointLightColors[i] * (pDiff * atten * u_PointLightIntensities[i]);
                            }
                        }

                        finalColor.rgb *= totalLight;
                    }

                    float camDist = -v_ViewPos.z;
                    if (u_NearFadeEnabled == 1) {
                        float nearFade = smoothstep(u_NearFadeStart, u_NearFadeEnd, camDist);
                        finalColor.a *= nearFade;
                    }

                    if (u_FarFadeEnabled == 1) {
                        float farFade = 1.0 - smoothstep(u_FarFadeStart, u_FarFadeEnd, camDist);
                        finalColor.a *= farFade;
                    }

                    if (u_SoftParticlesEnabled == 1 && u_ScreenSize.x > 0.0 && u_ScreenSize.y > 0.0) {
                        vec2 screenUV = gl_FragCoord.xy / u_ScreenSize;
                        float sceneDepthRaw = texture(u_SceneDepthBuffer, screenUV).r;
                        float sceneDepthLinear = LinearizeDepth(sceneDepthRaw, u_CameraNear, u_CameraFar);

                        float depthDiff = sceneDepthLinear - camDist;
                        if (depthDiff > 0.0) {
                            float rawFactor = clamp(depthDiff / max(0.001, u_SoftnessDistance), 0.0, 1.0);
                            float softFactor = smoothstep(0.0, 1.0, rawFactor);
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
        RenderCommand::SetDepthMask(false);
        RenderCommand::SetCullFace(false);
    }

    void ParticleRenderer::End() {
        if (!m_InBatch) return;

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
                                 const std::shared_ptr<Texture2D>& normalMap,
                                 float softnessDistance) {
        Render(system.GetPool(), system.GetConfig(), texture, normalMap, softnessDistance);
    }

    void ParticleRenderer::Render(const ParticlePool& pool,
                                 const ParticleEmitterConfig& config,
                                 const std::shared_ptr<Texture2D>& texture,
                                 const std::shared_ptr<Texture2D>& normalMap,
                                 float softnessDistance) {
        uint32_t activeCount = pool.GetActiveCount();
        if (activeCount == 0 || !m_Shader || !m_VAO) {
            return;
        }

        ApplyBlendMode(config.BlendMode);

        // 1. Texture Binding (Slot 0: Base / Flipbook, Slot 1: Normal Map)
        if (texture) {
            texture->Bind(0);
            m_Shader->SetInt("u_ParticleTexture", 0);
            m_Shader->SetInt("u_HasTexture", 1);
        } else {
            m_Shader->SetInt("u_HasTexture", 0);
        }

        if (normalMap) {
            normalMap->Bind(1);
            m_Shader->SetInt("u_ParticleNormalMap", 1);
            m_Shader->SetInt("u_HasNormalMap", 1);
            m_Shader->SetFloat("u_NormalMapStrength", config.NormalMapStrength);
        } else {
            m_Shader->SetInt("u_HasNormalMap", 0);
        }

        // 2. Soft Particles depth buffer setup (Slot 2)
        float effSoftDist = (softnessDistance > 0.0f) ? softnessDistance : config.SoftnessDistance;
        if (m_SceneFBO && glad_glBindTexture && glad_glActiveTexture) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, m_SceneFBO->GetDepthAttachmentRendererID());
            m_Shader->SetInt("u_SceneDepthBuffer", 2);
            m_Shader->SetInt("u_SoftParticlesEnabled", 1);
            m_Shader->SetFloat("u_SoftnessDistance", effSoftDist);
            m_Shader->SetFloat2("u_ScreenSize", m_ScreenSize);
            m_Shader->SetFloat("u_CameraNear", m_CameraNear);
            m_Shader->SetFloat("u_CameraFar", m_CameraFar);
        } else {
            m_Shader->SetInt("u_SoftParticlesEnabled", 0);
        }

        // 3. Sub-Frame Blending Uniforms
        m_Shader->SetInt("u_SubFrameBlendingEnabled", config.SubFrameBlending ? 1 : 0);

        // 4. Volumetric Lighting Uniforms
        m_Shader->SetInt("u_LightingEnabled", config.LightingEnabled ? 1 : 0);
        m_Shader->SetFloat("u_WrapLighting", config.WrapLighting);
        m_Shader->SetFloat("u_ScatteringFactor", config.ScatteringFactor);
        m_Shader->SetFloat3("u_SunDirection", m_LightEnvironment.SunDirection);
        m_Shader->SetFloat3("u_SunColor", m_LightEnvironment.SunColor);
        m_Shader->SetFloat3("u_AmbientColor", m_LightEnvironment.AmbientColor);

        int pointLightCount = static_cast<int>(std::min<size_t>(m_LightEnvironment.PointLights.size(), 4));
        m_Shader->SetInt("u_PointLightCount", pointLightCount);
        for (int k = 0; k < pointLightCount; ++k) {
            const auto& pl = m_LightEnvironment.PointLights[k];
            m_Shader->SetFloat3("u_PointLightPositions[" + std::to_string(k) + "]", pl.Position);
            m_Shader->SetFloat3("u_PointLightColors[" + std::to_string(k) + "]", pl.Color);
            m_Shader->SetFloat("u_PointLightRadii[" + std::to_string(k) + "]", pl.Radius);
            m_Shader->SetFloat("u_PointLightIntensities[" + std::to_string(k) + "]", pl.Intensity);
        }

        // 5. Camera Near-Plane & Far Distance Fading
        m_Shader->SetInt("u_NearFadeEnabled", config.NearFadeEnabled ? 1 : 0);
        m_Shader->SetFloat("u_NearFadeStart", config.NearFadeStart);
        m_Shader->SetFloat("u_NearFadeEnd", config.NearFadeEnd);
        m_Shader->SetInt("u_FarFadeEnabled", config.FarFadeEnabled ? 1 : 0);
        m_Shader->SetFloat("u_FarFadeStart", config.FarFadeStart);
        m_Shader->SetFloat("u_FarFadeEnd", config.FarFadeEnd);

        // 6. Compact instance data packing
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
        const auto& subFrames = pool.GetUVSubFrames();
        int orientation = static_cast<int>(config.Orientation);

        for (uint32_t i = 0; i < toRender; ++i) {
            m_InstanceStagingBuffer[i].Position = positions[i];
            m_InstanceStagingBuffer[i].Size = sizes[i];
            m_InstanceStagingBuffer[i].Color = colors[i];
            m_InstanceStagingBuffer[i].Rotation = rotations[i];
            m_InstanceStagingBuffer[i].Orientation = orientation;
            m_InstanceStagingBuffer[i].Velocity = velocities[i];
            m_InstanceStagingBuffer[i].UVOffsetScale = uvs[i];
            m_InstanceStagingBuffer[i].UVSubFrame = subFrames[i];
        }

        // 7. Upload instance buffer & dispatch GPU draw call
        m_InstanceVBO->SetData(m_InstanceStagingBuffer.data(), static_cast<u32>(toRender * sizeof(ParticleInstanceVertex)));
        RenderCommand::DrawIndexedInstanced(m_VAO, toRender);

        m_DrawCallCount++;
        m_RenderedParticleCount += toRender;
    }

}
