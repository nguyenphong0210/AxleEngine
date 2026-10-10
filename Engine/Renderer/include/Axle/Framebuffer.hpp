#pragma once
#include <Axle/Types.hpp>
#include <memory>

namespace Axle {

    struct FramebufferSpecification {
        u32 Width = 0;
        u32 Height = 0;
        u32 Samples = 1;
        bool SwapChainTarget = false;
    };

    class Framebuffer {
    public:
        virtual ~Framebuffer() = default;

        virtual void Bind() = 0;
        virtual void Unbind() = 0;

        virtual void Resize(u32 width, u32 height) = 0;

        virtual u32 GetColorAttachmentRendererID() const = 0;
        virtual const FramebufferSpecification& GetSpecification() const = 0;

        static std::shared_ptr<Framebuffer> Create(const FramebufferSpecification& spec);
    };

}
