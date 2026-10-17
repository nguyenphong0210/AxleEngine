#pragma once
#include <Axle/Types.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Cubemap.hpp>
#include <Axle/Texture2D.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Axle {

    enum class SkyboxMode {
        ProceduralAtmospheric = 0,
        HDRCubemap = 1
    };

    struct SkyboxSpecification {
        SkyboxMode Mode = SkyboxMode::ProceduralAtmospheric;
        std::string HDRPath;
        float HDRIntensity = 1.0f;

        glm::vec3 ZenithColor{ 0.18f, 0.42f, 0.78f };    // Deep sky blue at zenith
        glm::vec3 HorizonColor{ 0.70f, 0.82f, 0.95f };   // Soft atmospheric haze at horizon
        glm::vec3 GroundColor{ 0.18f, 0.16f, 0.14f };    // Sub-horizon ground terrain
        glm::vec3 SunDirection{ 0.45f, 0.80f, 0.40f };   // Direction vector towards sun
        glm::vec3 SunColor{ 1.0f, 0.96f, 0.88f };        // Sun light radiance color
        float SunSize = 0.045f;                           // Angular disk radius of sun
        float SunGlow = 0.16f;                            // Forward Mie scattering halo falloff
        float Turbidity = 1.0f;                           // Atmospheric thickness / haze
        float Exposure = 1.0f;                            // Photometric exposure multiplier
        int ToneMappingMode = 0;                          // 0: ACES, 1: Reinhard, 2: Uncharted 2, 3: Linear
        float Gamma = 2.2f;                               // Display gamma exponent
    };

    class Skybox {
    public:
        explicit Skybox(const SkyboxSpecification& spec = {});
        ~Skybox() = default;

        void Render(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);

        void LoadHDR(const std::string& filepath);
        void SetMode(SkyboxMode mode) { m_Spec.Mode = mode; }
        SkyboxMode GetMode() const { return m_Spec.Mode; }

        void SetSunDirection(const glm::vec3& dir) { m_Spec.SunDirection = glm::normalize(dir); }
        void SetSunColor(const glm::vec3& color) { m_Spec.SunColor = color; }
        void SetExposure(float exposure) { m_Spec.Exposure = exposure; }
        void SetToneMapping(int mode, float gamma) { m_Spec.ToneMappingMode = mode; m_Spec.Gamma = gamma; }
        void SetHDRIntensity(float intensity) { m_Spec.HDRIntensity = intensity; }

        SkyboxSpecification& GetSpecification() { return m_Spec; }
        const SkyboxSpecification& GetSpecification() const { return m_Spec; }

        Shader* GetShader() const { return m_Shader.get(); }
        std::shared_ptr<VertexArray> GetVAO() const { return m_CubeVAO; }

        // IBL Resources for PBR reflections
        std::shared_ptr<Cubemap> GetEnvironmentCubemap() const { return m_EnvironmentCubemap; }
        std::shared_ptr<Cubemap> GetIrradianceMap() const { return m_IrradianceMap; }
        std::shared_ptr<Cubemap> GetPrefilterMap() const { return m_PrefilterMap; }
        std::shared_ptr<Texture2D> GetBRDFLUT() const { return m_BRDFLUT; }

    private:
        SkyboxSpecification m_Spec;
        std::shared_ptr<VertexArray> m_CubeVAO;
        std::unique_ptr<Shader> m_Shader;
        std::unique_ptr<Shader> m_CubemapShader;

        std::shared_ptr<Cubemap> m_EnvironmentCubemap;
        std::shared_ptr<Cubemap> m_IrradianceMap;
        std::shared_ptr<Cubemap> m_PrefilterMap;
        std::shared_ptr<Texture2D> m_BRDFLUT;
    };

}
