#include <Axle/SceneScriptBridge.hpp>
#include <Axle/SceneData.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>

namespace Axle {

    void SceneScriptBridge::OnRuntimeStart(SceneData& scene) {
        auto& registry = scene.GetRegistry();
        auto scriptView = registry.view<NativeScriptComponent>();
        for (auto entityID : scriptView) {
            auto& nsc = scriptView.get<NativeScriptComponent>(entityID);
            if (!nsc.Instance && nsc.InstantiateScript) {
                nsc.Instance = nsc.InstantiateScript();
                if (nsc.Instance) {
                    nsc.Instance->SetEntity(Entity{ entityID, &scene });
                    nsc.SyncFieldsToInstance();
                    nsc.Instance->OnCreate();
                }
            }
        }
    }

    void SceneScriptBridge::OnUpdate(SceneData& scene, Timestep ts) {
        auto& registry = scene.GetRegistry();
        auto scriptView = registry.view<NativeScriptComponent>();
        for (auto entityID : scriptView) {
            auto& nsc = scriptView.get<NativeScriptComponent>(entityID);
            if (nsc.Instance) {
                nsc.Instance->OnUpdate(ts);
            }
        }
    }

    void SceneScriptBridge::OnRuntimeStop(SceneData& scene) {
        auto& registry = scene.GetRegistry();
        auto view = registry.view<NativeScriptComponent>();
        for (auto entityID : view) {
            auto& nsc = view.get<NativeScriptComponent>(entityID);
            if (nsc.Instance && nsc.DestroyScript) {
                nsc.Instance->OnDestroy();
                nsc.DestroyScript(&nsc);
                nsc.Instance = nullptr;
            }
        }
    }

    void SceneScriptBridge::OnPreScriptReload(SceneData& scene) {
        auto& registry = scene.GetRegistry();
        auto view = registry.view<NativeScriptComponent>();
        for (auto entityID : view) {
            auto& nsc = view.get<NativeScriptComponent>(entityID);
            if (nsc.Instance) {
                nsc.SyncFieldsFromInstance();
                if (nsc.DestroyScript) {
                    nsc.DestroyScript(&nsc);
                } else {
                    delete nsc.Instance;
                }
                nsc.Instance = nullptr;
            }
            nsc.InstantiateScript = nullptr;
            nsc.DestroyScript = nullptr;
        }
    }

    void SceneScriptBridge::OnPostScriptReload(SceneData& scene, bool isRuntimeActive) {
        auto& registry = scene.GetRegistry();
        auto view = registry.view<NativeScriptComponent>();
        for (auto entityID : view) {
            auto& nsc = view.get<NativeScriptComponent>(entityID);
            if (!nsc.ScriptName.empty() && ScriptRegistry::Has(nsc.ScriptName)) {
                nsc.Bind(nsc.ScriptName);
                if (isRuntimeActive && nsc.InstantiateScript) {
                    nsc.Instance = nsc.InstantiateScript();
                    if (nsc.Instance) {
                        nsc.Instance->SetEntity(Entity{ entityID, &scene });
                        nsc.SyncFieldsToInstance();
                        nsc.Instance->OnCreate();
                    }
                }
            } else {
                nsc.InstantiateScript = nullptr;
                nsc.DestroyScript = nullptr;
                nsc.Instance = nullptr;
            }
        }
    }

    void SceneScriptBridge::TeardownEntityScript(SceneData& scene, entt::entity entityID) {
        auto& registry = scene.GetRegistry();
        if (!registry.valid(entityID)) return;

        if (registry.all_of<NativeScriptComponent>(entityID)) {
            auto& nsc = registry.get<NativeScriptComponent>(entityID);
            if (nsc.Instance && nsc.DestroyScript) {
                nsc.Instance->OnDestroy();
                nsc.DestroyScript(&nsc);
                nsc.Instance = nullptr;
            }
        }
    }

}
