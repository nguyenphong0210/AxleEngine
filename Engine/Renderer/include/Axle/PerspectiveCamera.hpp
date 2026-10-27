#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>

namespace Axle {

    enum class CameraProjectionType {
        Perspective = 0,
        Orthographic = 1
    };

    class PerspectiveCamera {
    public:
        PerspectiveCamera(f32 fovDegrees, f32 aspectRatio, f32 nearClip = 0.1f, f32 farClip = 1000.0f);

        void SetProjection(f32 fovDegrees, f32 aspectRatio, f32 nearClip = 0.1f, f32 farClip = 1000.0f);
        void SetPerspective(f32 fovDegrees, f32 aspectRatio, f32 nearClip = 0.1f, f32 farClip = 1000.0f);
        void SetOrthographic(f32 size, f32 aspectRatio, f32 nearClip = -1000.0f, f32 farClip = 1000.0f);

        CameraProjectionType GetProjectionType() const { return m_ProjectionType; }
        bool IsOrthographic() const { return m_ProjectionType == CameraProjectionType::Orthographic; }
        
        void SetPosition(const glm::vec3& position);
        void SetRotation(const glm::vec3& rotation); // Euler angles (Pitch, Yaw, Roll) in degrees
        void SetWorldTransform(const glm::mat4& transform);
        void SetViewMatrix(const glm::mat4& viewMatrix, const glm::vec3& position);

        const glm::vec3& GetPosition() const { return m_Position; }
        const glm::vec3& GetRotation() const { return m_Rotation; }

        const glm::mat4& GetProjectionMatrix() const { return m_ProjectionMatrix; }
        const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
        const glm::mat4& GetViewProjectionMatrix() const { return m_ViewProjectionMatrix; }

        // Zero-to-One ([0, 1]) Depth Clip variants for Direct3D 12 and Vulkan
        const glm::mat4& GetProjectionMatrixZO() const { return m_ProjectionMatrixZO; }
        const glm::mat4& GetViewProjectionMatrixZO() const { return m_ViewProjectionMatrixZO; }

        float GetNearClip() const { return m_NearClip; }
        float GetFarClip() const { return m_FarClip; }
        float GetFOV() const { return m_FOV; }
        float GetAspectRatio() const { return m_AspectRatio; }
        float GetOrthographicSize() const { return m_OrthographicSize; }
        void SetOrthographicSize(float size);

    private:
        void RecalculateViewMatrix();

    private:
        CameraProjectionType m_ProjectionType = CameraProjectionType::Perspective;
        glm::mat4 m_ProjectionMatrix{ 1.0f };
        glm::mat4 m_ProjectionMatrixZO{ 1.0f };
        glm::mat4 m_ViewMatrix{ 1.0f };
        glm::mat4 m_ViewProjectionMatrix{ 1.0f };
        glm::mat4 m_ViewProjectionMatrixZO{ 1.0f };

        glm::vec3 m_Position{ 0.0f, 0.0f, 3.5f };
        glm::vec3 m_Rotation{ 0.0f, 0.0f, 0.0f }; // Degrees

        float m_FOV = 45.0f;
        float m_OrthographicSize = 10.0f;
        float m_AspectRatio = 1.7777778f;
        float m_NearClip = 0.1f;
        float m_FarClip = 1000.0f;
    };

}