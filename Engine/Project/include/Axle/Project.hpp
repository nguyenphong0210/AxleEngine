#pragma once
#include <Axle/Types.hpp>
#include <string>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Axle {

    struct ProjectRenderSettings {
        // Environment & Lighting / Indirect (M15-U10-03.1)
        bool EnableShadows = true;
        bool EnableSkybox = true;
        bool EnableGTAO = true;
        bool EnableContactShadows = true;
        bool EnableBloom = true;
        bool EnableSubsurfaceScattering = true;
        bool EnableVolumetricFog = true;
        int FogOverride = -1; // -1 = use scene component setting, 0 = force off, 1 = force on
        int FogQuality = 2; // 0 = Low, 1 = Medium, 2 = High, 3 = Ultra
        float FogTemporalWeight = 0.8f;
        bool EnableDDGI = true;
        int DDGIOverride = -1; // -1 = use scene component setting, 0 = force off, 1 = force on

        // Exposure & Anti-Aliasing (M15-U10-03.2)
        bool EnableToneMapping = true;
        int ToneMappingMode = 0; // 0 = ACES Filmic, 1 = Reinhard, 2 = Uncharted 2, 3 = Linear Bypass
        float Exposure = 1.0f;
        float Gamma = 2.2f;
        float WhitePoint = 4.0f;
        bool EnableAutoExposure = false;
        float AutoExposureMin = 0.1f;
        float AutoExposureMax = 10.0f;
        float AutoExposureSpeed = 3.0f;
        float AutoExposureKeyValue = 0.18f;
        bool EnableFXAA = true;
        bool EnableTAA = false;
        int MSAASamples = 1; // 1, 2, 4, 8

        // Reflections & Probe Capture Scheduling (M15-U10-03.3 / 02.2)
        bool EnableSSR = true;
        bool EnablePlanarReflections = true;
        float PlanarReflectionResolutionScale = 0.5f;
        float PlanarReflectionStrength = 1.0f;
        float PlanarReflectionMaxRoughness = 0.45f;
        int ReflectionCaptureMaxProbesPerFrame = 2;
        int ReflectionCaptureMaxFacesPerFrame = 6;
        uint64_t ReflectionCaptureCacheBudgetBytes = 64ull * 1024ull * 1024ull;

        // Quality preset & sample budgets (M15-U10-C)
        int QualityPreset = 4; // 0=Low, 1=Medium, 2=High, 3=Ultra, 4=Custom
        int GTAOSliceCount = 8;
        int GTAOStepCount = 7;
        int ContactShadowSteps = 16;
        int SSRMaxSteps = 64;
    };

    struct ProjectConfig {
        static constexpr int CurrentSchemaVersion = 1;
        int SchemaVersion = CurrentSchemaVersion;
        std::string Name = "Untitled";
        std::filesystem::path StartScene;
        std::filesystem::path AssetDirectory = "assets";
        std::string RHIBackend = "Direct3D12"; // "Direct3D12", "Vulkan", "OpenGL"
        std::vector<std::string> SelectedPlugins;
        std::string RuntimeSnapshot; // Set only in a compiled product's packaged configuration.
        ProjectRenderSettings RenderSettings{};
        bool HasAuthoredRenderSettings = false;
    };

    class Project {
    public:
        Project() = default;
        ~Project() = default;

        const ProjectConfig& GetConfig() const { return m_Config; }
        ProjectConfig& GetConfig() { return m_Config; }

        const std::filesystem::path& GetProjectDirectoryPath() const { return m_ProjectDirectory; }

        static const std::filesystem::path& GetProjectDirectory();
        static std::filesystem::path GetAssetDirectory();
        static std::filesystem::path GetAssetFileSystemPath(const std::filesystem::path& path);

        static std::shared_ptr<Project> GetActive();
        static void SetActive(std::shared_ptr<Project> project);

        static std::shared_ptr<Project> New(const std::filesystem::path& directory = {});
        static std::shared_ptr<Project> CreateUntitled();
        static std::shared_ptr<Project> Load(const std::filesystem::path& path);
        static bool SaveActive(const std::filesystem::path& path);

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory;

        static std::shared_ptr<Project> s_ActiveProject;

        friend class ProjectSerializer;
    };

}
