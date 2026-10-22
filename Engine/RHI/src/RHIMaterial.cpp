#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <sstream>
#include <cstdio>

namespace Axle::RHI {

    static std::shared_ptr<RHITexture> s_DefaultWhite;
    static std::shared_ptr<RHITexture> s_DefaultFlatNormal;
    static std::shared_ptr<RHITexture> s_DefaultRoughness;
    static std::shared_ptr<RHITexture> s_DefaultBlack;

    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    RHIMaterial::RHIMaterial(const std::string& name) {
        m_Desc.Name = name;
    }

    RHIMaterial::RHIMaterial(const RHIMaterialDesc& desc)
        : m_Desc(desc) {
    }

    void RHIMaterial::SetTexture(RHITextureSlot slot, const std::shared_ptr<RHITexture>& texture, const std::string& path) {
        auto index = static_cast<size_t>(slot);
        if (index >= m_Textures.size()) return;

        m_Textures[index] = texture;
        switch (slot) {
            case RHITextureSlot::Albedo:    m_Desc.AlbedoPath = path; break;
            case RHITextureSlot::Normal:    m_Desc.NormalPath = path; break;
            case RHITextureSlot::Roughness: m_Desc.RoughnessPath = path; break;
            case RHITextureSlot::Metallic:  m_Desc.MetallicPath = path; break;
            case RHITextureSlot::AO:        m_Desc.AOPath = path; break;
            case RHITextureSlot::Emissive:  m_Desc.EmissivePath = path; break;
            default: break;
        }

        if (texture != nullptr) {
            m_Desc.ShadingModel = RHIShadingModel::TexturedPBR;
        }
    }

    std::shared_ptr<RHITexture> RHIMaterial::GetTexture(RHITextureSlot slot) const {
        auto index = static_cast<size_t>(slot);
        if (index < m_Textures.size() && m_Textures[index] != nullptr) {
            return m_Textures[index];
        }

        switch (slot) {
            case RHITextureSlot::Albedo:    return GetDefaultWhiteTexture();
            case RHITextureSlot::Normal:    return GetDefaultFlatNormalTexture();
            case RHITextureSlot::Roughness: return GetDefaultRoughnessTexture();
            case RHITextureSlot::Metallic:  return GetDefaultBlackTexture();
            case RHITextureSlot::AO:        return GetDefaultWhiteTexture();
            case RHITextureSlot::Emissive:  return GetDefaultBlackTexture();
            default:                        return GetDefaultWhiteTexture();
        }
    }

    bool RHIMaterial::HasTexture(RHITextureSlot slot) const {
        auto index = static_cast<size_t>(slot);
        return (index < m_Textures.size() && m_Textures[index] != nullptr);
    }

    const std::string& RHIMaterial::GetTexturePath(RHITextureSlot slot) const {
        switch (slot) {
            case RHITextureSlot::Albedo:    return m_Desc.AlbedoPath;
            case RHITextureSlot::Normal:    return m_Desc.NormalPath;
            case RHITextureSlot::Roughness: return m_Desc.RoughnessPath;
            case RHITextureSlot::Metallic:  return m_Desc.MetallicPath;
            case RHITextureSlot::AO:        return m_Desc.AOPath;
            case RHITextureSlot::Emissive:  return m_Desc.EmissivePath;
            default: {
                static std::string empty;
                return empty;
            }
        }
    }

    void RHIMaterial::ApplyToPushConstants(RHIObjectPushConstants& pushConsts) const {
        pushConsts.ColorPrimary = m_Desc.BaseColor;
        pushConsts.ColorSecondary = glm::vec4(m_Desc.EmissiveColor, m_Desc.EmissiveIntensity);
        pushConsts.Params = glm::vec4(m_Desc.UVScale.x,
                                      static_cast<float>(m_Desc.ShadingModel),
                                      m_Desc.Roughness,
                                      m_Desc.Metallic);
    }

    std::shared_ptr<RHITexture> RHIMaterial::GetDefaultWhiteTexture() {
        if (!s_DefaultWhite && RHIDevice::Get().IsInitialized()) {
            s_DefaultWhite = std::make_shared<RHITexture>();
            RHITextureDesc desc;
            desc.Width = 1;
            desc.Height = 1;
            desc.Format = RHITextureFormat::RGBA8_UNORM;
            desc.Usage = RHITextureUsage::ShaderResource;
            desc.DebugName = "RHIDefault_White_1x1";
            s_DefaultWhite->Init(desc);
        }
        return s_DefaultWhite;
    }

