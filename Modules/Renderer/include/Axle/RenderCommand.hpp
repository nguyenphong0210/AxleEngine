#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>

namespace Axle {

    // Function pointer type matching GLADloadproc
    using GLProcAddressFn = void* (*)(const char*);

    class RenderCommand {
    public:
        // Initialize graphics API loader with a platform-provided function pointer loader
        static bool Init(GLProcAddressFn procAddressLoader);

        static void SetViewport(u32 x, u32 y, u32 width, u32 height);
        static void SetClearColor(const glm::vec4& color);
        static void Clear();
    };

}