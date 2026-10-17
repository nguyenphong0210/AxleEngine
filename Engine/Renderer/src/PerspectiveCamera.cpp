#include <Axle/PerspectiveCamera.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

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

    void PerspectiveCamera::SetWorldTransform(const glm::mat4& transform) {
        m_Position = glm::vec3(transform[3]);
        m_ViewMatrix = glm::inverse(transform);
        m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;

        // Cleanly extract pitch and yaw from camera forward vector (-Z in local camera space)
        glm::vec3 fwd = -glm::vec3(transform[2]);
        float len = glm::length(fwd);
        if (len > 1e-4f) {
            fwd /= len;
            f32 pitch = glm::degrees(std::asin(std::clamp(fwd.y, -0.999f, 0.999f)));
            f32 yaw = glm::degrees(std::atan2(-fwd.x, -fwd.z));
            m_Rotation = glm::vec3(pitch, yaw, 0.0f);
        }
    }

    void PerspectiveCamera::SetViewMatrix(const glm::mat4& viewMatrix, const glm::vec3& position) {
        m_Position = position;
        m_ViewMatrix = viewMatrix;
        m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
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