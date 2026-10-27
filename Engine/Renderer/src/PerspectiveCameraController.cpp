#include <Axle/PerspectiveCameraController.hpp>
#include <Axle/Input.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    PerspectiveCameraController::PerspectiveCameraController(f32 fovDegrees, f32 aspectRatio, f32 nearClip, f32 farClip)
        : m_AspectRatio(aspectRatio), m_FOV(fovDegrees), m_Camera(fovDegrees, aspectRatio, nearClip, farClip) {
        m_Camera.SetPosition(m_CameraPosition);
        m_Camera.SetRotation(m_CameraRotation);
    }

    void PerspectiveCameraController::Set2DMode(bool enabled) {
        if (m_Is2DMode == enabled) return;
        m_Is2DMode = enabled;

        if (m_Is2DMode) {
            // 1. Cache current 3D camera pose before entering 2D mode
            m_Saved3DPosition = m_CameraPosition;
            m_Saved3DRotation = m_CameraRotation;

            // 2. Lock view orientation to face active XY canvas plane (Pitch = 0, Yaw = 0, Roll = 0)
            m_CameraRotation = glm::vec3(0.0f, 0.0f, 0.0f);
            m_CameraPosition = glm::vec3(0.0f, 0.0f, 10.0f);

            // 3. Switch to Orthographic projection
            m_Camera.SetOrthographic(m_OrthographicSize, m_AspectRatio, -1000.0f, 1000.0f);
            m_Camera.SetPosition(m_CameraPosition);
            m_Camera.SetRotation(m_CameraRotation);
        } else {
            // 1. Restore 3D Perspective projection
            m_Camera.SetPerspective(m_FOV, m_AspectRatio, 0.1f, 1000.0f);

            // 2. Restore saved 3D camera pose
            m_CameraPosition = m_Saved3DPosition;
            m_CameraRotation = m_Saved3DRotation;
            m_Camera.SetPosition(m_CameraPosition);
            m_Camera.SetRotation(m_CameraRotation);
        }
    }

    void PerspectiveCameraController::SetOrthographicSize(f32 size) {
        m_OrthographicSize = std::max(1.0f, size);
        if (m_Is2DMode) {
            m_Camera.SetOrthographic(m_OrthographicSize, m_AspectRatio, -1000.0f, 1000.0f);
        }
    }

    void PerspectiveCameraController::OnUpdate(Timestep ts) {
        // 1. Mouse Look / Pan
        const auto [mouseX, mouseY] = Input::GetMousePosition();
        const glm::vec2 currentMousePos{ mouseX, mouseY };

        if (m_FirstMouseUpdate) {
            m_LastMousePosition = currentMousePos;
            m_FirstMouseUpdate = false;
        }

        const bool isInteracting = Input::IsMouseButtonPressed(MouseCode::ButtonRight) || 
                                   Input::IsMouseButtonPressed(MouseCode::ButtonMiddle) || 
                                   (Input::GetCursorMode() == CursorMode::Locked);
        if (isInteracting) {
            if (!m_WasInteracting) {
                m_LastMousePosition = currentMousePos;
                m_WasInteracting = true;
            }
            const glm::vec2 delta = currentMousePos - m_LastMousePosition;

            if (m_Is2DMode) {
                // In 2D mode, view orientation is strictly locked facing the canvas plane (0, 0, 0)
                m_CameraRotation = glm::vec3(0.0f, 0.0f, 0.0f);

                // Mouse drag translates camera across X and Y (pan)
                f32 panSpeed = (m_OrthographicSize / 720.0f) * 1.5f;
                m_CameraPosition.x -= delta.x * panSpeed;
                m_CameraPosition.y += delta.y * panSpeed;
            } else {
                m_CameraRotation.y -= delta.x * m_RotationSpeed; // Yaw (Rotate around world Y-axis)
                m_CameraRotation.x -= delta.y * m_RotationSpeed; // Pitch (Rotate around local X-axis)

                // Clamp pitch angle to avoid camera flipping
                m_CameraRotation.x = std::clamp(m_CameraRotation.x, -89.0f, 89.0f);
            }
        } else {
            m_WasInteracting = false;
        }

        m_LastMousePosition = currentMousePos;

        if (m_Is2DMode) {
            // Strictly enforce orientation lock facing canvas
            m_CameraRotation = glm::vec3(0.0f, 0.0f, 0.0f);

            // 2D Translation & Zoom
            if (isInteracting) {
                const f32 speedMultiplier = Input::IsKeyPressed(KeyCode::LeftShift) ? 2.5f : 1.0f;
                const f32 speed = m_TranslationSpeed * speedMultiplier * ts.GetSeconds();

                if (Input::IsKeyPressed(KeyCode::A)) m_CameraPosition.x -= speed;
                if (Input::IsKeyPressed(KeyCode::D)) m_CameraPosition.x += speed;
                if (Input::IsKeyPressed(KeyCode::W)) m_CameraPosition.y += speed;
                if (Input::IsKeyPressed(KeyCode::S)) m_CameraPosition.y -= speed;

                if (Input::IsKeyPressed(KeyCode::Q)) {
                    SetOrthographicSize(m_OrthographicSize * (1.0f + 1.5f * ts.GetSeconds()));
                }
                if (Input::IsKeyPressed(KeyCode::E)) {
                    SetOrthographicSize(m_OrthographicSize * (1.0f - 1.5f * ts.GetSeconds()));
                }
            }
        } else {
            // 2. 3D Forward direction vector from Euler angles (Pitch, Yaw)
            const f32 yawRad = glm::radians(m_CameraRotation.y);
            const f32 pitchRad = glm::radians(m_CameraRotation.x);

            glm::vec3 forward;
            forward.x = -std::sin(yawRad) * std::cos(pitchRad);
            forward.y = std::sin(pitchRad);
            forward.z = -std::cos(yawRad) * std::cos(pitchRad);
            forward = glm::normalize(forward);

            const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

            // 3. Move along view axes with optional sprint boost (only active when holding RMB)
            if (isInteracting) {
                const f32 speedMultiplier = Input::IsKeyPressed(KeyCode::LeftShift) ? 2.5f : 1.0f;
                const f32 speed = m_TranslationSpeed * speedMultiplier * ts.GetSeconds();

                if (Input::IsKeyPressed(KeyCode::W)) m_CameraPosition += forward * speed;
                if (Input::IsKeyPressed(KeyCode::S)) m_CameraPosition -= forward * speed;
                if (Input::IsKeyPressed(KeyCode::A)) m_CameraPosition -= right * speed;
                if (Input::IsKeyPressed(KeyCode::D)) m_CameraPosition += right * speed;
                if (Input::IsKeyPressed(KeyCode::E)) m_CameraPosition += up * speed;
                if (Input::IsKeyPressed(KeyCode::Q)) m_CameraPosition -= up * speed;
            }
        }

        // Update camera position and view matrix
        m_Camera.SetPosition(m_CameraPosition);
        m_Camera.SetRotation(m_CameraRotation);
    }

    void PerspectiveCameraController::OnResize(u32 width, u32 height) {
        if (width > 0 && height > 0) {
            m_AspectRatio = static_cast<f32>(width) / static_cast<f32>(height);
            if (m_Is2DMode) {
                m_Camera.SetOrthographic(m_OrthographicSize, m_AspectRatio, -1000.0f, 1000.0f);
            } else {
                m_Camera.SetPerspective(m_FOV, m_AspectRatio, m_Camera.GetNearClip(), m_Camera.GetFarClip());
            }
        }
    }

}