#pragma once
#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <entt/entt.hpp>

namespace Axle {

    class SceneData;

    class SceneScriptBridge {
    public:
        // Instantiate and initialize native scripts for runtime execution
        static void OnRuntimeStart(SceneData& scene);

        // Execute frame updates for native scripts
        static void OnUpdate(SceneData& scene, Timestep ts);

        // Execute fixed simulation tick updates for native scripts (OnFixedUpdate)
        static void OnFixedUpdate(SceneData& scene, Timestep ts);

        // Execute late presentation updates for native scripts (OnLateUpdate)
        static void OnLateUpdate(SceneData& scene, Timestep ts);

        // Teardown and destroy all active native script instances
        static void OnRuntimeStop(SceneData& scene);

        // Hot-reload preservation and sync
        static void OnPreScriptReload(SceneData& scene);
        static void OnPostScriptReload(SceneData& scene, bool isRuntimeActive);

        // Clean up single entity script on destruction
        static void TeardownEntityScript(SceneData& scene, entt::entity entityID);
    };

}
