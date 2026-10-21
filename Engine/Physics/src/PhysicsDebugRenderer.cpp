#include <Axle/PhysicsDebugRenderer.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Window.hpp>

namespace Axle {

    PhysicsDebugRenderer::PhysicsDebugRenderer()
        : JPH::DebugRendererSimple() {
        if (!Window::HasActiveContext()) {
            sInstance = this;
            return;
        }

        const std::string vertexSrc = R"(
            #version 450 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec4 a_Color;

            uniform mat4 u_ViewProjection;

            out vec4 v_Color;

            void main() {
                v_Color = a_Color;
                gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
            }
        )";

        const std::string fragmentSrc = R"(
            #version 450 core
            layout(location = 0) out vec4 o_Color;

            in vec4 v_Color;

            void main() {
                o_Color = v_Color;
            }
        )";

        m_Shader = std::make_unique<Shader>(vertexSrc, fragmentSrc);

        m_VertexBuffer = std::make_shared<VertexBuffer>(m_MaxVertices * static_cast<u32>(sizeof(DebugLineVertex)));
        m_VertexBuffer->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float4, "a_Color" }
        });

        m_VertexArray = std::make_shared<VertexArray>();
        m_VertexArray->AddVertexBuffer(m_VertexBuffer);

        sInstance = this;
    }

    PhysicsDebugRenderer::~PhysicsDebugRenderer() {
        if (sInstance == this) {
            sInstance = nullptr;
        }
    }

    void PhysicsDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) {
        const float fx = static_cast<float>(inFrom.GetX());
        const float fy = static_cast<float>(inFrom.GetY());
        const float fz = static_cast<float>(inFrom.GetZ());
        const float tx = static_cast<float>(inTo.GetX());
        const float ty = static_cast<float>(inTo.GetY());
        const float tz = static_cast<float>(inTo.GetZ());

        if (std::isnan(fx) || std::isnan(fy) || std::isnan(fz) ||
            std::isnan(tx) || std::isnan(ty) || std::isnan(tz) ||
            std::isinf(fx) || std::isinf(fy) || std::isinf(fz) ||
            std::isinf(tx) || std::isinf(ty) || std::isinf(tz)) {
            return;
        }

        const glm::vec4 col(
            static_cast<f32>(inColor.r) / 255.0f,
            static_cast<f32>(inColor.g) / 255.0f,
            static_cast<f32>(inColor.b) / 255.0f,
            static_cast<f32>(inColor.a) / 255.0f
        );

        m_LineVertices.push_back({ glm::vec3(fx, fy, fz), col });
        m_LineVertices.push_back({ glm::vec3(tx, ty, tz), col });
    }

    void PhysicsDebugRenderer::DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight) {
        // Optional 3D debug text hook for future enhancements
        (void)inPosition;
        (void)inString;
        (void)inColor;
        (void)inHeight;
    }

    void PhysicsDebugRenderer::Begin(const glm::vec3& cameraPos) {
        sInstance = this;
        SetCameraPos(JPH::RVec3(cameraPos.x, cameraPos.y, cameraPos.z));
        m_LineVertices.clear();
        NextFrame();
    }

    void PhysicsDebugRenderer::End(const glm::mat4& viewProjection) {
        if (m_LineVertices.empty()) {
            return;
        }

        Flush(viewProjection);
    }

    void PhysicsDebugRenderer::Clear() {
        m_LineVertices.clear();
    }

    void PhysicsDebugRenderer::Flush(const glm::mat4& viewProjection) {
        if (!Window::HasActiveContext() || !m_Shader || !m_VertexBuffer || !m_VertexArray || m_LineVertices.empty()) {
            return;
        }

        const u32 vertexCount = static_cast<u32>(m_LineVertices.size());
        if (vertexCount > m_MaxVertices) {
            m_MaxVertices = vertexCount * 2;
            m_VertexBuffer = std::make_shared<VertexBuffer>(m_MaxVertices * static_cast<u32>(sizeof(DebugLineVertex)));
            m_VertexBuffer->SetLayout({
                { ShaderDataType::Float3, "a_Position" },
                { ShaderDataType::Float4, "a_Color" }
            });
            m_VertexArray = std::make_shared<VertexArray>();
            m_VertexArray->AddVertexBuffer(m_VertexBuffer);
        }

        m_VertexBuffer->SetData(m_LineVertices.data(), vertexCount * static_cast<u32>(sizeof(DebugLineVertex)));

        m_Shader->Bind();
        m_Shader->SetMat4("u_ViewProjection", viewProjection);
        RenderCommand::DrawLines(m_VertexArray, vertexCount);
    }

}
