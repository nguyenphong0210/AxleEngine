#pragma once
#include <memory>
#include <cstdint>
namespace Axle {
    class Canvas2D;
    namespace RHI { class RHITexture; }
    class CanvasRenderer {
    public:
        CanvasRenderer();
        ~CanvasRenderer();
        uint32_t Submit(const Canvas2D& canvas, RHI::RHITexture* target);
    private:
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
    };
}