    std::shared_ptr<RHITexture> RHIMaterial::GetDefaultFlatNormalTexture() {
        if (!s_DefaultFlatNormal && RHIDevice::Get().IsInitialized()) {
            s_DefaultFlatNormal = std::make_shared<RHITexture>();
            RHITextureDesc desc;
            desc.Width = 1;
            desc.Height = 1;
            desc.Format = RHITextureFormat::RGBA8_UNORM;
            desc.Usage = RHITextureUsage::ShaderResource;
            desc.DebugName = "RHIDefault_FlatNormal_1x1";
            s_DefaultFlatNormal->Init(desc);
        }
        return s_DefaultFlatNormal;
    }

    std::shared_ptr<RHITexture> RHIMaterial::GetDefaultRoughnessTexture() {
        if (!s_DefaultRoughness && RHIDevice::Get().IsInitialized()) {
            s_DefaultRoughness = std::make_shared<RHITexture>();
            RHITextureDesc desc;
            desc.Width = 1;
            desc.Height = 1;
            desc.Format = RHITextureFormat::RGBA8_UNORM;
            desc.Usage = RHITextureUsage::ShaderResource;
            desc.DebugName = "RHIDefault_Roughness_1x1";
            s_DefaultRoughness->Init(desc);
        }
        return s_DefaultRoughness;
    }

    std::shared_ptr<RHITexture> RHIMaterial::GetDefaultBlackTexture() {
        if (!s_DefaultBlack && RHIDevice::Get().IsInitialized()) {
            s_DefaultBlack = std::make_shared<RHITexture>();
            RHITextureDesc desc;
            desc.Width = 1;
            desc.Height = 1;
            desc.Format = RHITextureFormat::RGBA8_UNORM;
            desc.Usage = RHITextureUsage::ShaderResource;
            desc.DebugName = "RHIDefault_Black_1x1";
            s_DefaultBlack->Init(desc);
        }
        return s_DefaultBlack;
    }

    void RHIMaterial::ReleaseDefaultTextures() {
        if (s_DefaultWhite) { s_DefaultWhite->Shutdown(); s_DefaultWhite = nullptr; }
        if (s_DefaultFlatNormal) { s_DefaultFlatNormal->Shutdown(); s_DefaultFlatNormal = nullptr; }
        if (s_DefaultRoughness) { s_DefaultRoughness->Shutdown(); s_DefaultRoughness = nullptr; }
        if (s_DefaultBlack) { s_DefaultBlack->Shutdown(); s_DefaultBlack = nullptr; }
    }

    // Presets
    std::shared_ptr<RHIMaterial> RHIMaterial::CreateCarPaintRed() {
        auto mat = std::make_shared<RHIMaterial>("CarPaint_Red");
        mat->SetBaseColor(glm::vec4(0.85f, 0.06f, 0.08f, 1.0f));
        mat->SetRoughness(0.15f);
        mat->SetMetallic(0.0f);
        mat->SetNormalStrength(1.0f);
        mat->SetAO(1.0f);
        return mat;
    }

    std::shared_ptr<RHIMaterial> RHIMaterial::CreateCarPaintBlue() {
        auto mat = std::make_shared<RHIMaterial>("CarPaint_Blue");
        mat->SetBaseColor(glm::vec4(0.08f, 0.28f, 0.65f, 1.0f));
        mat->SetRoughness(0.22f);
        mat->SetMetallic(0.75f);
        mat->SetNormalStrength(1.0f);
        mat->SetAO(1.0f);
        return mat;
    }

    std::shared_ptr<RHIMaterial> RHIMaterial::CreateCarPaintSilver() {
        auto mat = std::make_shared<RHIMaterial>("CarPaint_Silver");
        mat->SetBaseColor(glm::vec4(0.92f, 0.93f, 0.96f, 1.0f));
        mat->SetRoughness(0.14f);
        mat->SetMetallic(0.92f);
        mat->SetNormalStrength(1.0f);
        mat->SetAO(1.0f);
        return mat;
    }

    std::shared_ptr<RHIMaterial> RHIMaterial::CreateChrome() {
        auto mat = std::make_shared<RHIMaterial>("Chrome");
        mat->SetBaseColor(glm::vec4(0.95f, 0.95f, 0.95f, 1.0f));
        mat->SetRoughness(0.04f);
        mat->SetMetallic(1.0f);
        mat->SetNormalStrength(1.0f);
        mat->SetAO(1.0f);
        return mat;
    }

    std::shared_ptr<RHIMaterial> RHIMaterial::CreateGold() {
        auto mat = std::make_shared<RHIMaterial>("Gold");
        mat->SetBaseColor(glm::vec4(1.0f, 0.84f, 0.0f, 1.0f));
        mat->SetRoughness(0.25f);
        mat->SetMetallic(1.0f);
        mat->SetNormalStrength(1.0f);
        mat->SetAO(1.0f);
        return mat;
    }

