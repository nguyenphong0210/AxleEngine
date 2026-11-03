#include <Axle/Animator.hpp>
#include <Axle/SkinningPipeline.hpp>
#include <Axle/Shader.hpp>

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
        if (m_StateMachine && m_Skeleton) { m_StateMachine->SetResourceRig(m_Skeleton); m_StateMachine->Update(0, *m_Skeleton); }
    }

    void Animator::SetFloat(const std::string& name, float value) {
        if (m_StateMachine) {
            m_StateMachine->SetFloat(name, value);
        }
    }

    float Animator::GetFloat(const std::string& name, float defaultVal) const {
        return m_StateMachine ? m_StateMachine->GetFloat(name, defaultVal) : defaultVal;
    }

    void Animator::SetInt(const std::string& name, int32_t value) {
        if (m_StateMachine) {
            m_StateMachine->SetInt(name, value);
        }
    }

    int32_t Animator::GetInt(const std::string& name, int32_t defaultVal) const {
        return m_StateMachine ? m_StateMachine->GetInt(name, defaultVal) : defaultVal;
    }

    void Animator::SetBool(const std::string& name, bool value) {
        if (m_StateMachine) {
            m_StateMachine->SetBool(name, value);
        }
    }

    bool Animator::GetBool(const std::string& name, bool defaultVal) const {
        return m_StateMachine ? m_StateMachine->GetBool(name, defaultVal) : defaultVal;
    }

    void Animator::SetTrigger(const std::string& name) {
        if (m_StateMachine) {
            m_StateMachine->SetTrigger(name);
        }
    }

    bool Animator::PeekTrigger(const std::string& name) const {
        return m_StateMachine ? m_StateMachine->PeekTrigger(name) : false;
    }

    bool Animator::CheckAndConsumeTrigger(const std::string& name) {
        return m_StateMachine ? m_StateMachine->CheckAndConsumeTrigger(name) : false;
    }

    void Animator::DeclareParameter(const ParameterDefinition& param) {
        if (m_StateMachine) {
            m_StateMachine->DeclareParameter(param);
        }
    }

    const ParameterDefinition* Animator::GetParameterDeclaration(const std::string& nameOrId) const {
        return m_StateMachine ? m_StateMachine->GetParameterDeclaration(nameOrId) : nullptr;
    }

    bool Animator::RenameParameter(const std::string& oldName, const std::string& newName) {
        return m_StateMachine ? m_StateMachine->RenameParameter(oldName, newName) : false;
    }

    const std::string& Animator::GetCurrentStateName() const {
        static const std::string s_Empty;
        return m_StateMachine ? m_StateMachine->GetCurrentStateName() : s_Empty;
    }

    bool Animator::IsTransitioning() const {
        return m_StateMachine ? m_StateMachine->IsTransitioning() : false;
    }

    void Animator::Reset() {
        if (m_StateMachine) {
            m_StateMachine->Reset();
        }
    }

    std::shared_ptr<Animator> Animator::Clone(std::string* diagnostic) const {
        auto clone = std::make_shared<Animator>(m_Skeleton ? std::make_shared<Skeleton>(*m_Skeleton) : nullptr);
        if (m_StateMachine) {
            auto stateMachine = m_StateMachine->Clone(diagnostic);
            if (!stateMachine) return nullptr;
            clone->SetStateMachine(std::move(stateMachine));
            if (clone->m_Skeleton) clone->Update(0);
        }
        if (diagnostic) diagnostic->clear();
        return clone;
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
