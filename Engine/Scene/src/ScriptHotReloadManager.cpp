#include <Axle/ScriptHotReloadManager.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/ScriptModule.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/Input.hpp>
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
        if (!requestedPath.empty() && requestedPath.is_absolute() && std::filesystem::exists(requestedPath)) {
            return std::filesystem::absolute(requestedPath);
        }

        std::vector<std::filesystem::path> searchCandidates;

#ifdef _WIN32
        char exePath[MAX_PATH] = { 0 };
        if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
            std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
            if (!requestedPath.empty()) {
                searchCandidates.push_back(exeDir / requestedPath.filename());
            }
            searchCandidates.push_back(exeDir / "AxleScripts.dll");
        }
#endif

        if (!requestedPath.empty() && std::filesystem::exists(requestedPath)) {
            return std::filesystem::absolute(requestedPath);
        }

#ifdef _DEBUG
        searchCandidates.push_back("build/Editor/Debug/AxleScripts.dll");
        searchCandidates.push_back("../Editor/Debug/AxleScripts.dll");
        searchCandidates.push_back("build/Editor/Release/AxleScripts.dll");
        searchCandidates.push_back("../Editor/Release/AxleScripts.dll");
#else
        searchCandidates.push_back("build/Editor/Release/AxleScripts.dll");
        searchCandidates.push_back("../Editor/Release/AxleScripts.dll");
        searchCandidates.push_back("build/Editor/Debug/AxleScripts.dll");
        searchCandidates.push_back("../Editor/Debug/AxleScripts.dll");
#endif
        searchCandidates.push_back("AxleScripts.dll");

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

        // Verify source DLL exists
        if (!std::filesystem::exists(m_SourceDllPath)) {
            m_SourceDllPath = ResolveSourceDllPath(m_SourceDllPath);
        }

        if (!std::filesystem::exists(m_SourceDllPath)) {
            result.Success = false;
            result.Message = "Source DLL not found: " + m_SourceDllPath.string();
            AXLE_ERROR("ScriptHotReloadManager: {0}", result.Message);
            return result;
        }

        // Phase 1: Staging - Create candidate shadow copy and validate handshake before touching active module
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::string candidateFilename = "AxleScripts_Runtime_" + std::to_string(ms) + ".dll";
        std::filesystem::path candidatePath = m_SourceDllPath.parent_path() / candidateFilename;

        std::error_code ec;
        std::filesystem::copy_file(m_SourceDllPath, candidatePath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            result.Success = false;
            result.Message = "Failed to create candidate shadow copy: " + ec.message();
            AXLE_ERROR("ScriptHotReloadManager: {0}", result.Message);
            return result;
        }

        // Copy symbol file (.pdb) if available
        std::filesystem::path srcPdb = m_SourceDllPath;
        srcPdb.replace_extension(".pdb");
        if (std::filesystem::exists(srcPdb)) {
            std::filesystem::path dstPdb = candidatePath;
            dstPdb.replace_extension(".pdb");
            std::filesystem::copy_file(srcPdb, dstPdb, std::filesystem::copy_options::overwrite_existing, ec);
        }

