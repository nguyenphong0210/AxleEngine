#include <Axle/Material.hpp>
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

    void Material::ResetTextures() {
        ClearAlbedoTexture();
        ClearNormalTexture();
        ClearRoughnessTexture();
        ClearMetallicTexture();
        ClearAOTexture();
        ClearEmissiveTexture();
    }

    void Material::SetPresetCarPaintRed() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.85f, 0.06f, 0.08f, 1.0f);
        m_Roughness = 0.15f;
        m_Metallic = 0.0f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
    }

    void Material::SetPresetCarPaintBlue() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.08f, 0.28f, 0.65f, 1.0f);
        m_Roughness = 0.22f;
        m_Metallic = 0.75f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
    }

    void Material::SetPresetCarPaintSilver() {
        ResetTextures();
        m_AlbedoColor = glm::vec4(0.92f, 0.93f, 0.96f, 1.0f);
        m_Roughness = 0.14f;
        m_Metallic = 0.92f;
        m_EmissionColor = glm::vec3(0.0f);
        m_EmissionIntensity = 0.0f;
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

        shader.SetFloat2("u_UVScale", m_UVScale);
        shader.SetFloat("u_Roughness", m_Roughness);
        shader.SetFloat("u_Metallic", m_Metallic);
        shader.SetFloat("u_NormalStrength", m_NormalStrength);
        shader.SetFloat3("u_EmissionColor", m_EmissionColor);
        shader.SetFloat("u_EmissionIntensity", m_EmissionIntensity);
        shader.SetFloat4("u_Color1", m_AlbedoColor);
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

        fout << "MaterialAsset:\n";
        if (!m_Name.empty()) {
            fout << "  Name: " << m_Name << "\n";
        }
        fout << "  AlbedoColor: [ " << m_AlbedoColor.r << " , " << m_AlbedoColor.g << " , " << m_AlbedoColor.b << " , " << m_AlbedoColor.a << " ]\n";
        fout << "  AlbedoPath: " << m_AlbedoPath << "\n";
        fout << "  UseAlbedoMap: " << (m_UseAlbedoMap ? "true" : "false") << "\n";
        fout << "  NormalPath: " << m_NormalPath << "\n";
        fout << "  UseNormalMap: " << (m_UseNormalMap ? "true" : "false") << "\n";
        fout << "  NormalStrength: " << m_NormalStrength << "\n";
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

        fout.close();
        return true;
    }

    bool Material::Load(const std::filesystem::path& filepath) {
        std::string content = FileSystem::ReadTextFile(filepath);
        if (content.empty() && !FileSystem::Exists(filepath)) {
            AXLE_ERROR("Material::Load: Could not read file '{0}'", filepath.string());
            return false;
        }

        m_FilePath = filepath;
        if (m_Name.empty() || m_Name == "New Material") {
            m_Name = filepath.stem().string();
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
                } else if (trimmed.rfind("UVScale:", 0) == 0) {
                    float x = 1, y = 1;
                    std::sscanf(trimmed.c_str(), "UVScale: [ %f , %f ]", &x, &y);
                    m_UVScale = glm::vec2(x, y);
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

}
