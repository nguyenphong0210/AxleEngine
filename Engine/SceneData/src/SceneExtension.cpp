#include <Axle/SceneData.hpp>
#include <Axle/Entity.hpp>
#include <Axle/ExtensionDocument.hpp>
#include <algorithm>
#include <map>

namespace Axle {
namespace Detail {
    // Accessed exclusively from the scene owner thread, like the ECS itself.
    std::vector<SceneData*>& LiveScenes() { static auto* scenes = new std::vector<SceneData*>; return *scenes; }
    std::map<std::string, SceneExtensionRegistry::Factory>& ExtensionFactories() {
        static auto* factories = new std::map<std::string, SceneExtensionRegistry::Factory>; return *factories;
    }
    std::uint64_t NextSceneInstance() { static std::atomic<std::uint64_t> next{1}; return next.fetch_add(1); }
}
bool SceneExtensionRegistry::Register(const std::string& key, Factory factory) {
    auto& factories = Detail::ExtensionFactories();
    if (key.empty() || !factory || factories.contains(key)) return false;
    auto scenes = Detail::LiveScenes();
    if (std::any_of(scenes.begin(), scenes.end(), [](auto* scene) { return !scene->IsOwnerThread(); })) return false;
    if (std::any_of(scenes.begin(), scenes.end(), [&](auto* scene) { return scene->m_Extensions.contains(key); })) return false;
    // Construct before publication so a throwing/failed factory leaves every
    // existing document and the factory catalog unchanged.
    std::vector<std::shared_ptr<SceneExtension>> pending;
    try {
        for (auto* scene : scenes) { auto extension = factory(*scene); if (!extension) return false; pending.push_back(std::move(extension)); }
        factories.emplace(key, factory);
        for (std::size_t i = 0; i < scenes.size(); ++i) scenes[i]->m_Extensions.emplace(key, std::move(pending[i]));
    } catch (...) {
        factories.erase(key);
        for (auto* scene : scenes) scene->m_Extensions.erase(key);
        return false;
    }
    return true;
}
void SceneExtensionRegistry::Unregister(const std::string& key) { Detail::ExtensionFactories().erase(key); }
SceneData* SceneData::FindInstance(std::uint64_t id) {
    for (auto* scene : Detail::LiveScenes()) if (scene->IsOwnerThread() && scene->GetInstanceId() == id) return scene;
    return nullptr;
}
std::shared_ptr<SceneExtension> SceneData::GetExtension(const std::string& key) const {
    if (!IsOwnerThread()) return {};
    const auto found = m_Extensions.find(key); return found == m_Extensions.end() ? nullptr : found->second;
}
bool SceneData::SerializeExtensions(Entity entity, std::ostream& output) const {
    if (!IsOwnerThread()) return false;
    for (const auto& [key, extension] : m_Extensions) if (extension && !extension->Serialize(entity, output)) return false;
    return true;
}
bool SceneData::DeserializeExtension(Entity entity, const std::string& section, const std::vector<std::string>& lines) {
    for (const auto& [key, extension] : m_Extensions) if (extension && extension->Deserialize(entity, section, lines)) return true;
    return false;
}
bool SceneData::ExtensionsReady(std::vector<std::string>& errors) const {
    if (!IsOwnerThread()) return false;
    bool ready = true;
    for (auto entity : m_Registry.view<PassiveExtensionComponent>()) {
        for (const auto& section : m_Registry.get<PassiveExtensionComponent>(entity).Sections) {
            if (!IsExtensionDocument(section.SectionName)) continue;
            ExtensionDocument document;
            if (!ParseExtensionDocument(section.SectionName, section.Lines, document)) {
                ready = false; errors.push_back("Malformed plugin component data must be repaired before simulation.");
            } else if (document.Required) {
                ready = false; errors.push_back("Required plugin component is unavailable: " + document.TypeId + " (schema " + std::to_string(document.Schema) + ")");
            }
        }
    }
    for (const auto& [key, extension] : m_Extensions) if (extension && !extension->Ready(errors)) ready = false;
    return ready;
}
bool SceneData::CanCopyExtensions(Entity entity) const {
    if (const auto* passive = GetPassiveSections(entity)) for (const auto& section : *passive) {
        if (!IsExtensionDocument(section.SectionName)) continue;
        ExtensionDocument document;
        if (!ParseExtensionDocument(section.SectionName, section.Lines, document) || document.EntityReferences) return false;
    }
    for (const auto& [key, extension] : m_Extensions) if (extension && !extension->CanCopy(entity)) return false;
    return true;
}
bool SceneData::CopyExtensions(Entity destination, Entity source, const std::unordered_map<std::uint64_t, std::uint64_t>& remap) {
    for (const auto& [key, extension] : m_Extensions) {
        const auto other = source.GetSceneData()->GetExtension(key);
        if (extension && other && !extension->Copy(destination, *other, source, remap)) return false;
    }
    return true;
}
bool SceneData::StartExtensions() {
    for (const auto& [key, extension] : m_Extensions) if (extension) extension->Start();
    std::vector<std::string> errors; return ExtensionsReady(errors);
}
void SceneData::StopExtensions() { for (const auto& [key, extension] : m_Extensions) if (extension) extension->Stop(); }
void SceneData::ClearExtensions() { for (const auto& [key, extension] : m_Extensions) if (extension) extension->Clear(); }
void SceneData::DestroyExtensions(Entity entity) { for (const auto& [key, extension] : m_Extensions) if (extension) extension->Destroy(entity); }
bool SceneData::UpdateExtensions(ExtensionPhase phase, float dt) {
    {
        IterationScope iteration(*this);
        for (const auto& [key, extension] : m_Extensions) if (extension) extension->Update(phase, dt);
    }
    std::vector<std::string> errors; return ExtensionsReady(errors);
}
std::vector<ExtensionDebugLine> SceneData::ExtractExtensionDebug() const {
    std::vector<ExtensionDebugLine> lines;
    for (const auto& [key, extension] : m_Extensions) if (extension) extension->ExtractDebug(lines);
    return lines;
}
void SceneData::BeginExtensionTransaction() {
    for (const auto& [key, extension] : m_Extensions) if (extension) extension->BeginTransaction();
}
bool SceneData::EndExtensionTransaction(bool commit) {
    bool success = true;
    for (const auto& [key, extension] : m_Extensions) if (extension && !extension->EndTransaction(commit)) success = false;
    return success;
}
}
