#pragma once
#include <Axle/DecodedImage.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <span>
namespace Axle {
    namespace RHI { class RHITexture; }
    struct alignas(16) CanvasVertex {
        glm::vec2 ScreenPos{ 0.0f };      // [0..7]   Screen-space coords (pixels)
        glm::vec2 UV{ 0.0f };             // [8..15]  Normalized quad UV [0..1] or atlas UV
        glm::vec4 Color{ 1.0f };          // [16..31] Primary color (fill / text / arc)
        glm::vec4 BorderColor{ 0.0f };    // [32..47] Border / track color
        glm::vec2 QuadSize{ 100.0f };     // [48..55] Quad size in pixels
        float     CornerRadius = 0.0f;    // [56..59] Corner radius (pixels) or arc thickness
        float     BorderWidth  = 0.0f;    // [60..63] Border width (pixels) or active angle (rad)
        glm::vec2 AngleRange{ 0.0f };     // [64..71] (StartAngleRad, EndAngleRad)
        uint32_t  TextureIndex  = 0;      // [72..75] Bindless Texture Descriptor index
        uint32_t  PrimitiveType = 0;      // [76..79] 0: SDF_Rect, 1: Sprite, 2: MSDF_Text, 3: SDF_RadialArc
    };
    static_assert(sizeof(CanvasVertex) == 80, "CanvasVertex size must be exactly 80 bytes");

    struct CanvasDrawData {
        glm::vec2 Extent;
        std::span<const CanvasVertex> Vertices;
        std::span<const uint32_t> Indices;
        std::span<const ImageSourceData> Images;
        std::span<const glm::vec4> Clips; // x, y, width, height in physical pixels.
    };
    class CanvasRenderer {
    public:
        CanvasRenderer();
        ~CanvasRenderer();
        uint32_t Submit(const CanvasDrawData& batch, RHI::RHITexture* target);
    private:
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
    };
}
