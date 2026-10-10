#include <Axle/PerspectiveCamera.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Axle {

    PerspectiveCamera::PerspectiveCamera(f32 fovDegrees, f32 aspectRatio, f32 nearClip, f32 farClip) {
        SetProjection(fovDegrees, aspectRatio, nearClip, farClip);
        RecalculateViewMatrix();
    }

    void PerspectiveCamera::SetProjection(f32 fovDegrees, f32 aspectRatio, f32 nearClip, f32 farClip) {
        m_ProjectionMatrix = glm::perspective(glm::radians(fovDegrees), aspectRatio, nearClip, farClip);
        m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
    }

    void PerspectiveCamera::SetPosition(const glm::vec3& position) {
        m_Position = position;
        RecalculateViewMatrix();
    }

    void PerspectiveCamera::SetRotation(const glm::vec3& rotation) {
        m_Rotation = rotation;
        RecalculateViewMatrix();
    }

    void PerspectiveCamera::RecalculateViewMatrix() {
        // Build camera world transform
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_Position)
            * glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f))
            * glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f))
            * glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        // View matrix is the inverse of camera's world transform
        m_ViewMatrix = glm::inverse(transform);
        m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
    }

}