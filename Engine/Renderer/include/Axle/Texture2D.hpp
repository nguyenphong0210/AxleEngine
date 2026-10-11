#pragma once
#include <Axle/Types.hpp>
#include <string>
#include <memory>

namespace Axle {

    enum class ImageFormat {
        None = 0,
        R8,
        RG8,
        RGB8,
        RGBA8,
        RGBA32F
    };

    enum class TextureFilter {
        Linear = 0,
        Nearest
    };

    enum class TextureWrap {
        Repeat = 0,
        ClampToEdge,
        MirroredRepeat
    };

    struct TextureSpecification {
        u32 Width = 1;
        u32 Height = 1;
        ImageFormat Format = ImageFormat::RGBA8;
        TextureFilter MinFilter = TextureFilter::Linear;
        TextureFilter MagFilter = TextureFilter::Linear;
        TextureWrap WrapS = TextureWrap::Repeat;
        TextureWrap WrapT = TextureWrap::Repeat;
        bool GenerateMipmaps = true;
        bool FlipVertically = true;
        bool sRGB = false;
    };

    class Texture2D {
    public:
        Texture2D(const std::string& filepath, const TextureSpecification& spec = {});
        Texture2D(const TextureSpecification& spec, const void* data = nullptr);
        ~Texture2D();

        Texture2D(const Texture2D&) = delete;
        Texture2D& operator=(const Texture2D&) = delete;

        void SetData(const void* data, u32 size);
        void Bind(u32 slot = 0) const;
        void Unbind(u32 slot = 0) const;

        u32 GetRendererID() const { return m_RendererID; }
        u32 GetWidth() const { return m_Width; }
        u32 GetHeight() const { return m_Height; }
        const std::string& GetPath() const { return m_FilePath; }
        bool IsLoaded() const { return m_IsLoaded; }
        const TextureSpecification& GetSpecification() const { return m_Specification; }

        static std::shared_ptr<Texture2D> Create(const std::string& filepath, const TextureSpecification& spec = {});
        static std::shared_ptr<Texture2D> Create(const TextureSpecification& spec, const void* data = nullptr);

        // Pre-baked global 1x1 fallback textures for missing/unassigned material maps
        static std::shared_ptr<Texture2D> GetWhiteTexture();            // (255, 255, 255, 255) for Albedo / AO
        static std::shared_ptr<Texture2D> GetFlatNormalTexture();       // (128, 128, 255, 255) for Normal map [0, 0, 1]
        static std::shared_ptr<Texture2D> GetBlackTexture();            // (0, 0, 0, 255) for Metallic
        static std::shared_ptr<Texture2D> GetDefaultRoughnessTexture();  // (128, 128, 128, 255) for 0.5 Roughness

    private:
        void Invalidate(const void* initialData);

    private:
        std::string m_FilePath;
        TextureSpecification m_Specification;
        u32 m_RendererID = 0;
        u32 m_Width = 0;
        u32 m_Height = 0;
        bool m_IsLoaded = false;
        u32 m_InternalFormat = 0; // GLenum
        u32 m_DataFormat = 0;     // GLenum
    };

}