#ifdef _WIN32
        // Load candidate module into process memory
        HMODULE hCandidate = LoadLibraryA(candidatePath.string().c_str());
        if (!hCandidate) {
            DWORD err = GetLastError();
            std::filesystem::remove(candidatePath, ec);
            result.Success = false;
            result.Message = "LoadLibrary failed for candidate module with error code: " + std::to_string(err);
            AXLE_ERROR("ScriptHotReloadManager: {0}", result.Message);
            return result;
        }

        // Validate required ABI exports
        using GetCountFn = uint32_t(*)();
        using InitFn = void(*)(ScriptRegisterCallback);
        using GetABIVersionFn = uint32_t(*)();

        auto fnGetABI = (GetABIVersionFn)GetProcAddress(hCandidate, "Axle_GetScriptABIVersion");
        if (!fnGetABI) {
            FreeLibrary(hCandidate);
            std::filesystem::remove(candidatePath, ec);
            result.Success = false;
            result.Message = "Candidate AxleScripts module missing required export 'Axle_GetScriptABIVersion' (incompatible legacy ABI)!";
            AXLE_ERROR("ScriptHotReloadManager: {0}", result.Message);
            return result;
        }

        uint32_t candidateABI = fnGetABI();
        if (candidateABI != AXLE_SCRIPT_ABI_VERSION) {
            FreeLibrary(hCandidate);
            std::filesystem::remove(candidatePath, ec);
            result.Success = false;
            result.Message = "Candidate AxleScripts module ABI mismatch: expected version " +
                             std::to_string(AXLE_SCRIPT_ABI_VERSION) + ", got " + std::to_string(candidateABI);
            AXLE_ERROR("ScriptHotReloadManager: {0}", result.Message);
            return result;
        }

        auto fnGetCount = (GetCountFn)GetProcAddress(hCandidate, "Axle_GetScriptCount");
        auto fnInit = (InitFn)GetProcAddress(hCandidate, "Axle_InitializeScripts");

        if (!fnInit) {
            FreeLibrary(hCandidate);
            std::filesystem::remove(candidatePath, ec);
            result.Success = false;
            result.Message = "Candidate AxleScripts module missing required export 'Axle_InitializeScripts'!";
            AXLE_ERROR("ScriptHotReloadManager: {0}", result.Message);
            return result;
        }

        // Query candidate metadata into a staging registry before commit
        static thread_local std::vector<std::pair<std::string, ScriptMetadata>>* s_CandidateScripts = nullptr;
        std::vector<std::pair<std::string, ScriptMetadata>> candidateScripts;
        s_CandidateScripts = &candidateScripts;

        fnInit([](const char* name, const ScriptMetadata* meta) {
            if (name && meta && s_CandidateScripts) {
                s_CandidateScripts->emplace_back(std::string(name), *meta);
            }
        });
        s_CandidateScripts = nullptr;

        // Phase 2: Enter Scene Reload Barrier & destroy existing script instances safely
        if (activeScene) {
            AXLE_INFO("ScriptHotReloadManager: Preserving field states for scene entities...");
            activeScene->OnPreScriptReload();
        }

        // Phase 3: Unload previous runtime DLL
        if (m_ModuleHandle) {
            auto fnShutdown = (void(*)())GetProcAddress((HMODULE)m_ModuleHandle, "Axle_ShutdownScripts");
            if (fnShutdown) {
                fnShutdown();
            }

            FreeLibrary((HMODULE)m_ModuleHandle);
            m_OldRuntimeDlls.push_back(m_CurrentRuntimeDllPath);
            m_ModuleHandle = nullptr;
            AXLE_INFO("ScriptHotReloadManager: Unloaded previous runtime DLL '{0}'", m_CurrentRuntimeDllPath.filename().string());
        }

        // Phase 4: Commit candidate module and update generation
        m_ModuleHandle = (void*)hCandidate;
        m_CurrentRuntimeDllPath = candidatePath;
        m_ModuleGeneration.fetch_add(1, std::memory_order_release);

        // Commit candidate metadata to ScriptRegistry
        ScriptRegistry::ClearDynamicScripts();
        for (const auto& [name, meta] : candidateScripts) {
            ScriptRegistry::RegisterMetadata(name, meta, true);
        }

        result.ScriptCount = fnGetCount ? fnGetCount() : static_cast<uint32_t>(candidateScripts.size());

        // Synchronize engine input context to newly committed script DLL
        SyncInputContext();
#endif

        // Phase 5: Restore Scene entities and re-bind new class instances
        if (activeScene) {
            AXLE_INFO("ScriptHotReloadManager: Restoring and re-binding scene entities...");
            activeScene->OnPostScriptReload(isRuntimeActive);
        }

        // Phase 6: Clean up previous unlocked shadow copies
        CleanupOldShadowCopies();

        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = endTime - startTime;
        result.Success = true;
        result.DurationSeconds = duration.count();
        result.Message = "Hot reload completed successfully in " + std::to_string(result.DurationSeconds) + "s";

        AXLE_INFO("ScriptHotReloadManager: [OK] Reloaded {0} script(s) from '{1}' in {2:.3f}s (generation {3})",
            result.ScriptCount, m_CurrentRuntimeDllPath.filename().string(), result.DurationSeconds, m_ModuleGeneration.load());

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

    void ScriptHotReloadManager::SyncInputContext(void* windowContext) {
        if (windowContext) {
            m_WindowContext = windowContext;
        } else if (!m_WindowContext) {
            m_WindowContext = (void*)Input::GetContext();
        }

#ifdef _WIN32
        if (!m_ModuleHandle) return;
        const auto* inputCtx = Input::GetInputContext();
        void* ctxToSend = (inputCtx && inputCtx->IsKeyPressedFn) ? (void*)inputCtx : m_WindowContext;
        if (!ctxToSend) return;

        using SetInputFn = void(*)(void*);
        auto fnSetInput = (SetInputFn)GetProcAddress((HMODULE)m_ModuleHandle, "Axle_SetInputContext");
        if (fnSetInput) {
            fnSetInput(ctxToSend);
        }
        if (m_ImGuiContext) {
            SyncImGuiContext(m_ImGuiContext);
        }
#endif
    }

    void ScriptHotReloadManager::SyncImGuiContext(void* imguiContext) {
        if (imguiContext) {
            m_ImGuiContext = imguiContext;
        }
#ifdef _WIN32
        if (!m_ModuleHandle || !m_ImGuiContext) return;
        using SetImGuiFn = void(*)(void*);
        auto fnSetImGui = (SetImGuiFn)GetProcAddress((HMODULE)m_ModuleHandle, "Axle_SetImGuiContext");
        if (fnSetImGui) {
            fnSetImGui(m_ImGuiContext);
        }
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
        if (!m_ActiveScene.expired()) {
            auto scene = m_ActiveScene.lock();
            if (scene) {
                scene->OnPreScriptReload();
            }
        }
        m_ModuleGeneration.fetch_add(1, std::memory_order_release);
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
