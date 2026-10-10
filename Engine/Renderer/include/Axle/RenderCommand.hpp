#pragma once
#include <Axle/Types.hpp>
#include <Axle/VertexArray.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace Axle {

    using GLProcAddressFn = void* (*)(const char*);

    class RenderCommand {
    public:
        static bool Init(GLProcAddressFn procAddressLoader);

        static void SetViewport(u32 x, u32 y, u32 width, u32 height);
        static void SetClearColor(const glm::vec4& color);
        static void Clear();

        // Submit indexed draw call from a VertexArray
        static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray);

        // Submit non-indexed line draw call
        static void DrawLines(const std::shared_ptr<VertexArray>& vertexArray, u32 vertexCount);
        static void SetLineWidth(f32 width);
    };

}