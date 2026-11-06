#include <Axle/SceneCharacterBridge.hpp>
#include <Axle/SceneData.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/PhysicsScene.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    void SceneCharacterBridge::OnRuntimeStart(SceneData& scene, PhysicsScene* physicsScene) {
        auto& registry = scene.GetRegistry();

        // 1. Kinematic Character Controller Initialization
        if (physicsScene) {
            auto ccView = registry.view<CharacterControllerComponent, TransformComponent>();
            for (auto entityID : ccView) {
                auto& ccc = ccView.get<CharacterControllerComponent>(entityID);
                const auto& tc = ccView.get<TransformComponent>(entityID);
                ccc.InitRuntime(*physicsScene, tc.Translation, tc.Rotation);
            }
        }

        // 2. Character VFX Subsystem Initialization
        auto characterVFXView = registry.view<CharacterVFXComponent>();
        for (auto entityID : characterVFXView) {
            auto& cvfx = characterVFXView.get<CharacterVFXComponent>(entityID);
            cvfx.InitRuntime();
        }

        // 3. Particle Emitter AutoPlay Initialization
        auto particleView = registry.view<ParticleEmitterComponent>();
        for (auto entityID : particleView) {
            auto& pec = particleView.get<ParticleEmitterComponent>(entityID);
            pec.Initialize();
            pec.BindPhysicsScene(physicsScene);
        }
    }

    void SceneCharacterBridge::OnFixedUpdate(SceneData& scene, Timestep fixedDt, PhysicsScene* /*physicsScene*/) {
        auto& registry = scene.GetRegistry();

        // Kinematic Character Controller Pre-Physics Updates
        auto ccView = registry.view<CharacterControllerComponent, TransformComponent>();
        for (auto entityID : ccView) {
            auto& ccc = ccView.get<CharacterControllerComponent>(entityID);
            auto& tc = ccView.get<TransformComponent>(entityID);

            ccc.Update(fixedDt.GetSeconds());

            if (ccc.RuntimeController) {
                tc.Translation = ccc.RuntimeController->GetPosition();

                if (ccc.OrientRotationToMovement && glm::length(ccc.MoveDirection) > 0.001f) {
                    glm::quat targetRot = glm::angleAxis(std::atan2(ccc.MoveDirection.x, ccc.MoveDirection.z), glm::vec3(0.0f, 1.0f, 0.0f));
                    tc.Rotation = glm::slerp(tc.Rotation, targetRot, std::min(1.0f, fixedDt.GetSeconds() * ccc.TurnSmoothing));
                }
            }
        }
    }

    void SceneCharacterBridge::OnPostStepSync(SceneData& scene, PhysicsScene* /*physicsScene*/) {
        auto& registry = scene.GetRegistry();

        // Telemetry readback and synchronization with Animator and VFX
        auto ccView = registry.view<CharacterControllerComponent, TransformComponent>();
        for (auto entityID : ccView) {
            const auto& ccc = ccView.get<CharacterControllerComponent>(entityID);
            const auto& tc = ccView.get<TransformComponent>(entityID);

            // Sync with AnimatorComponent if present
            if (registry.all_of<AnimatorComponent>(entityID)) {
                auto& ac = registry.get<AnimatorComponent>(entityID);
                ac.SetFloat("Speed", ccc.CurrentSpeed);
                ac.SetBool("IsGrounded", ccc.IsGrounded);
                ac.SetFloat("VerticalVelocity", ccc.VerticalVelocity);
            }

            // Sync with CharacterVFXComponent if present
            if (registry.all_of<CharacterVFXComponent>(entityID)) {
                auto& cvfx = registry.get<CharacterVFXComponent>(entityID);
                if (cvfx.Enabled && cvfx.RuntimeVFX) {
                    cvfx.RuntimeVFX->Update(Timestep(0.016f), tc.Translation, tc.Rotation, ccc.Velocity, ccc.IsGrounded, ccc.CurrentSpeed, ccc.GroundNormal);
                }
            }
        }
    }

    void SceneCharacterBridge::UpdateAnimationAndVFX(SceneData& scene, Timestep fixedDt, PhysicsScene* physicsScene) {
        auto& registry = scene.GetRegistry();

        // 1. Standalone or Character-Attached Animator Component Updates
        auto animView = registry.view<AnimatorComponent>();
        for (auto entityID : animView) {
            auto& ac = animView.get<AnimatorComponent>(entityID);
            ac.Update(fixedDt.GetSeconds());
        }

        // 2. Particle Emitter System Updates
        auto particleUpdateView = registry.view<ParticleEmitterComponent, TransformComponent>();
        for (auto entityID : particleUpdateView) {
            auto& pec = particleUpdateView.get<ParticleEmitterComponent>(entityID);
            if (!pec.Active) continue;

            glm::mat4 worldMat = scene.GetWorldTransform(entityID);
            glm::vec3 worldPos = glm::vec3(worldMat[3]);
            glm::vec3 worldScale(glm::length(glm::vec3(worldMat[0])), glm::length(glm::vec3(worldMat[1])), glm::length(glm::vec3(worldMat[2])));
            auto axes = glm::mat3(worldMat);
            for (int a = 0; a < 3; ++a) {
                axes[a] /= std::max(worldScale[a], 0.0001f);
            }
            glm::quat worldRot = glm::quat_cast(axes);
            pec.BindPhysicsScene(physicsScene);
            pec.Update(fixedDt.GetSeconds(), worldPos, worldRot, worldScale);
        }
    }

    void SceneCharacterBridge::OnUpdate(SceneData& scene, Timestep ts, PhysicsScene* physicsScene) {
        OnFixedUpdate(scene, ts, physicsScene);
        OnPostStepSync(scene, physicsScene);
        UpdateAnimationAndVFX(scene, ts, physicsScene);
    }

    void SceneCharacterBridge::OnRuntimeStop(SceneData& scene) {
        auto& registry = scene.GetRegistry();

        // Stop all Character Controller instances
        auto ccStopView = registry.view<CharacterControllerComponent>();
        for (auto entityID : ccStopView) {
            auto& ccc = ccStopView.get<CharacterControllerComponent>(entityID);
            ccc.StopRuntime();
        }

        // Stop all Character VFX instances
        auto cvfxStopView = registry.view<CharacterVFXComponent>();
        for (auto entityID : cvfxStopView) {
            auto& cvfx = cvfxStopView.get<CharacterVFXComponent>(entityID);
            cvfx.StopRuntime();
        }

        // Reset particle emitters
        auto pecStopView = registry.view<ParticleEmitterComponent>();
        for (auto entityID : pecStopView) {
            auto& pec = pecStopView.get<ParticleEmitterComponent>(entityID);
            pec.Reset();
            pec.BindPhysicsScene(nullptr);
        }
    }

    void SceneCharacterBridge::TeardownEntityCharacter(SceneData& scene, entt::entity entityID) {
        auto& registry = scene.GetRegistry();
        if (!registry.valid(entityID)) return;

        if (registry.all_of<CharacterControllerComponent>(entityID)) {
            auto& ccc = registry.get<CharacterControllerComponent>(entityID);
            ccc.StopRuntime();
        }

        if (registry.all_of<CharacterVFXComponent>(entityID)) {
            auto& cvfx = registry.get<CharacterVFXComponent>(entityID);
            cvfx.StopRuntime();
        }

        if (registry.all_of<ParticleEmitterComponent>(entityID)) {
            auto& pec = registry.get<ParticleEmitterComponent>(entityID);
            pec.Reset();
            pec.BindPhysicsScene(nullptr);
        }
    }

}
