#include <Axle/Cubemap.hpp>
#include <Axle/Shader.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace Axle {

    namespace {
        // Pre-computed capture views for 6 cubemap faces
        const glm::mat4 g_CaptureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
        const glm::mat4 g_CaptureViews[] = {
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
        };

        // Render Quad VAO helper for BRDF LUT
        GLuint g_QuadVAO = 0;
        GLuint g_QuadVBO = 0;

        void RenderQuad() {
            if (glfwGetCurrentContext() == nullptr) return;
            if (g_QuadVAO == 0) {
                float quadVertices[] = {
                    // positions        // texture Coords
                    -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
                    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
                     1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
                     1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
                };
                glGenVertexArrays(1, &g_QuadVAO);
                glGenBuffers(1, &g_QuadVBO);
                glBindVertexArray(g_QuadVAO);
                glBindBuffer(GL_ARRAY_BUFFER, g_QuadVBO);
                glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
            }
            glBindVertexArray(g_QuadVAO);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindVertexArray(0);
        }
    }

    Cubemap::Cubemap(const CubemapSpecification& spec)
        : m_Spec(spec) {
        Invalidate();
    }

    Cubemap::~Cubemap() {
        if (glfwGetCurrentContext() != nullptr && m_RendererID != 0) {
            glDeleteTextures(1, &m_RendererID);
            m_RendererID = 0;
        }
    }

    void Cubemap::Invalidate() {
        if (glfwGetCurrentContext() == nullptr) {
            m_RendererID = 0;
            return;
        }

        if (m_RendererID != 0) {
            glDeleteTextures(1, &m_RendererID);
            m_RendererID = 0;
        }

        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);

        GLenum internalFormat = m_Spec.IsHDR ? GL_RGB16F : GL_RGB8;
        GLenum format = GL_RGB;
        GLenum type = m_Spec.IsHDR ? GL_FLOAT : GL_UNSIGNED_BYTE;

        for (u32 i = 0; i < 6; ++i) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat,
                         m_Spec.Width, m_Spec.Height, 0, format, type, nullptr);
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                        (m_Spec.MipLevels > 1) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        if (m_Spec.MipLevels > 1) {
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        }

        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    void Cubemap::Bind(u32 slot) const {
        if (glfwGetCurrentContext() == nullptr || m_RendererID == 0) return;
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
    }

    void Cubemap::Unbind(u32 slot) const {
        if (glfwGetCurrentContext() == nullptr) return;
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    std::shared_ptr<Cubemap> Cubemap::Create(const CubemapSpecification& spec) {
        return std::make_shared<Cubemap>(spec);
    }

    std::shared_ptr<Cubemap> Cubemap::CreateFromEquirectangular(const std::string& filepath, u32 resolution) {
        auto buffer = FileSystem::ReadBinaryFile(filepath);
        if (buffer.empty()) {
            AXLE_CORE_ERROR("Cubemap: Failed to read HDR file at '{0}'", filepath);
            return GetBlackCubemap();
        }

        stbi_set_flip_vertically_on_load(true);
        int width = 0, height = 0, channels = 0;
        float* data = stbi_loadf_from_memory(buffer.data(), static_cast<int>(buffer.size()), &width, &height, &channels, 0);
        if (!data) {
            AXLE_CORE_ERROR("Cubemap: Failed to decode HDR image '{0}': {1}", filepath, stbi_failure_reason());
            return GetBlackCubemap();
        }

        auto cubemap = CreateFromEquirectangularHDR(data, width, height, resolution);
        stbi_image_free(data);
        if (cubemap) {
            cubemap->m_FilePath = filepath;
        }
        return cubemap;
    }

    std::shared_ptr<Cubemap> Cubemap::CreateFromEquirectangularHDR(const float* hdrData, int width, int height, u32 resolution) {
        if (glfwGetCurrentContext() == nullptr) {
            // Headless / unit testing mode safe fallback
            CubemapSpecification spec;
            spec.Width = resolution;
            spec.Height = resolution;
            spec.MipLevels = 5;
            spec.IsHDR = true;
            return std::make_shared<Cubemap>(spec);
        }

        if (!hdrData || width <= 0 || height <= 0) {
            return GetBlackCubemap();
        }

        // 1. Upload 2D HDR texture
        GLuint hdrTexture = 0;
        glGenTextures(1, &hdrTexture);
        glBindTexture(GL_TEXTURE_2D, hdrTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, hdrData);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // 2. Setup FBO & RBO for capture
        GLuint captureFBO = 0, captureRBO = 0;
        glGenFramebuffers(1, &captureFBO);
        glGenRenderbuffers(1, &captureRBO);
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, resolution, resolution);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

        // 3. Create target cubemap
        CubemapSpecification spec;
        spec.Width = resolution;
        spec.Height = resolution;
        spec.MipLevels = 5;
        spec.IsHDR = true;
        auto cubemap = std::make_shared<Cubemap>(spec);

        // 4. Shaders for equirectangular to cubemap projection
        std::string vertSrc = R"(
            #version 450 core
            layout (location = 0) in vec3 a_Position;
            out vec3 v_LocalPos;
            uniform mat4 u_Projection;
            uniform mat4 u_View;
            void main() {
                v_LocalPos = a_Position;
                gl_Position = u_Projection * u_View * vec4(a_Position, 1.0);
            }
        )";

        std::string fragSrc = R"(
            #version 450 core
            layout (location = 0) out vec4 o_Color;
            in vec3 v_LocalPos;
            uniform sampler2D u_EquirectangularMap;
            const vec2 invAtan = vec2(0.15915494309, 0.31830988618);
            vec2 SampleSphericalMap(vec3 v) {
                vec2 uv = vec2(atan(v.z, v.x), asin(clamp(v.y, -1.0, 1.0)));
                uv *= invAtan;
                uv += 0.5;
                return uv;
            }
            void main() {
                vec2 uv = SampleSphericalMap(normalize(v_LocalPos));
                vec3 color = texture(u_EquirectangularMap, uv).rgb;
                o_Color = vec4(color, 1.0);
            }
        )";

        Shader shader(vertSrc, fragSrc);
        shader.Bind();
        shader.SetInt("u_EquirectangularMap", 0);
        shader.SetMat4("u_Projection", g_CaptureProjection);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrTexture);

        glViewport(0, 0, resolution, resolution);
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

        auto cubeVAO = PrimitiveMesh::CreateCube();

        for (u32 i = 0; i < 6; ++i) {
            shader.SetMat4("u_View", g_CaptureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubemap->GetRendererID(), 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            RenderCommand::DrawIndexed(cubeVAO);
        }

        // Generate mipmaps for prefiltering
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap->GetRendererID());
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

        // Cleanup
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteTextures(1, &hdrTexture);
        glDeleteRenderbuffers(1, &captureRBO);
        glDeleteFramebuffers(1, &captureFBO);

        return cubemap;
    }

    std::shared_ptr<Cubemap> Cubemap::CreateIrradianceMap(const std::shared_ptr<Cubemap>& envMap, u32 size) {
        if (glfwGetCurrentContext() == nullptr || !envMap || envMap->GetRendererID() == 0) {
            CubemapSpecification spec;
            spec.Width = size;
            spec.Height = size;
            spec.MipLevels = 1;
            spec.IsHDR = true;
            return std::make_shared<Cubemap>(spec);
        }

        GLuint captureFBO = 0, captureRBO = 0;
        glGenFramebuffers(1, &captureFBO);
        glGenRenderbuffers(1, &captureRBO);
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

        CubemapSpecification spec;
        spec.Width = size;
        spec.Height = size;
        spec.MipLevels = 1;
        spec.IsHDR = true;
        auto irradianceMap = std::make_shared<Cubemap>(spec);

        std::string vertSrc = R"(
            #version 450 core
            layout (location = 0) in vec3 a_Position;
            out vec3 v_LocalPos;
            uniform mat4 u_Projection;
            uniform mat4 u_View;
            void main() {
                v_LocalPos = a_Position;
                gl_Position = u_Projection * u_View * vec4(a_Position, 1.0);
            }
        )";

        std::string fragSrc = R"(
            #version 450 core
            layout (location = 0) out vec4 o_Color;
            in vec3 v_LocalPos;
            uniform samplerCube u_EnvironmentMap;
            const float PI = 3.14159265358979323846;

            void main() {
                vec3 normal = normalize(v_LocalPos);
                vec3 irradiance = vec3(0.0);

                vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
                vec3 right = normalize(cross(up, normal));
                up = normalize(cross(normal, right));

                float sampleDelta = 0.05;
                float nrSamples = 0.0;
                for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
                    for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
                        vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
                        vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal;
                        irradiance += texture(u_EnvironmentMap, sampleVec).rgb * cos(theta) * sin(theta);
                        nrSamples += 1.0;
                    }
                }
                irradiance = PI * irradiance * (1.0 / nrSamples);
                o_Color = vec4(irradiance, 1.0);
            }
        )";

        Shader shader(vertSrc, fragSrc);
        shader.Bind();
        shader.SetInt("u_EnvironmentMap", 0);
        shader.SetMat4("u_Projection", g_CaptureProjection);

        envMap->Bind(0);

        glViewport(0, 0, size, size);
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

        auto cubeVAO = PrimitiveMesh::CreateCube();

        for (u32 i = 0; i < 6; ++i) {
            shader.SetMat4("u_View", g_CaptureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradianceMap->GetRendererID(), 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            RenderCommand::DrawIndexed(cubeVAO);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteRenderbuffers(1, &captureRBO);
        glDeleteFramebuffers(1, &captureFBO);

        return irradianceMap;
    }

    std::shared_ptr<Cubemap> Cubemap::CreatePrefilterMap(const std::shared_ptr<Cubemap>& envMap, u32 size) {
        if (glfwGetCurrentContext() == nullptr || !envMap || envMap->GetRendererID() == 0) {
            CubemapSpecification spec;
            spec.Width = size;
            spec.Height = size;
            spec.MipLevels = 5;
            spec.IsHDR = true;
            return std::make_shared<Cubemap>(spec);
        }

        CubemapSpecification spec;
        spec.Width = size;
        spec.Height = size;
        spec.MipLevels = 5;
        spec.IsHDR = true;
        auto prefilterMap = std::make_shared<Cubemap>(spec);

        glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap->GetRendererID());
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

        GLuint captureFBO = 0, captureRBO = 0;
        glGenFramebuffers(1, &captureFBO);
        glGenRenderbuffers(1, &captureRBO);
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

        std::string vertSrc = R"(
            #version 450 core
            layout (location = 0) in vec3 a_Position;
            out vec3 v_LocalPos;
            uniform mat4 u_Projection;
            uniform mat4 u_View;
            void main() {
                v_LocalPos = a_Position;
                gl_Position = u_Projection * u_View * vec4(a_Position, 1.0);
            }
        )";

        std::string fragSrc = R"(
            #version 450 core
            layout (location = 0) out vec4 o_Color;
            in vec3 v_LocalPos;
            uniform samplerCube u_EnvironmentMap;
            uniform float u_Roughness;
            const float PI = 3.14159265358979323846;

            float RadicalInverse_VdC(uint bits) {
                bits = (bits << 16u) | (bits >> 16u);
                bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                return float(bits) * 2.3283064365386963e-10;
            }

            vec2 Hammersley(uint i, uint N) {
                return vec2(float(i) / float(N), RadicalInverse_VdC(i));
            }

            vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
                float a = roughness * roughness;
                float phi = 2.0 * PI * Xi.x;
                float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
                float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

                vec3 H;
                H.x = cos(phi) * sinTheta;
                H.y = sin(phi) * sinTheta;
                H.z = cosTheta;

                vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
                vec3 tangent = normalize(cross(up, N));
                vec3 bitangent = cross(N, tangent);

                vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
                return normalize(sampleVec);
            }

            void main() {
                vec3 N = normalize(v_LocalPos);
                vec3 R = N;
                vec3 V = R;

                const uint SAMPLE_COUNT = 512u;
                vec3 prefilteredColor = vec3(0.0);
                float totalWeight = 0.0;

                for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
                    vec2 Xi = Hammersley(i, SAMPLE_COUNT);
                    vec3 H = ImportanceSampleGGX(Xi, N, u_Roughness);
                    vec3 L = normalize(2.0 * dot(V, H) * H - V);

                    float NdotL = max(dot(N, L), 0.0);
                    if (NdotL > 0.0) {
                        prefilteredColor += texture(u_EnvironmentMap, L).rgb * NdotL;
                        totalWeight += NdotL;
                    }
                }
                prefilteredColor = prefilteredColor / max(totalWeight, 0.0001);
                o_Color = vec4(prefilteredColor, 1.0);
            }
        )";

        Shader shader(vertSrc, fragSrc);
        shader.Bind();
        shader.SetInt("u_EnvironmentMap", 0);
        shader.SetMat4("u_Projection", g_CaptureProjection);

        envMap->Bind(0);
        auto cubeVAO = PrimitiveMesh::CreateCube();

        u32 maxMipLevels = 5;
        for (u32 mip = 0; mip < maxMipLevels; ++mip) {
            u32 mipWidth = static_cast<u32>(size * std::pow(0.5, mip));
            u32 mipHeight = static_cast<u32>(size * std::pow(0.5, mip));

            glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

            glViewport(0, 0, mipWidth, mipHeight);

            float roughness = static_cast<float>(mip) / static_cast<float>(maxMipLevels - 1);
            shader.SetFloat("u_Roughness", roughness);

            for (u32 i = 0; i < 6; ++i) {
                shader.SetMat4("u_View", g_CaptureViews[i]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilterMap->GetRendererID(), mip);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                RenderCommand::DrawIndexed(cubeVAO);
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteRenderbuffers(1, &captureRBO);
        glDeleteFramebuffers(1, &captureFBO);

        return prefilterMap;
    }

    std::shared_ptr<Texture2D> Cubemap::CreateBRDFLUT(u32 size) {
        if (glfwGetCurrentContext() == nullptr) {
            TextureSpecification spec;
            spec.Width = size;
            spec.Height = size;
            spec.Format = ImageFormat::RGBA32F;
            spec.GenerateMipmaps = false;
            return Texture2D::Create(spec);
        }

        GLuint brdfLUTTexture = 0;
        glGenTextures(1, &brdfLUTTexture);
        glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, size, size, 0, GL_RG, GL_FLOAT, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        GLuint captureFBO = 0, captureRBO = 0;
        glGenFramebuffers(1, &captureFBO);
        glGenRenderbuffers(1, &captureRBO);
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTexture, 0);

        glViewport(0, 0, size, size);

        std::string vertSrc = R"(
            #version 450 core
            layout (location = 0) in vec3 a_Position;
            layout (location = 1) in vec2 a_TexCoords;
            out vec2 v_TexCoords;
            void main() {
                v_TexCoords = a_TexCoords;
                gl_Position = vec4(a_Position, 1.0);
            }
        )";

        std::string fragSrc = R"(
            #version 450 core
            layout (location = 0) out vec2 o_Color;
            in vec2 v_TexCoords;
            const float PI = 3.14159265358979323846;

            float RadicalInverse_VdC(uint bits) {
                bits = (bits << 16u) | (bits >> 16u);
                bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                return float(bits) * 2.3283064365386963e-10;
            }

            vec2 Hammersley(uint i, uint N) {
                return vec2(float(i) / float(N), RadicalInverse_VdC(i));
            }

            vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
                float a = roughness * roughness;
                float phi = 2.0 * PI * Xi.x;
                float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
                float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

                vec3 H;
                H.x = cos(phi) * sinTheta;
                H.y = sin(phi) * sinTheta;
                H.z = cosTheta;

                vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
                vec3 tangent = normalize(cross(up, N));
                vec3 bitangent = cross(N, tangent);

                vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
                return normalize(sampleVec);
            }

            float GeometrySchlickGGX(float NdotV, float roughness) {
                float k = (roughness * roughness) / 2.0;
                float nom = NdotV;
                float denom = NdotV * (1.0 - k) + k;
                return nom / max(denom, 0.0000001);
            }

            float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
                float NdotV = max(dot(N, V), 0.0);
                float NdotL = max(dot(N, L), 0.0);
                float ggx2 = GeometrySchlickGGX(NdotV, roughness);
                float ggx1 = GeometrySchlickGGX(NdotL, roughness);
                return ggx1 * ggx2;
            }

            vec2 IntegrateBRDF(float NdotV, float roughness) {
                vec3 V;
                V.x = sqrt(1.0 - NdotV * NdotV);
                V.y = 0.0;
                V.z = NdotV;

                float A = 0.0;
                float B = 0.0;
                vec3 N = vec3(0.0, 0.0, 1.0);

                const uint SAMPLE_COUNT = 1024u;
                for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
                    vec2 Xi = Hammersley(i, SAMPLE_COUNT);
                    vec3 H = ImportanceSampleGGX(Xi, N, roughness);
                    vec3 L = normalize(2.0 * dot(V, H) * H - V);

                    float NdotL = max(L.z, 0.0);
                    float NdotH = max(H.z, 0.0);
                    float VdotH = max(dot(V, H), 0.0);

                    if (NdotL > 0.0) {
                        float G = GeometrySmith(N, V, L, roughness);
                        float G_Vis = (G * VdotH) / (NdotH * NdotV);
                        float Fc = pow(1.0 - VdotH, 5.0);

                        A += (1.0 - Fc) * G_Vis;
                        B += Fc * G_Vis;
                    }
                }
                return vec2(A / float(SAMPLE_COUNT), B / float(SAMPLE_COUNT));
            }

            void main() {
                vec2 integratedBRDF = IntegrateBRDF(v_TexCoords.x, v_TexCoords.y);
                o_Color = integratedBRDF;
            }
        )";

        Shader shader(vertSrc, fragSrc);
        shader.Bind();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RenderQuad();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteRenderbuffers(1, &captureRBO);
        glDeleteFramebuffers(1, &captureFBO);

        // Wrap the OpenGL texture in Texture2D
        auto brdfTexture = Texture2D::CreateFromRendererID(brdfLUTTexture, size, size);
        return brdfTexture;
    }

    IBLMaps Cubemap::GenerateIBLMaps(const std::string& hdrFilepath, u32 envResolution) {
        IBLMaps maps;
        maps.EnvironmentMap = CreateFromEquirectangular(hdrFilepath, envResolution);
        if (maps.EnvironmentMap) {
            maps.IrradianceMap = CreateIrradianceMap(maps.EnvironmentMap, 32);
            maps.PrefilterMap = CreatePrefilterMap(maps.EnvironmentMap, 128);
        } else {
            maps.IrradianceMap = GetDefaultIrradianceMap();
            maps.PrefilterMap = GetDefaultPrefilterMap();
        }
        maps.BRDFLUT = GetGlobalBRDFLUT();
        return maps;
    }

    std::shared_ptr<Cubemap> Cubemap::GetBlackCubemap() {
        static std::shared_ptr<Cubemap> s_BlackCubemap;
        if (!s_BlackCubemap) {
            CubemapSpecification spec;
            spec.Width = 1;
            spec.Height = 1;
            spec.MipLevels = 1;
            spec.IsHDR = false;
            s_BlackCubemap = std::make_shared<Cubemap>(spec);
        }
        return s_BlackCubemap;
    }

    std::shared_ptr<Cubemap> Cubemap::GetDefaultIrradianceMap() {
        return GetBlackCubemap();
    }

    std::shared_ptr<Cubemap> Cubemap::GetDefaultPrefilterMap() {
        return GetBlackCubemap();
    }

    std::shared_ptr<Texture2D> Cubemap::GetGlobalBRDFLUT() {
        static std::shared_ptr<Texture2D> s_BRDFLUT;
        if (!s_BRDFLUT) {
            s_BRDFLUT = CreateBRDFLUT(256);
        }
        return s_BRDFLUT;
    }

}
