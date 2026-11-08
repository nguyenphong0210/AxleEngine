#include <Axle/PluginSDK.hpp>
#include "Value.hpp"
#include <memory>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <stdexcept>
#ifdef AXLE_FIXTURE_COMPILE_FAILURE
#error Intentional external plugin compiler failure
#endif
namespace {
int live = 0;
int components = 0, completed = 0, jobsDestroyed = 0, contexts = 0, totalStarts = 0, totalStops = 0;
std::uint64_t startupScene = 0;
bool failStartup = false;
int workAccess = 0;
bool workOnOwner = false;
std::thread::id ownerThread;
bool completionAccess = false;
int dependencyStops = 0;
const Axle::SDK::RuntimeServices* services = nullptr;
std::uint64_t owner = 0;
struct Mover {
    std::string Label = "Native mover with nested data";
    float Speed = 2;
    std::uint64_t Reference = 0;
    std::uint64_t ReferenceAtStart = 0;
    bool PhysicsAtStart = false;
    int ObservedBase = -1;
    int Starts = 0, Stops = 0, Phases[5]{};
    Mover() { ++components; }
    Mover(const Mover& other) : Label(other.Label), Speed(other.Speed), Reference(other.Reference) { ++components; }
    ~Mover() { --components; }
};
struct Context { std::string Name = "owned descriptor context"; Context() { ++contexts; } ~Context() { --contexts; } };
void DestroyContext(void* pointer) { if (services) services->Log(services->User, owner, "destroy context"); delete static_cast<Context*>(pointer); }
void* Create(void* context, Axle::SDK::EntityRef, const char* text, std::uint32_t size) {
    if (!context || static_cast<Context*>(context)->Name.empty()) return nullptr;
    auto value = std::make_unique<Mover>();
    if (size) { std::istringstream input(std::string(text, size)); if (!(input >> value->Speed >> value->Reference >> std::quoted(value->Label))) return nullptr; }
    return value.release();
}
void DestroyMover(void* context, void* data) { if (!context || static_cast<Context*>(context)->Name.empty()) std::terminate(); if (services) services->Log(services->User, owner, "destroy value"); delete static_cast<Mover*>(data); }
bool Serialize(void*, const void* data, Axle::SDK::TextSink sink, void* user) {
    const auto& value = *static_cast<const Mover*>(data); std::ostringstream text;
    text << std::setprecision(9) << value.Speed << ' ' << value.Reference << ' ' << std::quoted(value.Label);
    const auto bytes = text.str(); return sink(user, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}
void* Clone(void*, const void* data, Axle::SDK::EntityRef, const Axle::SDK::EntityRemap* remap, std::uint32_t count) {
    auto value = std::make_unique<Mover>(*static_cast<const Mover*>(data));
    if (value->Label == "fail-clone") return nullptr;
    for (std::uint32_t i = 0; i < count; ++i) if (value->Reference == remap[i].Source) { value->Reference = remap[i].Destination; break; }
    return value.release();
}
void Start(void*, void* data, Axle::SDK::EntityRef entity) {
    auto& value = *static_cast<Mover*>(data); ++value.Starts; ++totalStarts; value.ReferenceAtStart = value.Reference;
    float velocity[3]{}; value.PhysicsAtStart = services->GetLinearVelocity(services->User, owner, entity, velocity);
    if (value.Label == "throw-start") throw std::runtime_error("fixture Start failed");
}
void Stop(void*, void* data, Axle::SDK::EntityRef entity) {
    ++static_cast<Mover*>(data)->Stops; ++totalStops;
    const auto* base = static_cast<const FixtureBase*>(services->GetComponent(services->User, owner, entity, "custom.plugin/ZBase"));
    if (base) dependencyStops += base->Started ? 1 : -1000;
}
void Update(void*, void* data, Axle::SDK::EntityRef entity, Axle::SDK::Phase phase, float dt) {
    auto& value = *static_cast<Mover*>(data); ++value.Phases[static_cast<int>(phase)];
    if (value.Label == "throw-update") throw std::runtime_error("fixture Update failed");
    if (phase == Axle::SDK::Phase::Frame) {
        Axle::SDK::Transform transform{};
        if (services->GetTransform(services->User, owner, entity, &transform)) { transform.Position[0] += value.Speed * dt; services->SetTransform(services->User, owner, entity, &transform); }
    }
    if (phase == Axle::SDK::Phase::FixedControls) {
        const auto* base = static_cast<const FixtureBase*>(services->GetComponent(services->User, owner, entity, "custom.plugin/ZBase"));
        value.ObservedBase = base ? base->Value : -1;
        float velocity[]{value.Speed, 0, 0}; services->SetLinearVelocity(services->User, owner, entity, velocity);
    }
    if (phase == Axle::SDK::Phase::Late) { const float from[]{0, 0, 0}, to[]{value.Speed, 1, 0}, color[]{1, 0, 0, 1}; services->DebugLine(services->User, owner, entity.Scene, from, to, color); }
}
struct Counter {
    std::string Name = "nontrivial project-owned payload";
    Counter() { ++live; }
    ~Counter() { --live; }
};
void Destroy(void* value) { delete static_cast<Counter*>(value); }
}
AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID, AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    try {
        if (!host || host->struct_size != sizeof(*host) || host->abi_version != Axle::PluginRuntime::kPluginAbiVersion) return false;
        owner = host->owner_id;
        ownerThread = std::this_thread::get_id();
        services = static_cast<const Axle::SDK::RuntimeServices*>(host->get_service(host->user_data, Axle::SDK::RuntimeService, sizeof(Axle::SDK::RuntimeServices)));
        if (services) {
            auto context = std::make_unique<Context>();
            const Axle::SDK::ComponentType mover{sizeof(mover), "custom.plugin/Mover", 1, true, true, context.get(), DestroyContext,
                Create, DestroyMover, Serialize, Clone, Start, Stop, Update};
            if (!services->RegisterComponent(services->User, owner, &mover)) return false;
            context.release();
            if (startupScene) {
                if (!services->SetComponent(services->User, owner, {startupScene, 100}, "custom.plugin/Mover", nullptr, 0)) return false;
                if (failStartup) return false;
            }
        }
        auto object = std::make_unique<Counter>();
        const auto id = std::string(AXLE_PLUGIN_ID) + "/counter";
        if (!host->register_resource(host->user_data, id.c_str(), object.get(), Destroy)) return false;
        object.release(); return true;
    } catch (...) { return false; }
}
AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept {}
AXLE_PLUGIN_EXPORT int ax_fixture_live() noexcept { return live; }
AXLE_PLUGIN_EXPORT int ax_fixture_value() noexcept { return 7; }
AXLE_PLUGIN_EXPORT int ax_fixture_shared_value() noexcept { return SharedValue; }
AXLE_PLUGIN_EXPORT const void* ax_fixture_runtime_table() noexcept { return AxleRuntimeHost_GetV1(); }
AXLE_PLUGIN_EXPORT int ax_fixture_components() noexcept { return components; }
AXLE_PLUGIN_EXPORT int ax_fixture_contexts() noexcept { return contexts; }
AXLE_PLUGIN_EXPORT int ax_fixture_total_starts() noexcept { return totalStarts; }
AXLE_PLUGIN_EXPORT int ax_fixture_total_stops() noexcept { return totalStops; }
AXLE_PLUGIN_EXPORT int ax_fixture_dependency_stops() noexcept { return dependencyStops; }
AXLE_PLUGIN_EXPORT int ax_fixture_observed_base(const void* data) noexcept { return static_cast<const Mover*>(data)->ObservedBase; }
AXLE_PLUGIN_EXPORT void ax_fixture_startup(std::uint64_t scene, bool fail) noexcept { startupScene = scene; failStartup = fail; }
AXLE_PLUGIN_EXPORT float* ax_fixture_speed(void* data) noexcept { return &static_cast<Mover*>(data)->Speed; }
AXLE_PLUGIN_EXPORT std::uint64_t ax_fixture_reference(const void* data) noexcept { return static_cast<const Mover*>(data)->Reference; }
AXLE_PLUGIN_EXPORT int ax_fixture_starts(const void* data) noexcept { return static_cast<const Mover*>(data)->Starts; }
AXLE_PLUGIN_EXPORT int ax_fixture_stops(const void* data) noexcept { return static_cast<const Mover*>(data)->Stops; }
AXLE_PLUGIN_EXPORT std::uint64_t ax_fixture_start_reference(const void* data) noexcept { return static_cast<const Mover*>(data)->ReferenceAtStart; }
AXLE_PLUGIN_EXPORT bool ax_fixture_start_physics(const void* data) noexcept { return static_cast<const Mover*>(data)->PhysicsAtStart; }
AXLE_PLUGIN_EXPORT int ax_fixture_phase(const void* data, int phase) noexcept { return phase >= 0 && phase < 5 ? static_cast<const Mover*>(data)->Phases[phase] : -1; }
AXLE_PLUGIN_EXPORT bool ax_fixture_submit(std::uint64_t scene) noexcept {
    try {
        if (!services) return false;
        auto data = std::make_unique<std::string>("nontrivial job data");
        if (!services->SubmitJob(services->User, owner, scene,
            [](void*) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); },
            [](void*) { ++completed; }, data.get(), [](void* pointer) { delete static_cast<std::string*>(pointer); ++jobsDestroyed; })) return false;
        data.release(); return true;
    } catch (...) { return false; }
}
AXLE_PLUGIN_EXPORT int ax_fixture_completed() noexcept { return completed; }
AXLE_PLUGIN_EXPORT int ax_fixture_jobs_destroyed() noexcept { return jobsDestroyed; }
AXLE_PLUGIN_EXPORT bool ax_fixture_submit_mutator(std::uint64_t scene, std::uint64_t entity) noexcept {
    try {
        auto data = std::make_unique<Axle::SDK::EntityRef>(Axle::SDK::EntityRef{scene, entity});
        if (!services->SubmitJob(services->User, owner, scene, [](void* data) {
            const auto ref = *static_cast<Axle::SDK::EntityRef*>(data); workOnOwner = std::this_thread::get_id() == ownerThread;
            Axle::SDK::Transform transform{{999,0,0},{1,0,0,0},{1,1,1}};
            if (services->GetTransform(services->User, owner, ref, &transform)) workAccess |= 1;
            if (services->SetTransform(services->User, owner, ref, &transform)) workAccess |= 2;
            if (services->GetComponent(services->User, owner, ref, "custom.plugin/Mover")) workAccess |= 4;
            float velocity[]{999,0,0}; if (services->SetLinearVelocity(services->User, owner, ref, velocity)) workAccess |= 8;
        }, [](void* data) {
            const auto ref = *static_cast<Axle::SDK::EntityRef*>(data); Axle::SDK::Transform transform{};
            completionAccess = services->GetTransform(services->User, owner, ref, &transform) && services->SetTransform(services->User, owner, ref, &transform);
        }, data.get(), [](void* data) { delete static_cast<Axle::SDK::EntityRef*>(data); })) return false;
        data.release(); return true;
    } catch (...) { return false; }
}
AXLE_PLUGIN_EXPORT int ax_fixture_work_access() noexcept { return workAccess; }
AXLE_PLUGIN_EXPORT bool ax_fixture_work_on_owner() noexcept { return workOnOwner; }
AXLE_PLUGIN_EXPORT bool ax_fixture_completion_access() noexcept { return completionAccess; }
