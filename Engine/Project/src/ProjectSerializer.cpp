#include <Axle/Path.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace Axle {

    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    static float ClampFiniteFloat(float v, float lo, float hi, float fallback) {
        if (!std::isfinite(v)) return fallback;
        return std::clamp(v, lo, hi);
    }

    static int ClampMSAASamples(int samples) {
        if (samples >= 8) return 8;
        if (samples >= 4) return 4;
        if (samples >= 2) return 2;
        return 1;
    }

    static void ClampRenderSettings(ProjectRenderSettings& cfg) {
        cfg.FogQuality = std::clamp(cfg.FogQuality, 0, 3);
        cfg.FogTemporalWeight = ClampFiniteFloat(cfg.FogTemporalWeight, 0.0f, 0.9f, 0.8f);
        cfg.FogOverride = std::clamp(cfg.FogOverride, -1, 1);
        cfg.DDGIOverride = std::clamp(cfg.DDGIOverride, -1, 1);
        cfg.ToneMappingMode = std::clamp(cfg.ToneMappingMode, 0, 3);
        cfg.Exposure = ClampFiniteFloat(cfg.Exposure, 0.05f, 16.0f, 1.0f);
        cfg.Gamma = ClampFiniteFloat(cfg.Gamma, 1.0f, 4.0f, 2.2f);
        cfg.WhitePoint = ClampFiniteFloat(cfg.WhitePoint, 0.5f, 32.0f, 4.0f);
        cfg.AutoExposureMin = ClampFiniteFloat(cfg.AutoExposureMin, 0.001f, 16.0f, 0.1f);
        cfg.AutoExposureMax = std::clamp(
            std::isfinite(cfg.AutoExposureMax) ? cfg.AutoExposureMax : 10.0f,
            std::max(0.05f, cfg.AutoExposureMin + 0.01f),
            64.0f);
        cfg.AutoExposureSpeed = ClampFiniteFloat(cfg.AutoExposureSpeed, 0.05f, 32.0f, 3.0f);
        cfg.AutoExposureKeyValue = ClampFiniteFloat(cfg.AutoExposureKeyValue, 0.01f, 2.0f, 0.18f);
        cfg.MSAASamples = ClampMSAASamples(cfg.MSAASamples);
        cfg.PlanarReflectionResolutionScale = ClampFiniteFloat(cfg.PlanarReflectionResolutionScale, 0.25f, 1.0f, 0.5f);
        cfg.PlanarReflectionStrength = ClampFiniteFloat(cfg.PlanarReflectionStrength, 0.0f, 2.0f, 1.0f);
        cfg.PlanarReflectionMaxRoughness = ClampFiniteFloat(cfg.PlanarReflectionMaxRoughness, 0.02f, 1.0f, 0.45f);
        cfg.ReflectionCaptureMaxProbesPerFrame = std::clamp(cfg.ReflectionCaptureMaxProbesPerFrame, 1, 8);
        cfg.ReflectionCaptureMaxFacesPerFrame = std::clamp(cfg.ReflectionCaptureMaxFacesPerFrame, 1, 48);
        constexpr uint64_t kMinBudget = 8ull * 1024ull * 1024ull;
        constexpr uint64_t kMaxBudget = 512ull * 1024ull * 1024ull;
        cfg.ReflectionCaptureCacheBudgetBytes = std::clamp(cfg.ReflectionCaptureCacheBudgetBytes, kMinBudget, kMaxBudget);
    }

    void ProjectSerializer::AppendRenderSettingsYaml(std::ostream& out, const ProjectRenderSettings& raw) {
        ProjectRenderSettings rs = raw;
        ClampRenderSettings(rs);
        out << "  RenderSettings:\n";
        out << "    EnableShadows: " << (rs.EnableShadows ? "true" : "false") << "\n";
        out << "    EnableSkybox: " << (rs.EnableSkybox ? "true" : "false") << "\n";
        out << "    EnableGTAO: " << (rs.EnableGTAO ? "true" : "false") << "\n";
        out << "    EnableContactShadows: " << (rs.EnableContactShadows ? "true" : "false") << "\n";
        out << "    EnableBloom: " << (rs.EnableBloom ? "true" : "false") << "\n";
        out << "    EnableSubsurfaceScattering: " << (rs.EnableSubsurfaceScattering ? "true" : "false") << "\n";
        out << "    EnableVolumetricFog: " << (rs.EnableVolumetricFog ? "true" : "false") << "\n";
        out << "    FogOverride: " << rs.FogOverride << "\n";
        out << "    FogQuality: " << rs.FogQuality << "\n";
        out << "    FogTemporalWeight: " << rs.FogTemporalWeight << "\n";
        out << "    EnableDDGI: " << (rs.EnableDDGI ? "true" : "false") << "\n";
        out << "    DDGIOverride: " << rs.DDGIOverride << "\n";
        out << "    EnableToneMapping: " << (rs.EnableToneMapping ? "true" : "false") << "\n";
        out << "    ToneMappingMode: " << rs.ToneMappingMode << "\n";
        out << "    Exposure: " << rs.Exposure << "\n";
        out << "    Gamma: " << rs.Gamma << "\n";
        out << "    WhitePoint: " << rs.WhitePoint << "\n";
        out << "    EnableAutoExposure: " << (rs.EnableAutoExposure ? "true" : "false") << "\n";
        out << "    AutoExposureMin: " << rs.AutoExposureMin << "\n";
        out << "    AutoExposureMax: " << rs.AutoExposureMax << "\n";
        out << "    AutoExposureSpeed: " << rs.AutoExposureSpeed << "\n";
        out << "    AutoExposureKeyValue: " << rs.AutoExposureKeyValue << "\n";
        out << "    EnableFXAA: " << (rs.EnableFXAA ? "true" : "false") << "\n";
        out << "    EnableTAA: " << (rs.EnableTAA ? "true" : "false") << "\n";
        out << "    MSAASamples: " << rs.MSAASamples << "\n";
        out << "    EnableSSR: " << (rs.EnableSSR ? "true" : "false") << "\n";
        out << "    EnablePlanarReflections: " << (rs.EnablePlanarReflections ? "true" : "false") << "\n";
        out << "    PlanarReflectionResolutionScale: " << rs.PlanarReflectionResolutionScale << "\n";
        out << "    PlanarReflectionStrength: " << rs.PlanarReflectionStrength << "\n";
        out << "    PlanarReflectionMaxRoughness: " << rs.PlanarReflectionMaxRoughness << "\n";
        out << "    ReflectionCaptureMaxProbesPerFrame: " << rs.ReflectionCaptureMaxProbesPerFrame << "\n";
        out << "    ReflectionCaptureMaxFacesPerFrame: " << rs.ReflectionCaptureMaxFacesPerFrame << "\n";
        out << "    ReflectionCaptureCacheBudgetBytes: " << rs.ReflectionCaptureCacheBudgetBytes << "\n";
    }

    ProjectSerializer::ProjectSerializer(std::shared_ptr<Project> project)
        : m_Project(project) {
    }

    bool ProjectSerializer::Serialize(const std::filesystem::path& filepath) {
        if (!m_Project) return false;

        const auto& config = m_Project->GetConfig();
        std::stringstream fout;
        fout << "# Axle Engine Project Configuration File\n";
        fout << "Project:\n";
        fout << "  SchemaVersion: " << config.SchemaVersion << "\n";
        fout << "  Name: " << config.Name << "\n";
        fout << "  StartScene: " << PathToUtf8(config.StartScene) << "\n";
        fout << "  AssetDirectory: " << PathToUtf8(config.AssetDirectory) << "\n";
        fout << "  RHIBackend: " << config.RHIBackend << "\n";
        AppendRenderSettingsYaml(fout, config.RenderSettings);
        fout << "  Plugins:\n";
        for (const auto& id : config.SelectedPlugins) fout << "    - " << id << "\n";

        std::string outError;
        if (!FileSystem::WriteTextFileAtomically(filepath, fout.str(), &outError)) {
            AXLE_ERROR("ProjectSerializer: Failed to atomically save project to '{0}': {1}", PathToUtf8(filepath), outError);
            return false;
        }

        AXLE_INFO("ProjectSerializer: Successfully saved project to '{0}'", PathToUtf8(filepath));
        return true;
    }

    bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath) {
        if (!m_Project) return false;

        std::string content = FileSystem::ReadTextFile(filepath);
        if (content.empty() && !FileSystem::Exists(filepath)) {
            AXLE_ERROR("ProjectSerializer: Could not read file: '{0}'", PathToUtf8(filepath));
            return false;
        }

        std::istringstream fin(content);
        auto parsedConfig = m_Project->GetConfig();
        std::string line;
        bool inProjectSection = false;
        bool inPlugins = false;
        bool inRenderSettings = false;
        bool sawSchema = false;
        std::vector<std::string> selectedPlugins;

        auto parseBool = [](const std::string& val) { return val == "true" || val == "1"; };

        while (std::getline(fin, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed == "Project:") {
                inProjectSection = true;
                inPlugins = false;
                inRenderSettings = false;
                continue;
            }

            if (inProjectSection) {
                if (trimmed == "Plugins:") {
                    inPlugins = true;
                    inRenderSettings = false;
                } else if (trimmed == "RenderSettings:") {
                    inRenderSettings = true;
                    inPlugins = false;
                    parsedConfig.HasAuthoredRenderSettings = true;
                } else if (inPlugins && trimmed.rfind("- ", 0) == 0) {
                    const auto id = Trim(trimmed.substr(2));
                    if (id.empty() || std::find(selectedPlugins.begin(), selectedPlugins.end(), id) != selectedPlugins.end()) {
                        AXLE_WARN("ProjectSerializer: invalid or duplicate plugin selection in '{0}'", PathToUtf8(filepath));
                        return false;
                    }
                    selectedPlugins.push_back(id);
                } else if (trimmed.rfind("SchemaVersion:", 0) == 0) {
                    try { parsedConfig.SchemaVersion = std::stoi(Trim(trimmed.substr(14))); }
                    catch (...) { AXLE_WARN("ProjectSerializer: invalid schema version in '{0}'", PathToUtf8(filepath)); return false; }
                    sawSchema = true;
                    inPlugins = false;
                    inRenderSettings = false;
                } else if (trimmed.rfind("Name:", 0) == 0) {
                    inPlugins = false;
                    inRenderSettings = false;
                    parsedConfig.Name = Trim(trimmed.substr(5));
                } else if (trimmed.rfind("StartScene:", 0) == 0) {
                    inPlugins = false;
                    inRenderSettings = false;
                    parsedConfig.StartScene = PathFromUtf8(Trim(trimmed.substr(11)));
                } else if (trimmed.rfind("AssetDirectory:", 0) == 0) {
                    inPlugins = false;
                    inRenderSettings = false;
                    parsedConfig.AssetDirectory = PathFromUtf8(Trim(trimmed.substr(15)));
                } else if (trimmed.rfind("RHIBackend:", 0) == 0) {
                    parsedConfig.RHIBackend = Trim(trimmed.substr(11));
                    inPlugins = false;
                    inRenderSettings = false;
                } else if (trimmed.rfind("RuntimeSnapshot:", 0) == 0) {
                    parsedConfig.RuntimeSnapshot = Trim(trimmed.substr(16));
                    inPlugins = false;
                    inRenderSettings = false;
                } else if (inRenderSettings) {
                    auto& rs = parsedConfig.RenderSettings;
                    const auto colon = trimmed.find(':');
                    if (colon != std::string::npos) {
                        const std::string key = Trim(trimmed.substr(0, colon));
                        const std::string val = Trim(trimmed.substr(colon + 1));
                        try {
                            if (key == "EnableShadows") rs.EnableShadows = parseBool(val);
                            else if (key == "EnableSkybox") rs.EnableSkybox = parseBool(val);
                            else if (key == "EnableGTAO") rs.EnableGTAO = parseBool(val);
                            else if (key == "EnableContactShadows") rs.EnableContactShadows = parseBool(val);
                            else if (key == "EnableBloom") rs.EnableBloom = parseBool(val);
                            else if (key == "EnableSubsurfaceScattering") rs.EnableSubsurfaceScattering = parseBool(val);
                            else if (key == "EnableVolumetricFog") rs.EnableVolumetricFog = parseBool(val);
                            else if (key == "FogOverride") rs.FogOverride = std::stoi(val);
                            else if (key == "FogQuality") rs.FogQuality = std::stoi(val);
                            else if (key == "FogTemporalWeight") rs.FogTemporalWeight = std::stof(val);
                            else if (key == "EnableDDGI") rs.EnableDDGI = parseBool(val);
                            else if (key == "DDGIOverride") rs.DDGIOverride = std::stoi(val);
                            else if (key == "EnableToneMapping") rs.EnableToneMapping = parseBool(val);
                            else if (key == "ToneMappingMode") rs.ToneMappingMode = std::stoi(val);
                            else if (key == "Exposure") rs.Exposure = std::stof(val);
                            else if (key == "Gamma") rs.Gamma = std::stof(val);
                            else if (key == "WhitePoint") rs.WhitePoint = std::stof(val);
                            else if (key == "EnableAutoExposure") rs.EnableAutoExposure = parseBool(val);
                            else if (key == "AutoExposureMin") rs.AutoExposureMin = std::stof(val);
                            else if (key == "AutoExposureMax") rs.AutoExposureMax = std::stof(val);
                            else if (key == "AutoExposureSpeed") rs.AutoExposureSpeed = std::stof(val);
                            else if (key == "AutoExposureKeyValue") rs.AutoExposureKeyValue = std::stof(val);
                            else if (key == "EnableFXAA") rs.EnableFXAA = parseBool(val);
                            else if (key == "EnableTAA") rs.EnableTAA = parseBool(val);
                            else if (key == "MSAASamples") rs.MSAASamples = std::stoi(val);
                            else if (key == "EnableSSR") rs.EnableSSR = parseBool(val);
                            else if (key == "EnablePlanarReflections") rs.EnablePlanarReflections = parseBool(val);
                            else if (key == "PlanarReflectionResolutionScale") rs.PlanarReflectionResolutionScale = std::stof(val);
                            else if (key == "PlanarReflectionStrength") rs.PlanarReflectionStrength = std::stof(val);
                            else if (key == "PlanarReflectionMaxRoughness") rs.PlanarReflectionMaxRoughness = std::stof(val);
                            else if (key == "ReflectionCaptureMaxProbesPerFrame") rs.ReflectionCaptureMaxProbesPerFrame = std::stoi(val);
                            else if (key == "ReflectionCaptureMaxFacesPerFrame") rs.ReflectionCaptureMaxFacesPerFrame = std::stoi(val);
                            else if (key == "ReflectionCaptureCacheBudgetBytes") rs.ReflectionCaptureCacheBudgetBytes = std::stoull(val);
                        } catch (...) {}
                    }
                }
            }
        }

        if (!sawSchema || parsedConfig.SchemaVersion != ProjectConfig::CurrentSchemaVersion) {
            AXLE_WARN("ProjectSerializer: unsupported or missing project schema in '{0}'", PathToUtf8(filepath));
            return false;
        }
        ClampRenderSettings(parsedConfig.RenderSettings);
        parsedConfig.SelectedPlugins = std::move(selectedPlugins);
        m_Project->GetConfig() = std::move(parsedConfig);

        m_Project->m_ProjectDirectory = filepath.parent_path();
        if (m_Project->m_ProjectDirectory.empty()) {
            m_Project->m_ProjectDirectory = std::filesystem::current_path();
        }

        AXLE_INFO("ProjectSerializer: Successfully loaded project '{0}' from '{1}'",
            m_Project->GetConfig().Name, PathToUtf8(filepath));
        return true;
    }

}
