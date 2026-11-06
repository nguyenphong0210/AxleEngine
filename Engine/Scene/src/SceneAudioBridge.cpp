#include <Axle/SceneAudioBridge.hpp>
#include <Axle/SceneData.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/AudioListener.hpp>
#include <Axle/PhysicsScene.hpp>

namespace Axle {

    void SceneAudioBridge::OnRuntimeStart(SceneData& scene) {
        auto& registry = scene.GetRegistry();
        auto audioSourceView = registry.view<AudioSourceComponent>();
        for (auto entityID : audioSourceView) {
            auto& asc = audioSourceView.get<AudioSourceComponent>(entityID);
            if (!asc.AssetPath.empty()) {
                if (asc.PlayOnAwake) {
                    asc.Play();
                }
            }
        }
    }

    void SceneAudioBridge::OnUpdate(SceneData& scene, Timestep /*ts*/, PhysicsScene* physicsScene) {
        auto& registry = scene.GetRegistry();

        // 1. Locate primary audio listener
        Entity listenerEntity{};
        auto listenerView = registry.view<AudioListenerComponent>();
        for (auto entityID : listenerView) {
            const auto& alc = listenerView.get<AudioListenerComponent>(entityID);
            if (alc.Active) {
                listenerEntity = Entity{ entityID, &scene };
                break;
            }
        }

        // Fallback: Primary Camera
        if (!listenerEntity) {
            auto camView = registry.view<CameraComponent>();
            for (auto entityID : camView) {
                const auto& cam = camView.get<CameraComponent>(entityID);
                if (cam.Primary) {
                    listenerEntity = Entity{ entityID, &scene };
                    break;
                }
            }
            if (!listenerEntity) {
                for (auto entityID : camView) {
                    listenerEntity = Entity{ entityID, &scene };
                    break;
                }
            }
        }

        // Synchronize listener pose
        if (listenerEntity && listenerEntity.HasComponent<TransformComponent>()) {
            glm::mat4 listenerMat = scene.GetWorldTransform(listenerEntity);
            glm::vec3 listenerPos = glm::vec3(listenerMat[3]);
            glm::vec3 listenerForward = -glm::normalize(glm::vec3(listenerMat[2]));
            glm::vec3 listenerUp = glm::normalize(glm::vec3(listenerMat[1]));
            glm::vec3 listenerVel(0.0f);

            if (physicsScene && listenerEntity.HasComponent<RigidBody3D>()) {
                const auto& rb = listenerEntity.GetComponent<RigidBody3D>();
                if (!rb.RuntimeBodyID.IsInvalid() && physicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                    listenerVel = physicsScene->GetLinearVelocity(rb.RuntimeBodyID);
                }
            }

            AudioListener::SetPosition(listenerPos);
            AudioListener::SetOrientation(listenerForward, listenerUp);
            AudioListener::SetVelocity(listenerVel);
        }

        // Synchronize 3D spatial audio sources
        auto audioSyncView = registry.view<AudioSourceComponent, TransformComponent>();
        for (auto entityID : audioSyncView) {
            auto& asc = audioSyncView.get<AudioSourceComponent>(entityID);
            if (!asc.Source || !asc.Spatialized) continue;

            glm::mat4 worldMat = scene.GetWorldTransform(entityID);
            glm::vec3 pos = glm::vec3(worldMat[3]);
            glm::vec3 forward = -glm::normalize(glm::vec3(worldMat[2]));
            asc.Source->SetPosition(pos);
            asc.Source->SetDirection(forward);
        }
    }

    void SceneAudioBridge::OnRuntimeStop(SceneData& scene) {
        auto& registry = scene.GetRegistry();
        auto audioSourceView = registry.view<AudioSourceComponent>();
        for (auto entityID : audioSourceView) {
            auto& asc = audioSourceView.get<AudioSourceComponent>(entityID);
            asc.Stop();
            asc.Source = nullptr;
        }
    }

    void SceneAudioBridge::TeardownEntityAudio(SceneData& scene, entt::entity entityID) {
        auto& registry = scene.GetRegistry();
        if (!registry.valid(entityID)) return;

        if (registry.all_of<AudioSourceComponent>(entityID)) {
            auto& asc = registry.get<AudioSourceComponent>(entityID);
            asc.Stop();
            asc.Source = nullptr;
        }
    }

}
