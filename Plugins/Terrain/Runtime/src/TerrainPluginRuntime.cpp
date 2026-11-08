#include <Axle/PluginSDK.hpp>
#include <AxleTerrain/TerrainPluginRuntime.hpp>
#include <AxleTerrain/TerrainComponent.hpp>
#include <AxleTerrain/TerrainSerializer.hpp>
#include <AxleTerrain/TerrainMesh.hpp>
#include <AxleTerrain/TerrainCollider.hpp>
#include <Axle/PhysicsScene.hpp>

#include <glm/glm.hpp>
#include <memory>
#include <string>

using namespace Axle::Terrain;

namespace {

const Axle::SDK::RuntimeServices* s_Services = nullptr;
std::uint64_t s_Owner = 0;

struct ComponentContext {
    std::string Name;
};

void DestroyContext(void* ptr) {
    delete static_cast<ComponentContext*>(ptr);
}

void* CreateTerrain(void*, Axle::SDK::EntityRef, const char* text, std::uint32_t size) {
    auto comp = std::make_unique<TerrainComponent>();
    if (size > 0 && text) {
        if (!TerrainSerializer::DeserializeFromString(*comp, std::string(text, size))) {
            return nullptr;
        }
    }
    return comp.release();
}

void DestroyTerrain(void*, void* data) {
    delete static_cast<TerrainComponent*>(data);
}

bool SerializeTerrainHook(void*, const void* data, Axle::SDK::TextSink sink, void* user) {
    if (!data || !sink) return false;
    const auto& comp = *static_cast<const TerrainComponent*>(data);
    std::string text = TerrainSerializer::SerializeToString(comp);
    return sink(user, text.data(), static_cast<std::uint32_t>(text.size()));
}

void* CloneTerrain(void*, const void* pointer, Axle::SDK::EntityRef, const Axle::SDK::EntityRemap*, std::uint32_t) {
    if (!pointer) return nullptr;
    auto copy = std::make_unique<TerrainComponent>(*static_cast<const TerrainComponent*>(pointer));
    copy->PhysicsBodyID = JPH::BodyID{};
    copy->CombinedVAO = nullptr;
    copy->Chunks.clear();
    return copy.release();
}

void StartTerrain(void*, void* data, Axle::SDK::EntityRef entity) {
    if (!data) return;
    auto& comp = *static_cast<TerrainComponent*>(data);
    if (!s_Services) return;

    auto* physicsScene = static_cast<Axle::PhysicsScene*>(
        const_cast<void*>(s_Services->Address(s_Services->User, s_Owner, Axle::SDK::ServiceAddress::Physics, entity.Scene)));

    if (comp.UseChunkedLOD) {
        TerrainMesh::InitializeChunks(comp.Data, comp.Chunks, comp.ChunkSize);
        TerrainMesh::BuildAllChunksParallel(comp.Data, comp.Chunks);
        TerrainMesh::UploadAllChunksGPU(comp.Chunks);
    } else {
        comp.CombinedVAO = TerrainMesh::BuildCombinedVAO(comp.Data);
    }

    if (physicsScene && comp.GeneratePhysics) {
        if (!comp.PhysicsBodyID.IsInvalid() && physicsScene->IsBodyValid(comp.PhysicsBodyID)) {
            comp.PhysicsBodyID = TerrainCollider::UpdateTerrainCollider(
                comp.PhysicsBodyID,
                comp.Data,
                *physicsScene,
                comp.Friction,
                comp.Restitution
            );
        } else {
            comp.PhysicsBodyID = TerrainCollider::CreateTerrainCollider(
                comp.Data,
                *physicsScene,
                comp.Friction,
                comp.Restitution
            );
        }
    }
}

void StopTerrain(void*, void* data, Axle::SDK::EntityRef entity) {
    if (!data) return;
    auto& comp = *static_cast<TerrainComponent*>(data);
    if (s_Services && !comp.PhysicsBodyID.IsInvalid()) {
        auto* physicsScene = static_cast<Axle::PhysicsScene*>(
            const_cast<void*>(s_Services->Address(s_Services->User, s_Owner, Axle::SDK::ServiceAddress::Physics, entity.Scene)));
        if (physicsScene && physicsScene->IsBodyValid(comp.PhysicsBodyID)) {
            TerrainCollider::DestroyTerrainCollider(comp.PhysicsBodyID, *physicsScene);
        }
        comp.PhysicsBodyID = JPH::BodyID{};
    }
    comp.CombinedVAO = nullptr;
    comp.Chunks.clear();
}

void UpdateTerrain(void*, void*, Axle::SDK::EntityRef, Axle::SDK::Phase, float) {
    // Terrain runtime tick (LOD updates can be evaluated here)
}

} // namespace

#if defined(AXLE_PLUGIN_DESCRIPTOR) && defined(AXLE_PLUGIN_ID) && defined(AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID, AXLE_PLUGIN_MODULE_NAME)
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

    auto ctx = std::make_unique<ComponentContext>(ComponentContext{"TerrainContext"});
    Axle::SDK::ComponentType terrainType{};
    terrainType.StructSize = sizeof(terrainType);
    terrainType.TypeId = kTerrainComponentTypeId;
    terrainType.Schema = 1;
    terrainType.Required = false;
    terrainType.ContainsEntityReferences = false;
    terrainType.User = ctx.get();
    terrainType.DestroyUser = DestroyContext;
    terrainType.Create = CreateTerrain;
    terrainType.Destroy = DestroyTerrain;
    terrainType.Serialize = SerializeTerrainHook;
    terrainType.Clone = CloneTerrain;
    terrainType.Start = StartTerrain;
    terrainType.Stop = StopTerrain;
    terrainType.Update = UpdateTerrain;

    if (!s_Services->RegisterComponent(s_Services->User, host->owner_id, &terrainType)) {
        return false;
    }
    ctx.release();
    return true;
}

AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept {
    s_Services = nullptr;
    s_Owner = 0;
}
