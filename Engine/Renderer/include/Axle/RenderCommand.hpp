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
        static void BindDefaultFramebuffer();

        static void SetDepthMask(bool writeEnabled);
        static void SetDepthFuncLEqual();
        static void SetDepthFuncLess();
        static void SetCullFace(bool enabled);

        // Submit indexed draw call from a VertexArray
        static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray);

        // Submit instanced indexed draw call from a VertexArray
        static void DrawIndexedInstanced(const std::shared_ptr<VertexArray>& vertexArray, u32 instanceCount);

        // Submit non-indexed line draw call
        static void DrawLines(const std::shared_ptr<VertexArray>& vertexArray, u32 vertexCount);
        static void SetLineWidth(f32 width);

        // Blending controls
        static void SetBlendEnabled(bool enabled);
        static void SetBlendAdditive();
        static void SetBlendAlpha();
        static void SetBlendMultiply();
    };

}