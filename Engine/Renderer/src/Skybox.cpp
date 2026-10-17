#include <Axle/Skybox.hpp>
#include <Axle/Window.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <filesystem>

namespace Axle {

    Skybox::Skybox(const SkyboxSpecification& spec)
        : m_Spec(spec)
    {
        if (!Window::HasActiveContext()) {
            return;
        }

        // 1. Initialize unit skybox cube geometry
        m_CubeVAO = PrimitiveMesh::CreateSkyboxCube();

        // 2. Load and compile Atmospheric procedural shader
        std::string vertSrc = R"(
            #version 450 core
            layout(location = 0) in vec3 a_Position;
            out vec3 v_TexCoord;
            uniform mat4 u_View;
            uniform mat4 u_Projection;
            void main() {
                v_TexCoord = a_Position;
                mat4 rotView = mat4(mat3(u_View));
                vec4 clipPos = u_Projection * rotView * vec4(a_Position, 1.0);
                gl_Position = clipPos.xyww;
            }
        )";

        std::string fragSrc = R"(
            #version 450 core
            layout(location = 0) out vec4 o_Color;
            in vec3 v_TexCoord;
            uniform vec3 u_SunDirection;
            uniform vec3 u_SunColor;
            uniform vec3 u_ZenithColor;
            uniform vec3 u_HorizonColor;
            uniform vec3 u_GroundColor;
            uniform float u_SunSize;
            uniform float u_SunGlow;
            uniform float u_Turbidity;
            uniform float u_Exposure;
            uniform int u_ToneMappingMode;
            uniform float u_Gamma;

            vec3 ToneMapACES(vec3 x) {
                const float a = 2.51; const float b = 0.03; const float c = 2.43; const float d = 0.59; const float e = 0.14;
                return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
            }

