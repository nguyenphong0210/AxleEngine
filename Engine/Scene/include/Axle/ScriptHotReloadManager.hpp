#pragma once
#include <Axle/Types.hpp>
#include <string>
#include <filesystem>
#include <memory>
#include <vector>

namespace Axle {

    class Scene;

    struct HotReloadResult {
        bool Success = false;
        std::string Message;
        uint32_t ScriptCount = 0;
        double DurationSeconds = 0.0;
    };

    /**
     * @brief Manages dynamic loading and zero-lock hot-reloading of AxleScripts.dll.
     * 
     * Uses the Shadow-Copy / Timestamp DLL pattern (Unreal Engine / Hazel style):
     * The compiler writes to AxleScripts.dll without ever being locked by Windows (LNK1104).
     * The engine copies AxleScripts.dll -> AxleScripts_Runtime_<timestamp>.dll and loads the copy.
     * Before reload, active scene entity fields are serialized into buffers, instances recreated,
     * and re-bound with new class code without any lost parameters.
     */
    class ScriptHotReloadManager {
    public:
        static ScriptHotReloadManager& Get();

        /**
         * @brief Initialize the hot-reload manager with the source DLL path.
         * If empty, auto-detects AxleScripts.dll relative to executable.
         */
        bool Initialize(const std::filesystem::path& sourceDllPath = {});

        /**
         * @brief Perform complete hot-reload:
         * 1. Preserves scene script states & field buffers
         * 2. Unloads old runtime DLL (FreeLibrary)
         * 3. Creates new timestamped shadow copy
         * 4. Loads new shadow copy (LoadLibrary)
         * 5. Re-registers scripts in ScriptRegistry
         * 6. Re-binds scene entities & restores fields to new instances
         */
        HotReloadResult Reload(std::shared_ptr<Scene> activeScene = nullptr, bool isRuntimeActive = false);

        /**
         * @brief Unloads current module and deletes temporary shadow copies.
         */
        void Shutdown();

        bool IsLoaded() const { return m_ModuleHandle != nullptr; }
        const std::filesystem::path& GetSourceDllPath() const { return m_SourceDllPath; }
        const std::filesystem::path& GetCurrentRuntimeDllPath() const { return m_CurrentRuntimeDllPath; }
        uint32_t GetLoadedScriptCount() const;

        void SyncInputContext(void* windowContext = nullptr);

        void SetActiveScene(std::shared_ptr<Scene> scene) { m_ActiveScene = scene; }

    private:
        ScriptHotReloadManager() = default;
        ~ScriptHotReloadManager();

        ScriptHotReloadManager(const ScriptHotReloadManager&) = delete;
        ScriptHotReloadManager& operator=(const ScriptHotReloadManager&) = delete;

        void CleanupOldShadowCopies();
        std::filesystem::path ResolveSourceDllPath(const std::filesystem::path& requestedPath) const;

    private:
        void* m_ModuleHandle = nullptr;
        void* m_WindowContext = nullptr;
        std::filesystem::path m_SourceDllPath;
        std::filesystem::path m_CurrentRuntimeDllPath;
        std::vector<std::filesystem::path> m_OldRuntimeDlls;
        std::weak_ptr<Scene> m_ActiveScene;
    };

}
