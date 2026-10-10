#pragma once
#include <Axle/Types.hpp>
#include <Axle/Shader.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/Buffer.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRendererSimple.h>

#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string_view>

namespace Axle {

    struct DebugLineVertex {
        glm::vec3 Position;
        glm::vec4 Color;
    };

    /// PhysicsDebugRenderer bridges Jolt Physics debug visualization with OpenGL line rendering
    class PhysicsDebugRenderer : public JPH::DebugRendererSimple {
    public:
        PhysicsDebugRenderer();
        ~PhysicsDebugRenderer() override = default;

        // JPH::DebugRendererSimple virtual overrides
        void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;
        void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight) override;

        // Frame batching cycle
        void Begin(const glm::vec3& cameraPos);
        void End(const glm::mat4& viewProjection);

        void Clear();

        u32 GetLineCount() const { return static_cast<u32>(m_LineVertices.size() / 2); }

    private:
        void Flush(const glm::mat4& viewProjection);

    private:
        std::vector<DebugLineVertex> m_LineVertices;
        u32 m_MaxVertices = 100000;

        std::shared_ptr<VertexArray> m_VertexArray;
        std::shared_ptr<VertexBuffer> m_VertexBuffer;
        std::unique_ptr<Shader> m_Shader;
    };

}
