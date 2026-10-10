#include <Axle/PhysicsDebugRenderer.hpp>
#include <Axle/RenderCommand.hpp>

namespace Axle {

    PhysicsDebugRenderer::PhysicsDebugRenderer()
        : JPH::DebugRendererSimple() {
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
    }

    void PhysicsDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) {
        const glm::vec4 col(
            static_cast<f32>(inColor.r) / 255.0f,
            static_cast<f32>(inColor.g) / 255.0f,
            static_cast<f32>(inColor.b) / 255.0f,
            static_cast<f32>(inColor.a) / 255.0f
        );

        m_LineVertices.push_back({ glm::vec3(inFrom.GetX(), inFrom.GetY(), inFrom.GetZ()), col });
        m_LineVertices.push_back({ glm::vec3(inTo.GetX(), inTo.GetY(), inTo.GetZ()), col });
    }

    void PhysicsDebugRenderer::DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight) {
        // Optional 3D debug text hook for future enhancements
        (void)inPosition;
        (void)inString;
        (void)inColor;
        (void)inHeight;
    }

    void PhysicsDebugRenderer::Begin(const glm::vec3& cameraPos) {
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
        if (m_LineVertices.empty()) {
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