            void main() {
                vec3 dir = normalize(v_TexCoord);
                vec3 sunDir = normalize(u_SunDirection);
                float sunElev = sunDir.y;
                float sunsetFactor = clamp(1.0 - abs(sunElev) * 2.5, 0.0, 1.0);
                float dayFactor = clamp(sunElev * 3.0 + 0.5, 0.0, 1.0);
                vec3 sunsetHorizon = vec3(1.0, 0.45, 0.15);
                vec3 dynamicHorizon = mix(u_HorizonColor, sunsetHorizon, sunsetFactor * 0.85);
                vec3 nightZenith = vec3(0.015, 0.025, 0.06);
                vec3 nightHorizon = vec3(0.04, 0.05, 0.08);
                vec3 dynamicZenith = mix(nightZenith, u_ZenithColor, dayFactor);
                dynamicHorizon = mix(nightHorizon, dynamicHorizon, dayFactor);

                vec3 skyColor;
                if (dir.y >= 0.0) {
                    float t = pow(dir.y, clamp(0.55 * u_Turbidity, 0.2, 2.0));
                    skyColor = mix(dynamicHorizon, dynamicZenith, t);
                } else {
                    float t = pow(-dir.y, 0.5);
                    skyColor = mix(dynamicHorizon, u_GroundColor * (0.3 + 0.7 * dayFactor), t);
                }

                float cosTheta = dot(dir, sunDir);
                if (cosTheta > 0.0) {
                    float glowExp = max(16.0 / max(u_SunGlow, 0.01), 1.0);
                    float glow = pow(cosTheta, glowExp) * dayFactor;
                    vec3 haloColor = mix(u_SunColor, sunsetHorizon, sunsetFactor * 0.5);
                    skyColor += haloColor * glow * 1.8;

                    float sunAngle = acos(clamp(cosTheta, -1.0, 1.0));
                    float radius = max(u_SunSize, 0.005);
                    float disk = 1.0 - smoothstep(radius * 0.90, radius * 1.08, sunAngle);
                    disk *= step(0.0, sunElev + radius);
                    skyColor += u_SunColor * disk * 12.0 * dayFactor;
                }

                float exposure = (u_Exposure > 0.0) ? u_Exposure : 1.0;
                vec3 exposed = skyColor * exposure;
                vec3 mappedColor = (u_ToneMappingMode == 0) ? ToneMapACES(exposed) : clamp(exposed, 0.0, 1.0);
                float gamma = (u_Gamma > 0.0) ? u_Gamma : 2.2;
                vec3 srgbColor = pow(mappedColor, vec3(1.0 / gamma));
                o_Color = vec4(srgbColor, 1.0);
            }
        )";

        m_Shader = std::make_unique<Shader>(vertSrc, fragSrc);

        // 3. Load and compile HDR Cubemap skybox shader
        std::string cubemapFragSrc = R"(
            #version 450 core
            layout(location = 0) out vec4 o_Color;
            in vec3 v_TexCoord;
            uniform samplerCube u_SkyboxCubemap;
            uniform float u_Exposure;
            uniform float u_Intensity;
            uniform int u_ToneMappingMode;
            uniform float u_Gamma;

            vec3 ToneMapACES(vec3 x) {
                const float a = 2.51; const float b = 0.03; const float c = 2.43; const float d = 0.59; const float e = 0.14;
                return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
            }

            void main() {
                vec3 envColor = texture(u_SkyboxCubemap, v_TexCoord).rgb;
                envColor *= max(u_Intensity, 0.0);
                float exposure = (u_Exposure > 0.0) ? u_Exposure : 1.0;
                vec3 exposed = envColor * exposure;
                vec3 mappedColor = (u_ToneMappingMode == 0) ? ToneMapACES(exposed) : clamp(exposed, 0.0, 1.0);
                float gamma = (u_Gamma > 0.0) ? u_Gamma : 2.2;
                vec3 srgbColor = pow(mappedColor, vec3(1.0 / gamma));
                o_Color = vec4(srgbColor, 1.0);
            }
        )";

        m_CubemapShader = std::make_unique<Shader>(vertSrc, cubemapFragSrc);

        // Load HDR if specified in initial spec
        if (!m_Spec.HDRPath.empty()) {
            LoadHDR(m_Spec.HDRPath);
        }

        AXLE_CORE_INFO("Skybox: Atmospheric & HDR Cubemap IBL pipelines initialized successfully");
    }

    void Skybox::LoadHDR(const std::string& filepath) {
        m_Spec.HDRPath = filepath;
        if (filepath.empty()) {
            m_Spec.Mode = SkyboxMode::ProceduralAtmospheric;
            m_EnvironmentCubemap = nullptr;
            m_IrradianceMap = nullptr;
            m_PrefilterMap = nullptr;
            m_BRDFLUT = nullptr;
            return;
        }

        auto resolved = FileSystem::ResolveAssetPath(filepath);
        if (!std::filesystem::exists(resolved)) {
            AXLE_CORE_WARN("Skybox: HDR environment file '{0}' not found, falling back to procedural sky.", filepath);
            m_Spec.Mode = SkyboxMode::ProceduralAtmospheric;
            return;
        }

        auto ibl = Cubemap::GenerateIBLMaps(resolved.string());
        m_EnvironmentCubemap = ibl.EnvironmentMap;
        m_IrradianceMap = ibl.IrradianceMap;
        m_PrefilterMap = ibl.PrefilterMap;
        m_BRDFLUT = ibl.BRDFLUT;
        m_Spec.Mode = SkyboxMode::HDRCubemap;
        AXLE_CORE_INFO("Skybox: Successfully loaded HDR environment map '{0}' and convolved IBL probes.", filepath);
    }

    void Skybox::Render(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) {
        if (!Window::HasActiveContext() || !m_CubeVAO) return;

        // 1. Configure depth and culling states for skybox rendering
        GLboolean prevCullFace = glIsEnabled(GL_CULL_FACE);
        RenderCommand::SetDepthFuncLEqual();
        RenderCommand::SetDepthMask(false);
        RenderCommand::SetCullFace(false);

        if (m_Spec.Mode == SkyboxMode::HDRCubemap && m_EnvironmentCubemap && m_CubemapShader) {
            // Render 360 HDR Cubemap Skybox
            m_CubemapShader->Bind();
            m_CubemapShader->SetMat4("u_View", viewMatrix);
            m_CubemapShader->SetMat4("u_Projection", projectionMatrix);
            m_CubemapShader->SetFloat("u_Exposure", m_Spec.Exposure);
            m_CubemapShader->SetFloat("u_Intensity", m_Spec.HDRIntensity);
            m_CubemapShader->SetInt("u_ToneMappingMode", m_Spec.ToneMappingMode);
            m_CubemapShader->SetFloat("u_Gamma", m_Spec.Gamma);

            m_EnvironmentCubemap->Bind(0);
            m_CubemapShader->SetInt("u_SkyboxCubemap", 0);

            RenderCommand::DrawIndexed(m_CubeVAO);
        } else if (m_Shader) {
            // Render Procedural Atmospheric Skybox
            m_Shader->Bind();
            m_Shader->SetMat4("u_View", viewMatrix);
            m_Shader->SetMat4("u_Projection", projectionMatrix);
            m_Shader->SetFloat3("u_SunDirection", m_Spec.SunDirection);
            m_Shader->SetFloat3("u_SunColor", m_Spec.SunColor);
            m_Shader->SetFloat3("u_ZenithColor", m_Spec.ZenithColor);
            m_Shader->SetFloat3("u_HorizonColor", m_Spec.HorizonColor);
            m_Shader->SetFloat3("u_GroundColor", m_Spec.GroundColor);
            m_Shader->SetFloat("u_SunSize", m_Spec.SunSize);
            m_Shader->SetFloat("u_SunGlow", m_Spec.SunGlow);
            m_Shader->SetFloat("u_Turbidity", m_Spec.Turbidity);
            m_Shader->SetFloat("u_Exposure", m_Spec.Exposure);
            m_Shader->SetInt("u_ToneMappingMode", m_Spec.ToneMappingMode);
            m_Shader->SetFloat("u_Gamma", m_Spec.Gamma);

            RenderCommand::DrawIndexed(m_CubeVAO);
        }

        // 3. Restore standard render states
        RenderCommand::SetDepthMask(true);
        RenderCommand::SetDepthFuncLess();
        RenderCommand::SetCullFace(prevCullFace);
    }

}
