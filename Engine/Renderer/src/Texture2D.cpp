#include <Axle/Texture2D.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Window.hpp>
#include <Axle/Log.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <Axle/DecodedImage.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <filesystem>

namespace Axle {

    namespace {
        GLenum ImageFormatToGLInternal(ImageFormat format, bool sRGB) {
            switch (format) {
                case ImageFormat::R8:      return GL_R8;
                case ImageFormat::RG8:     return GL_RG8;
                case ImageFormat::RGB8:    return sRGB ? GL_SRGB8 : GL_RGB8;
                case ImageFormat::RGBA8:   return sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
                case ImageFormat::RGBA32F: return GL_RGBA32F;
                default:                   return GL_RGBA8;
            }
        }

        GLenum ImageFormatToGLData(ImageFormat format) {
            switch (format) {
                case ImageFormat::R8:      return GL_RED;
                case ImageFormat::RG8:     return GL_RG;
                case ImageFormat::RGB8:    return GL_RGB;
                case ImageFormat::RGBA8:   return GL_RGBA;
                case ImageFormat::RGBA32F: return GL_RGBA;
                default:                   return GL_RGBA;
            }
        }

        GLenum TextureFilterToGL(TextureFilter filter, bool isMin, bool mipmaps) {
            if (isMin && mipmaps) {
                return filter == TextureFilter::Linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
            }
            return filter == TextureFilter::Linear ? GL_LINEAR : GL_NEAREST;
        }

        GLenum TextureWrapToGL(TextureWrap wrap) {
            switch (wrap) {
                case TextureWrap::Repeat:         return GL_REPEAT;
                case TextureWrap::ClampToEdge:    return GL_CLAMP_TO_EDGE;
                case TextureWrap::MirroredRepeat: return GL_MIRRORED_REPEAT;
                default:                          return GL_REPEAT;
            }
        }
    }

    Texture2D::Texture2D(const std::string& filepath, const TextureSpecification& spec)
        : m_FilePath(filepath), m_Specification(spec) {
        
        ImageDecodeOptions options; options.FlipVertically = spec.FlipVertically;
        options.SRGB = spec.sRGB; options.GenerateMips = spec.GenerateMipmaps;
        const auto source = ImageTextureCache::Resolve(filepath, options);
        if (!source.Image) {
            m_Specification.Width = m_Specification.Height = 1; m_Width = m_Height = 1;
            m_Specification.Format = ImageFormat::RGBA8;
            const u32 magenta = 0xFFFF00FF; Invalidate(&magenta); return;
        }
        const auto& mip = source.Image->Mips.front();
        m_Width = m_Specification.Width = mip.Width; m_Height = m_Specification.Height = mip.Height;
        m_Specification.Format = source.Image->HDR ? ImageFormat::RGBA32F : ImageFormat::RGBA8;
        Invalidate(mip.Pixels.data()); m_IsLoaded = GetRendererID() != 0;
    }

    Texture2D::Texture2D(const TextureSpecification& spec, const void* data)
        : m_Specification(spec), m_Width(spec.Width), m_Height(spec.Height) {
        Invalidate(data);
        m_IsLoaded = (data != nullptr);
    }

    Texture2D::Texture2D(u32 rendererID, u32 width, u32 height)
        : m_RendererID(rendererID), m_Width(width), m_Height(height), m_IsLoaded(true) {
        m_Specification.Width = width;
        m_Specification.Height = height;
        m_ContextGeneration = Window::GetContextGeneration();
    }

    u32 Texture2D::GetRendererID() const {
        return Window::HasActiveContext() && m_ContextGeneration == Window::GetContextGeneration() ? m_RendererID : 0;
    }

    Texture2D::~Texture2D() {
        if (GetRendererID()) {
            glDeleteTextures(1, &m_RendererID);
            m_RendererID = 0;
        }
    }

