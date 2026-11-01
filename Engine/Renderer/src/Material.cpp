#include <Axle/Material.hpp>
#include <Axle/ShaderPropertyPersistence.hpp>
#include <Axle/Window.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <GLFW/glfw3.h>
#include <fstream>
#include <sstream>
#include <cstdio>

namespace Axle {

    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    Material::Material(const std::string& name, UUID handle)
        : m_Handle(handle), m_Name(name) {
    }

    void Material::SetAlbedoTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path) {
        m_AlbedoMap = texture;
        m_AlbedoPath = path;
        m_UseAlbedoMap = (texture != nullptr && texture->IsLoaded());
    }

    void Material::SetAlbedoPath(const std::string& path) {
        m_AlbedoPath = path;
        if (path.empty()) {
            ClearAlbedoTexture();
        } else {
            if (Window::HasActiveContext()) {
                std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
                m_AlbedoMap = Texture2D::Create(resolved.string());
                m_UseAlbedoMap = (m_AlbedoMap && m_AlbedoMap->IsLoaded());
            } else {
                m_UseAlbedoMap = true;
            }
        }
    }

    void Material::ClearAlbedoTexture() {
        m_AlbedoPath.clear();
        m_AlbedoMap = nullptr;
        m_UseAlbedoMap = false;
    }

    void Material::SetNormalTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path) {
        m_NormalMap = texture;
        m_NormalPath = path;
        m_UseNormalMap = (texture != nullptr && texture->IsLoaded());
    }

    void Material::SetNormalPath(const std::string& path) {
        m_NormalPath = path;
        if (path.empty()) {
            ClearNormalTexture();
        } else {
            if (Window::HasActiveContext()) {
                std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
                m_NormalMap = Texture2D::Create(resolved.string());
                m_UseNormalMap = (m_NormalMap && m_NormalMap->IsLoaded());
            } else {
                m_UseNormalMap = true;
            }
        }
    }

    void Material::ClearNormalTexture() {
        m_NormalPath.clear();
        m_NormalMap = nullptr;
        m_UseNormalMap = false;
    }

    void Material::SetRoughnessTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path) {
        m_RoughnessMap = texture;
        m_RoughnessPath = path;
        m_UseRoughnessMap = (texture != nullptr && texture->IsLoaded());
    }

    void Material::SetRoughnessPath(const std::string& path) {
        m_RoughnessPath = path;
        if (path.empty()) {
            ClearRoughnessTexture();
        } else {
            if (Window::HasActiveContext()) {
                std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
                m_RoughnessMap = Texture2D::Create(resolved.string());
                m_UseRoughnessMap = (m_RoughnessMap && m_RoughnessMap->IsLoaded());
            } else {
                m_UseRoughnessMap = true;
            }
        }
    }

    void Material::ClearRoughnessTexture() {
        m_RoughnessPath.clear();
        m_RoughnessMap = nullptr;
        m_UseRoughnessMap = false;
    }

    void Material::SetMetallicTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path) {
        m_MetallicMap = texture;
        m_MetallicPath = path;
        m_UseMetallicMap = (texture != nullptr && texture->IsLoaded());
    }

    void Material::SetMetallicPath(const std::string& path) {
        m_MetallicPath = path;
        if (path.empty()) {
            ClearMetallicTexture();
        } else {
            if (Window::HasActiveContext()) {
                std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
                m_MetallicMap = Texture2D::Create(resolved.string());
                m_UseMetallicMap = (m_MetallicMap && m_MetallicMap->IsLoaded());
            } else {
                m_UseMetallicMap = true;
            }
        }
    }

    void Material::ClearMetallicTexture() {
        m_MetallicPath.clear();
        m_MetallicMap = nullptr;
        m_UseMetallicMap = false;
    }

    void Material::SetAOTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path) {
        m_AOMap = texture;
        m_AOPath = path;
        m_UseAOMap = (texture != nullptr && texture->IsLoaded());
    }

    void Material::SetAOPath(const std::string& path) {
        m_AOPath = path;
        if (path.empty()) {
            ClearAOTexture();
        } else {
            if (Window::HasActiveContext()) {
                std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
                m_AOMap = Texture2D::Create(resolved.string());
                m_UseAOMap = (m_AOMap && m_AOMap->IsLoaded());
            } else {
                m_UseAOMap = true;
            }
        }
    }

    void Material::ClearAOTexture() {
        m_AOPath.clear();
        m_AOMap = nullptr;
        m_UseAOMap = false;
    }

    void Material::SetEmissiveTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path) {
        m_EmissiveMap = texture;
        m_EmissivePath = path;
        m_UseEmissiveMap = (texture != nullptr && texture->IsLoaded());
    }

    void Material::SetEmissivePath(const std::string& path) {
        m_EmissivePath = path;
        if (path.empty()) {
            ClearEmissiveTexture();
        } else {
            if (Window::HasActiveContext()) {
                std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
                m_EmissiveMap = Texture2D::Create(resolved.string());
                m_UseEmissiveMap = (m_EmissiveMap && m_EmissiveMap->IsLoaded());
            } else {
                m_UseEmissiveMap = true;
            }
        }
    }

    void Material::ClearEmissiveTexture() {
        m_EmissivePath.clear();
        m_EmissiveMap = nullptr;
        m_UseEmissiveMap = false;
    }

    void Material::SetORMTexture(const std::shared_ptr<Texture2D>& texture, const std::string& path) {
        m_ORMMap = texture;
        m_ORMPath = path;
        m_UseORMMap = (m_ORMMap != nullptr);
    }

    void Material::SetORMPath(const std::string& path) {
        m_ORMPath = path;
        if (!path.empty()) {
            if (Window::HasActiveContext()) {
                auto resolved = FileSystem::ResolveAssetPath(path);
                m_ORMMap = Texture2D::Create(resolved.string());
                m_UseORMMap = (m_ORMMap && m_ORMMap->IsLoaded());
            } else {
                m_UseORMMap = true;
            }
        } else {
            m_ORMMap = nullptr;
            m_UseORMMap = false;
        }
    }

    void Material::ClearORMTexture() {
        m_ORMMap = nullptr;
        m_ORMPath.clear();
        m_UseORMMap = false;
    }

    void Material::ResetTextures() {
        m_AnisotropyFlowPath.clear(); m_AnisotropyFlowMap.reset(); m_UseAnisotropyFlowMap=false;
        m_DetailNormalPath.clear(); m_DetailNormalMap.reset(); m_UseDetailNormalMap=false;
        m_DetailMaskPath.clear(); m_DetailMaskMap.reset(); m_UseDetailMaskMap=false;
        m_ClearcoatPath.clear(); m_ClearcoatMap.reset(); m_UseClearcoatMap=false;
        ClearAlbedoTexture();
        ClearNormalTexture();
        ClearRoughnessTexture();
        ClearMetallicTexture();
        ClearAOTexture();
        ClearEmissiveTexture();
        ClearORMTexture();
        m_TextureMode = TextureMode::SeparateMaps;
        m_ClearcoatStrength = 0.0f;
        m_ClearcoatRoughness = 0.04f;
        m_AnisotropyStrength = 0.0f;
        m_AnisotropyRotation = 0.0f;
        m_SurfaceType = SurfaceType::Opaque;
        m_CullMode = CullMode::Back;
        m_AlphaCutoff = 0.5f;
        m_DepthWrite = true;
        m_UVOffset = glm::vec2(0.0f);
    }

    void Material::SetPresetCarPaintRed() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.85f, 0.06f, 0.08f, 1.0f);
        m_Roughness = 0.15f;
        m_Metallic = 0.0f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
        m_ClearcoatStrength = 1.0f;
        m_ClearcoatRoughness = 0.03f;
    }

    void Material::SetPresetCarPaintBlue() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.08f, 0.28f, 0.65f, 1.0f);
        m_Roughness = 0.22f;
        m_Metallic = 0.75f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
        m_ClearcoatStrength = 1.0f;
        m_ClearcoatRoughness = 0.03f;
    }

    void Material::SetPresetCarPaintSilver() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.92f, 0.93f, 0.96f, 1.0f);
        m_Roughness = 0.14f;
        m_Metallic = 0.92f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
        m_ClearcoatStrength = 1.0f;
        m_ClearcoatRoughness = 0.03f;
    }

    void Material::SetPresetChrome() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.95f, 0.95f, 0.95f, 1.0f);
        m_Roughness = 0.04f;
        m_Metallic = 1.0f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
    }

    void Material::SetPresetGold() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(1.00f, 0.77f, 0.34f, 1.0f);
        m_Roughness = 0.18f;
        m_Metallic = 1.0f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
    }

    void Material::SetPresetCopper() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.96f, 0.64f, 0.54f, 1.0f);
        m_Roughness = 0.35f;
        m_Metallic = 0.95f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
    }

    void Material::SetPresetRubber() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.07f, 0.07f, 0.07f, 1.0f);
        m_Roughness = 0.85f;
        m_Metallic = 0.0f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
    }

    void Material::SetPresetAluminum() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.91f, 0.92f, 0.92f, 1.0f);
        m_Roughness = 0.25f;
        m_Metallic = 0.90f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
    }

    void Material::SetPresetObsidian() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.12f, 0.12f, 0.14f, 1.0f);
        m_Roughness = 0.08f;
        m_Metallic = 0.05f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
    }

    void Material::SetPresetPlastic() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.80f, 0.80f, 0.80f, 1.0f);
        m_Roughness = 0.40f;
        m_Metallic = 0.00f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
    }

    void Material::SetPresetCastIron() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.25f, 0.25f, 0.26f, 1.0f);
        m_Roughness = 0.70f;
        m_Metallic = 0.85f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
    }

    void Material::SetPresetHeadlightLED() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        m_Roughness = 0.05f;
        m_Metallic = 0.0f;
        m_EmissionColor = glm::vec3(1.0f, 0.95f, 0.85f);
        m_EmissionIntensity = 15.0f;
    }

    void Material::SetPresetTaillightLED() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.85f, 0.05f, 0.05f, 1.0f);
        m_Roughness = 0.10f;
        m_Metallic = 0.0f;
        m_EmissionColor = glm::vec3(1.0f, 0.05f, 0.05f);
        m_EmissionIntensity = 12.0f;
        m_SurfaceType = SurfaceType::Additive;
        m_DepthWrite = false;
    }

    void Material::SetPresetGlass() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.92f, 0.96f, 1.0f, 0.35f);
        m_Roughness = 0.04f;
        m_Metallic = 0.0f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
        m_SurfaceType = SurfaceType::Transparent;
        m_CullMode = CullMode::None;
        m_DepthWrite = false;
    }

    void Material::SetPresetFoliage() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.20f, 0.65f, 0.15f, 1.0f);
        m_Roughness = 0.60f;
        m_Metallic = 0.0f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
        m_SurfaceType = SurfaceType::Cutout;
        m_CullMode = CullMode::None;
        m_AlphaCutoff = 0.5f;
        m_DepthWrite = true;
    }

    void Material::Bind(Shader& shader, uint32_t slotAlbedo, uint32_t slotNormal, uint32_t slotRoughness, uint32_t slotMetallic) const {
        if (UseAlbedoMap() && m_AlbedoMap->IsLoaded()) {
            m_AlbedoMap->Bind(slotAlbedo);
            shader.SetInt("u_UseAlbedoMap", 1);
        } else {
            Texture2D::GetWhiteTexture()->Bind(slotAlbedo);
            shader.SetInt("u_UseAlbedoMap", 0);
        }

        if (UseNormalMap() && m_NormalMap->IsLoaded()) {
            m_NormalMap->Bind(slotNormal);
            shader.SetInt("u_UseNormalMap", 1);
        } else {
            Texture2D::GetFlatNormalTexture()->Bind(slotNormal);
            shader.SetInt("u_UseNormalMap", 0);
        }

        if (UseRoughnessMap() && m_RoughnessMap->IsLoaded()) {
            m_RoughnessMap->Bind(slotRoughness);
            shader.SetInt("u_UseRoughnessMap", 1);
        } else {
            Texture2D::GetDefaultRoughnessTexture()->Bind(slotRoughness);
            shader.SetInt("u_UseRoughnessMap", 0);
        }

        if (UseMetallicMap() && m_MetallicMap->IsLoaded()) {
            m_MetallicMap->Bind(slotMetallic);
            shader.SetInt("u_UseMetallicMap", 1);
        } else {
            Texture2D::GetBlackTexture()->Bind(slotMetallic);
            shader.SetInt("u_UseMetallicMap", 0);
        }

        if (m_TextureMode == TextureMode::PackedORM && UseORMMap() && m_ORMMap->IsLoaded()) {
            m_ORMMap->Bind(slotRoughness);
            shader.SetInt("u_TextureMode", 1);
        } else {
            shader.SetInt("u_TextureMode", 0);
        }
        shader.SetFloat2("u_UVScale", m_UVScale);
        shader.SetFloat2("u_UVOffset", m_UVOffset);
        shader.SetFloat("u_Roughness", m_Roughness);
        shader.SetFloat("u_Metallic", m_Metallic);
        shader.SetFloat("u_NormalStrength", m_NormalStrength);
        shader.SetFloat3("u_EmissionColor", m_EmissionColor);
        shader.SetFloat("u_EmissionIntensity", m_EmissionIntensity);
        shader.SetFloat4("u_Color1", m_AlbedoColor);
        shader.SetInt("u_SurfaceType", static_cast<int>(m_SurfaceType));
        shader.SetFloat("u_AlphaCutoff", m_AlphaCutoff);
        shader.SetFloat("u_ClearcoatStrength", m_ClearcoatStrength);
        shader.SetFloat("u_ClearcoatRoughness", m_ClearcoatRoughness);
        shader.SetFloat("u_AnisotropyStrength", m_AnisotropyStrength);
        shader.SetFloat("u_AnisotropyRotation", m_AnisotropyRotation);
    }

    void Material::BindDefault(Shader& shader, const glm::vec4& color1, const glm::vec4& color2,
                               uint32_t slotAlbedo, uint32_t slotNormal, uint32_t slotRoughness, uint32_t slotMetallic) {
        Texture2D::GetWhiteTexture()->Bind(slotAlbedo);
        Texture2D::GetFlatNormalTexture()->Bind(slotNormal);
        Texture2D::GetDefaultRoughnessTexture()->Bind(slotRoughness);
        Texture2D::GetBlackTexture()->Bind(slotMetallic);

        shader.SetInt("u_UseAlbedoMap", 0);
        shader.SetInt("u_UseNormalMap", 0);
        shader.SetInt("u_UseRoughnessMap", 0);
        shader.SetInt("u_UseMetallicMap", 0);
        shader.SetFloat2("u_UVScale", glm::vec2(1.0f, 1.0f));
        shader.SetFloat("u_Roughness", 0.5f);
        shader.SetFloat("u_Metallic", 0.0f);
        shader.SetFloat("u_NormalStrength", 1.0f);
        shader.SetFloat3("u_EmissionColor", glm::vec3(0.0f));
        shader.SetFloat("u_EmissionIntensity", 0.0f);
        shader.SetFloat4("u_Color1", color1);
        shader.SetFloat4("u_Color2", color2);
    }

    bool Material::Save(const std::filesystem::path& filepath) const {
        std::ofstream fout(filepath);
        if (!fout.is_open()) {
            AXLE_ERROR("Material::Save: Failed to open file for writing: '{0}'", filepath.string());
            return false;
        }

        std::string ext = filepath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
        if (ext == ".axleasset" || ext.empty()) {
            fout << "Material {\n";
            fout << "    name: \"" << m_Name << "\"\n";
            fout << "    shader: \"ForwardPBR\"\n";
            fout << "    useCustomShader: " << (m_UseCustomShader ? "true" : "false") << "\n";
            if (!m_CustomShaderPath.empty()) {
                fout << "    customShaderPath: \"" << m_CustomShaderPath << "\"\n";
            }
            fout << "\n";
            fout << "    // Texture Packing Mode\n";
            fout << "    textureMode: \"" << TextureModeToString(m_TextureMode) << "\"\n";
            if (!m_ORMPath.empty()) {
                fout << "    ormMap: \"" << m_ORMPath << "\"\n";
            }

            fout << "    // Surface Options\n";
            fout << "    surfaceType: \"" << SurfaceTypeToString(m_SurfaceType) << "\"\n";
            fout << "    cullMode: \"" << CullModeToString(m_CullMode) << "\"\n";
            fout << "    alphaCutoff: " << m_AlphaCutoff << "\n";
            fout << "    depthWrite: " << (m_DepthWrite ? "true" : "false") << "\n";
            fout << "    specularHighlights: " << (m_EnableSpecularHighlights ? "true" : "false") << "\n";
            fout << "    environmentReflections: " << (m_EnableEnvironmentReflections ? "true" : "false") << "\n";
            fout << "    renderQueue: " << m_RenderQueue << "\n\n";
            fout << "    // Base Color & Texture\n";
            fout << "    albedoColor: [ " << m_AlbedoColor.r << " , " << m_AlbedoColor.g << " , " << m_AlbedoColor.b << " , " << m_AlbedoColor.a << " ]\n";
            fout << "    albedoMap: \"" << m_AlbedoPath << "\"\n\n";
            fout << "    // Surface Microfacet\n";
            fout << "    metallic: " << m_Metallic << "\n";
            fout << "    roughness: " << m_Roughness << "\n";
            fout << "    normalMap: \"" << m_NormalPath << "\"\n";
            fout << "    roughnessMap: \"" << m_RoughnessPath << "\"\n";
            fout << "    metallicMap: \"" << m_MetallicPath << "\"\n";
            fout << "    emissiveMap: \"" << m_EmissivePath << "\"\n";
            fout << "    normalStrength: " << m_NormalStrength << "\n";
            fout << "    detailNormalStrength: " << m_DetailNormalStrength << "\n";
            fout << "    detailUVScale: " << "[ " << m_DetailUVScale.x << " , " << m_DetailUVScale.y << " ]\n";
            fout << "    detailUVOffset: " << "[ " << m_DetailUVOffset.x << " , " << m_DetailUVOffset.y << " ]\n";
            fout << "    clearcoatStrength: " << m_ClearcoatStrength << "\n";
            fout << "    clearcoatRoughness: " << m_ClearcoatRoughness << "\n";
            fout << "    anisotropyStrength: " << m_AnisotropyStrength << "\n";
            fout << "    anisotropyRotation: " << m_AnisotropyRotation << "\n";
            fout << "    occlusionMap: \"" << m_AOPath << "\"\n";
            fout << "    occlusionStrength: " << m_AOStrength << "\n\n";
            fout << "    // Emission & UV Scaling\n";
            fout << "    emissionColor: [ " << m_EmissionColor.r << " , " << m_EmissionColor.g << " , " << m_EmissionColor.b << " ]\n";
            fout << "    emissionIntensity: " << m_EmissionIntensity << "\n";
            fout << "    uvTiling: [ " << m_UVScale.x << " , " << m_UVScale.y << " ]\n";
            fout << "    uvOffset: [ " << m_UVOffset.x << " , " << m_UVOffset.y << " ]\n";
            fout << "    useORMMap: " << (m_UseORMMap ? "true" : "false") << "\n";
            fout << "    useAlbedoMap: " << (m_UseAlbedoMap ? "true" : "false") << "\n";
            fout << "    useNormalMap: " << (m_UseNormalMap ? "true" : "false") << "\n";
            fout << "    useRoughnessMap: " << (m_UseRoughnessMap ? "true" : "false") << "\n";
            fout << "    useMetallicMap: " << (m_UseMetallicMap ? "true" : "false") << "\n";
            fout << "    useAOMap: " << (m_UseAOMap ? "true" : "false") << "\n";
            fout << "    useEmissiveMap: " << (m_UseEmissiveMap ? "true" : "false") << "\n";
            fout << "    clearcoatMap: \"" << m_ClearcoatPath << "\"\n";
            fout << "    useClearcoatMap: " << (m_UseClearcoatMap ? "true" : "false") << "\n";
            fout << "    anisotropyFlowMap: \"" << m_AnisotropyFlowPath << "\"\n";
            fout << "    useAnisotropyFlowMap: " << (m_UseAnisotropyFlowMap?"true":"false") << "\n";
            fout << "    detailNormalMap: \"" << m_DetailNormalPath << "\"\n";
            fout << "    useDetailNormalMap: " << (m_UseDetailNormalMap?"true":"false") << "\n";
            fout << "    detailMaskMap: \"" << m_DetailMaskPath << "\"\n";
            fout << "    useDetailMaskMap: " << (m_UseDetailMaskMap?"true":"false") << "\n";
            WriteShaderProperties(fout,m_CustomProperties);
            fout << "}\n";
            return true;
        }

        fout << "MaterialAsset:\n";
        fout << "  TextureMode: " << TextureModeToString(m_TextureMode) << "\n";
        if (!m_ORMPath.empty()) {
            fout << "  ORMPath: " << m_ORMPath << "\n";
            fout << "  UseORMMap: " << (m_UseORMMap ? "true" : "false") << "\n";
        }

        if (!m_Name.empty()) {
            fout << "  Name: " << m_Name << "\n";
        }
        fout << "  SurfaceType: " << SurfaceTypeToString(m_SurfaceType) << "\n";
        fout << "  CullMode: " << CullModeToString(m_CullMode) << "\n";
        fout << "  AlphaCutoff: " << m_AlphaCutoff << "\n";
        fout << "  DepthWrite: " << (m_DepthWrite ? "true" : "false") << "\n";
        fout << "  AlbedoColor: [ " << m_AlbedoColor.r << " , " << m_AlbedoColor.g << " , " << m_AlbedoColor.b << " , " << m_AlbedoColor.a << " ]\n";
        fout << "  AlbedoPath: " << m_AlbedoPath << "\n";
        fout << "  UseAlbedoMap: " << (m_UseAlbedoMap ? "true" : "false") << "\n";
        fout << "  NormalPath: " << m_NormalPath << "\n";
        fout << "  UseNormalMap: " << (m_UseNormalMap ? "true" : "false") << "\n";
        fout << "  NormalStrength: " << m_NormalStrength << "\n";
        fout << "  ClearcoatStrength: " << m_ClearcoatStrength << "\n";
        fout << "  ClearcoatRoughness: " << m_ClearcoatRoughness << "\n";
        fout << "  AnisotropyStrength: " << m_AnisotropyStrength << "\n";
        fout << "  AnisotropyRotation: " << m_AnisotropyRotation << "\n";
        fout << "  Roughness: " << m_Roughness << "\n";
        fout << "  RoughnessPath: " << m_RoughnessPath << "\n";
        fout << "  UseRoughnessMap: " << (m_UseRoughnessMap ? "true" : "false") << "\n";
        fout << "  Metallic: " << m_Metallic << "\n";
        fout << "  MetallicPath: " << m_MetallicPath << "\n";
        fout << "  UseMetallicMap: " << (m_UseMetallicMap ? "true" : "false") << "\n";
        fout << "  AO: " << m_AOStrength << "\n";
        fout << "  AOPath: " << m_AOPath << "\n";
        fout << "  UseAOMap: " << (m_UseAOMap ? "true" : "false") << "\n";
        fout << "  EmissivePath: " << m_EmissivePath << "\n";
        fout << "  UseEmissiveMap: " << (m_UseEmissiveMap ? "true" : "false") << "\n";
        fout << "  EmissionColor: [ " << m_EmissionColor.r << " , " << m_EmissionColor.g << " , " << m_EmissionColor.b << " ]\n";
        fout << "  EmissionIntensity: " << m_EmissionIntensity << "\n";
        fout << "  UVScale: [ " << m_UVScale.x << " , " << m_UVScale.y << " ]\n";
        fout << "  UVOffset: [ " << m_UVOffset.x << " , " << m_UVOffset.y << " ]\n";

        fout.close();
        return true;
    }

    bool Material::Load(const std::filesystem::path& filepath) {
        std::string content = FileSystem::ReadTextFile(filepath);
        if (content.empty() && !FileSystem::Exists(filepath)) {
            AXLE_ERROR("Material::Load: Could not read file '{0}'", filepath.string());
            return false;
        }

        // Preserve identity while resetting fields absent from the replacement asset.
        *this = Material(m_Name, m_Handle);
        m_FilePath = filepath;
        if (m_Name.empty() || m_Name == "New Material") {
            m_Name = filepath.stem().string();
        }

        // Support declarative .axleasset Material { ... }
        if (content.find("Material {") != std::string::npos || content.find("Material{") != std::string::npos) {
            std::istringstream stream(content);
            std::string line;
            while (std::getline(stream, line)) {
                std::string trimmed = Trim(line);
                if (trimmed.empty() || trimmed[0] == '/' || trimmed[0] == '#') continue;
                if (trimmed.rfind("name:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(5));
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
                    m_Name = val;
                } else if (trimmed.rfind("useCustomShader:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(16));
                    m_UseCustomShader = (val == "true" || val == "1");
                } else if (trimmed.rfind("customShaderPath:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(17));
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
                    m_CustomShaderPath = val;
                    if (!m_CustomShaderPath.empty()) m_UseCustomShader = true;
                } else if (trimmed.rfind("surfaceType:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(12));
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
                    m_SurfaceType = StringToSurfaceType(val);
                } else if (trimmed.rfind("cullMode:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(9));
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
                    m_CullMode = StringToCullMode(val);
                } else if (trimmed.rfind("alphaCutoff:", 0) == 0) {
                    try { m_AlphaCutoff = std::clamp(std::stof(Trim(trimmed.substr(12))), 0.0f, 1.0f); } catch (...) {}
                } else if (trimmed.rfind("depthWrite:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(11));
                    m_DepthWrite = (val == "true" || val == "1");
                } else if (trimmed.rfind("specularHighlights:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(19));
                    m_EnableSpecularHighlights = (val == "true" || val == "1");
                } else if (trimmed.rfind("environmentReflections:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(23));
                    m_EnableEnvironmentReflections = (val == "true" || val == "1");
                } else if (ReadShaderPropertyLine(trimmed,m_CustomProperties)) {
                    // Typed values and range metadata share the scene/material schema.
                } else if (trimmed.rfind("renderQueue:", 0) == 0) {
                    try { m_RenderQueue = std::stoi(Trim(trimmed.substr(12))); } catch (...) {}
                } else if (trimmed.rfind("useORMMap:", 0) == 0) {
                    m_UseORMMap = Trim(trimmed.substr(10)) == "true";
                } else if (trimmed.rfind("useAlbedoMap:", 0) == 0) {
                    m_UseAlbedoMap = Trim(trimmed.substr(13)) == "true";
                } else if (trimmed.rfind("useNormalMap:", 0) == 0) {
                    m_UseNormalMap = Trim(trimmed.substr(13)) == "true";
                } else if (trimmed.rfind("useRoughnessMap:", 0) == 0) {
                    m_UseRoughnessMap = Trim(trimmed.substr(16)) == "true";
                } else if (trimmed.rfind("useMetallicMap:", 0) == 0) {
                    m_UseMetallicMap = Trim(trimmed.substr(15)) == "true";
                } else if (trimmed.rfind("useAOMap:", 0) == 0) {
                    m_UseAOMap = Trim(trimmed.substr(9)) == "true";
                } else if (trimmed.rfind("useEmissiveMap:", 0) == 0) {
                    m_UseEmissiveMap = Trim(trimmed.substr(15)) == "true";
                } else if (trimmed.rfind("roughnessMap:", 0) == 0) {
                    std::string value = Trim(trimmed.substr(13));
                    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size()-2);
                    SetRoughnessPath(value);
                    m_UseRoughnessMap = !value.empty();
                } else if (trimmed.rfind("metallicMap:", 0) == 0) {
                    std::string value = Trim(trimmed.substr(12));
                    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size()-2);
                    SetMetallicPath(value);
                    m_UseMetallicMap = !value.empty();
                } else if (trimmed.rfind("emissiveMap:", 0) == 0) {
                    std::string value = Trim(trimmed.substr(12));
                    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size()-2);
                    SetEmissivePath(value);
                    m_UseEmissiveMap = !value.empty();
                } else if (trimmed.rfind("albedoColor:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(12));
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
                    if (!val.empty() && val[0] == '#') {
                        auto hexCharToInt = [](char c) -> uint32_t {
                            if (c >= '0' && c <= '9') return c - '0';
                            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                            return 0;
                        };
                        std::string h = val.substr(1);
                        if (h.size() == 6) {
                            uint32_t r = (hexCharToInt(h[0]) << 4) | hexCharToInt(h[1]);
                            uint32_t g = (hexCharToInt(h[2]) << 4) | hexCharToInt(h[3]);
                            uint32_t b = (hexCharToInt(h[4]) << 4) | hexCharToInt(h[5]);
                            m_AlbedoColor = glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
                        } else if (h.size() == 8) {
                            uint32_t r = (hexCharToInt(h[0]) << 4) | hexCharToInt(h[1]);
                            uint32_t g = (hexCharToInt(h[2]) << 4) | hexCharToInt(h[3]);
                            uint32_t b = (hexCharToInt(h[4]) << 4) | hexCharToInt(h[5]);
                            uint32_t a = (hexCharToInt(h[6]) << 4) | hexCharToInt(h[7]);
                            m_AlbedoColor = glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
                        }
                    } else {
                        float r = 1, g = 1, b = 1, a = 1;
                        std::sscanf(val.c_str(), "[ %f , %f , %f , %f ]", &r, &g, &b, &a);
                        m_AlbedoColor = glm::vec4(r, g, b, a);
                    }
                } else if (trimmed.rfind("textureMode:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(12));
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
                    m_TextureMode = StringToTextureMode(val);
                } else if (trimmed.rfind("ormMap:", 0) == 0) {
                    std::string p = Trim(trimmed.substr(7));
                    if (p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
                    if (!p.empty()) { SetORMPath(p); m_UseORMMap = true; }
                    else ClearORMTexture();
                } else if (trimmed.rfind("albedoMap:", 0) == 0) {
                    std::string p = Trim(trimmed.substr(10));
                    if (p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
                    if (!p.empty()) { SetAlbedoPath(p); m_UseAlbedoMap = true; }
                    else ClearAlbedoTexture();
                } else if (trimmed.rfind("metallic:", 0) == 0) {
                    try { SetMetallic(std::stof(Trim(trimmed.substr(9)))); } catch (...) {}
                } else if (trimmed.rfind("roughness:", 0) == 0) {
                    try { SetRoughness(std::stof(Trim(trimmed.substr(10)))); } catch (...) {}
                } else if (trimmed.rfind("normalMap:", 0) == 0) {
                    std::string p = Trim(trimmed.substr(10));
                    if (p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
                    if (!p.empty()) { SetNormalPath(p); m_UseNormalMap = true; }
                    else ClearNormalTexture();
                } else if (trimmed.rfind("clearcoatMap:", 0) == 0) {
                    auto value=Trim(trimmed.substr(13));
                    if (value.size()>=2 && value.front()=='"' && value.back()=='"') value=value.substr(1,value.size()-2);
                    m_ClearcoatPath=value; m_UseClearcoatMap=!value.empty();
                } else if (trimmed.rfind("useClearcoatMap:", 0) == 0) {
                    m_UseClearcoatMap=Trim(trimmed.substr(16))=="true";
                } else if (trimmed.rfind("anisotropyFlowMap:", 0) == 0) {
                    auto value=Trim(trimmed.substr(18));
                    if (value.size()>=2 && value.front()=='"' && value.back()=='"') value=value.substr(1,value.size()-2);
                    m_AnisotropyFlowPath=value; m_UseAnisotropyFlowMap=!value.empty();
                } else if (trimmed.rfind("useAnisotropyFlowMap:", 0) == 0) {
                    m_UseAnisotropyFlowMap=Trim(trimmed.substr(21))=="true";
                } else if (trimmed.rfind("detailNormalMap:", 0) == 0) {
                    auto value=Trim(trimmed.substr(16));
                    if (value.size()>=2 && value.front()=='"' && value.back()=='"') value=value.substr(1,value.size()-2);
                    m_DetailNormalPath=value; m_UseDetailNormalMap=!value.empty();
                } else if (trimmed.rfind("useDetailNormalMap:", 0) == 0) {
                    m_UseDetailNormalMap=Trim(trimmed.substr(19))=="true";
                } else if (trimmed.rfind("detailMaskMap:", 0) == 0) {
                    auto value=Trim(trimmed.substr(14));
                    if (value.size()>=2 && value.front()=='"' && value.back()=='"') value=value.substr(1,value.size()-2);
                    m_DetailMaskPath=value; m_UseDetailMaskMap=!value.empty();
                } else if (trimmed.rfind("useDetailMaskMap:", 0) == 0) {
                    m_UseDetailMaskMap=Trim(trimmed.substr(17))=="true";
                } else if (trimmed.rfind("detailNormalStrength:", 0) == 0) {
                    m_DetailNormalStrength=std::stof(Trim(trimmed.substr(21)));
                } else if (trimmed.rfind("detailUVScale:", 0) == 0) {
                    std::sscanf(trimmed.c_str(), "detailUVScale: [ %f , %f ]", &m_DetailUVScale.x, &m_DetailUVScale.y);
                } else if (trimmed.rfind("detailUVOffset:", 0) == 0) {
                    std::sscanf(trimmed.c_str(), "detailUVOffset: [ %f , %f ]", &m_DetailUVOffset.x, &m_DetailUVOffset.y);
                } else if (trimmed.rfind("clearcoatStrength:", 0) == 0) {
                    try { m_ClearcoatStrength = std::clamp(std::stof(Trim(trimmed.substr(18))), 0.0f, 1.0f); } catch (...) {}
                } else if (trimmed.rfind("clearcoatRoughness:", 0) == 0) {
                    try { m_ClearcoatRoughness = std::clamp(std::stof(Trim(trimmed.substr(19))), 0.001f, 1.0f); } catch (...) {}
                } else if (trimmed.rfind("anisotropyStrength:", 0) == 0) {
                    try { m_AnisotropyStrength = std::clamp(std::stof(Trim(trimmed.substr(19))), -1.0f, 1.0f); } catch (...) {}
                } else if (trimmed.rfind("anisotropyRotation:", 0) == 0) {
                    try { m_AnisotropyRotation = std::clamp(std::stof(Trim(trimmed.substr(19))), 0.0f, 1.0f); } catch (...) {}
                } else if (trimmed.rfind("normalStrength:", 0) == 0) {
                    try { m_NormalStrength = std::stof(Trim(trimmed.substr(15))); } catch (...) {}
                } else if (trimmed.rfind("uvScale:", 0) == 0 || trimmed.rfind("uvTiling:", 0) == 0) {
                    float x = 1, y = 1;
                    const char* fmt = (trimmed.rfind("uvTiling:", 0) == 0) ? "uvTiling: [ %f , %f ]" : "uvScale: [ %f , %f ]";
                    std::sscanf(trimmed.c_str(), fmt, &x, &y);
                    m_UVScale = glm::vec2(x, y);
                } else if (trimmed.rfind("uvOffset:", 0) == 0) {
                    float x = 0, y = 0;
                    std::sscanf(trimmed.c_str(), "uvOffset: [ %f , %f ]", &x, &y);
                    m_UVOffset = glm::vec2(x, y);
                } else if (trimmed.rfind("occlusionMap:", 0) == 0 || trimmed.rfind("aoMap:", 0) == 0) {
                    size_t colon = trimmed.find(':');
                    std::string p = Trim(trimmed.substr(colon + 1));
                    if (p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
                    if (!p.empty()) { SetAOPath(p); m_UseAOMap = true; }
                    else ClearAOTexture();
                } else if (trimmed.rfind("occlusionStrength:", 0) == 0 || trimmed.rfind("ao:", 0) == 0) {
                    size_t colon = trimmed.find(':');
                    try { SetAO(std::stof(Trim(trimmed.substr(colon + 1)))); } catch (...) {}
                } else if (trimmed.rfind("emissionColor:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(14));
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
                    if (!val.empty() && val[0] == '#') {
                        auto hexCharToInt = [](char c) -> uint32_t {
                            if (c >= '0' && c <= '9') return c - '0';
                            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                            return 0;
                        };
                        std::string h = val.substr(1);
                        if (h.size() >= 6) {
                            uint32_t r = (hexCharToInt(h[0]) << 4) | hexCharToInt(h[1]);
                            uint32_t g = (hexCharToInt(h[2]) << 4) | hexCharToInt(h[3]);
                            uint32_t b = (hexCharToInt(h[4]) << 4) | hexCharToInt(h[5]);
                            m_EmissionColor = glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
                        }
                    } else {
                        float r = 0, g = 0, b = 0;
                        std::sscanf(val.c_str(), "[ %f , %f , %f ]", &r, &g, &b);
                        m_EmissionColor = glm::vec3(r, g, b);
                    }
                } else if (trimmed.rfind("emissionIntensity:", 0) == 0) {
                    try { m_EmissionIntensity = std::stof(Trim(trimmed.substr(18))); } catch (...) {}
                }
            }
            return true;
        }

        std::istringstream stream(content);
        std::string line;
        bool inSection = false;

        while (std::getline(stream, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed == "MaterialAsset:" || trimmed == "MaterialComponent:" || trimmed == "RHIMaterial:") {
                inSection = true;
                continue;
            }

            if (inSection) {
                if (trimmed.rfind("Name:", 0) == 0) {
                    m_Name = Trim(trimmed.substr(5));
                } else if (trimmed.rfind("SurfaceType:", 0) == 0) {
                    m_SurfaceType = StringToSurfaceType(Trim(trimmed.substr(12)));
                } else if (trimmed.rfind("CullMode:", 0) == 0) {
                    m_CullMode = StringToCullMode(Trim(trimmed.substr(9)));
                } else if (trimmed.rfind("AlphaCutoff:", 0) == 0) {
                    try { m_AlphaCutoff = std::clamp(std::stof(Trim(trimmed.substr(12))), 0.0f, 1.0f); } catch (...) {}
                } else if (trimmed.rfind("DepthWrite:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(11));
                    m_DepthWrite = (val == "true" || val == "1");
                } else if (trimmed.rfind("TextureMode:", 0) == 0) {
                m_TextureMode = StringToTextureMode(Trim(trimmed.substr(12)));
            } else if (trimmed.rfind("ORMPath:", 0) == 0) {
                SetORMPath(Trim(trimmed.substr(8)));
            } else if (trimmed.rfind("UseORMMap:", 0) == 0) {
                m_UseORMMap = (Trim(trimmed.substr(10)) == "true");
            } else if (trimmed.rfind("AlbedoPath:", 0) == 0) {
                    std::string p = Trim(trimmed.substr(11));
                    if (!p.empty()) SetAlbedoPath(p);
                    else ClearAlbedoTexture();
                } else if (trimmed.rfind("UseAlbedoMap:", 0) == 0) {
                    m_UseAlbedoMap = (Trim(trimmed.substr(13)) == "true");
                } else if (trimmed.rfind("AlbedoColor:", 0) == 0) {
                    float r = 1, g = 1, b = 1, a = 1;
                    std::sscanf(trimmed.c_str(), "AlbedoColor: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
                    m_AlbedoColor = glm::vec4(r, g, b, a);
                } else if (trimmed.rfind("NormalPath:", 0) == 0) {
                    std::string p = Trim(trimmed.substr(11));
                    if (!p.empty()) SetNormalPath(p);
                    else ClearNormalTexture();
                } else if (trimmed.rfind("UseNormalMap:", 0) == 0) {
                    m_UseNormalMap = (Trim(trimmed.substr(13)) == "true");
                } else if (trimmed.rfind("ClearcoatStrength:", 0) == 0) {
                try { m_ClearcoatStrength = std::clamp(std::stof(Trim(trimmed.substr(18))), 0.0f, 1.0f); } catch (...) {}
            } else if (trimmed.rfind("ClearcoatRoughness:", 0) == 0) {
                try { m_ClearcoatRoughness = std::clamp(std::stof(Trim(trimmed.substr(19))), 0.001f, 1.0f); } catch (...) {}
            } else if (trimmed.rfind("AnisotropyStrength:", 0) == 0) {
                try { m_AnisotropyStrength = std::clamp(std::stof(Trim(trimmed.substr(19))), -1.0f, 1.0f); } catch (...) {}
            } else if (trimmed.rfind("AnisotropyRotation:", 0) == 0) {
                try { m_AnisotropyRotation = std::clamp(std::stof(Trim(trimmed.substr(19))), 0.0f, 1.0f); } catch (...) {}
            } else if (trimmed.rfind("NormalStrength:", 0) == 0) {
                    m_NormalStrength = std::stof(Trim(trimmed.substr(15)));
                } else if (trimmed.rfind("RoughnessPath:", 0) == 0) {
                    std::string p = Trim(trimmed.substr(14));
                    if (!p.empty()) SetRoughnessPath(p);
                    else ClearRoughnessTexture();
                } else if (trimmed.rfind("UseRoughnessMap:", 0) == 0) {
                    m_UseRoughnessMap = (Trim(trimmed.substr(16)) == "true");
                } else if (trimmed.rfind("Roughness:", 0) == 0) {
                    m_Roughness = std::stof(Trim(trimmed.substr(10)));
                } else if (trimmed.rfind("MetallicPath:", 0) == 0) {
                    std::string p = Trim(trimmed.substr(13));
                    if (!p.empty()) SetMetallicPath(p);
                    else ClearMetallicTexture();
                } else if (trimmed.rfind("UseMetallicMap:", 0) == 0) {
                    m_UseMetallicMap = (Trim(trimmed.substr(15)) == "true");
                } else if (trimmed.rfind("Metallic:", 0) == 0) {
                    m_Metallic = std::stof(Trim(trimmed.substr(9)));
                } else if (trimmed.rfind("AOPath:", 0) == 0) {
                    std::string p = Trim(trimmed.substr(7));
                    if (!p.empty()) SetAOPath(p);
                    else ClearAOTexture();
                } else if (trimmed.rfind("UseAOMap:", 0) == 0) {
                    m_UseAOMap = (Trim(trimmed.substr(9)) == "true");
                } else if (trimmed.rfind("AO:", 0) == 0) {
                    m_AOStrength = std::stof(Trim(trimmed.substr(3)));
                } else if (trimmed.rfind("EmissivePath:", 0) == 0) {
                    std::string p = Trim(trimmed.substr(13));
                    if (!p.empty()) SetEmissivePath(p);
                    else ClearEmissiveTexture();
                } else if (trimmed.rfind("UseEmissiveMap:", 0) == 0) {
                    m_UseEmissiveMap = (Trim(trimmed.substr(15)) == "true");
                } else if (trimmed.rfind("EmissionColor:", 0) == 0) {
                    float r = 0, g = 0, b = 0;
                    std::sscanf(trimmed.c_str(), "EmissionColor: [ %f , %f , %f ]", &r, &g, &b);
                    m_EmissionColor = glm::vec3(r, g, b);
                } else if (trimmed.rfind("EmissionIntensity:", 0) == 0) {
                    m_EmissionIntensity = std::stof(Trim(trimmed.substr(18)));
                } else if (trimmed.rfind("UVScale:", 0) == 0 || trimmed.rfind("UVTiling:", 0) == 0) {
                    float x = 1, y = 1;
                    const char* fmt = (trimmed.rfind("UVTiling:", 0) == 0) ? "UVTiling: [ %f , %f ]" : "UVScale: [ %f , %f ]";
                    std::sscanf(trimmed.c_str(), fmt, &x, &y);
                    m_UVScale = glm::vec2(x, y);
                } else if (trimmed.rfind("UVOffset:", 0) == 0) {
                    float x = 0, y = 0;
                    std::sscanf(trimmed.c_str(), "UVOffset: [ %f , %f ]", &x, &y);
                    m_UVOffset = glm::vec2(x, y);
                }
            }
        }

        return true;
    }

    std::shared_ptr<Material> Material::Create(const std::string& name, UUID handle) {
        return std::make_shared<Material>(name, handle);
    }

    std::shared_ptr<Material> Material::LoadFromFile(const std::filesystem::path& filepath) {
        auto mat = std::make_shared<Material>(filepath.stem().string());
        if (mat->Load(filepath)) {
            return mat;
        }
        return nullptr;
    }


    void Material::SetCustomFloat(const std::string& name, float value) {
        for (auto& prop : m_CustomProperties) {
            if (prop.Desc.Name == name) {
                prop.FloatVal = value;
                return;
            }
        }
        ShaderPropertyInstance inst;
        inst.Desc.Name = name;
        inst.Desc.DisplayName = ShaderPropertyParser::FormatDisplayName(name);
        inst.Desc.Type = ShaderPropertyType::Float;
        inst.FloatVal = value;
        m_CustomProperties.push_back(inst);
    }

    float Material::GetCustomFloat(const std::string& name, float defaultVal) const {
        for (const auto& prop : m_CustomProperties) {
            if (prop.Desc.Name == name) return prop.FloatVal;
        }
        return defaultVal;
    }

    void Material::SetCustomVec4(const std::string& name, const glm::vec4& value) {
        for (auto& prop : m_CustomProperties) {
            if (prop.Desc.Name == name) {
                prop.Vec4Val = value;
                return;
            }
        }
        ShaderPropertyInstance inst;
        inst.Desc.Name = name;
        inst.Desc.DisplayName = ShaderPropertyParser::FormatDisplayName(name);
        inst.Desc.Type = ShaderPropertyType::Color;
        inst.Vec4Val = value;
        m_CustomProperties.push_back(inst);
    }

    glm::vec4 Material::GetCustomVec4(const std::string& name, const glm::vec4& defaultVal) const {
        for (const auto& prop : m_CustomProperties) {
            if (prop.Desc.Name == name) return prop.Vec4Val;
        }
        return defaultVal;
    }

    void Material::SetCustomTexture(const std::string& name, const std::string& path) {
        for (auto& prop : m_CustomProperties) {
            if (prop.Desc.Name == name) {
                prop.TexturePath = path;
                return;
            }
        }
        ShaderPropertyInstance inst;
        inst.Desc.Name = name;
        inst.Desc.DisplayName = ShaderPropertyParser::FormatDisplayName(name);
        inst.Desc.Type = ShaderPropertyType::Texture2D;
        inst.TexturePath = path;
        m_CustomProperties.push_back(inst);
    }

    std::string Material::GetCustomTexture(const std::string& name) const {
        for (const auto& prop : m_CustomProperties) {
            if (prop.Desc.Name == name) return prop.TexturePath;
        }
        return "";
    }

}
