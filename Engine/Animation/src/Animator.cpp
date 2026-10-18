#include <Axle/Animator.hpp>

namespace Axle {

    Animator::Animator(std::shared_ptr<Skeleton> skeleton)
        : m_Skeleton(std::move(skeleton)) {
        if (m_Skeleton) {
            m_FallbackPose.InitFromSkeletonBindPose(*m_Skeleton);
            m_FallbackPose.ComputeGlobalMatrices(*m_Skeleton, m_FallbackGlobalMatrices);
            m_FallbackPose.ComputeSkinningMatrices(*m_Skeleton, m_FallbackSkinningMatrices);
        }
    }

    void Animator::SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
        m_Skeleton = std::move(skeleton);
        if (m_Skeleton) {
            m_FallbackPose.InitFromSkeletonBindPose(*m_Skeleton);
            m_FallbackPose.ComputeGlobalMatrices(*m_Skeleton, m_FallbackGlobalMatrices);
            m_FallbackPose.ComputeSkinningMatrices(*m_Skeleton, m_FallbackSkinningMatrices);
        }
    }

    void Animator::SetStateMachine(std::shared_ptr<AnimationStateMachine> stateMachine) {
        m_StateMachine = std::move(stateMachine);
    }

    void Animator::SetFloat(const std::string& name, float value) {
        if (m_StateMachine) {
            m_StateMachine->SetFloat(name, value);
        }
    }

    void Animator::SetBool(const std::string& name, bool value) {
        if (m_StateMachine) {
            m_StateMachine->SetBool(name, value);
        }
    }

    void Animator::SetTrigger(const std::string& name) {
        if (m_StateMachine) {
            m_StateMachine->SetTrigger(name);
        }
    }

    void Animator::Update(float dt) {
        if (m_StateMachine && m_Skeleton) {
            m_StateMachine->Update(dt, *m_Skeleton);
        }
    }

    void Animator::UploadToPipeline(SkinningPipeline& pipeline, Shader& shader) const {
        const auto& matrices = GetSkinningMatrices();
        pipeline.UploadBonePalette(shader, matrices);
    }

    const AnimationPose& Animator::GetCurrentPose() const {
        if (m_StateMachine) {
            return m_StateMachine->GetCurrentPose();
        }
        return m_FallbackPose;
    }

    const std::vector<glm::mat4>& Animator::GetGlobalMatrices() const {
        if (m_StateMachine) {
            return m_StateMachine->GetGlobalMatrices();
        }
        return m_FallbackGlobalMatrices;
    }

    const std::vector<glm::mat4>& Animator::GetSkinningMatrices() const {
        if (m_StateMachine) {
            return m_StateMachine->GetSkinningMatrices();
        }
        return m_FallbackSkinningMatrices;
    }

}