    void Texture2D::Invalidate(const void* initialData) {
        if (!Window::HasActiveContext()) {
            m_RendererID = 0;
            return;
        }

        if (GetRendererID()) {
            glDeleteTextures(1, &m_RendererID);
            m_RendererID = 0;
        }

        m_ContextGeneration = Window::GetContextGeneration();
        m_InternalFormat = ImageFormatToGLInternal(m_Specification.Format, m_Specification.sRGB);
        m_DataFormat = ImageFormatToGLData(m_Specification.Format);

        glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, TextureWrapToGL(m_Specification.WrapS));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, TextureWrapToGL(m_Specification.WrapT));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
            TextureFilterToGL(m_Specification.MinFilter, true, m_Specification.GenerateMipmaps));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
            TextureFilterToGL(m_Specification.MagFilter, false, false));

        GLenum dataType = (m_Specification.Format == ImageFormat::RGBA32F) ? GL_FLOAT : GL_UNSIGNED_BYTE;

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            m_InternalFormat,
            static_cast<GLsizei>(m_Width),
            static_cast<GLsizei>(m_Height),
            0,
            m_DataFormat,
            dataType,
            initialData
        );

        if (m_Specification.GenerateMipmaps && initialData) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void Texture2D::SetData(const void* data, u32 size) {
        if (!GetRendererID() || !data) return;

        glBindTexture(GL_TEXTURE_2D, m_RendererID);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        GLenum dataType = (m_Specification.Format == ImageFormat::RGBA32F) ? GL_FLOAT : GL_UNSIGNED_BYTE;
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0, 0,
            static_cast<GLsizei>(m_Width),
            static_cast<GLsizei>(m_Height),
            m_DataFormat,
            dataType,
            data
        );

        if (m_Specification.GenerateMipmaps) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        m_IsLoaded = true;
    }

    void Texture2D::Bind(u32 slot) const {
        if (!GetRendererID()) return;
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
    }

    void Texture2D::Unbind(u32 slot) const {
        if (!Window::HasActiveContext()) return;
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    std::shared_ptr<Texture2D> Texture2D::Create(const std::string& filepath, const TextureSpecification& spec) {
        ImageDecodeOptions options; options.FlipVertically = spec.FlipVertically; options.SRGB = spec.sRGB; options.GenerateMips = spec.GenerateMipmaps;
        auto source = ImageTextureCache::Resolve(filepath, options);
        auto texture = ImageTextureCache::GetGL(source, spec);
        return texture ? texture : std::make_shared<Texture2D>(filepath, spec);
    }

    std::shared_ptr<Texture2D> Texture2D::Create(const TextureSpecification& spec, const void* data) {
        return std::make_shared<Texture2D>(spec, data);
    }

    std::shared_ptr<Texture2D> Texture2D::CreateFromRendererID(u32 rendererID, u32 width, u32 height) {
        return std::make_shared<Texture2D>(rendererID, width, height);
    }

    std::shared_ptr<Texture2D> Texture2D::GetWhiteTexture() {
        static std::shared_ptr<Texture2D> s_WhiteTexture;
        if (!s_WhiteTexture || !s_WhiteTexture->IsLoaded()) {
            TextureSpecification spec;
            spec.Width = 1;
            spec.Height = 1;
            spec.Format = ImageFormat::RGBA8;
            spec.GenerateMipmaps = false;
            u32 whitePixel = 0xFFFFFFFF;
            s_WhiteTexture = std::make_shared<Texture2D>(spec, &whitePixel);
        }
        return s_WhiteTexture;
    }

    std::shared_ptr<Texture2D> Texture2D::GetFlatNormalTexture() {
        static std::shared_ptr<Texture2D> s_FlatNormalTexture;
        if (!s_FlatNormalTexture || !s_FlatNormalTexture->IsLoaded()) {
            TextureSpecification spec;
            spec.Width = 1;
            spec.Height = 1;
            spec.Format = ImageFormat::RGBA8;
            spec.GenerateMipmaps = false;
            // Normal (0, 0, 1) mapped to [0..255]: R=128, G=128, B=255, A=255
            u8 flatNormalPixel[4] = { 128, 128, 255, 255 };
            s_FlatNormalTexture = std::make_shared<Texture2D>(spec, flatNormalPixel);
        }
        return s_FlatNormalTexture;
    }

    std::shared_ptr<Texture2D> Texture2D::GetBlackTexture() {
        static std::shared_ptr<Texture2D> s_BlackTexture;
        if (!s_BlackTexture || !s_BlackTexture->IsLoaded()) {
            TextureSpecification spec;
            spec.Width = 1;
            spec.Height = 1;
            spec.Format = ImageFormat::RGBA8;
            spec.GenerateMipmaps = false;
            u8 blackPixel[4] = { 0, 0, 0, 255 };
            s_BlackTexture = std::make_shared<Texture2D>(spec, blackPixel);
        }
        return s_BlackTexture;
    }

    std::shared_ptr<Texture2D> Texture2D::GetDefaultRoughnessTexture() {
        static std::shared_ptr<Texture2D> s_RoughnessTexture;
        if (!s_RoughnessTexture || !s_RoughnessTexture->IsLoaded()) {
            TextureSpecification spec;
            spec.Width = 1;
            spec.Height = 1;
            spec.Format = ImageFormat::RGBA8;
            spec.GenerateMipmaps = false;
            // 0.5 Roughness: R=128, G=128, B=128, A=255
            u8 midGrayPixel[4] = { 128, 128, 128, 255 };
            s_RoughnessTexture = std::make_shared<Texture2D>(spec, midGrayPixel);
        }
        return s_RoughnessTexture;
    }

}
