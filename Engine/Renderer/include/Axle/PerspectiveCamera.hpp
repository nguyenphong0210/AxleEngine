#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>

namespace Axle {

    class PerspectiveCamera {
    public:
        PerspectiveCamera(f32 fovDegrees, f32 aspectRatio, f32 nearClip = 0.1f, f32 farClip = 1000.0f);

        void SetProjection(f32 fovDegrees, f32 aspectRatio, f32 nearClip = 0.1f, f32 farClip = 1000.0f);
        
        void SetPosition(const glm::vec3& position);
        void SetRotation(const glm::vec3& rotation); // Euler angles (Pitch, Yaw, Roll) in degrees
        void SetWorldTransform(const glm::mat4& transform);
        void SetViewMatrix(const glm::mat4& viewMatrix, const glm::vec3& position);

        const glm::vec3& GetPosition() const { return m_Position; }
        const glm::vec3& GetRotation() const { return m_Rotation; }

        const glm::mat4& GetProjectionMatrix() const { return m_ProjectionMatrix; }
        const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
        const glm::mat4& GetViewProjectionMatrix() const { return m_ViewProjectionMatrix; }

        float GetNearClip() const { return m_NearClip; }
        float GetFarClip() const { return m_FarClip; }

    private:
        void RecalculateViewMatrix();

    private:
        glm::mat4 m_ProjectionMatrix{ 1.0f };
        glm::mat4 m_ViewMatrix{ 1.0f };
        glm::mat4 m_ViewProjectionMatrix{ 1.0f };

        glm::vec3 m_Position{ 0.0f, 0.0f, 3.5f };
        glm::vec3 m_Rotation{ 0.0f, 0.0f, 0.0f }; // Degrees

        float m_NearClip = 0.1f;
        float m_FarClip = 1000.0f;
    };

}