#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <sstream>
#include <cstdio>

namespace Axle::RHI {

    static std::shared_ptr<RHITexture> s_DefaultWhite;
    static std::shared_ptr<RHITexture> s_DefaultFlatNormal;
    static std::shared_ptr<RHITexture> s_DefaultRoughness;
    static std::shared_ptr<RHITexture> s_DefaultBlack;
    static std::shared_ptr<RHITexture> s_DefaultIrradianceCubemap;
    static std::shared_ptr<RHITexture> s_DefaultPrefilterCubemap;
    static std::shared_ptr<RHITexture> s_DefaultBRDFLUT;

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

    std::shared_ptr<RHITexture> RHIMaterial::GetDefaultIrradianceCubemap() {
        if (!s_DefaultIrradianceCubemap && RHIDevice::Get().IsInitialized()) {
            s_DefaultIrradianceCubemap = std::make_shared<RHITexture>();
            s_DefaultIrradianceCubemap->Init(RHITextureDesc::Cubemap(16, 1, RHITextureFormat::RGBA8_UNORM, "RHIDefault_Irradiance_Cubemap"));
        }
        return s_DefaultIrradianceCubemap;
    }

    std::shared_ptr<RHITexture> RHIMaterial::GetDefaultPrefilterCubemap() {
        if (!s_DefaultPrefilterCubemap && RHIDevice::Get().IsInitialized()) {
            s_DefaultPrefilterCubemap = std::make_shared<RHITexture>();
            s_DefaultPrefilterCubemap->Init(RHITextureDesc::Cubemap(16, 5, RHITextureFormat::RGBA8_UNORM, "RHIDefault_Prefilter_Cubemap"));
        }
        return s_DefaultPrefilterCubemap;
    }

    std::shared_ptr<RHITexture> RHIMaterial::GetDefaultBRDFLUT() {
        if (!s_DefaultBRDFLUT && RHIDevice::Get().IsInitialized()) {
            s_DefaultBRDFLUT = std::make_shared<RHITexture>();
            RHITextureDesc desc;
            desc.Width = 16;
            desc.Height = 16;
            desc.Format = RHITextureFormat::RGBA8_UNORM;
            desc.Usage = RHITextureUsage::ShaderResource;
            desc.DebugName = "RHIDefault_BRDFLUT_16x16";
            s_DefaultBRDFLUT->Init(desc);
        }
        return s_DefaultBRDFLUT;
    }

