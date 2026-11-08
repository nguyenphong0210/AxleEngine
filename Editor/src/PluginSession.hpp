#pragma once
#include "PluginBuild.hpp"
#include "PluginRestartSnapshot.hpp"
#include "PluginEditorServices.hpp"
#include <Axle/Project.hpp>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace Axle {
struct PluginSessionEnvironment {
    std::filesystem::path EnginePlugins;
    std::filesystem::path SdkDirectory;
    std::filesystem::path CMake;
    std::string Configuration, SdkFingerprint, ToolchainFingerprint;
    std::string Generator, GeneratorToolset;
    std::filesystem::path GeneratorInstance;
};
PluginSessionEnvironment GetPluginSessionEnvironment();

// One immutable native graph per Editor process. Desired metadata/builds may
// advance while this graph stays alive; activation always uses a new process.
class PluginProjectSession final {
public:
    enum class Status { Empty, Active, NeedsBuild, NeedsRestart, Recovery, Error };
    explicit PluginProjectSession(PluginSessionEnvironment environment);
    ~PluginProjectSession();
    bool Bind(std::string& error);
    bool Open(std::shared_ptr<Project> project, std::filesystem::path projectPath, bool safeMode = false,
        std::optional<PluginRestartSnapshot> expected = {});
    bool Refresh();
    bool SetProjectFile(std::filesystem::path path);
    bool MatchesDesired();
    bool BeginBuild();
    void CancelBuild();
    void Pump();
    void Quiesce();
    void ResumeJobs() { m_Services.ResumeJobs(); }
    void Shutdown();
    bool IsBuilding() const { return m_Build.valid(); }
    bool HasNativeModules() const { return !m_Modules.empty(); }
    bool IsSafeMode() const { return m_Status == Status::Recovery; }
    Status GetStatus() const { return m_Status; }
    const std::string& Error() const { return m_Error; }
    const Plugin::Catalog& Catalog() const { return m_Catalog; }
    const Plugin::BuildPlan& DesiredPlan() const { return m_Plan; }
    const std::vector<std::string>& ActiveIds() const { return m_ActiveIds; }
    const std::string& ActiveFingerprint() const { return m_ActiveFingerprint; }
    const PluginBuildResult& LastBuild() const { return m_LastBuild; }
    std::pair<std::string, std::filesystem::path> BuildProgress() const;
    const std::filesystem::path& ProjectPath() const { return m_ProjectPath; }
    PluginEditorServices& EditorServices() { return m_EditorServices; }
    bool ClearAbandonedSessions();
private:
    struct Marker;
    PluginBuildRequest Request(bool tools) const;
    bool Activate(const PluginBuildResult& generation, const PluginRestartSnapshot& snapshot);
    PluginSessionEnvironment m_Environment;
    PluginRuntime::RegistrationRegistry m_Registrations;
    PluginServiceSession m_Services;
    PluginEditorServices m_EditorServices;
    std::vector<std::unique_ptr<PluginRuntime::NativePlugin>> m_Modules;
    std::unique_ptr<Marker> m_Marker;
    std::shared_ptr<Project> m_Project;
    std::filesystem::path m_ProjectPath;
    Plugin::Catalog m_Catalog;
    Plugin::BuildPlan m_Plan;
    std::vector<std::string> m_ActiveIds;
    std::string m_ActiveFingerprint, m_StagedFingerprint, m_Error;
    Status m_Status = Status::Empty;
    bool m_Bound = false, m_AttemptedNative = false, m_Opened = false;
    std::future<PluginBuildResult> m_Build;
    std::shared_ptr<std::atomic_bool> m_Cancel;
    PluginBuildResult m_LastBuild;
    std::shared_ptr<PluginBuildProgress> m_Progress;
};
}
