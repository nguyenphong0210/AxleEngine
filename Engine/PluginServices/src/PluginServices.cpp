#include <Axle/PluginServices.hpp>
#include "PluginSDKConfig.hpp"
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/ExtensionDocument.hpp>
#include <Axle/SceneDataDocument.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Project.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <tuple>

namespace Axle {
bool ValidatePluginAssetDocument(const std::filesystem::path& path, std::string& error) {
    try {
        std::vector<std::string> errors;
        if (AssemblySerializer::IsAssemblyFile(path)) {
            Scene preview;
            if (!AssemblySerializer::Deserialize(path, &preview) || !preview.ExtensionsReady(errors)) {
                error = errors.empty() ? "Assembly plugin data could not be instantiated" : errors.front(); return false;
            }
            return true;
        }
        SceneData preview;
        if (!SceneDataDocument::Deserialize(FileSystem::ReadTextFile(path), preview) || !preview.ExtensionsReady(errors)) {
            error = errors.empty() ? "Scene plugin data could not be read" : errors.front(); return false;
        }
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
namespace {
using Registry = PluginRuntime::RegistrationRegistry;
constexpr const char* ExtensionKey = "axle.native.plugins";
template<class F> bool Checked(F&& operation) noexcept { try { return operation(); } catch (...) { return false; } }
struct CallbackScope { int& Depth; explicit CallbackScope(int& depth) : Depth(depth) { ++Depth; } ~CallbackScope() { --Depth; } };
struct NativeType {
    std::shared_ptr<void> Code;
    SDK::ComponentType Hooks{};
    std::string Id;
    std::uint64_t Owner = 0;
    bool Active = true;
    bool OwnsUser = false;
    ~NativeType() { if (OwnsUser && Hooks.User && Hooks.DestroyUser) Checked([&] { Hooks.DestroyUser(Hooks.User); return true; }); }
};
struct Payload {
    std::shared_ptr<NativeType> Type;
    SDK::EntityRef Entity{};
    entt::entity Handle = entt::null;
    void* Data = nullptr;
    bool Started = false;
    ~Payload() {
        if (Started && Type->Hooks.Stop) Checked([&] { Type->Hooks.Stop(Type->Hooks.User, Data, Entity); return true; });
        if (Data) Checked([&] { Type->Hooks.Destroy(Type->Hooks.User, Data); return true; });
    }
};
// EnTT owns one host-compiled wrapper pool. No plugin instantiates an EnTT pool
// or places a DLL vtable in the registry. Each scene additionally pins every
// descriptor it has used, including after the last visible value is removed.
struct NativeComponents { std::map<std::string, std::shared_ptr<Payload>> Values; };
struct OwnerContext {
    std::shared_ptr<void> Code;
    OperationOwner Completions;
    bool Active = true;
    bool Initialized = false;
};
struct PluginJob {
    std::shared_ptr<void> Code;
    JobHandle Handle;
    std::uint64_t Owner = 0, Scene = 0;
    bool Posted = false;
    std::atomic<bool> Succeeded{false};
    void (*Complete)(void*) = nullptr;
    void (*Destroy)(void*) = nullptr;
    void* Data = nullptr;
    ~PluginJob() { if (Destroy) Checked([&] { Destroy(Data); return true; }); }
};
class PluginScene;
}

struct PluginServiceSession::State : std::enable_shared_from_this<State> {
    std::shared_ptr<Registry::State> Registrations;
    const std::thread::id Thread = std::this_thread::get_id();
    bool Alive = false;
    bool AcceptJobs = true;
    int CallbackDepth = 0;
    std::map<std::string, std::shared_ptr<NativeType>> Types;
    std::map<std::uint64_t, std::shared_ptr<OwnerContext>> Owners;
    std::map<std::uint64_t, std::unique_ptr<PluginJob>> Jobs;
    std::uint64_t NextJob = 1;
    const SDK::EditorServices* EditorServices = nullptr;
    std::shared_ptr<void> EditorLifetime;
    std::vector<std::weak_ptr<SceneExtension>> Scenes;
    bool Access(std::uint64_t owner) const {
        return Thread == std::this_thread::get_id() && JobSystem::GetActiveTaskDepth() == 0 && Alive && Registry::OwnerIsActive(Registrations, owner);
    }
    void StopOwner(std::uint64_t owner);
    std::shared_ptr<OwnerContext> EnsureOwner(std::uint64_t owner) {
        if (!Access(owner)) return {};
        const auto found = Owners.find(owner); if (found != Owners.end()) return found->second;
        auto context = std::make_shared<OwnerContext>();
        context->Code = Registry::LeaseFor(Registrations, owner); if (!context->Code) return {};
        struct Token { std::weak_ptr<State> Session; std::uint64_t Owner; };
        auto token = std::make_unique<Token>(Token{weak_from_this(), owner});
        const auto id = "axle.native.owner/" + std::to_string(owner);
        Owners.emplace(owner, context);
        bool registered = false;
        try { registered = Registry::RegisterFor(Registrations, owner, id.c_str(), token.get(), [](void* value) {
            std::unique_ptr<Token> token(static_cast<Token*>(value));
            if (auto state = token->Session.lock()) state->StopOwner(token->Owner);
        }); } catch (...) { Owners.erase(owner); throw; }
        if (!registered) { Owners.erase(owner); return {}; }
        token.release(); return context;
    }
    void Drain() {
        if (Thread != std::this_thread::get_id()) return;
        // Use the Engine's scheduler and owner-token completion queue; the
        // session keeps job data/code until work has finished and the owner
        // completion has either run or been explicitly invalidated/drained.
        std::vector<std::uint64_t> ready;
        for (auto& [id, job] : Jobs) if (!job->Posted && job->Handle.IsComplete()) ready.push_back(id);
        for (auto id : ready) {
            auto found = Jobs.find(id); if (found == Jobs.end()) continue;
            auto& job = *found->second;
            job.Handle.Wait(); job.Posted = true;
            auto context = Owners.find(job.Owner);
            if (context == Owners.end() || !Access(job.Owner)) { Jobs.erase(found); continue; }
            const auto posted = JobSystem::PostToMainThread(context->second->Completions.GetToken(), [weak = weak_from_this(), id] {
                auto state = weak.lock(); if (!state) return;
                const auto found = state->Jobs.find(id); if (found == state->Jobs.end()) return;
                auto job = std::move(found->second); state->Jobs.erase(found);
                if (state->Access(job->Owner) && job->Succeeded && SceneData::FindInstance(job->Scene) && job->Complete)
                    Checked([&] { job->Complete(job->Data); return true; });
            });
            if (posted != PostResult::Success) Jobs.erase(id);
        }
        PluginRuntime::NativePlugin::CollectRetiredModules();
    }
};

namespace {
class PluginScene final : public SceneExtension {
public:
    SceneData& Scene;
    std::shared_ptr<PluginServiceSession::State> State;
    std::map<std::string, std::shared_ptr<NativeType>> TypePins;
    std::map<std::uint64_t, std::vector<ExtensionDebugLine>> DebugLines;
    bool Running = false, Faulted = false;
    unsigned Transactions = 0;
    PluginScene(SceneData& scene, std::shared_ptr<PluginServiceSession::State> state) : Scene(scene), State(std::move(state)) {}
    ~PluginScene() override = default;
    SDK::EntityRef Ref(Entity entity) const { return {Scene.GetInstanceId(), static_cast<std::uint64_t>(entity.GetUUID())}; }
    std::vector<std::shared_ptr<Payload>> Snapshot() const {
        std::vector<std::shared_ptr<Payload>> values;
        for (auto entity : Scene.GetRegistry().view<NativeComponents>())
            for (const auto& [id, payload] : Scene.GetRegistry().get<NativeComponents>(entity).Values) values.push_back(payload);
        // The session admits modules in resolved dependency order. Execute a
        // whole owner's phase before its dependents, independent of EnTT's
        // entity iteration order or the spelling of component type IDs.
        std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) {
            return std::tie(a->Type->Owner, a->Type->Id, a->Entity.Entity) < std::tie(b->Type->Owner, b->Type->Id, b->Entity.Entity);
        });
        return values;
    }
    std::shared_ptr<Payload> Find(Entity entity, const std::string& id) const {
        if (!entity || !entity.HasComponent<NativeComponents>()) return {};
        const auto& values = entity.GetComponent<NativeComponents>().Values;
        const auto found = values.find(id); return found == values.end() ? nullptr : found->second;
    }
    void Publish(Entity entity, const std::string& id, std::shared_ptr<Payload> payload) {
        TypePins.emplace(id, payload->Type);
        auto& values = Scene.GetRegistry().get_or_emplace<NativeComponents>(entity).Values;
        auto previous = values.extract(id);
        values.emplace(id, std::move(payload));
    }
    bool Set(std::uint64_t owner, Entity entity, const std::string& id, const std::string& text) {
        if (!State->Access(owner) || State->CallbackDepth || !Scene.CanMutate() || !entity) return false;
        const auto found = State->Types.find(id);
        if (found == State->Types.end() || found->second->Owner != owner || !found->second->Active) return false;
        auto payload = std::make_shared<Payload>(); payload->Type = found->second; payload->Entity = Ref(entity); payload->Handle = entity;
        CallbackScope callback(State->CallbackDepth);
        if (!Checked([&] { payload->Data = payload->Type->Hooks.Create(payload->Type->Hooks.User, payload->Entity, text.data(), static_cast<std::uint32_t>(text.size())); return payload->Data != nullptr; }) ||
            !State->Access(owner) || !payload->Type->Active || !entity) return false;
        Publish(entity, id, payload);
        if (Running && !Transactions) StartValue(payload);
        return true;
    }
    bool Remove(std::uint64_t owner, Entity entity, const std::string& id) {
        if (!State->Access(owner) || State->CallbackDepth || !Scene.CanMutate()) return false;
        const auto value = Find(entity, id); if (!value || value->Type->Owner != owner) return false;
        entity.GetComponent<NativeComponents>().Values.erase(id); return true;
    }
    void Destroy(Entity entity) override {
        if (entity.HasComponent<NativeComponents>()) {
            CallbackScope callback(State->CallbackDepth);
            auto values = Snapshot();
            std::erase_if(values, [&](const auto& value) { return value->Handle != static_cast<entt::entity>(entity); });
            Release(std::move(values));
            Scene.GetRegistry().remove<NativeComponents>(entity);
        }
    }
    void Release(std::vector<std::shared_ptr<Payload>> values) {
        while (!values.empty()) {
            auto value = std::move(values.back()); values.pop_back();
            if (Scene.GetRegistry().valid(value->Handle) && Scene.GetRegistry().all_of<NativeComponents>(value->Handle)) {
                auto& entries = Scene.GetRegistry().get<NativeComponents>(value->Handle).Values;
                const auto found = entries.find(value->Type->Id);
                if (found != entries.end() && found->second == value) entries.erase(found);
            }
            value.reset();
        }
    }
    void Clear() override {
        Running = false;
        DebugLines.clear();
        CallbackScope callback(State->CallbackDepth);
        Release(Snapshot());
        Scene.GetRegistry().clear<NativeComponents>();
        Faulted = false;
    }
    void RollbackOwner(std::uint64_t owner) {
        CallbackScope callback(State->CallbackDepth);
        auto values = Snapshot(); std::erase_if(values, [&](const auto& value) { return value->Type->Owner != owner; });
        Release(std::move(values));
        std::erase_if(TypePins, [&](const auto& item) { return item.second->Owner == owner; });
        DebugLines.erase(owner);
    }
    bool CanCopy(Entity entity) const override {
        if (!entity.HasComponent<NativeComponents>()) return true;
        for (const auto& [id, payload] : entity.GetComponent<NativeComponents>().Values)
            if (!payload->Type->Active || !State->Access(payload->Type->Owner)) return false;
        return true;
    }
    bool Copy(Entity destination, const SceneExtension& extension, Entity source,
              const std::unordered_map<std::uint64_t, std::uint64_t>& remap) override {
        const auto* other = dynamic_cast<const PluginScene*>(&extension);
        if (!other || !CanCopy(source) || !Scene.CanMutate()) return false;
        if (!source.HasComponent<NativeComponents>()) return true;
        std::vector<SDK::EntityRemap> mapping;
        for (const auto& [from, to] : remap) mapping.push_back({from, to});
        std::vector<std::shared_ptr<Payload>> sources;
        for (const auto& [id, payload] : source.GetComponent<NativeComponents>().Values) sources.push_back(payload);
        CallbackScope callback(State->CallbackDepth);
        for (const auto& value : sources) {
            auto clone = std::make_shared<Payload>(); clone->Type = value->Type; clone->Entity = Ref(destination); clone->Handle = destination;
            if (!Checked([&] { clone->Data = value->Type->Hooks.Clone(value->Type->Hooks.User, value->Data, clone->Entity, mapping.data(), static_cast<std::uint32_t>(mapping.size())); return clone->Data != nullptr; }) ||
                !clone->Type->Active || !State->Access(clone->Type->Owner) || !destination) return false;
            Publish(destination, clone->Type->Id, clone);
            if (Running && !Transactions) StartValue(clone);
        }
        return true;
    }
    bool Serialize(Entity entity, std::ostream& output) const override {
        if (!entity.HasComponent<NativeComponents>()) return true;
        auto values = entity.GetComponent<NativeComponents>().Values;
        CallbackScope callback(State->CallbackDepth);
        for (const auto& [id, value] : values) {
            if (!value->Type->Active || !State->Access(value->Type->Owner)) return false;
            ExtensionDocument document{id, value->Type->Hooks.Schema, value->Type->Hooks.Required, value->Type->Hooks.ContainsEntityReferences};
            if (!Checked([&] { return value->Type->Hooks.Serialize(value->Type->Hooks.User, value->Data,
                [](void* sink, const char* bytes, std::uint32_t count) -> bool {
                    return Checked([&] { if (!bytes && count) return false; if (count) static_cast<std::string*>(sink)->append(bytes, count); return true; });
                }, &document.Payload); })) return false;
            WriteExtensionDocument(output, document);
        }
        return true;
    }
    bool Deserialize(Entity entity, const std::string& section, const std::vector<std::string>& lines) override {
        ExtensionDocument document;
        if (!ParseExtensionDocument(section, lines, document)) return false;
        const auto type = State->Types.find(document.TypeId);
        if (type == State->Types.end()) return false;
        const auto& hooks = type->second->Hooks;
        if (document.Schema != hooks.Schema || document.Required != hooks.Required || document.EntityReferences != hooks.ContainsEntityReferences) return false;
        return Set(type->second->Owner, entity, document.TypeId, document.Payload);
    }
    bool Ready(std::vector<std::string>& errors) const override {
        bool ready = !Faulted;
        for (const auto& value : Snapshot()) if (!value->Type->Active || !State->Access(value->Type->Owner)) ready = false;
        for (auto entity : Scene.GetRegistry().view<PassiveExtensionComponent>())
            for (const auto& section : Scene.GetRegistry().get<PassiveExtensionComponent>(entity).Sections) {
                ExtensionDocument document;
                if (ParseExtensionDocument(section.SectionName, section.Lines, document) && State->Types.contains(document.TypeId)) ready = false;
            }
        if (!ready) errors.push_back("Native plugin data is inactive, incompatible or faulted");
        return ready;
    }
    void StartValue(const std::shared_ptr<Payload>& value) {
        if (value->Started || !value->Type->Active || !State->Access(value->Type->Owner)) return;
        value->Started = true;
        if (value->Type->Hooks.Start && !Checked([&] { value->Type->Hooks.Start(value->Type->Hooks.User, value->Data, value->Entity); return true; })) Faulted = true;
    }
    void Start() override {
        if (Running) return;
        Running = true;
        CallbackScope callback(State->CallbackDepth);
        for (const auto& value : Snapshot()) { StartValue(value); if (Faulted) break; }
    }
    void Stop() override {
        Running = false;
        DebugLines.clear();
        CallbackScope callback(State->CallbackDepth);
        const auto values = Snapshot();
        for (auto it = values.rbegin(); it != values.rend(); ++it) if (const auto& value = *it; value->Started) {
            value->Started = false;
            if (value->Type->Hooks.Stop && !Checked([&] { value->Type->Hooks.Stop(value->Type->Hooks.User, value->Data, value->Entity); return true; })) Faulted = true;
        }
    }
    void Update(ExtensionPhase phase, float dt) override {
        if (!Running || Faulted) return;
        if (phase == ExtensionPhase::Frame) DebugLines.clear();
        State->Drain();
        CallbackScope callback(State->CallbackDepth);
        for (const auto& value : Snapshot()) {
            if (!value->Started || !value->Type->Active || !State->Access(value->Type->Owner) || !value->Type->Hooks.Update) continue;
            if (!Checked([&] { value->Type->Hooks.Update(value->Type->Hooks.User, value->Data, value->Entity, static_cast<SDK::Phase>(phase), dt); return true; })) { Faulted = true; break; }
        }
    }
    void ExtractDebug(std::vector<ExtensionDebugLine>& output) const override {
        for (const auto& [owner, lines] : DebugLines) if (State->Access(owner)) output.insert(output.end(), lines.begin(), lines.end());
    }
    void BeginTransaction() override { ++Transactions; }
    bool EndTransaction(bool commit) override {
        if (!Transactions) return false;
        --Transactions;
        if (commit && !Transactions && Running) {
            CallbackScope callback(State->CallbackDepth);
            for (const auto& value : Snapshot()) { StartValue(value); if (Faulted) break; }
        }
        return !Faulted;
    }
};
PluginServiceSession::State* Access(void* user, std::uint64_t owner) {
    auto* state = static_cast<PluginServiceSession::State*>(user); return state && state->Access(owner) ? state : nullptr;
}

} // namespace
void PluginServiceSession::State::StopOwner(std::uint64_t owner) {
    const auto found = Owners.find(owner);
    const bool initialized = found != Owners.end() && found->second->Initialized;
    if (found != Owners.end()) { found->second->Active = false; found->second->Completions.Invalidate(); }
    for (auto& [id, type] : Types) if (type->Owner == owner) type->Active = false;
    for (;;) {
        const auto it = std::find_if(Jobs.begin(), Jobs.end(), [&](const auto& entry) { return entry.second->Owner == owner; });
        if (it == Jobs.end()) break;
        auto job = std::move(it->second); Jobs.erase(it); job->Handle.Cancel(); job->Handle.Wait();
    }
    if (!initialized) {
        const auto scenes = Scenes;
        for (const auto& weak : scenes) if (auto scene = std::dynamic_pointer_cast<PluginScene>(weak.lock())) scene->RollbackOwner(owner);
    }
    Owners.erase(owner);
}
namespace {
Entity Find(SDK::EntityRef ref) {
    auto* scene = SceneData::FindInstance(ref.Scene); return scene ? scene->GetEntityByUUID(UUID(ref.Entity)) : Entity{};
}
std::shared_ptr<PluginScene> Pool(Entity entity) {
    return entity ? std::dynamic_pointer_cast<PluginScene>(entity.GetSceneData()->GetExtension(ExtensionKey)) : nullptr;
}
bool OwnsType(PluginServiceSession::State& state, std::uint64_t caller, std::uint64_t typeOwner) {
    const auto callerId = Registry::PluginIdFor(state.Registrations, caller);
    return !callerId.empty() && callerId == Registry::PluginIdFor(state.Registrations, typeOwner);
}
bool RegisterType(PluginServiceSession::State& state, std::uint64_t owner, const SDK::ComponentType* hooks) {
    if (!hooks || hooks->StructSize != sizeof(*hooks) || !hooks->TypeId || !hooks->Schema || (hooks->User && !hooks->DestroyUser) || !hooks->Create || !hooks->Destroy || !hooks->Serialize || !hooks->Clone || state.CallbackDepth) return false;
    const std::string id = hooks->TypeId;
    const auto plugin = Registry::PluginIdFor(state.Registrations, owner);
    if (plugin.empty() || !id.starts_with(plugin + "/") || id.size() <= plugin.size() + 1 || state.Types.contains(id)) return false;
    const auto context = state.EnsureOwner(owner); if (!context) return false;
    auto type = std::make_shared<NativeType>(); type->Code = context->Code; type->Id = id; type->Hooks = *hooks; type->Hooks.TypeId = type->Id.c_str(); type->Owner = owner;
    struct Token { std::weak_ptr<PluginServiceSession::State> State; std::shared_ptr<NativeType> Type; };
    auto token = std::make_unique<Token>(Token{state.weak_from_this(), type});
    state.Types.emplace(id, type);
    bool registered = false;
    try { registered = Registry::RegisterFor(state.Registrations, owner, ("axle.native.type/" + id).c_str(), token.get(), [](void* value) {
        std::unique_ptr<Token> token(static_cast<Token*>(value)); token->Type->Active = false;
        if (auto state = token->State.lock()) { const auto found = state->Types.find(token->Type->Id); if (found != state->Types.end() && found->second == token->Type) state->Types.erase(found); }
    }); } catch (...) { state.Types.erase(id); throw; }
    if (!registered) { state.Types.erase(id); return false; }
    type->OwnsUser = true; token.release(); return true;
}
}

PluginServiceSession::PluginServiceSession(Registry& registry) : m_State(std::make_shared<State>()) { m_State->Registrations = registry.GetState(); }
PluginServiceSession::~PluginServiceSession() { Stop(); }
bool PluginServiceSession::Bind(std::string& error) {
    error.clear();
    if (m_State->Alive || m_State->Thread != std::this_thread::get_id()) { error = "Native services require one owner-thread session"; return false; }
    SDK::RuntimeServices api{};
    api.StructSize = sizeof(api); api.Version = SDK::Version; api.SdkFingerprint = AXLE_PLUGIN_SDK_FINGERPRINT; api.User = m_State.get();
    api.Address = [](void* user, std::uint64_t owner, SDK::ServiceAddress service, std::uint64_t sceneId) -> const void* {
        auto* state = Access(user, owner); if (!state) return nullptr;
        auto* scene = SceneData::FindInstance(sceneId);
        switch (service) {
        case SDK::ServiceAddress::Project: return Project::GetActive().get();
        case SDK::ServiceAddress::Jobs: return JobSystem::GetScheduler();
        case SDK::ServiceAddress::Physics: return scene && scene->GetRuntimeScene() ? scene->GetRuntimeScene()->GetPhysicsScene() : nullptr;
        case SDK::ServiceAddress::Renderer: return scene ? scene->GetExtension(ExtensionKey).get() : nullptr;
        case SDK::ServiceAddress::Components: return scene ? &scene->GetRegistry() : nullptr;
        } return nullptr;
    };
    api.RegisterComponent = [](void* user, std::uint64_t owner, const SDK::ComponentType* type) -> bool {
        return Checked([&] { auto* state = Access(user, owner); return state && RegisterType(*state, owner, type); });
    };
    api.GetTransform = [](void* user, std::uint64_t owner, SDK::EntityRef ref, SDK::Transform* output) -> bool {
        return Checked([&] { if (!Access(user, owner) || !output) return false; auto entity = Find(ref); if (!entity || !entity.HasComponent<TransformComponent>()) return false;
            const auto& value = entity.GetComponent<TransformComponent>();
            *output = {{value.Translation.x, value.Translation.y, value.Translation.z}, {value.Rotation.w, value.Rotation.x, value.Rotation.y, value.Rotation.z}, {value.Scale.x, value.Scale.y, value.Scale.z}}; return true; });
    };
    api.SetTransform = [](void* user, std::uint64_t owner, SDK::EntityRef ref, const SDK::Transform* input) -> bool {
        return Checked([&] { if (!Access(user, owner) || !input) return false; auto entity = Find(ref); if (!entity || !entity.HasComponent<TransformComponent>()) return false;
            for (float v : input->Position) if (!std::isfinite(v)) return false;
            for (float v : input->Rotation) if (!std::isfinite(v)) return false;
            for (float v : input->Scale) if (!std::isfinite(v)) return false;
            auto& value = entity.GetComponent<TransformComponent>();
            value.Translation = {input->Position[0], input->Position[1], input->Position[2]};
            value.Rotation = {input->Rotation[0], input->Rotation[1], input->Rotation[2], input->Rotation[3]};
            value.Scale = {input->Scale[0], input->Scale[1], input->Scale[2]}; return true; });
    };
    api.GetLinearVelocity = [](void* user, std::uint64_t owner, SDK::EntityRef ref, float* output) -> bool {
        return Checked([&] { if (!Access(user, owner) || !output) return false; auto entity = Find(ref);
            auto* scene = entity.GetScene(); auto* physics = scene ? scene->GetPhysicsScene() : nullptr;
            if (!physics || !entity.HasComponent<RigidBody3D>()) return false;
            const auto body = entity.GetComponent<RigidBody3D>().RuntimeBodyID; if (!physics->IsBodyValid(body)) return false;
            const auto velocity = physics->GetLinearVelocity(body); output[0] = velocity.x; output[1] = velocity.y; output[2] = velocity.z; return true; });
    };
    api.SetLinearVelocity = [](void* user, std::uint64_t owner, SDK::EntityRef ref, const float* input) -> bool {
        return Checked([&] { if (!Access(user, owner) || !input || !std::isfinite(input[0]) || !std::isfinite(input[1]) || !std::isfinite(input[2])) return false; auto entity = Find(ref);
            auto* scene = entity.GetScene(); auto* physics = scene ? scene->GetPhysicsScene() : nullptr;
            if (!physics || !entity.HasComponent<RigidBody3D>()) return false;
            const auto body = entity.GetComponent<RigidBody3D>().RuntimeBodyID; if (!physics->IsBodyValid(body)) return false;
            physics->SetLinearVelocity(body, {input[0], input[1], input[2]}); return true; });
    };
    api.SetComponent = [](void* user, std::uint64_t owner, SDK::EntityRef ref, const char* id, const char* payload, std::uint32_t bytes) -> bool {
        return Checked([&] { auto* state = Access(user, owner); if (!state || !id || (!payload && bytes)) return false; auto entity = Find(ref); auto pool = Pool(entity);
            const auto type = state->Types.find(id);
            return pool && type != state->Types.end() && OwnsType(*state, owner, type->second->Owner) &&
                pool->Set(type->second->Owner, entity, id, bytes ? std::string(payload, bytes) : std::string{}); });
    };
    api.GetComponent = [](void* user, std::uint64_t owner, SDK::EntityRef ref, const char* id) -> void* {
        try { auto* state = Access(user, owner); if (!state || !id) return nullptr; auto entity = Find(ref); auto pool = Pool(entity); const auto value = pool ? pool->Find(entity, id) : nullptr;
            return value && OwnsType(*state, owner, value->Type->Owner) && value->Type->Active ? value->Data : nullptr; } catch (...) { return nullptr; }
    };
    api.RemoveComponent = [](void* user, std::uint64_t owner, SDK::EntityRef ref, const char* id) -> bool {
        return Checked([&] { auto* state = Access(user, owner); if (!state || !id) return false; auto entity = Find(ref); auto pool = Pool(entity);
            const auto value = pool ? pool->Find(entity, id) : nullptr;
            return value && OwnsType(*state, owner, value->Type->Owner) && pool->Remove(value->Type->Owner, entity, id); });
    };
    api.SubmitJob = [](void* user, std::uint64_t owner, std::uint64_t scene, void (*work)(void*), void (*complete)(void*), void* data, void (*destroy)(void*)) -> bool {
        return Checked([&] { auto* state = Access(user, owner); if (!state || !work || !destroy || !SceneData::FindInstance(scene) || !JobSystem::IsInitialized()) return false;
            if (!state->AcceptJobs) return false;
            auto context = state->EnsureOwner(owner); if (!context) return false;
            auto job = std::make_unique<PluginJob>(); job->Code = context->Code; job->Owner = owner; job->Scene = scene; job->Data = data; job->Complete = complete;
            auto* pending = job.get();
            const auto id = state->NextJob++; state->Jobs.emplace(id, std::move(job));
            try { pending->Handle = JobSystem::Dispatch([pending, work] { pending->Succeeded = Checked([&] { work(pending->Data); return true; }); }); }
            catch (...) { state->Jobs.erase(id); throw; }
            if (!pending->Handle.IsValid()) { state->Jobs.erase(id); return false; }
            pending->Destroy = destroy; return true; });
    };
    api.DebugLine = [](void* user, std::uint64_t owner, std::uint64_t sceneId, const float* from, const float* to, const float* color) -> bool {
        return Checked([&] {
            auto* state = Access(user, owner); auto* scene = state ? SceneData::FindInstance(sceneId) : nullptr;
            if (!scene || !from || !to || !color) return false;
            auto pool = std::dynamic_pointer_cast<PluginScene>(scene->GetExtension(ExtensionKey)); if (!pool) return false;
            auto& lines = pool->DebugLines[owner]; if (lines.size() >= 4096) return false;
            ExtensionDebugLine line; line.CodeLease = Registry::LeaseFor(state->Registrations, owner); if (!line.CodeLease) return false;
            for (std::size_t i = 0; i < 3; ++i) { if (!std::isfinite(from[i]) || !std::isfinite(to[i])) return false; line.From[i] = from[i]; line.To[i] = to[i]; }
            for (std::size_t i = 0; i < 4; ++i) { if (!std::isfinite(color[i])) return false; line.Color[i] = std::clamp(color[i], 0.0f, 1.0f); }
            lines.push_back(std::move(line)); return true;
        });
    };
    api.Log = [](void* user, std::uint64_t owner, const char* text) { Checked([&] { if (Access(user, owner) && text) AXLE_INFO("[Plugin {0}] {1}", owner, text); return true; }); };
    if (!AxleRuntimeHost_BindV1(&api)) { error = "Could not bind the canonical Runtime host"; return false; }
    m_State->Alive = true;
    if (!SceneExtensionRegistry::Register(ExtensionKey, [state = m_State](SceneData& scene) {
        std::erase_if(state->Scenes, [](const auto& scene) { return scene.expired(); });
        auto extension = std::make_shared<PluginScene>(scene, state); state->Scenes.push_back(extension); return extension;
    })) {
        m_State->Alive = false; AxleRuntimeHost_UnbindV1(m_State.get()); error = "Could not attach native services to existing scenes"; return false;
    }
    return true;
}
void PluginServiceSession::Attach(PluginRuntime::LoadRequest& request) const {
    request.ServiceUser = m_State.get(); request.ServiceLifetime = m_State;
    request.OwnerInitialized = [](void* user, std::uint64_t owner, bool succeeded) {
        auto* state = static_cast<State*>(user);
        const auto found = state->Owners.find(owner); if (found != state->Owners.end()) found->second->Initialized = succeeded;
    };
    request.GetService = [](void* user, std::uint64_t owner, std::uint32_t service, std::uint32_t size) -> const void* {
        auto* state = Access(user, owner); if (!state) return nullptr;
        if (service == SDK::RuntimeService && size <= sizeof(SDK::RuntimeServices)) return AxleRuntimeHost_GetV1();
        if (service == SDK::EditorService && state->EditorServices && size <= sizeof(SDK::EditorServices)) return state->EditorServices;
        return nullptr;
    };
}
void PluginServiceSession::Drain() { m_State->Drain(); }
void PluginServiceSession::Quiesce() {
    if (!m_State || m_State->Thread != std::this_thread::get_id()) return;
    m_State->AcceptJobs = false;
    for (auto& [id, owner] : m_State->Owners) owner->Completions.Invalidate();
    while (!m_State->Jobs.empty()) {
        auto job = std::move(m_State->Jobs.begin()->second);
        m_State->Jobs.erase(m_State->Jobs.begin());
        job->Handle.Cancel(); job->Handle.Wait();
    }
}
void PluginServiceSession::ResumeJobs() {
    if (!m_State || !m_State->Alive || m_State->Thread != std::this_thread::get_id() || m_State->AcceptJobs) return;
    for (auto& [id, owner] : m_State->Owners) owner->Completions.Reset();
    m_State->AcceptJobs = true;
}
void PluginServiceSession::RehydrateScenes() {
    if (!m_State->Alive || m_State->Thread != std::this_thread::get_id()) return;
    const auto scenes = m_State->Scenes;
    for (const auto& weak : scenes) {
        const auto scene = std::dynamic_pointer_cast<PluginScene>(weak.lock());
        if (!scene || !scene->Scene.CanMutate()) continue;
        std::vector<entt::entity> entities;
        for (auto entity : scene->Scene.GetRegistry().view<PassiveExtensionComponent>()) entities.push_back(entity);
        for (auto id : entities) {
            Entity entity{id, &scene->Scene}; if (!entity || !entity.HasComponent<PassiveExtensionComponent>()) continue;
            const auto sections = entity.GetComponent<PassiveExtensionComponent>().Sections;
            std::vector<UnknownComponentSection> retained;
            for (const auto& section : sections) if (!scene->Deserialize(entity, section.SectionName, section.Lines)) retained.push_back(section);
            if (retained.empty()) entity.RemoveComponent<PassiveExtensionComponent>();
            else entity.GetComponent<PassiveExtensionComponent>().Sections = std::move(retained);
        }
    }
}
void PluginServiceSession::Stop() {
    if (!m_State || !m_State->Alive || m_State->Thread != std::this_thread::get_id()) return;
    m_State->Alive = false;
    m_State->EditorServices = nullptr; m_State->EditorLifetime.reset();
    SceneExtensionRegistry::Unregister(ExtensionKey);
    while (!m_State->Owners.empty()) m_State->StopOwner(m_State->Owners.begin()->first);
    for (auto& [id, type] : m_State->Types) type->Active = false;
    AxleRuntimeHost_UnbindV1(m_State.get());
    PluginRuntime::NativePlugin::CollectRetiredModules();
}
const SDK::RuntimeServices& PluginServiceSession::Services() const { return *AxleRuntimeHost_GetV1(); }
void PluginServiceSession::SetEditorServices(const SDK::EditorServices* services, std::shared_ptr<void> lifetime) {
    if (m_State->Thread != std::this_thread::get_id()) return;
    m_State->EditorServices = services; m_State->EditorLifetime = std::move(lifetime);
}
}
