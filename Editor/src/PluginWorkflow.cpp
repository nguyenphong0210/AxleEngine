#include "EditorLayer.hpp"
#include "PluginSDKConfig.hpp"
#include "EditorStartupConfig.hpp"
#include <Axle/Application.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Path.hpp>
#include <Axle/PlatformUtils.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/SceneDataDocument.hpp>
#include <Axle/UI/UIDocument.hpp>
#include <Axle/Log.hpp>
#include <Axle/ScriptCompiler.hpp>
#include <set>
#include <windows.h>

Axle::PluginSessionEnvironment Axle::GetPluginSessionEnvironment() {
    return {AXLE_ENGINE_PLUGIN_ROOT, AXLE_PLUGIN_SDK_DIRECTORY, GetRuntimeBuildRequest().CMake,
        AXLE_PLUGIN_BUILD_CONFIGURATION, AXLE_PLUGIN_SDK_FINGERPRINT, AXLE_PLUGIN_TOOLCHAIN_FINGERPRINT,
        AXLE_PLUGIN_CMAKE_GENERATOR, AXLE_PLUGIN_CMAKE_TOOLSET, AXLE_PLUGIN_CMAKE_INSTANCE};
}

bool EditorLayer::WorkFrozen() const {
    return m_Closing || m_DocumentGuard.IsPending() || m_PluginRestart.IsWaiting() ||
        (m_PluginSession && m_PluginSession->IsBuilding()) || m_RuntimeBuildFuture.valid();
}
bool EditorLayer::CheckDocuments(std::string action, std::string title, std::function<void()> continuation, std::string document) {
    if (m_Attaching) return true;
    if (m_Closing || m_PluginRestart.IsWaiting() || m_RuntimeBuildFuture.valid() ||
        (m_PluginSession && m_PluginSession->IsBuilding())) return false;
    // Invalidate mutation-capable completion work before capturing documents.
    m_CompileOwner.Invalidate(); Axle::ScriptCompiler::Get().Cancel(); Axle::ScriptCompiler::Get().WaitForCompletion(); m_CompileOwner.Reset();
    if (m_PluginSession) m_PluginSession->Quiesce();
    return m_DocumentGuard.Check(std::move(action), std::move(title), std::move(continuation), std::move(document));
}
std::string EditorLayer::SerializeEditorScene() const {
    if (!m_EditorScene) return {};
    Axle::SceneSerializer serializer(m_EditorScene.get(), EditorPhysics());
    serializer.SetContext(m_SerializerContext);
    auto text = serializer.SerializeToString();
    if (text.empty()) throw std::runtime_error("Could not capture the scene document; plugin serialization failed");
    return text;
}
std::vector<Axle::GuardedDocument> EditorLayer::CollectDocuments() {
    using namespace Axle;
    std::vector<GuardedDocument> documents;
    documents.push_back({"assembly", "Assembly", m_AssemblyEditorPanel.GetCurrentAsset(),
        [this] { return m_AssemblyEditorPanel.IsDirty(); },
        [this](std::string&) { return m_AssemblyEditorPanel.SaveDocument(); },
        [this](std::string&) { m_AssemblyEditorPanel.DiscardChanges(); return !m_AssemblyEditorPanel.IsDirty(); }});
    documents.push_back({"ui-source", "UI Canvas source", m_UICanvasDesignerPanel.GetCurrentPath(),
        [this] { return m_UICanvasDesignerPanel.IsDirty(); },
        [this](std::string&) { return m_UICanvasDesignerPanel.SaveDocument(); },
        [this](std::string&) { m_UICanvasDesignerPanel.DiscardChanges(); return true; }});
    documents.push_back({"hlsl", "HLSL source", m_HLSLStudioPanel.GetCurrentFilePath(),
        [this] { return m_HLSLStudioPanel.IsDirty(); },
        [this](std::string&) { return m_HLSLStudioPanel.SaveFile(); },
        [this](std::string&) { m_HLSLStudioPanel.DiscardChanges(); return true; }});
    documents.push_back({"vfx", "VFX document", PathFromUtf8(m_VFXEditorPanel.GetDocument().GetFilePath()),
        [this] { return m_VFXEditorPanel.GetDocument().IsDirty(); },
        [this](std::string&) {
            if (!m_VFXEditorPanel.GetDocument().GetFilePath().empty()) return m_VFXEditorPanel.SaveDocument();
            const auto root = PathToUtf8(FileSystem::GetAssetDirectory());
            auto path = FileDialogs::SaveFile("VFX (*.axlevfx)\0*.axlevfx\0", root.c_str(), "axlevfx");
            return !path.empty() && m_VFXEditorPanel.SaveDocumentAs(path);
        }, [this](std::string&) { m_VFXEditorPanel.DiscardDocument(); return true; }});
    std::set<UIDocument*> seen;
    if (m_EditorScene) for (auto id : m_EditorScene->GetRegistry().view<UICanvasComponent>()) {
        auto& canvas = m_EditorScene->GetRegistry().get<UICanvasComponent>(id);
        auto document = canvas.Document;
        if (!document || !seen.insert(document.get()).second) continue;
        const bool newDocument = document->GetFilePath().empty() && canvas.AssetPath.empty();
        const auto path = SceneSerializer::CanvasDocumentPath(Entity{id, m_EditorScene.get()});
        const auto scene = m_EditorScene;
        documents.push_back({"canvas/" + std::to_string(static_cast<std::uint32_t>(id)), "Scene UI Canvas", path,
            [document] { return document->IsDirty(); },
            [document, path, scene, id](std::string& error) {
                std::string reference;
                if (!FileSystem::MakeAssetReference(path, reference)) { error = "UI document must be inside the active asset root"; return false; }
                if (!scene->GetRegistry().valid(id) || !scene->GetRegistry().all_of<UICanvasComponent>(id)) return false;
                std::filesystem::create_directories(path.parent_path());
                if (!document->SaveToFile(PathToUtf8(path))) return false;
                scene->GetRegistry().get<UICanvasComponent>(id).AssetPath = reference; return true;
            }, [this, document, path, newDocument](std::string& error) {
                m_SceneHierarchyPanel.SetSelectedUINode(nullptr);
                if (newDocument) { *document = UIDocument{}; return true; }
                return document->LoadFromFile(PathToUtf8(path));
            }});
    }
    // Last: saving private documents may update references in this scene.
    documents.push_back({"scene", "Scene", m_CurrentScenePath,
        [this] { return SerializeEditorScene() != m_SceneSavedData; },
        [this](std::string&) { return SaveScene(); },
        [this](std::string& error) {
            if (!m_EditorScene) return true;
            m_SceneHierarchyPanel.SetSelectedEntity({}); m_SceneHierarchyPanel.SetSelectedUINode(nullptr);
            Axle::SceneSerializer serializer(m_EditorScene.get(), EditorPhysics());
            serializer.SetContext(m_SerializerContext);
            if (!serializer.DeserializeFromString(m_SceneSavedData)) { error = "Could not restore the saved scene"; return false; }
            m_SceneSavedData = SerializeEditorScene(); return true;
        }});
    return documents;
}
void EditorLayer::RenderDocumentDecision() {
    if (!m_DocumentGuard.IsPending()) return;
    ImGui::OpenPopup("Unsaved documents###PluginDocumentDecision");
    if (ImGui::BeginPopupModal("Unsaved documents###PluginDocumentDecision", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_DocumentGuard.Title().c_str());
        for (const auto& document : m_DocumentGuard.Documents()) if (document.IsDirty()) ImGui::BulletText("%s", document.Label.c_str());
        if (!m_DocumentGuard.Error().empty()) ImGui::TextWrapped("%s", m_DocumentGuard.Error().c_str());
        if (ImGui::Button("Save all")) {
            m_DocumentGuard.Resolve(Axle::DocumentGuard::Choice::Save);
            if (!m_DocumentGuard.IsPending()) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard edits")) {
            m_DocumentGuard.Resolve(Axle::DocumentGuard::Choice::Discard);
            if (!m_DocumentGuard.IsPending()) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { m_DocumentGuard.Resolve(Axle::DocumentGuard::Choice::Cancel); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}
void EditorLayer::RefreshPluginPanel(bool preserveDraft) {
    if (!m_PluginSession) return;
    m_PluginSession->Refresh();
    m_PluginsPanel.SetCatalog(m_PluginSession->Catalog(), preserveDraft);
    m_PluginsPanel.SetActiveIds(m_PluginSession->ActiveIds());
    m_PluginsPanel.SetExternalError(m_PluginSession->Error());
    m_PluginsPanel.SetNeedsRestart(m_PluginSession->ActiveFingerprint() != m_PluginSession->DesiredPlan().Fingerprint || m_PluginSession->IsSafeMode());
}
void EditorLayer::BuildSelectedPlugins() {
    if (m_SceneState != SceneState::Edit || !m_PluginSession) return;
    if (!CheckDocuments("build-plugins", "Save or discard edits before building plugins.", [this] { BuildSelectedPlugins(); })) return;
    if (!m_PluginSession->BeginBuild()) m_PluginsPanel.SetExternalError(m_PluginSession->Error());
}
void EditorLayer::StartExportGame() {
    using namespace Axle;
    if (m_SceneState != SceneState::Edit || !CheckDocuments("export-game", "Save or discard document edits before exporting.", [this] { StartExportGame(); })) return;
    if (m_CurrentProjectPath.empty()) { m_ExportStatusMessage = "Save Project As before exporting"; return; }
    CookSettings settings;
    settings.ProjectPath = m_CurrentProjectPath; settings.AssetDirectory = Project::GetAssetDirectory();
    settings.OutputDirectory = PathFromUtf8(m_ExportDirectoryBuffer); settings.BakeMeshesToBinary = m_ExportBakeMeshes;
    settings.IncludeShaders = m_ExportIncludeShaders; settings.CopyExecutables = m_ExportCopyExecutables;
    settings.Preflight = [this, copy = settings.CopyExecutables](std::string& error) {
        if (!m_PluginSession || !m_PluginSession->MatchesDesired()) { error = "Build and restart to activate Desired plugins before exporting"; return false; }
        if (copy) for (const auto& module : m_PluginSession->DesiredPlan().Artifacts) if (module.Kind == Plugin::ModuleKind::Runtime) {
            error = "Standalone native plugin composition is scheduled for PLG-22; this player cannot contain selected native modules"; return false;
        }
        std::vector<std::string> errors;
        if (m_EditorScene && !m_EditorScene->ExtensionsReady(errors)) { error = errors.empty() ? "Scene plugin data is not ready" : errors.front(); return false; }
        return true;
    };
    settings.ValidatePluginDocument = ValidatePluginAssetDocument;
    m_ExportStatusSuccess = false; m_ExportLastOutputDir.clear();
    if (!settings.Preflight(m_ExportStatusMessage)) return;
    if (settings.CopyExecutables) {
        const auto request = GetRuntimeBuildRequest(); settings.ExecutableDirectory = request.ExecutableDirectory;
        m_PendingExportSettings = settings; m_RuntimeBuildCancel = std::make_shared<std::atomic_bool>(false);
        try {
            m_RuntimeBuildFuture = std::async(std::launch::async, [request, cancel = m_RuntimeBuildCancel] { return BuildRuntime(request, *cancel); });
            m_ExportStatusMessage = "Building standalone player... Export starts after the build succeeds.";
        } catch (const std::exception& error) { m_ExportStatusMessage = error.what(); }
    } else FinishExportGame(settings);
}
void EditorLayer::OpenAssemblyDocument(const std::filesystem::path& path) {
    if (!CheckDocuments("open-assembly", "Save or discard Assembly edits before opening another file.", [this, path] { OpenAssemblyDocument(path); }, "assembly")) return;
    m_AssemblyEditorPanel.SetAssembly(path); m_ShowAssemblyEditor = true;
}
void EditorLayer::OpenShaderDocument(const std::filesystem::path& path) {
    if (!CheckDocuments("open-hlsl", "Save or discard shader edits before opening another file.", [this, path] { OpenShaderDocument(path); }, "hlsl")) return;
    m_HLSLStudioPanel.OpenFile(path); m_ShowHLSLStudio = true; m_HLSLStudioPanel.SetOpen(true); m_HLSLStudioPanel.Focus();
}
void EditorLayer::OpenVFXDocument(const std::filesystem::path& path) {
    if (!CheckDocuments("open-vfx", "Save or discard VFX edits before opening another file.", [this, path] { OpenVFXDocument(path); }, "vfx")) return;
    m_ShowVFXEditor = true; m_VFXEditorPanel.SetOpen(true); m_VFXEditorPanel.OpenDocument(Axle::PathToUtf8(path));
}
void EditorLayer::RequestClose() {
    if (m_PluginRestart.IsWaiting()) m_PluginRestart.Cancel();
    if (m_PluginSession && m_PluginSession->IsBuilding()) { m_PluginSession->CancelBuild(); m_CloseAfterWork = true; return; }
    if (m_RuntimeBuildFuture.valid()) { if (m_RuntimeBuildCancel) *m_RuntimeBuildCancel = true; m_CloseAfterWork = true; return; }
    if (!CheckDocuments("close-editor", "Save or discard edits before closing the Editor.", [this] { RequestClose(); })) return;
    m_Closing = true; Axle::Application::Get().Close();
}
void EditorLayer::RelocateCleanDocuments(const std::filesystem::path& oldAssets, const std::filesystem::path& newAssets) {
    if (oldAssets == newAssets) return;
    const auto rebase = [&](const std::filesystem::path& path) {
        if (path.empty()) return path;
        const auto relative = std::filesystem::weakly_canonical(path).lexically_relative(std::filesystem::weakly_canonical(oldAssets));
        return relative.empty() || relative.is_absolute() || *relative.begin() == ".." ? path : newAssets / relative;
    };
    m_SceneHierarchyPanel.SetSelectedUINode(nullptr);
    if (m_EditorScene) for (auto id : m_EditorScene->GetRegistry().view<Axle::UICanvasComponent>()) {
        auto document = m_EditorScene->GetRegistry().get<Axle::UICanvasComponent>(id).Document;
        if (document) document->SetFilePath(Axle::PathToUtf8(rebase(Axle::PathFromUtf8(document->GetFilePath()))));
    }
    m_UICanvasDesignerPanel.RelocateCleanDocument(rebase(m_UICanvasDesignerPanel.GetCurrentPath()));
    m_HLSLStudioPanel.RelocateCleanDocument(rebase(m_HLSLStudioPanel.GetCurrentFilePath()));
    m_AssemblyEditorPanel.RelocateCleanDocument(rebase(m_AssemblyEditorPanel.GetCurrentAsset()));
    m_VFXEditorPanel.RelocateCleanDocument(rebase(Axle::PathFromUtf8(m_VFXEditorPanel.GetDocument().GetFilePath())));
}
void EditorLayer::LaunchProjectRestart(const std::filesystem::path& path, const std::string& backend) {
    using namespace Axle;
    if (m_SceneState != SceneState::Edit) { m_PluginsPanel.SetExternalError("Stop simulation before restarting the Editor"); return; }
    if (!CheckDocuments("restart-editor", "Save or discard edits before restarting the Editor.", [this, path, backend] { LaunchProjectRestart(path, backend); })) return;
    try {
        if (path.empty()) { m_PluginsPanel.SetExternalError("Save Project As before restarting the Editor"); return; }
        // Inspect the target without changing the canonical active Project.
        auto target = std::make_shared<Project>(); ProjectSerializer serializer(target);
        if (!serializer.Deserialize(path)) { m_PluginsPanel.SetExternalError("Could not read target project"); return; }
        auto environment = GetPluginSessionEnvironment();
        Plugin::DiscoveryOptions options;
        options.Roots = {environment.EnginePlugins, target->GetProjectDirectoryPath() / "Plugins"};
        options.ProjectRoots = {target->GetProjectDirectoryPath() / "Plugins"};
        auto catalog = Plugin::Discover(options);
        auto plan = Plugin::CreateBuildPlan(Plugin::Resolve(catalog, target->GetConfig().SelectedPlugins, Plugin::ProductKind::Editor),
            Plugin::ProductKind::Editor, environment.Configuration, environment.SdkFingerprint, environment.ToolchainFingerprint);
        PluginBuildRequest request; request.ProjectDirectory = target->GetProjectDirectoryPath(); request.Plan = plan;
        std::atomic_bool cancel{false}; auto built = BuildPlugins(request, cancel);
        if (!built.Success) { m_PluginsPanel.SetExternalError("Build target plugins before restarting: " + built.Error); return; }
        PluginRestartSnapshot snapshot; std::vector<std::string> modules;
        for (const auto& artifact : plan.Artifacts) if (artifact.Kind != Plugin::ModuleKind::Content) modules.push_back(artifact.ModuleName);
        std::string snapshotError;
        if (!CapturePluginRestartSnapshot(built.GenerationDirectory,modules,snapshot,snapshotError)) { m_PluginsPanel.SetExternalError(snapshotError); return; }
        // A retry clears only closed recovery markers; the live parent is retained.
        if (m_PluginSession && path == m_CurrentProjectPath && !m_PluginSession->ClearAbandonedSessions()) {
            m_PluginsPanel.SetExternalError("Could not clear abandoned plugin session markers"); return;
        }
        std::wstring executable(32768, L'\0'); const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size()) throw std::runtime_error("Could not locate this Editor executable");
        executable.resize(length);
        const auto wideBackend = PathFromUtf8("--rhi=" + backend).wstring();
        std::string error;
        if (!m_PluginRestart.Launch(executable, {L"--project=" + std::filesystem::absolute(path).wstring(),
            wideBackend, L"--expected-plugin-plan=" + PathFromUtf8(plan.Fingerprint).wstring(),
            L"--expected-plugin-generation=" + (snapshot.Generation.empty() ? L"none" : snapshot.Generation.wstring()),
            L"--expected-plugin-receipts=" + PathFromUtf8(snapshot.ReceiptSet).wstring()}, error))
            m_PluginsPanel.SetExternalError(error);
        else m_PluginsPanel.SetBusy(true, "Waiting for the new Editor to acknowledge startup...");
    } catch (const std::exception& error) { m_PluginsPanel.SetExternalError(error.what()); }
}
