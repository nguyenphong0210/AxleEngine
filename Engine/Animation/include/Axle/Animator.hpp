#pragma once
#include <Axle/Skeleton.hpp>
#include <Axle/AnimationClip.hpp>
#include <Axle/AnimationPose.hpp>
#include <Axle/AnimationStateMachine.hpp>
#include <Axle/SkinningPipeline.hpp>
#include <memory>
#include <vector>

namespace Axle {

    class Animator {
    public:
        Animator() = default;
        explicit Animator(std::shared_ptr<Skeleton> skeleton);

        void SetSkeleton(std::shared_ptr<Skeleton> skeleton);
        const std::shared_ptr<Skeleton>& GetSkeleton() const { return m_Skeleton; }

        void SetStateMachine(std::shared_ptr<AnimationStateMachine> stateMachine);
        const std::shared_ptr<AnimationStateMachine>& GetStateMachine() const { return m_StateMachine; }
        std::shared_ptr<AnimationStateMachine>& GetStateMachine() { return m_StateMachine; }

        // Convenience parameter setters passing through to state machine
        void SetFloat(const std::string& name, float value);
        void SetBool(const std::string& name, bool value);
        void SetTrigger(const std::string& name);

        // Update animation logic, evaluate state transitions and bone palettes
        void Update(float dt);

        // Upload computed skinning palette to active shader via SkinningPipeline
        void UploadToPipeline(SkinningPipeline& pipeline, Shader& shader) const;

        const AnimationPose& GetCurrentPose() const;
        const std::vector<glm::mat4>& GetGlobalMatrices() const;
        const std::vector<glm::mat4>& GetSkinningMatrices() const;

    private:
        std::shared_ptr<Skeleton> m_Skeleton;
        std::shared_ptr<AnimationStateMachine> m_StateMachine;

        AnimationPose m_FallbackPose;
        std::vector<glm::mat4> m_FallbackGlobalMatrices;
        std::vector<glm::mat4> m_FallbackSkinningMatrices;
    };

}
