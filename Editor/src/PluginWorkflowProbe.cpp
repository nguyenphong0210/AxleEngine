#include "EditorLayer.hpp"
#include <Axle/Application.hpp>
#include <Axle/ExtensionDocument.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Path.hpp>
#include <Axle/UI/UIParser.hpp>
#include <Axle/Log.hpp>
#include <sstream>
#include <chrono>
#include <stdexcept>
namespace {
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct FailingStart final : Axle::SceneExtension {
    bool Failed = false;
    void Destroy(Axle::Entity) override {}
    bool CanCopy(Axle::Entity) const override { return true; }
    bool Copy(Axle::Entity, const Axle::SceneExtension&, Axle::Entity, const std::unordered_map<std::uint64_t,std::uint64_t>&) override { return true; }
    bool Serialize(Axle::Entity, std::ostream&) const override { return true; }
    bool Deserialize(Axle::Entity, const std::string&, const std::vector<std::string>&) override { return false; }
    bool Ready(std::vector<std::string>&) const override { return !Failed; }
    void Start() override { Failed = true; }
};
}
void EditorLayer::VerifyPluginWorkflow() {
    using namespace Axle;
    const auto root = Project::GetProjectDirectory();
    Require(std::filesystem::exists(root / "workflow.state"), "Plugin workflow probe requires the generated disposable fixture");
    Require(m_PluginSession && m_PluginSession->MatchesDesired(), "Native template did not activate in the product");
    const auto sourceScene = m_CurrentScenePath;
    auto edit = m_EditorScene; const auto physics = m_PhysicsScene.get();
    const auto original = SerializeEditorScene();
    auto actor = edit->GetEntityByUUID(UUID(100)); Require(static_cast<bool>(actor), "Template actor is missing");
    m_SceneHierarchyPanel.SetSelectedEntity(actor);
    auto opaque = edit->CreateEntity("Opaque references");
    std::ostringstream envelope; WriteExtensionDocument(envelope, {"unavailable.probe/References",1,false,true,"opaque"});
    std::istringstream input(envelope.str()); std::string line; std::getline(input,line);
    const auto section = line.substr(4,line.size()-5); std::vector<std::string> lines;
    while (std::getline(input,line)) lines.push_back(line);
    edit->StorePassiveSection(opaque,section,lines);
    OnScenePlay();
    Require(m_SceneState == SceneState::Edit && m_Scene == edit && m_PhysicsScene.get() == physics && !m_RuntimeScene,
        "Failed native Clone changed the edit world");
    Require(m_SceneHierarchyPanel.GetSelectedEntity() == actor, "Failed Play changed selection");
    edit->DestroyEntity(opaque);
    Require(SceneExtensionRegistry::Register("test.product.failed-start", [](SceneData&) { return std::make_shared<FailingStart>(); }), "Could not install startup failure probe");
    OnScenePlay();
    Require(m_SceneState == SceneState::Edit && m_Scene == edit && m_PhysicsScene.get() == physics && !m_RuntimeScene,
        "Failed native Start published a runtime world");
    SceneExtensionRegistry::Unregister("test.product.failed-start");
    auto bodyActor = edit->CreateEntity("Authored body type"); bodyActor.AddComponent<CollisionShape3D>(); bodyActor.AddComponent<RigidBody3D>().GravityFactor = 0;
    edit->RestorePhysicsBodies(m_PhysicsScene.get());
    const auto bodyAuthored = SerializeEditorScene(); const auto projectBeforePlay = m_CurrentProjectPath;
    OnScenePlay(); Require(m_SceneState == SceneState::Play && m_RuntimeScene && m_RuntimeScene->IsRuntimeActive(), "Template Play failed");
    SaveProjectAs(); Require(m_CurrentProjectPath == projectBeforePlay && Project::GetProjectDirectory() == root, "Save Project As changed the project during Play");
    const auto runtimeBody = m_RuntimeScene->GetEntityByUUID(bodyActor.GetUUID()).GetComponent<RigidBody3D>().RuntimeBodyID;
    m_PhysicsScene->GetBodyInterface().SetMotionType(runtimeBody,JPH::EMotionType::Static,JPH::EActivation::DontActivate);
    Require(SerializeEditorScene() == bodyAuthored, "Dirty capture read the Play physics world");
    Require(SaveScene(root/"assets/scenes/AuthoredBody.axlescene"), "Could not save the edit scene during Play");
    m_ShowPluginsPanel = true;
    m_SceneHierarchyPanel.SetSelectedEntity(m_RuntimeScene->GetEntityByUUID(UUID(100)));
    Require(Application::Get().Run(6), "Native plugin UI/render frames failed");
    Require(m_RuntimeScene->GetEntityByUUID(UUID(100)).GetComponent<TransformComponent>().Translation.x > 0, "Template update did not move its runtime entity");
    OnSceneStop(); Require(SerializeEditorScene() == bodyAuthored, "Stop changed authored body data");
    edit->DestroyEntity(bodyActor,m_PhysicsScene.get()); m_CurrentScenePath = sourceScene;
    Require(SerializeEditorScene() == original, "Stop changed authored template data");

    auto canvas = edit->CreateEntity("Same Canvas Name"); auto second = edit->CreateEntity("Same Canvas Name");
    auto& firstCanvas = canvas.AddComponent<UICanvasComponent>(); auto& secondCanvas = second.AddComponent<UICanvasComponent>();
    firstCanvas.Document = std::make_shared<UIDocument>(); secondCanvas.Document = std::make_shared<UIDocument>();
    std::string error;
    Require(UIParser::ParseSource("Canvas { Text { text: \"first\" } }", *firstCanvas.Document, &error), "Could not create canvas fixture");
    Require(UIParser::ParseSource("Canvas { Text { text: \"second\" } }", *secondCanvas.Document, &error), "Could not create second canvas fixture");
    firstCanvas.Document->SetDirty(); secondCanvas.Document->SetDirty();
    const auto firstPath = SceneSerializer::CanvasDocumentPath(canvas), secondPath = SceneSerializer::CanvasDocumentPath(second);
    Require(firstPath != secondPath, "Same-named canvases share an invented filename");
    const auto captured = SerializeEditorScene();
    Require(firstCanvas.AssetPath.empty() && firstCanvas.Document->IsDirty() && !std::filesystem::exists(firstPath), "Dirty capture persisted or changed UI data");
    Require(!m_DocumentGuard.Check("probe-cancel", "Probe cancellation", [] {}), "Dirty UI did not request a decision");
    m_DocumentGuard.Resolve(DocumentGuard::Choice::Cancel);
    Require(firstCanvas.Document->IsDirty() && SerializeEditorScene() == captured && !std::filesystem::exists(firstPath), "Cancel changed dirty UI data");
    m_CurrentScenePath = root / "assets/scenes/Guarded.axlescene";
    std::function<void()> saveAction;
    bool proceeded = false;
    saveAction = [&] { if (m_DocumentGuard.Check("probe-save", "Probe saving new UI documents", saveAction)) proceeded = true; };
    saveAction(); Require(m_DocumentGuard.Resolve(DocumentGuard::Choice::Save) && proceeded, "Save All failed for a new pathless UI Canvas");
    Require(std::filesystem::exists(firstPath) && std::filesystem::exists(secondPath), "Save All did not persist both UI documents");
    m_SceneHierarchyPanel.SetSelectedUINode(firstCanvas.Document->GetRootNode());
    Require(FileSystem::WriteIdentifiedTextFile(firstPath,"Canvas { Text { text: \"Reloaded source\" } }"), "Could not update UI source");
    std::filesystem::last_write_time(firstPath,std::filesystem::last_write_time(firstPath)+std::chrono::seconds(2));
    Require(Application::Get().Run(2) && !m_SceneHierarchyPanel.GetSelectedUINode(), "UI hot reload retained a dangling selected node");
    const auto beforeConflict = FileSystem::ReadTextFile(firstPath);
    secondCanvas.Document->SetFilePath(PathToUtf8(firstPath)); firstCanvas.Document->SetDirty(); secondCanvas.Document->SetDirty();
    Require(!SaveScene() && FileSystem::ReadTextFile(firstPath) == beforeConflict, "Direct scene Save overwrote conflicting UI buffers");
    firstCanvas.Document->SetDirty(false); secondCanvas.Document->SetDirty(false);
    // A selected node in an unsaved canvas must be released before its arena.
    auto pathless = edit->CreateEntity("Pathless selected UI"); auto& newCanvas = pathless.AddComponent<UICanvasComponent>();
    newCanvas.Document = std::make_shared<UIDocument>();
    Require(UIParser::ParseSource("Canvas { Text { text: \"discard\" } }", *newCanvas.Document, &error), "Could not create discard fixture");
    m_SceneSavedData = SerializeEditorScene(); newCanvas.Document->SetDirty();
    m_SceneHierarchyPanel.SetSelectedUINode(newCanvas.Document->GetRootNode());
    std::function<void()> discardAction;
    discardAction = [&] { m_DocumentGuard.Check("probe-discard", "Probe pathless UI discard", discardAction); };
    discardAction(); Require(m_DocumentGuard.Resolve(DocumentGuard::Choice::Discard), "Pathless UI discard failed");
    Require(!m_SceneHierarchyPanel.GetSelectedUINode() && !newCanvas.Document->IsDirty(), "Discard left a dangling selected UI node");
    edit->DestroyEntity(pathless); edit->DestroyEntity(second); edit->DestroyEntity(canvas);
    m_SceneSavedData = SerializeEditorScene();
    OpenScene(sourceScene);
    Require(m_EditorScene->GetEntityByUUID(UUID(100)) && SerializeEditorScene() == original, "Template scene reopen lost native data");
    const auto oldAssets = Project::GetAssetDirectory();
    auto relocatedCanvas = m_EditorScene->CreateEntity("Relocated Canvas"); auto& movingCanvas = relocatedCanvas.AddComponent<UICanvasComponent>();
    movingCanvas.Document = std::make_shared<UIDocument>(); Require(movingCanvas.Document->LoadFromFile(PathToUtf8(firstPath)), "Could not open UI before relocation");
    Require(FileSystem::MakeAssetReference(firstPath,movingCanvas.AssetPath), "UI fixture path is not owned");
    m_UICanvasDesignerPanel.OpenDocument(firstPath);
    const auto shader = oldAssets / "shaders/Relocated.hlsl";
    Require(FileSystem::WriteTextFileAtomically(shader,"// clean shader document\n"), "Could not write shader fixture"); m_HLSLStudioPanel.OpenFile(shader);
    const auto assembly = oldAssets / "Relocated.axle";
    Require(AssemblySerializer::Serialize(assembly,m_EditorScene->GetEntityByUUID(UUID(100))), "Could not write Assembly fixture"); m_AssemblyEditorPanel.SetAssembly(assembly);
    const auto effect = oldAssets / "Relocated.axleasset";
    Require(m_VFXEditorPanel.SaveDocumentAs(PathToUtf8(effect)), "Could not write VFX fixture");
    const auto originalUI = FileSystem::ReadTextFile(firstPath);
    const auto copiedProject = root / ("ProductSaveAs-"+std::to_string(static_cast<std::uint64_t>(UUID{}))) / "Copied.axleproj";
    Require(Project::SaveActive(copiedProject), "Save As failed to preserve assets and project plugin sources");
    RelocateCleanDocuments(oldAssets,Project::GetAssetDirectory()); m_CurrentProjectPath = copiedProject;
    Require(m_PluginSession->SetProjectFile(copiedProject), "Save As did not update the plugin session");
    const auto newAssets = Project::GetAssetDirectory();
    const auto copiedUI = newAssets / std::filesystem::relative(firstPath,oldAssets);
    Require(PathFromUtf8(movingCanvas.Document->GetFilePath()) == copiedUI && m_UICanvasDesignerPanel.GetCurrentPath() == copiedUI,
        "Save As left a UI document on the old root");
    Require(m_HLSLStudioPanel.GetCurrentFilePath() == newAssets / shader.lexically_relative(oldAssets) &&
        m_AssemblyEditorPanel.GetCurrentAsset() == newAssets / assembly.lexically_relative(oldAssets) &&
        PathFromUtf8(m_VFXEditorPanel.GetDocument().GetFilePath()) == newAssets / effect.lexically_relative(oldAssets), "Save As left private documents on the old root");
    movingCanvas.Document->GetRootNode()->ID = "ChangedInCopiedProject"; movingCanvas.Document->SetDirty();
    Require(SaveScene(newAssets/"scenes/Copied.axlescene"), "Relocated scene/UI could not be saved");
    Require(FileSystem::ReadTextFile(firstPath) == originalUI && FileSystem::ReadTextFile(copiedUI) != originalUI, "Relocated UI wrote back into the original project");
    AXLE_INFO("Native plugin product workflow: PASS (Inspector/render frames, failed Clone/Start rollback, Play/Stop, read-only capture, Canvas Save/Cancel/Discard, conflict refusal, reopen, Save As with private documents)");
}
