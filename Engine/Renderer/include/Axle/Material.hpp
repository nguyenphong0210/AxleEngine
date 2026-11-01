#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/ShaderPropertyReflection.hpp>
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

        // Texture Mode (Separate vs Packed ORM)
        TextureMode GetTextureMode() const { return m_TextureMode; }
        TextureMode& GetTextureModeRef() { return m_TextureMode; }
        void SetTextureMode(TextureMode mode) { m_TextureMode = mode; }

        const std::string& GetORMPath() const { return m_ORMPath; }
        const std::shared_ptr<Texture2D>& GetORMMap() const { return m_ORMMap; }
        bool UseORMMap() const { return m_UseORMMap && (m_ORMMap != nullptr); }
        bool IsORMMapEnabled() const { return m_UseORMMap; }
        void SetUseORMMap(bool use) { m_UseORMMap = use; }
        void SetORMTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path = "");
        void SetORMPath(const std::string& path);
        void ClearORMTexture();

        const std::string& GetClearcoatPath() const { return m_ClearcoatPath; }
        const std::shared_ptr<Texture2D>& GetClearcoatMap() const { return m_ClearcoatMap; }
        bool IsClearcoatMapEnabled() const { return m_UseClearcoatMap; }
        void SetUseClearcoatMap(bool value) { m_UseClearcoatMap=value; }
        void SetClearcoatTexture(const std::shared_ptr<Texture2D>& texture,const std::string& path="") { m_ClearcoatMap=texture; m_ClearcoatPath=path; m_UseClearcoatMap=!path.empty(); }

        const std::string& GetAnisotropyFlowPath() const { return m_AnisotropyFlowPath; }
        const std::shared_ptr<Texture2D>& GetAnisotropyFlowMap() const { return m_AnisotropyFlowMap; }
        bool IsAnisotropyFlowMapEnabled() const { return m_UseAnisotropyFlowMap; }
        void SetUseAnisotropyFlowMap(bool value) { m_UseAnisotropyFlowMap=value; }
        void SetAnisotropyFlowTexture(const std::shared_ptr<Texture2D>& texture,const std::string& path="") { m_AnisotropyFlowMap=texture; m_AnisotropyFlowPath=path; m_UseAnisotropyFlowMap=!path.empty(); }

        const std::string& GetDetailNormalPath() const { return m_DetailNormalPath; }
        const std::shared_ptr<Texture2D>& GetDetailNormalMap() const { return m_DetailNormalMap; }
        bool IsDetailNormalMapEnabled() const { return m_UseDetailNormalMap; }
        void SetUseDetailNormalMap(bool value) { m_UseDetailNormalMap=value; }
        void SetDetailNormalTexture(const std::shared_ptr<Texture2D>& texture,const std::string& path="") { m_DetailNormalMap=texture; m_DetailNormalPath=path; m_UseDetailNormalMap=!path.empty(); }

        const std::string& GetDetailMaskPath() const { return m_DetailMaskPath; }
        const std::shared_ptr<Texture2D>& GetDetailMaskMap() const { return m_DetailMaskMap; }
        bool IsDetailMaskMapEnabled() const { return m_UseDetailMaskMap; }
        void SetUseDetailMaskMap(bool value) { m_UseDetailMaskMap=value; }
        void SetDetailMaskTexture(const std::shared_ptr<Texture2D>& texture,const std::string& path="") { m_DetailMaskMap=texture; m_DetailMaskPath=path; m_UseDetailMaskMap=!path.empty(); }

        float GetDetailNormalStrength() const { return m_DetailNormalStrength; }
        void SetDetailNormalStrength(float value) { m_DetailNormalStrength=value; }
        glm::vec2 GetDetailUVScale() const { return m_DetailUVScale; }
        void SetDetailUVScale(glm::vec2 value) { m_DetailUVScale=value; }
        glm::vec2 GetDetailUVOffset() const { return m_DetailUVOffset; }
        void SetDetailUVOffset(glm::vec2 value) { m_DetailUVOffset=value; }

        // Automotive & Advanced Shading (Clearcoat & Anisotropy)
        float GetClearcoatStrength() const { return m_ClearcoatStrength; }
        float& GetClearcoatStrengthRef() { return m_ClearcoatStrength; }
        void SetClearcoatStrength(float val) { m_ClearcoatStrength = std::clamp(val, 0.0f, 1.0f); }

        float GetClearcoatRoughness() const { return m_ClearcoatRoughness; }
        float& GetClearcoatRoughnessRef() { return m_ClearcoatRoughness; }
        void SetClearcoatRoughness(float val) { m_ClearcoatRoughness = std::clamp(val, 0.001f, 1.0f); }

        float GetAnisotropyStrength() const { return m_AnisotropyStrength; }
        float& GetAnisotropyStrengthRef() { return m_AnisotropyStrength; }
        void SetAnisotropyStrength(float val) { m_AnisotropyStrength = std::clamp(val, -1.0f, 1.0f); }

        float GetAnisotropyRotation() const { return m_AnisotropyRotation; }
        float& GetAnisotropyRotationRef() { return m_AnisotropyRotation; }
        void SetAnisotropyRotation(float val) { m_AnisotropyRotation = std::clamp(val, 0.0f, 1.0f); }

        // Custom HLSL Shader Overrides
        bool GetUseCustomShader() const { return m_UseCustomShader; }
        bool& GetUseCustomShaderRef() { return m_UseCustomShader; }
        void SetUseCustomShader(bool use) { m_UseCustomShader = use; }

        const std::string& GetCustomShaderPath() const { return m_CustomShaderPath; }
        std::string& GetCustomShaderPathRef() { return m_CustomShaderPath; }
        void SetCustomShaderPath(const std::string& path) { m_CustomShaderPath = path; }

        // Surface Options (Unity / Unreal Standard)
        SurfaceType GetSurfaceType() const { return m_SurfaceType; }
        void SetSurfaceType(SurfaceType type) {
            m_SurfaceType = type;
            if (type == SurfaceType::Transparent || type == SurfaceType::Additive) m_DepthWrite = false;
            else m_DepthWrite = true;
        }

        CullMode GetCullMode() const { return m_CullMode; }
        void SetCullMode(CullMode mode) { m_CullMode = mode; }

        float GetAlphaCutoff() const { return m_AlphaCutoff; }
        float& GetAlphaCutoffRef() { return m_AlphaCutoff; }
        void SetAlphaCutoff(float cutoff) { m_AlphaCutoff = std::clamp(cutoff, 0.0f, 1.0f); }

        bool GetDepthWrite() const { return m_DepthWrite; }
        bool& GetDepthWriteRef() { return m_DepthWrite; }
        void SetDepthWrite(bool depthWrite) { m_DepthWrite = depthWrite; }

        // Advanced Options (Unity URP Standard)
        bool GetEnableSpecularHighlights() const { return m_EnableSpecularHighlights; }
        bool& GetEnableSpecularHighlightsRef() { return m_EnableSpecularHighlights; }
        void SetEnableSpecularHighlights(bool enable) { m_EnableSpecularHighlights = enable; }

        bool GetEnableEnvironmentReflections() const { return m_EnableEnvironmentReflections; }
        bool& GetEnableEnvironmentReflectionsRef() { return m_EnableEnvironmentReflections; }
        void SetEnableEnvironmentReflections(bool enable) { m_EnableEnvironmentReflections = enable; }

        int GetRenderQueue() const { return m_RenderQueue; }
        int& GetRenderQueueRef() { return m_RenderQueue; }
        void SetRenderQueue(int queue) { m_RenderQueue = queue; }

        // Dynamic Shader Properties (Phase 38.2)
        const std::vector<ShaderPropertyInstance>& GetCustomProperties() const { return m_CustomProperties; }
        std::vector<ShaderPropertyInstance>& GetCustomPropertiesRef() { return m_CustomProperties; }
        void SetCustomProperties(const std::vector<ShaderPropertyInstance>& props) { m_CustomProperties = props; }
        void SetCustomFloat(const std::string& name, float value);
        float GetCustomFloat(const std::string& name, float defaultVal = 0.0f) const;
        void SetCustomVec4(const std::string& name, const glm::vec4& value);
        glm::vec4 GetCustomVec4(const std::string& name, const glm::vec4& defaultVal = glm::vec4(0.0f)) const;
        void SetCustomTexture(const std::string& name, const std::string& path);
        std::string GetCustomTexture(const std::string& name) const;

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

        const glm::vec2& GetUVTiling() const { return m_UVScale; }
        glm::vec2& GetUVTilingRef() { return m_UVScale; }
        void SetUVTiling(const glm::vec2& uvTiling) { m_UVScale = uvTiling; }

        const glm::vec2& GetUVOffset() const { return m_UVOffset; }
        glm::vec2& GetUVOffsetRef() { return m_UVOffset; }
        void SetUVOffset(const glm::vec2& uvOffset) { m_UVOffset = uvOffset; }

        // Texture Slots
        const std::string& GetAlbedoPath() const { return m_AlbedoPath; }
        const std::shared_ptr<Texture2D>& GetAlbedoMap() const { return m_AlbedoMap; }
        bool UseAlbedoMap() const { return m_UseAlbedoMap && (m_AlbedoMap != nullptr); }
        bool IsAlbedoMapEnabled() const { return m_UseAlbedoMap; }
        void SetUseAlbedoMap(bool use) { m_UseAlbedoMap = use; }
        void SetAlbedoTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path = "");
        void SetAlbedoPath(const std::string& path);
        void ClearAlbedoTexture();

        const std::string& GetNormalPath() const { return m_NormalPath; }
        const std::shared_ptr<Texture2D>& GetNormalMap() const { return m_NormalMap; }
        bool UseNormalMap() const { return m_UseNormalMap && (m_NormalMap != nullptr); }
        bool IsNormalMapEnabled() const { return m_UseNormalMap; }
        void SetUseNormalMap(bool use) { m_UseNormalMap = use; }
        void SetNormalTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path = "");
        void SetNormalPath(const std::string& path);
        void ClearNormalTexture();

        const std::string& GetRoughnessPath() const { return m_RoughnessPath; }
        const std::shared_ptr<Texture2D>& GetRoughnessMap() const { return m_RoughnessMap; }
        bool UseRoughnessMap() const { return m_UseRoughnessMap && (m_RoughnessMap != nullptr); }
        bool IsRoughnessMapEnabled() const { return m_UseRoughnessMap; }
        void SetUseRoughnessMap(bool use) { m_UseRoughnessMap = use; }
        void SetRoughnessTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path = "");
        void SetRoughnessPath(const std::string& path);
        void ClearRoughnessTexture();

        const std::string& GetMetallicPath() const { return m_MetallicPath; }
        const std::shared_ptr<Texture2D>& GetMetallicMap() const { return m_MetallicMap; }
        bool UseMetallicMap() const { return m_UseMetallicMap && (m_MetallicMap != nullptr); }
        bool IsMetallicMapEnabled() const { return m_UseMetallicMap; }
        void SetUseMetallicMap(bool use) { m_UseMetallicMap = use; }
        void SetMetallicTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path = "");
        void SetMetallicPath(const std::string& path);
        void ClearMetallicTexture();

        float GetAO() const { return m_AOStrength; }
        float& GetAORef() { return m_AOStrength; }
        void SetAO(float ao) { m_AOStrength = std::clamp(ao, 0.0f, 1.0f); }
        const std::string& GetAOPath() const { return m_AOPath; }
        const std::shared_ptr<Texture2D>& GetAOMap() const { return m_AOMap; }
        bool UseAOMap() const { return m_UseAOMap && (m_AOMap != nullptr); }
        bool IsAOMapEnabled() const { return m_UseAOMap; }
        void SetUseAOMap(bool use) { m_UseAOMap = use; }
        void SetAOTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path = "");
        void SetAOPath(const std::string& path);
        void ClearAOTexture();

        const std::string& GetEmissivePath() const { return m_EmissivePath; }
        const std::shared_ptr<Texture2D>& GetEmissiveMap() const { return m_EmissiveMap; }
        bool UseEmissiveMap() const { return m_UseEmissiveMap && (m_EmissiveMap != nullptr); }
        bool IsEmissiveMapEnabled() const { return m_UseEmissiveMap; }
        void SetUseEmissiveMap(bool use) { m_UseEmissiveMap = use; }
        void SetEmissiveTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path = "");
        void SetEmissivePath(const std::string& path);
        void ClearEmissiveTexture();

        void ResetTextures();

        // Presets
        void SetPresetCarPaintRed();
        void SetPresetCarPaintBlue();
        void SetPresetCarPaintSilver();
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
        void SetPresetGlass();
        void SetPresetFoliage();

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

        SurfaceType m_SurfaceType = SurfaceType::Opaque;
        CullMode m_CullMode = CullMode::Back;
        float m_AlphaCutoff = 0.5f;
        bool m_DepthWrite = true;
        bool m_EnableSpecularHighlights = true;
        bool m_EnableEnvironmentReflections = true;
        int m_RenderQueue = 2000;
        bool m_UseCustomShader = false;
        std::string m_CustomShaderPath;
        std::vector<ShaderPropertyInstance> m_CustomProperties;

        glm::vec4 m_AlbedoColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float m_Roughness = 0.5f;
        float m_Metallic = 0.0f;
        float m_NormalStrength = 1.0f;
        float m_DetailNormalStrength = 1.0f;
        glm::vec2 m_DetailUVScale = {1.0f,1.0f};
        glm::vec2 m_DetailUVOffset = {0.0f,0.0f};
        float m_ClearcoatStrength = 0.0f;
        float m_ClearcoatRoughness = 0.04f;
        float m_AnisotropyStrength = 0.0f;
        float m_AnisotropyRotation = 0.0f;
        glm::vec3 m_EmissionColor{ 0.0f, 0.0f, 0.0f };
        float m_EmissionIntensity = 0.0f;
        glm::vec2 m_UVScale{ 1.0f, 1.0f };
        glm::vec2 m_UVOffset{ 0.0f, 0.0f };

        TextureMode m_TextureMode = TextureMode::SeparateMaps;
        std::string m_ClearcoatPath;
        std::shared_ptr<Texture2D> m_ClearcoatMap;
        bool m_UseClearcoatMap=false;
        std::string m_AnisotropyFlowPath;
        std::shared_ptr<Texture2D> m_AnisotropyFlowMap;
        bool m_UseAnisotropyFlowMap=false;
        std::string m_DetailNormalPath;
        std::shared_ptr<Texture2D> m_DetailNormalMap;
        bool m_UseDetailNormalMap=false;
        std::string m_DetailMaskPath;
        std::shared_ptr<Texture2D> m_DetailMaskMap;
        bool m_UseDetailMaskMap=false;
        std::string m_ORMPath;
        std::shared_ptr<Texture2D> m_ORMMap;
        bool m_UseORMMap = false;

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

        float m_AOStrength = 1.0f;
        std::string m_AOPath;
        std::shared_ptr<Texture2D> m_AOMap;
        bool m_UseAOMap = false;

        std::string m_EmissivePath;
        std::shared_ptr<Texture2D> m_EmissiveMap;
        bool m_UseEmissiveMap = false;
    };

}
