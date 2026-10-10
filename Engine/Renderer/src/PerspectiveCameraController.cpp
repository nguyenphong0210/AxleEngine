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

    void PerspectiveCameraController::OnUpdate(Timestep ts) {
        // 1. Mouse Look (Chuyển std::pair<Axle::f32, Axle::f32> sang glm::vec2)
        const auto [mouseX, mouseY] = Input::GetMousePosition();
        const glm::vec2 currentMousePos{ mouseX, mouseY };

        if (m_FirstMouseUpdate) {
            m_LastMousePosition = currentMousePos;
            m_FirstMouseUpdate = false;
        }

        if (Input::IsMouseButtonPressed(MouseCode::ButtonRight)) {
            const glm::vec2 delta = currentMousePos - m_LastMousePosition;

            m_CameraRotation.y -= delta.x * m_RotationSpeed; // Yaw (Xoay quanh trục Y thế giới)
            m_CameraRotation.x -= delta.y * m_RotationSpeed; // Pitch (Xoay quanh trục X cục bộ)

            // Khóa góc pitch tránh lật ngược camera
            m_CameraRotation.x = std::clamp(m_CameraRotation.x, -89.0f, 89.0f);
        }

        m_LastMousePosition = currentMousePos;

        // 2. Tính toán vector hướng nhìn từ góc Euler (Pitch, Yaw)
        const f32 yawRad = glm::radians(m_CameraRotation.y);
        const f32 pitchRad = glm::radians(m_CameraRotation.x);

        glm::vec3 forward;
        forward.x = -std::sin(yawRad) * std::cos(pitchRad);
        forward.y = std::sin(pitchRad);
        forward.z = -std::cos(yawRad) * std::cos(pitchRad);
        forward = glm::normalize(forward);

        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

        // 3. Di chuyển phím theo hướng nhìn
        const f32 speed = m_TranslationSpeed * ts.GetSeconds();

        if (Input::IsKeyPressed(KeyCode::W)) {
            m_CameraPosition += forward * speed;
        }
        if (Input::IsKeyPressed(KeyCode::S)) {
            m_CameraPosition -= forward * speed;
        }
        if (Input::IsKeyPressed(KeyCode::A)) {
            m_CameraPosition -= right * speed;
        }
        if (Input::IsKeyPressed(KeyCode::D)) {
            m_CameraPosition += right * speed;
        }
        if (Input::IsKeyPressed(KeyCode::E) || Input::IsKeyPressed(KeyCode::Space)) {
            m_CameraPosition += up * speed;
        }
        if (Input::IsKeyPressed(KeyCode::Q) || Input::IsKeyPressed(KeyCode::LeftShift)) {
            m_CameraPosition -= up * speed;
        }

        // Cập nhật ma trận camera
        m_Camera.SetPosition(m_CameraPosition);
        m_Camera.SetRotation(m_CameraRotation);
    }

    void PerspectiveCameraController::OnResize(u32 width, u32 height) {
        if (width > 0 && height > 0) {
            m_AspectRatio = static_cast<f32>(width) / static_cast<f32>(height);
            m_Camera.SetProjection(m_FOV, m_AspectRatio);
        }
    }

}