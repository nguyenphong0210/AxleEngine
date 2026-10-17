#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/Shader.hpp>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <filesystem>
#include <algorithm>

namespace Axle {

    class Material {
    public:
        Material(const std::string& name = "New Material", UUID handle = 0);
        ~Material() = default;

        // Identification
        UUID GetHandle() const { return m_Handle; }
        void SetHandle(UUID handle) { m_Handle = handle; }

        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }

        const std::filesystem::path& GetFilePath() const { return m_FilePath; }
        void SetFilePath(const std::filesystem::path& path) { m_FilePath = path; }

        // PBR Core Properties
        const glm::vec4& GetAlbedoColor() const { return m_AlbedoColor; }
        glm::vec4& GetAlbedoColorRef() { return m_AlbedoColor; }
        void SetAlbedoColor(const glm::vec4& color) { m_AlbedoColor = color; }

        float GetRoughness() const { return m_Roughness; }
        float& GetRoughnessRef() { return m_Roughness; }
        void SetRoughness(float roughness) { m_Roughness = std::clamp(roughness, 0.0f, 1.0f); }

        float GetMetallic() const { return m_Metallic; }
        float& GetMetallicRef() { return m_Metallic; }
        void SetMetallic(float metallic) { m_Metallic = std::clamp(metallic, 0.0f, 1.0f); }

        float GetNormalStrength() const { return m_NormalStrength; }
        float& GetNormalStrengthRef() { return m_NormalStrength; }
        void SetNormalStrength(float strength) { m_NormalStrength = strength; }

        const glm::vec3& GetEmissionColor() const { return m_EmissionColor; }
        glm::vec3& GetEmissionColorRef() { return m_EmissionColor; }
        void SetEmissionColor(const glm::vec3& color) { m_EmissionColor = color; }

        float GetEmissionIntensity() const { return m_EmissionIntensity; }
        float& GetEmissionIntensityRef() { return m_EmissionIntensity; }
        void SetEmissionIntensity(float intensity) { m_EmissionIntensity = std::max(0.0f, intensity); }

        const glm::vec2& GetUVScale() const { return m_UVScale; }
        glm::vec2& GetUVScaleRef() { return m_UVScale; }
        void SetUVScale(const glm::vec2& uvScale) { m_UVScale = uvScale; }

        // Texture Slots
        const std::string& GetAlbedoPath() const { return m_AlbedoPath; }
        const std::shared_ptr<Texture2D>& GetAlbedoMap() const { return m_AlbedoMap; }
        bool UseAlbedoMap() const { return m_UseAlbedoMap && (m_AlbedoMap != nullptr); }
        void SetUseAlbedoMap(bool use) { m_UseAlbedoMap = use; }
        void SetAlbedoTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path = "");
        void SetAlbedoPath(const std::string& path);
        void ClearAlbedoTexture();

        const std::string& GetNormalPath() const { return m_NormalPath; }
        const std::shared_ptr<Texture2D>& GetNormalMap() const { return m_NormalMap; }
        bool UseNormalMap() const { return m_UseNormalMap && (m_NormalMap != nullptr); }
        void SetUseNormalMap(bool use) { m_UseNormalMap = use; }
        void SetNormalTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path = "");
        void SetNormalPath(const std::string& path);
        void ClearNormalTexture();

        const std::string& GetRoughnessPath() const { return m_RoughnessPath; }
        const std::shared_ptr<Texture2D>& GetRoughnessMap() const { return m_RoughnessMap; }
        bool UseRoughnessMap() const { return m_UseRoughnessMap && (m_RoughnessMap != nullptr); }
        void SetUseRoughnessMap(bool use) { m_UseRoughnessMap = use; }
        void SetRoughnessTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path = "");
        void SetRoughnessPath(const std::string& path);
        void ClearRoughnessTexture();

        const std::string& GetMetallicPath() const { return m_MetallicPath; }
        const std::shared_ptr<Texture2D>& GetMetallicMap() const { return m_MetallicMap; }
        bool UseMetallicMap() const { return m_UseMetallicMap && (m_MetallicMap != nullptr); }
        void SetUseMetallicMap(bool use) { m_UseMetallicMap = use; }
        void SetMetallicTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path = "");
        void SetMetallicPath(const std::string& path);
        void ClearMetallicTexture();

        void ResetTextures();

        // Presets
        void SetPresetCarPaintRed();
        void SetPresetCarPaintBlue();
        void SetPresetChrome();
        void SetPresetGold();
        void SetPresetCopper();
        void SetPresetRubber();
        void SetPresetAluminum();
        void SetPresetObsidian();
        void SetPresetPlastic();
        void SetPresetCastIron();
        void SetPresetHeadlightLED();
        void SetPresetTaillightLED();

        // GPU Shader Binding
        void Bind(Shader& shader, uint32_t slotAlbedo = 0, uint32_t slotNormal = 2, uint32_t slotRoughness = 3, uint32_t slotMetallic = 4) const;
        static void BindDefault(Shader& shader, const glm::vec4& color1 = glm::vec4(1.0f), const glm::vec4& color2 = glm::vec4(0.8f),
                                uint32_t slotAlbedo = 0, uint32_t slotNormal = 2, uint32_t slotRoughness = 3, uint32_t slotMetallic = 4);

        // Serialization
        bool Save(const std::filesystem::path& filepath) const;
        bool Load(const std::filesystem::path& filepath);

        // Factory
        static std::shared_ptr<Material> Create(const std::string& name = "Material", UUID handle = 0);
        static std::shared_ptr<Material> LoadFromFile(const std::filesystem::path& filepath);

    private:
        UUID m_Handle = 0;
        std::string m_Name;
        std::filesystem::path m_FilePath;

        glm::vec4 m_AlbedoColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float m_Roughness = 0.5f;
        float m_Metallic = 0.0f;
        float m_NormalStrength = 1.0f;
        glm::vec3 m_EmissionColor{ 0.0f, 0.0f, 0.0f };
        float m_EmissionIntensity = 0.0f;
        glm::vec2 m_UVScale{ 1.0f, 1.0f };

        std::string m_AlbedoPath;
        std::shared_ptr<Texture2D> m_AlbedoMap;
        bool m_UseAlbedoMap = false;

        std::string m_NormalPath;
        std::shared_ptr<Texture2D> m_NormalMap;
        bool m_UseNormalMap = false;

        std::string m_RoughnessPath;
        std::shared_ptr<Texture2D> m_RoughnessMap;
        bool m_UseRoughnessMap = false;

        std::string m_MetallicPath;
        std::shared_ptr<Texture2D> m_MetallicMap;
        bool m_UseMetallicMap = false;
    };

}