    void RHIMaterial::ReleaseDefaultTextures() {
        if (s_DefaultWhite) { s_DefaultWhite->Shutdown(); s_DefaultWhite = nullptr; }
        if (s_DefaultFlatNormal) { s_DefaultFlatNormal->Shutdown(); s_DefaultFlatNormal = nullptr; }
        if (s_DefaultRoughness) { s_DefaultRoughness->Shutdown(); s_DefaultRoughness = nullptr; }
        if (s_DefaultBlack) { s_DefaultBlack->Shutdown(); s_DefaultBlack = nullptr; }
        if (s_DefaultIrradianceCubemap) { s_DefaultIrradianceCubemap->Shutdown(); s_DefaultIrradianceCubemap = nullptr; }
        if (s_DefaultPrefilterCubemap) { s_DefaultPrefilterCubemap->Shutdown(); s_DefaultPrefilterCubemap = nullptr; }
        if (s_DefaultBRDFLUT) { s_DefaultBRDFLUT->Shutdown(); s_DefaultBRDFLUT = nullptr; }
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

        std::string ext = filepath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
        if (ext == ".axleasset" || ext.empty()) {
            auto formatHex = [](const glm::vec4& c) {
                uint32_t r = (uint32_t)std::clamp(c.r * 255.0f + 0.5f, 0.0f, 255.0f);
                uint32_t g = (uint32_t)std::clamp(c.g * 255.0f + 0.5f, 0.0f, 255.0f);
                uint32_t b = (uint32_t)std::clamp(c.b * 255.0f + 0.5f, 0.0f, 255.0f);
                std::ostringstream ss;
                ss << "#" << std::uppercase << std::hex << std::setfill('0')
                   << std::setw(2) << r << std::setw(2) << g << std::setw(2) << b;
                return ss.str();
            };
            fout << "Material {\n";
            fout << "    name: \"" << m_Desc.Name << "\"\n";
            fout << "    shader: \"ForwardPBR\"\n\n";
            fout << "    // Base Color & Texture\n";
            fout << "    albedoColor: \"" << formatHex(m_Desc.BaseColor) << "\"\n";
            fout << "    albedoMap: \"" << m_Desc.AlbedoPath << "\"\n\n";
            fout << "    // Surface Microfacet\n";
            fout << "    metallic: " << m_Desc.Metallic << "\n";
            fout << "    roughness: " << m_Desc.Roughness << "\n";
            fout << "    normalMap: \"" << m_Desc.NormalPath << "\"\n";
            fout << "    normalStrength: " << m_Desc.NormalStrength << "\n\n";
            fout << "    // Emission & UV Scaling\n";
            fout << "    emissionColor: \"" << formatHex(glm::vec4(m_Desc.EmissiveColor, 1.0f)) << "\"\n";
            fout << "    emissionIntensity: " << m_Desc.EmissiveIntensity << "\n";
            fout << "    uvScale: [ " << m_Desc.UVScale.x << " , " << m_Desc.UVScale.y << " ]\n";
            fout << "}\n";
            return true;
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

        std::stringstream ss;
        ss << fin.rdbuf();
        std::string content = ss.str();

        if (content.find("Material {") != std::string::npos || content.find("Material{") != std::string::npos) {
            std::istringstream stream(content);
            std::string line;
            while (std::getline(stream, line)) {
                std::string trimmed = Trim(line);
                if (trimmed.empty() || trimmed[0] == '/' || trimmed[0] == '#') continue;
                if (trimmed.rfind("name:", 0) == 0) {
                    std::string val = Trim(trimmed.substr(5));
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
                    m_Desc.Name = val;
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
                            m_Desc.BaseColor = glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
                        } else if (h.size() == 8) {
                            uint32_t r = (hexCharToInt(h[0]) << 4) | hexCharToInt(h[1]);
                            uint32_t g = (hexCharToInt(h[2]) << 4) | hexCharToInt(h[3]);
                            uint32_t b = (hexCharToInt(h[4]) << 4) | hexCharToInt(h[5]);
                            uint32_t a = (hexCharToInt(h[6]) << 4) | hexCharToInt(h[7]);
                            m_Desc.BaseColor = glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
                        }
                    } else {
                        float r = 1, g = 1, b = 1, a = 1;
                        std::sscanf(val.c_str(), "[ %f , %f , %f , %f ]", &r, &g, &b, &a);
                        m_Desc.BaseColor = glm::vec4(r, g, b, a);
                    }
                } else if (trimmed.rfind("albedoMap:", 0) == 0) {
                    std::string p = Trim(trimmed.substr(10));
                    if (p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
                    m_Desc.AlbedoPath = p;
                } else if (trimmed.rfind("metallic:", 0) == 0) {
                    try { SetMetallic(std::stof(Trim(trimmed.substr(9)))); } catch (...) {}
                } else if (trimmed.rfind("roughness:", 0) == 0) {
                    try { SetRoughness(std::stof(Trim(trimmed.substr(10)))); } catch (...) {}
                } else if (trimmed.rfind("normalMap:", 0) == 0) {
                    std::string p = Trim(trimmed.substr(10));
                    if (p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
                    m_Desc.NormalPath = p;
                } else if (trimmed.rfind("normalStrength:", 0) == 0) {
                    try { m_Desc.NormalStrength = std::stof(Trim(trimmed.substr(15))); } catch (...) {}
                } else if (trimmed.rfind("uvScale:", 0) == 0) {
                    float x = 1, y = 1;
                    std::sscanf(trimmed.c_str(), "uvScale: [ %f , %f ]", &x, &y);
                    m_Desc.UVScale = glm::vec2(x, y);
                } else if (trimmed.rfind("emissionIntensity:", 0) == 0) {
                    try { m_Desc.EmissiveIntensity = std::stof(Trim(trimmed.substr(18))); } catch (...) {}
                }
            }
            return true;
        }

        std::string line;
        bool inSection = false;
        fin.clear();
        fin.seekg(0);

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

    nri::DescriptorSet* RHIMaterial::CreateDescriptorSet(RHIDescriptorPool& pool, const RHIPipelineLayout& layout, nri::Descriptor* sampler) {
        if (!pool.IsValid() || !layout.IsValid()) return nullptr;

        nri::DescriptorSet* set = pool.AllocateDescriptorSet(layout, 0);
        if (!set) return nullptr;

        auto whiteTex = GetDefaultWhiteTexture();
        auto normalTex = GetDefaultFlatNormalTexture();
        auto roughTex = GetDefaultRoughnessTexture();
        auto blackTex = GetDefaultBlackTexture();

        nri::Descriptor* albedoSRV = m_Textures[0] ? m_Textures[0]->GetSRV() : (whiteTex ? whiteTex->GetSRV() : nullptr);
        nri::Descriptor* normalSRV = m_Textures[1] ? m_Textures[1]->GetSRV() : (normalTex ? normalTex->GetSRV() : nullptr);
        nri::Descriptor* roughSRV  = m_Textures[2] ? m_Textures[2]->GetSRV() : (roughTex ? roughTex->GetSRV() : nullptr);
        nri::Descriptor* metalSRV  = m_Textures[3] ? m_Textures[3]->GetSRV() : (blackTex ? blackTex->GetSRV() : nullptr);
        nri::Descriptor* aoSRV     = m_Textures[4] ? m_Textures[4]->GetSRV() : (whiteTex ? whiteTex->GetSRV() : nullptr);
        nri::Descriptor* emissSRV  = m_Textures[5] ? m_Textures[5]->GetSRV() : (blackTex ? blackTex->GetSRV() : nullptr);

        std::vector<nri::Descriptor*> srvs = { albedoSRV, normalSRV, roughSRV, metalSRV, aoSRV, emissSRV };
        pool.UpdateTextureRange(set, 0, 0, srvs);

        if (sampler) {
            pool.UpdateSampler(set, 1, 0, sampler);
        }

        return set;
    }

}
