#pragma once
#include <Axle/Types.hpp>
#include <Axle/Texture2D.hpp>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <vector>

namespace Axle {

    struct CubemapSpecification {
        u32 Width = 512;
        u32 Height = 512;
        u32 MipLevels = 1;
        bool IsHDR = true; // GL_RGB16F or GL_RGBA16F
    };

    struct IBLMaps {
        std::shared_ptr<class Cubemap> EnvironmentMap; // Filtered radiance cubemap (512x512)
        std::shared_ptr<class Cubemap> IrradianceMap;  // Diffuse irradiance cubemap (32x32)
        std::shared_ptr<class Cubemap> PrefilterMap;   // Specular prefiltered cubemap (128x128, 5 mips)
        std::shared_ptr<Texture2D> BRDFLUT;            // BRDF integration 2D LUT (256x256)
    };

    class Cubemap {
    public:
        explicit Cubemap(const CubemapSpecification& spec = {});
        ~Cubemap();

        Cubemap(const Cubemap&) = delete;
        Cubemap& operator=(const Cubemap&) = delete;

        void Bind(u32 slot = 0) const;
        void Unbind(u32 slot = 0) const;

        u32 GetRendererID() const { return m_RendererID; }
        u32 GetWidth() const { return m_Spec.Width; }
        u32 GetHeight() const { return m_Spec.Height; }
        u32 GetMipLevels() const { return m_Spec.MipLevels; }
        const CubemapSpecification& GetSpecification() const { return m_Spec; }
        const std::string& GetPath() const { return m_FilePath; }

        // Factory methods
        static std::shared_ptr<Cubemap> Create(const CubemapSpecification& spec = {});
        static std::shared_ptr<Cubemap> CreateFromEquirectangular(const std::string& filepath, u32 resolution = 512);
        static std::shared_ptr<Cubemap> CreateFromEquirectangularHDR(const float* hdrData, int width, int height, u32 resolution = 512);

        // IBL Convolution pipeline
        static std::shared_ptr<Cubemap> CreateIrradianceMap(const std::shared_ptr<Cubemap>& envMap, u32 size = 32);
        static std::shared_ptr<Cubemap> CreatePrefilterMap(const std::shared_ptr<Cubemap>& envMap, u32 size = 128);
        static std::shared_ptr<Texture2D> CreateBRDFLUT(u32 size = 256);

        // Complete IBL maps generator
        static IBLMaps GenerateIBLMaps(const std::string& hdrFilepath, u32 envResolution = 512);

        // Global pre-baked fallbacks
        static std::shared_ptr<Cubemap> GetBlackCubemap();
        static std::shared_ptr<Cubemap> GetDefaultIrradianceMap();
        static std::shared_ptr<Cubemap> GetDefaultPrefilterMap();
        static std::shared_ptr<Texture2D> GetGlobalBRDFLUT();

    private:
        void Invalidate();

    private:
        CubemapSpecification m_Spec;
        u32 m_RendererID = 0;
        std::string m_FilePath;
    };

}
