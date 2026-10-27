#pragma once
#include <Axle/Types.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/Timestep.hpp>
#include <glm/glm.hpp>

namespace Axle {

    class PerspectiveCameraController {
    public:
        PerspectiveCameraController(f32 fovDegrees, f32 aspectRatio, f32 nearClip = 0.1f, f32 farClip = 1000.0f);

        void OnUpdate(Timestep ts);
        void OnResize(u32 width, u32 height);

        PerspectiveCamera& GetCamera() { return m_Camera; }
        const PerspectiveCamera& GetCamera() const { return m_Camera; }

        void SetMoveSpeed(f32 speed) { m_TranslationSpeed = speed; }
        void SetRotationSpeed(f32 speed) { m_RotationSpeed = speed; }

        void SetPosition(const glm::vec3& position) { m_CameraPosition = position; m_Camera.SetPosition(position); }
        void SetRotation(const glm::vec3& rotation) { m_CameraRotation = rotation; m_Camera.SetRotation(rotation); }

        // Viewport 2D UI Edit Mode (Phase 34 Step 7.3)
        void Set2DMode(bool enabled);
        bool Is2DMode() const { return m_Is2DMode; }

        void SetOrthographicSize(f32 size);
        f32 GetOrthographicSize() const { return m_OrthographicSize; }

        const glm::vec3& GetSaved3DPosition() const { return m_Saved3DPosition; }
        const glm::vec3& GetSaved3DRotation() const { return m_Saved3DRotation; }

    private:
        f32 m_AspectRatio;
        f32 m_FOV;
        PerspectiveCamera m_Camera;

        glm::vec3 m_CameraPosition{ 0.0f, 1.0f, 4.0f };
        glm::vec3 m_CameraRotation{ 0.0f, 0.0f, 0.0f }; // Pitch (X), Yaw (Y), Roll (Z) in degrees

        f32 m_TranslationSpeed = 8.0f;
        f32 m_RotationSpeed = 0.15f;

        glm::vec2 m_LastMousePosition{ 0.0f, 0.0f };
        bool m_FirstMouseUpdate = true;
        bool m_WasInteracting = false;

        // 2D View Mode state
        bool m_Is2DMode = false;
        f32 m_OrthographicSize = 10.0f;
        glm::vec3 m_Saved3DPosition{ 0.0f, 6.0f, 18.0f };
        glm::vec3 m_Saved3DRotation{ -12.0f, 0.0f, 0.0f };
    };

}