    std::shared_ptr<RHIMaterial> RHIMaterial::CreateCopper() {
        auto mat = std::make_shared<RHIMaterial>("Copper");
        mat->SetBaseColor(glm::vec4(0.95f, 0.64f, 0.54f, 1.0f));
        mat->SetRoughness(0.30f);
        mat->SetMetallic(0.95f);
        mat->SetNormalStrength(1.0f);
        mat->SetAO(1.0f);
        return mat;
    }

    std::shared_ptr<RHIMaterial> RHIMaterial::CreateCarbonFiber() {
        auto mat = std::make_shared<RHIMaterial>("CarbonFiber");
        mat->SetBaseColor(glm::vec4(0.12f, 0.12f, 0.12f, 1.0f));
        mat->SetRoughness(0.35f);
        mat->SetMetallic(0.05f);
        mat->SetUVScale(glm::vec2(8.0f, 8.0f));
        mat->SetAO(0.9f);
        return mat;
    }

    std::shared_ptr<RHIMaterial> RHIMaterial::CreateTireRubber() {
        auto mat = std::make_shared<RHIMaterial>("TireRubber");
        mat->SetBaseColor(glm::vec4(0.05f, 0.05f, 0.05f, 1.0f));
        mat->SetRoughness(0.85f);
        mat->SetMetallic(0.0f);
        mat->SetAO(1.0f);
        return mat;
    }

    std::shared_ptr<RHIMaterial> RHIMaterial::CreateCastIron() {
        auto mat = std::make_shared<RHIMaterial>("CastIron");
        mat->SetBaseColor(glm::vec4(0.35f, 0.35f, 0.35f, 1.0f));
        mat->SetRoughness(0.70f);
        mat->SetMetallic(0.85f);
        mat->SetAO(0.95f);
        return mat;
    }

