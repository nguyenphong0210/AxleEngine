#include "PluginEditorServices.hpp"
#include <imgui.h>
#include <Axle/JobSystem.hpp>
#include <map>
#include <thread>
#include <cstring>

namespace Axle {
namespace {
using Registry = PluginRuntime::RegistrationRegistry;
template<class F> bool Checked(F&& fn) noexcept { try { return fn(); } catch (...) { return false; } }
struct Inspector {
    std::shared_ptr<void> Code;
    std::uint64_t Owner = 0;
    std::string Type;
    SDK::InspectorDraw Draw = nullptr;
    void* User = nullptr;
    bool Active = true;
};
}
struct PluginEditorServices::State : std::enable_shared_from_this<State> {
    std::shared_ptr<Registry::State> RegistryState;
    std::map<std::string, std::shared_ptr<Inspector>> Inspectors;
    std::thread::id Thread = std::this_thread::get_id();
    bool Alive = false, Changed = false;
    std::uint64_t DrawingOwner = 0;
    bool Access() const { return Thread == std::this_thread::get_id() && JobSystem::GetActiveTaskDepth() == 0 && Alive && ImGui::GetCurrentContext(); }
    bool CanDraw() const { return Access() && DrawingOwner && Registry::OwnerIsActive(RegistryState, DrawingOwner); }
};
PluginEditorServices::PluginEditorServices(PluginServiceSession& runtime, Registry& registry)
    : m_Runtime(runtime), m_State(std::make_shared<State>()) { m_State->RegistryState = registry.GetState(); }
PluginEditorServices::~PluginEditorServices() { Stop(); }
bool PluginEditorServices::Bind(std::string& error) {
    if (m_State->Alive || m_State->Thread != std::this_thread::get_id() || !ImGui::GetCurrentContext()) { error = "Editor services require the host GUI context and owner thread"; return false; }
    if (!m_Runtime.Services().SdkFingerprint || std::strcmp(m_Runtime.Services().SdkFingerprint, AxleEditorHost_SdkFingerprint()) != 0) { error = "Editor host SDK does not match the Runtime host"; return false; }
    SDK::EditorServices api{}; api.StructSize = sizeof(api); api.Version = SDK::Version; api.User = m_State.get();
    api.Context = [](void* user) -> const void* { const auto* state = static_cast<State*>(user); return state->Access() ? ImGui::GetCurrentContext() : nullptr; };
    api.RegisterInspector = [](void* user, std::uint64_t owner, const char* type, SDK::InspectorDraw draw, void* context) -> bool {
        return Checked([&] {
            auto* state = static_cast<State*>(user);
            if (!state->Access() || !type || !draw || !Registry::OwnerIsActive(state->RegistryState, owner) || state->Inspectors.contains(type)) return false;
            const auto id = Registry::PluginIdFor(state->RegistryState, owner);
            if (id.empty() || !std::string(type).starts_with(id + "/")) return false;
            auto inspector = std::make_shared<Inspector>(); inspector->Code = Registry::LeaseFor(state->RegistryState, owner);
            if (!inspector->Code) return false;
            inspector->Owner = owner; inspector->Type = type; inspector->Draw = draw; inspector->User = context;
            struct Token { std::weak_ptr<State> State; std::shared_ptr<Inspector> Entry; };
            auto token = std::make_unique<Token>(Token{state->weak_from_this(), inspector});
            if (!Registry::RegisterFor(state->RegistryState, owner, ("axle.native.inspector/" + inspector->Type).c_str(), token.get(), [](void* resource) {
                std::unique_ptr<Token> token(static_cast<Token*>(resource)); token->Entry->Active = false;
                if (auto state = token->State.lock()) { const auto entry = state->Inspectors.find(token->Entry->Type); if (entry != state->Inspectors.end() && entry->second == token->Entry) state->Inspectors.erase(entry); }
            })) return false;
            token.release(); state->Inspectors.emplace(inspector->Type, inspector); return true;
        });
    };
    api.Text = [](void* user, const char* text) { Checked([&] { auto* state = static_cast<State*>(user); if (state->CanDraw() && text) ImGui::TextUnformatted(text); return true; }); };
    api.DragFloat = [](void* user, const char* label, float* value, float speed) -> bool {
        return Checked([&] { auto* state = static_cast<State*>(user); if (!state->CanDraw() || !label || !value) return false; const bool changed = ImGui::DragFloat(label, value, speed); state->Changed |= changed; return changed; });
    };
    api.Button = [](void* user, const char* label) -> bool {
        return Checked([&] { auto* state = static_cast<State*>(user); if (!state->CanDraw() || !label) return false; const bool changed = ImGui::Button(label); state->Changed |= changed; return changed; });
    };
    m_State->Alive = true;
    if (!AxleEditorHost_BindV1(&api)) { m_State->Alive = false; error = "Could not bind canonical Editor services"; return false; }
    m_Runtime.SetEditorServices(AxleEditorHost_GetV1(), m_State); error.clear(); return true;
}
void PluginEditorServices::Stop() {
    if (!m_State->Alive || m_State->Thread != std::this_thread::get_id()) return;
    m_State->Alive = false; m_Runtime.SetEditorServices(nullptr, {});
    for (auto& [id, inspector] : m_State->Inspectors) inspector->Active = false;
    m_State->Inspectors.clear(); AxleEditorHost_UnbindV1(m_State.get());
}
bool PluginEditorServices::DrawComponents(Entity entity) {
    if (!m_State->Access() || !entity) return false;
    const auto entries = m_State->Inspectors;
    auto& runtime = m_Runtime.Services(); m_State->Changed = false;
    const SDK::EntityRef ref{entity.GetSceneData()->GetInstanceId(), static_cast<std::uint64_t>(entity.GetUUID())};
    for (const auto& [id, entry] : entries) {
        if (!entry->Active || !Registry::OwnerIsActive(m_State->RegistryState, entry->Owner) || !runtime.GetComponent) continue;
        auto* component = runtime.GetComponent(runtime.User, entry->Owner, ref, id.c_str()); if (!component) continue;
        ImGui::PushID(id.c_str());
        if (ImGui::CollapsingHeader(id.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            m_State->DrawingOwner = entry->Owner;
            Checked([&] { entry->Draw(entry->User, ref, component); return true; });
            m_State->DrawingOwner = 0;
            if (entry->Active && ImGui::SmallButton("Remove component")) m_State->Changed |= runtime.RemoveComponent(runtime.User, entry->Owner, ref, id.c_str());
        }
        ImGui::PopID();
    }
    return m_State->Changed;
}
bool PluginEditorServices::DrawAddComponents(Entity entity) {
    if (!m_State->Access() || !entity) return false;
    const auto entries = m_State->Inspectors;
    auto& runtime = m_Runtime.Services(); bool changed = false;
    const SDK::EntityRef ref{entity.GetSceneData()->GetInstanceId(), static_cast<std::uint64_t>(entity.GetUUID())};
    for (const auto& [id, entry] : entries) {
        if (!entry->Active || !Registry::OwnerIsActive(m_State->RegistryState, entry->Owner) || !runtime.GetComponent) continue;
        if (!runtime.GetComponent(runtime.User, entry->Owner, ref, id.c_str()) && ImGui::MenuItem(id.c_str()))
            changed |= runtime.SetComponent(runtime.User, entry->Owner, ref, id.c_str(), nullptr, 0);
    }
    return changed;
}
}
