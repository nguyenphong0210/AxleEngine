#include <Axle/ScriptHotReloadManager.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/ScriptModule.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/Log.hpp>
#include <chrono>
#include <system_error>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace Axle {

    ScriptHotReloadManager& ScriptHotReloadManager::Get() {
        static ScriptHotReloadManager s_Instance;
        return s_Instance;
    }

    ScriptHotReloadManager::~ScriptHotReloadManager() {
        Shutdown();
    }

    std::filesystem::path ScriptHotReloadManager::ResolveSourceDllPath(const std::filesystem::path& requestedPath) const {
        if (!requestedPath.empty() && std::filesystem::exists(requestedPath)) {
            return std::filesystem::absolute(requestedPath);
        }

        std::vector<std::filesystem::path> searchCandidates = {
            "AxleScripts.dll",
            "build/Editor/Release/AxleScripts.dll",
            "build/Editor/Debug/AxleScripts.dll",
            "../Editor/Release/AxleScripts.dll"
        };

#ifdef _WIN32
        char exePath[MAX_PATH] = { 0 };
        if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
            std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
            searchCandidates.push_back(exeDir / "AxleScripts.dll");
        }
#endif

        for (const auto& candidate : searchCandidates) {
            if (std::filesystem::exists(candidate)) {
                return std::filesystem::absolute(candidate);
            }
        }

        return requestedPath.empty() ? std::filesystem::path("AxleScripts.dll") : requestedPath;
    }

    bool ScriptHotReloadManager::Initialize(const std::filesystem::path& sourceDllPath) {
        m_SourceDllPath = ResolveSourceDllPath(sourceDllPath);
        AXLE_INFO("ScriptHotReloadManager: Resolved source DLL: '{0}'", m_SourceDllPath.string());

        // Pre-clean old temporary runtime shadow copies from prior crashed/aborted sessions
        if (m_SourceDllPath.has_parent_path() && std::filesystem::exists(m_SourceDllPath.parent_path())) {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(m_SourceDllPath.parent_path(), ec)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (filename.rfind("AxleScripts_Runtime_", 0) == 0) {
                        std::filesystem::remove(entry.path(), ec);
                    }
                }
            }
        }

        if (!std::filesystem::exists(m_SourceDllPath)) {
            AXLE_WARN("ScriptHotReloadManager: Source DLL '{0}' does not exist on disk yet. Ready for first build.", m_SourceDllPath.string());
            return false;
        }

        HotReloadResult result = Reload(nullptr, false);
        return result.Success;
    }

    HotReloadResult ScriptHotReloadManager::Reload(std::shared_ptr<Scene> activeScene, bool isRuntimeActive) {
        auto startTime = std::chrono::high_resolution_clock::now();
        HotReloadResult result;

        if (!activeScene && !m_ActiveScene.expired()) {
            activeScene = m_ActiveScene.lock();
        }

        // Phase 1: Preserve Scene Script States and Unbind old instances
        if (activeScene) {
            AXLE_INFO("ScriptHotReloadManager: Preserving field states for scene entities...");
            activeScene->OnPreScriptReload();
        }

        // Phase 2: Unload previous runtime DLL
        if (m_ModuleHandle) {
#ifdef _WIN32
            auto fnShutdown = (void(*)())GetProcAddress((HMODULE)m_ModuleHandle, "Axle_ShutdownScripts");
            if (fnShutdown) {
                fnShutdown();
            }

            ScriptRegistry::ClearDynamicScripts();

            FreeLibrary((HMODULE)m_ModuleHandle);
            m_OldRuntimeDlls.push_back(m_CurrentRuntimeDllPath);
            m_ModuleHandle = nullptr;
            AXLE_INFO("ScriptHotReloadManager: Unloaded old runtime DLL '{0}'", m_CurrentRuntimeDllPath.filename().string());
#endif
        }

        // Phase 3: Create anti-lock timestamped shadow copy
        if (!std::filesystem::exists(m_SourceDllPath)) {
            m_SourceDllPath = ResolveSourceDllPath(m_SourceDllPath);
        }

        if (!std::filesystem::exists(m_SourceDllPath)) {
            result.Success = false;
            result.Message = "Source DLL not found: " + m_SourceDllPath.string();
            AXLE_ERROR("ScriptHotReloadManager: {0}", result.Message);
            return result;
        }

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::string runtimeDllFilename = "AxleScripts_Runtime_" + std::to_string(ms) + ".dll";
        m_CurrentRuntimeDllPath = m_SourceDllPath.parent_path() / runtimeDllFilename;

        std::error_code ec;
        std::filesystem::copy_file(m_SourceDllPath, m_CurrentRuntimeDllPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            result.Success = false;
            result.Message = "Failed to create runtime shadow copy: " + ec.message();
            AXLE_ERROR("ScriptHotReloadManager: {0}", result.Message);
            return result;
        }

        // Also copy .pdb symbol file if available for stack tracing and debugging
        std::filesystem::path srcPdb = m_SourceDllPath;
        srcPdb.replace_extension(".pdb");
        if (std::filesystem::exists(srcPdb)) {
            std::filesystem::path dstPdb = m_CurrentRuntimeDllPath;
            dstPdb.replace_extension(".pdb");
            std::filesystem::copy_file(srcPdb, dstPdb, std::filesystem::copy_options::overwrite_existing, ec);
        }

        // Phase 4: Load runtime shadow copy into process
#ifdef _WIN32
        m_ModuleHandle = LoadLibraryA(m_CurrentRuntimeDllPath.string().c_str());
        if (!m_ModuleHandle) {
            DWORD err = GetLastError();
            result.Success = false;
            result.Message = "LoadLibrary failed with error code: " + std::to_string(err);
            AXLE_ERROR("ScriptHotReloadManager: {0}", result.Message);
            return result;
        }

        // Phase 5: Query exports and register scripts into ScriptRegistry
        using GetCountFn = uint32_t(*)();
        using InitFn = void(*)(ScriptRegisterCallback);

        auto fnGetCount = (GetCountFn)GetProcAddress((HMODULE)m_ModuleHandle, "Axle_GetScriptCount");
        auto fnInit = (InitFn)GetProcAddress((HMODULE)m_ModuleHandle, "Axle_InitializeScripts");

        if (!fnInit) {
            result.Success = false;
            result.Message = "AxleScripts module missing required export 'Axle_InitializeScripts'!";
            AXLE_ERROR("ScriptHotReloadManager: {0}", result.Message);
            FreeLibrary((HMODULE)m_ModuleHandle);
            m_ModuleHandle = nullptr;
            return result;
        }

        fnInit([](const char* name, const ScriptMetadata* meta) {
            ScriptRegistry::RegisterMetadata(name, *meta, true);
        });

        result.ScriptCount = fnGetCount ? fnGetCount() : 0;
#endif

        // Phase 6: Restore Scene entities and re-bind new class instances
        if (activeScene) {
            AXLE_INFO("ScriptHotReloadManager: Restoring and re-binding scene entities...");
            activeScene->OnPostScriptReload(isRuntimeActive);
        }

        // Phase 7: Clean up previous unlocked shadow copies
        CleanupOldShadowCopies();

        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = endTime - startTime;
        result.Success = true;
        result.DurationSeconds = duration.count();
        result.Message = "Hot reload completed successfully in " + std::to_string(result.DurationSeconds) + "s";

        AXLE_INFO("ScriptHotReloadManager: [OK] Reloaded {0} script(s) from '{1}' in {2:.3f}s",
            result.ScriptCount, m_CurrentRuntimeDllPath.filename().string(), result.DurationSeconds);

        return result;
    }

    uint32_t ScriptHotReloadManager::GetLoadedScriptCount() const {
#ifdef _WIN32
        if (!m_ModuleHandle) return 0;
        using GetCountFn = uint32_t(*)();
        auto fnGetCount = (GetCountFn)GetProcAddress((HMODULE)m_ModuleHandle, "Axle_GetScriptCount");
        return fnGetCount ? fnGetCount() : 0;
#else
        return 0;
#endif
    }

    void ScriptHotReloadManager::CleanupOldShadowCopies() {
        std::error_code ec;
        auto it = m_OldRuntimeDlls.begin();
        while (it != m_OldRuntimeDlls.end()) {
            if (std::filesystem::exists(*it)) {
                std::filesystem::remove(*it, ec);
                std::filesystem::path pdbPath = *it;
                pdbPath.replace_extension(".pdb");
                if (std::filesystem::exists(pdbPath)) {
                    std::filesystem::remove(pdbPath, ec);
                }
            }

            if (!ec && !std::filesystem::exists(*it)) {
                it = m_OldRuntimeDlls.erase(it);
            } else {
                ++it;
            }
        }
    }

    void ScriptHotReloadManager::Shutdown() {
#ifdef _WIN32
        if (m_ModuleHandle) {
            auto fnShutdown = (void(*)())GetProcAddress((HMODULE)m_ModuleHandle, "Axle_ShutdownScripts");
            if (fnShutdown) {
                fnShutdown();
            }

            ScriptRegistry::ClearDynamicScripts();

            FreeLibrary((HMODULE)m_ModuleHandle);
            m_OldRuntimeDlls.push_back(m_CurrentRuntimeDllPath);
            m_ModuleHandle = nullptr;
        }
#endif
        CleanupOldShadowCopies();
    }

}