    std::shared_ptr<RHIMaterial> RHIMaterial::CreateHeadlightLED() {
        auto mat = std::make_shared<RHIMaterial>("HeadlightLED");
        mat->SetBaseColor(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        mat->SetRoughness(0.1f);
        mat->SetMetallic(0.0f);
        mat->SetEmissiveColor(glm::vec3(1.0f, 1.0f, 1.0f));
        mat->SetEmissiveIntensity(5.0f);
        return mat;
    }

    bool RHIMaterial::Save(const std::filesystem::path& filepath) const {
        std::ofstream fout(filepath);
        if (!fout.is_open()) {
            AXLE_CORE_ERROR("RHIMaterial::Save: Could not open file '{0}' for writing", filepath.string());
            return false;
        }

        fout << "RHIMaterial:\n";
        fout << "  Name: " << m_Desc.Name << "\n";
        fout << "  BaseColor: [ " << m_Desc.BaseColor.r << " , " << m_Desc.BaseColor.g << " , " << m_Desc.BaseColor.b << " , " << m_Desc.BaseColor.a << " ]\n";
        fout << "  Roughness: " << m_Desc.Roughness << "\n";
        fout << "  Metallic: " << m_Desc.Metallic << "\n";
        fout << "  NormalStrength: " << m_Desc.NormalStrength << "\n";
        fout << "  AO: " << m_Desc.AO << "\n";
        fout << "  EmissiveColor: [ " << m_Desc.EmissiveColor.r << " , " << m_Desc.EmissiveColor.g << " , " << m_Desc.EmissiveColor.b << " ]\n";
        fout << "  EmissiveIntensity: " << m_Desc.EmissiveIntensity << "\n";
        fout << "  UVScale: [ " << m_Desc.UVScale.x << " , " << m_Desc.UVScale.y << " ]\n";
        fout << "  UVOffset: [ " << m_Desc.UVOffset.x << " , " << m_Desc.UVOffset.y << " ]\n";
        fout << "  ShadingModel: " << static_cast<uint32_t>(m_Desc.ShadingModel) << "\n";

        if (!m_Desc.AlbedoPath.empty()) fout << "  AlbedoPath: " << m_Desc.AlbedoPath << "\n";
        if (!m_Desc.NormalPath.empty()) fout << "  NormalPath: " << m_Desc.NormalPath << "\n";
        if (!m_Desc.RoughnessPath.empty()) fout << "  RoughnessPath: " << m_Desc.RoughnessPath << "\n";
        if (!m_Desc.MetallicPath.empty()) fout << "  MetallicPath: " << m_Desc.MetallicPath << "\n";
        if (!m_Desc.AOPath.empty()) fout << "  AOPath: " << m_Desc.AOPath << "\n";
        if (!m_Desc.EmissivePath.empty()) fout << "  EmissivePath: " << m_Desc.EmissivePath << "\n";

        fout.close();
        return true;
    }

    bool RHIMaterial::Load(const std::filesystem::path& filepath) {
        std::ifstream fin(filepath);
        if (!fin.is_open()) {
            AXLE_CORE_ERROR("RHIMaterial::Load: Could not open file '{0}' for reading", filepath.string());
            return false;
        }

        std::string line;
        bool inSection = false;

        while (std::getline(fin, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed == "RHIMaterial:" || trimmed == "MaterialAsset:" || trimmed == "MaterialComponent:") {
                inSection = true;
                continue;
            }

            if (inSection) {
                if (trimmed.rfind("Name:", 0) == 0) {
                    m_Desc.Name = Trim(trimmed.substr(5));
                } else if (trimmed.rfind("BaseColor:", 0) == 0 || trimmed.rfind("AlbedoColor:", 0) == 0) {
                    float r = 1, g = 1, b = 1, a = 1;
                    const char* fmt = (trimmed.rfind("BaseColor:", 0) == 0) ? "BaseColor: [ %f , %f , %f , %f ]" : "AlbedoColor: [ %f , %f , %f , %f ]";
                    std::sscanf(trimmed.c_str(), fmt, &r, &g, &b, &a);
                    m_Desc.BaseColor = glm::vec4(r, g, b, a);
                } else if (trimmed.rfind("Roughness:", 0) == 0) {
                    SetRoughness(std::stof(Trim(trimmed.substr(10))));
                } else if (trimmed.rfind("Metallic:", 0) == 0) {
                    SetMetallic(std::stof(Trim(trimmed.substr(9))));
                } else if (trimmed.rfind("NormalStrength:", 0) == 0) {
                    m_Desc.NormalStrength = std::stof(Trim(trimmed.substr(15)));
                } else if (trimmed.rfind("AO:", 0) == 0) {
                    SetAO(std::stof(Trim(trimmed.substr(3))));
                } else if (trimmed.rfind("EmissiveColor:", 0) == 0 || trimmed.rfind("EmissionColor:", 0) == 0) {
                    float r = 0, g = 0, b = 0;
                    const char* fmt = (trimmed.rfind("EmissiveColor:", 0) == 0) ? "EmissiveColor: [ %f , %f , %f ]" : "EmissionColor: [ %f , %f , %f ]";
                    std::sscanf(trimmed.c_str(), fmt, &r, &g, &b);
                    m_Desc.EmissiveColor = glm::vec3(r, g, b);
                } else if (trimmed.rfind("EmissiveIntensity:", 0) == 0 || trimmed.rfind("EmissionIntensity:", 0) == 0) {
                    size_t colon = trimmed.find(':');
                    m_Desc.EmissiveIntensity = std::stof(Trim(trimmed.substr(colon + 1)));
                } else if (trimmed.rfind("UVScale:", 0) == 0) {
                    float x = 1, y = 1;
                    std::sscanf(trimmed.c_str(), "UVScale: [ %f , %f ]", &x, &y);
                    m_Desc.UVScale = glm::vec2(x, y);
                } else if (trimmed.rfind("UVOffset:", 0) == 0) {
                    float x = 0, y = 0;
                    std::sscanf(trimmed.c_str(), "UVOffset: [ %f , %f ]", &x, &y);
                    m_Desc.UVOffset = glm::vec2(x, y);
                } else if (trimmed.rfind("ShadingModel:", 0) == 0) {
                    m_Desc.ShadingModel = static_cast<RHIShadingModel>(std::stoul(Trim(trimmed.substr(13))));
                } else if (trimmed.rfind("AlbedoPath:", 0) == 0) {
                    m_Desc.AlbedoPath = Trim(trimmed.substr(11));
                } else if (trimmed.rfind("NormalPath:", 0) == 0) {
                    m_Desc.NormalPath = Trim(trimmed.substr(11));
                } else if (trimmed.rfind("RoughnessPath:", 0) == 0) {
                    m_Desc.RoughnessPath = Trim(trimmed.substr(14));
                } else if (trimmed.rfind("MetallicPath:", 0) == 0) {
                    m_Desc.MetallicPath = Trim(trimmed.substr(13));
                } else if (trimmed.rfind("AOPath:", 0) == 0) {
                    m_Desc.AOPath = Trim(trimmed.substr(7));
                } else if (trimmed.rfind("EmissivePath:", 0) == 0) {
                    m_Desc.EmissivePath = Trim(trimmed.substr(13));
                }
            }
        }

        fin.close();
        return true;
    }

    std::shared_ptr<RHIMaterial> RHIMaterial::LoadFromFile(const std::filesystem::path& filepath) {
        auto mat = std::make_shared<RHIMaterial>(filepath.stem().string());
        if (mat->Load(filepath)) {
            return mat;
        }
        return nullptr;
    }

}
