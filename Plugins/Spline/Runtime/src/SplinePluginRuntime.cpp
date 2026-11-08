#include <Axle/PluginSDK.hpp>
#include <AxleSpline/SplinePluginRuntime.hpp>
#include <AxleSpline/SplineComponent.hpp>
#include <AxleSpline/SplineSerializer.hpp>
#include <AxleSpline/TrackGenerator.hpp>
#include <Axle/PhysicsScene.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>

using namespace AxleSpline;

namespace {

const Axle::SDK::RuntimeServices* s_Services = nullptr;
std::uint64_t s_Owner = 0;

struct ComponentContext {
    std::string Name;
};

void DestroyContext(void* ptr) {
    delete static_cast<ComponentContext*>(ptr);
}

void* CreateSpline(void*, Axle::SDK::EntityRef, const char* text, std::uint32_t size) {
    auto comp = std::make_unique<Axle::Spline::SplineComponent>();
    if (size > 0) {
        if (!Axle::Spline::SplineSerializer::DeserializeFromString(std::string(text, size), *comp)) {
            return nullptr;
        }
    }
    return comp.release();
}

void DestroySpline(void*, void* data) {
    delete static_cast<Axle::Spline::SplineComponent*>(data);
}

bool SerializeSplineHook(void*, const void* data, Axle::SDK::TextSink sink, void* user) {
    if (!data) return false;
    const auto& comp = *static_cast<const Axle::Spline::SplineComponent*>(data);
    std::string text = Axle::Spline::SplineSerializer::SerializeToString(comp);
    return sink(user, text.data(), static_cast<std::uint32_t>(text.size()));
}

void* CloneSpline(void*, const void* pointer, Axle::SDK::EntityRef, const Axle::SDK::EntityRemap*, std::uint32_t) {
    if (!pointer) return nullptr;
    auto copy = std::make_unique<Axle::Spline::SplineComponent>(*static_cast<const Axle::Spline::SplineComponent*>(pointer));
    copy->RuntimeBodyID = Axle::BodyID{};
    copy->TrackVAO = nullptr;
    copy->Dirty = true;
    return copy.release();
}

void StartSpline(void*, void* data, Axle::SDK::EntityRef entity) {
    if (!data) return;
    auto& comp = *static_cast<Axle::Spline::SplineComponent*>(data);
    if (!s_Services) return;

    auto* physicsScene = static_cast<Axle::PhysicsScene*>(
        const_cast<void*>(s_Services->Address(s_Services->User, s_Owner, Axle::SDK::ServiceAddress::Physics, entity.Scene)));

    glm::mat4 wt(1.0f);
    Axle::SDK::Transform transform{};
    if (s_Services->GetTransform(s_Services->User, s_Owner, entity, &transform)) {
        glm::vec3 pos{transform.Position[0], transform.Position[1], transform.Position[2]};
        glm::quat rot{transform.Rotation[0], transform.Rotation[1], transform.Rotation[2], transform.Rotation[3]};
        glm::vec3 scale{transform.Scale[0], transform.Scale[1], transform.Scale[2]};
        wt = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), scale);
    }

    comp.Curve.RebuildCache();
    auto mesh = Axle::Spline::TrackGenerator::GenerateTrackMesh(comp.Curve, comp);
    comp.TrackVAO = Axle::Spline::TrackGenerator::BuildVAO(mesh);
    comp.VertexCount = static_cast<Axle::u32>(mesh.GetVertexCount());
    comp.TriangleCount = static_cast<Axle::u32>(mesh.GetTriangleCount());
    comp.Dirty = false;

    if (physicsScene && comp.GeneratePhysics) {
        if (!comp.RuntimeBodyID.IsInvalid()) {
            Axle::Spline::TrackGenerator::DestroyPhysicsCollider(comp.RuntimeBodyID, *physicsScene);
            comp.RuntimeBodyID = Axle::BodyID{};
        }
        comp.RuntimeBodyID = Axle::Spline::TrackGenerator::CreatePhysicsCollider(mesh, comp, *physicsScene, wt);
    }
}

void StopSpline(void*, void* data, Axle::SDK::EntityRef entity) {
    if (!data) return;
    auto& comp = *static_cast<Axle::Spline::SplineComponent*>(data);
    if (s_Services && !comp.RuntimeBodyID.IsInvalid()) {
        auto* physicsScene = static_cast<Axle::PhysicsScene*>(
            const_cast<void*>(s_Services->Address(s_Services->User, s_Owner, Axle::SDK::ServiceAddress::Physics, entity.Scene)));
        if (physicsScene) {
            Axle::Spline::TrackGenerator::DestroyPhysicsCollider(comp.RuntimeBodyID, *physicsScene);
        }
        comp.RuntimeBodyID = Axle::BodyID{};
    }
}

void UpdateSpline(void*, void* data, Axle::SDK::EntityRef entity, Axle::SDK::Phase phase, float) {
    if (!data || !s_Services) return;
    auto& comp = *static_cast<Axle::Spline::SplineComponent*>(data);
    if (phase == Axle::SDK::Phase::Late && comp.Curve.GetNodeCount() > 1) {
        // Optional debug rendering of spline segments
    }
}

} // namespace

#if defined(AXLE_PLUGIN_DESCRIPTOR) && defined(AXLE_PLUGIN_ID) && defined(AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_DESCRIPTOR(kSplinePluginId, "SplineRuntime")
#endif

AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    if (!host || host->struct_size != sizeof(*host) || host->abi_version != Axle::PluginRuntime::kPluginAbiVersion) {
        return false;
    }
    s_Services = static_cast<const Axle::SDK::RuntimeServices*>(
        host->get_service(host->user_data, Axle::SDK::RuntimeService, sizeof(Axle::SDK::RuntimeServices)));
    if (!s_Services || s_Services != AxleRuntimeHost_GetV1()) {
        return false;
    }
    s_Owner = host->owner_id;

    auto ctx = std::make_unique<ComponentContext>(ComponentContext{"SplineContext"});
    Axle::SDK::ComponentType splineType{};
    splineType.StructSize = sizeof(splineType);
    splineType.TypeId = kSplineTypeId;
    splineType.Schema = kSplineSchemaVersion;
    splineType.Required = false;
    splineType.ContainsEntityReferences = false;
    splineType.User = ctx.get();
    splineType.DestroyUser = DestroyContext;
    splineType.Create = CreateSpline;
    splineType.Destroy = DestroySpline;
    splineType.Serialize = SerializeSplineHook;
    splineType.Clone = CloneSpline;
    splineType.Start = StartSpline;
    splineType.Stop = StopSpline;
    splineType.Update = UpdateSpline;

    if (!s_Services->RegisterComponent(s_Services->User, s_Owner, &splineType)) {
        return false;
    }
    ctx.release(); // Ownership transferred to host
    return true;
}

AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept {
    s_Services = nullptr;
    s_Owner = 0;
}
