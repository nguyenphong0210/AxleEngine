#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHITransform.hpp>

#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <array>
#include <filesystem>
#include <algorithm>

namespace Axle::RHI {

    class RHIDescriptorPool;
    class RHIPipelineLayout;

    // Shading model selector matching ForwardPBR.hlsl
    enum class RHIShadingModel : uint32_t {
        MetallicRoughness = 0, // Solid / local Cook-Torrance GGX PBR
        ProceduralCaro    = 1, // 3D Procedural Checkerboard Pattern
        TerrainSplat      = 2, // Vertex Color Splatting (Heightfield)
        TexturedPBR       = 3  // Full 6-slot textured PBR (Albedo, Normal, Rough, Metal, AO, Emissive)
    };

    // Standard 6 PBR Texture Map Slots
    enum class RHITextureSlot : uint32_t {
        Albedo    = 0, // Base Color / Diffuse reflectance (sRGB / linear)
        Normal    = 1, // MikkTSpace Tangent Space Normal (+Y, Tangent.w handedness)
        Roughness = 2, // Microfacet roughness (linear float, channel R)
        Metallic  = 3, // Conductor / Dielectric mask (linear float, channel R)
        AO        = 4, // Ambient Occlusion (linear float, channel R)
        Emissive  = 5, // Self-illumination radiance (linear RGB)
        Count     = 6
    };

    struct RHIMaterialDesc {
        std::string Name = "PBR_Material";
        glm::vec4 BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float Roughness = 0.5f;       // Clamped to [0.04, 1.0]
        float Metallic = 0.0f;        // Clamped to [0.0, 1.0]
        float NormalStrength = 1.0f;  // Normal perturbation scale
        float AO = 1.0f;              // Clamped to [0.0, 1.0]
        glm::vec3 EmissiveColor{ 0.0f, 0.0f, 0.0f };
        float EmissiveIntensity = 0.0f;
        glm::vec2 UVScale{ 1.0f, 1.0f };
        glm::vec2 UVOffset{ 0.0f, 0.0f };
        RHIShadingModel ShadingModel = RHIShadingModel::MetallicRoughness;

        // Optional texture map source paths
        std::string AlbedoPath;
        std::string NormalPath;
        std::string RoughnessPath;
        std::string MetallicPath;
        std::string AOPath;
        std::string EmissivePath;
    };

    class RHIMaterial {
    public:
        RHIMaterial(const std::string& name = "RHIMaterial");
        RHIMaterial(const RHIMaterialDesc& desc);
        ~RHIMaterial() = default;

        // Identification & Properties
        const std::string& GetName() const { return m_Desc.Name; }
        void SetName(const std::string& name) { m_Desc.Name = name; }

        const glm::vec4& GetBaseColor() const { return m_Desc.BaseColor; }
        void SetBaseColor(const glm::vec4& color) { m_Desc.BaseColor = color; }

        float GetRoughness() const { return m_Desc.Roughness; }
        void SetRoughness(float roughness) { m_Desc.Roughness = std::clamp(roughness, 0.04f, 1.0f); }

        float GetMetallic() const { return m_Desc.Metallic; }
        void SetMetallic(float metallic) { m_Desc.Metallic = std::clamp(metallic, 0.0f, 1.0f); }

        float GetNormalStrength() const { return m_Desc.NormalStrength; }
        void SetNormalStrength(float strength) { m_Desc.NormalStrength = strength; }

        float GetAO() const { return m_Desc.AO; }
        void SetAO(float ao) { m_Desc.AO = std::clamp(ao, 0.0f, 1.0f); }

        const glm::vec3& GetEmissiveColor() const { return m_Desc.EmissiveColor; }
        void SetEmissiveColor(const glm::vec3& color) { m_Desc.EmissiveColor = color; }

        float GetEmissiveIntensity() const { return m_Desc.EmissiveIntensity; }
        void SetEmissiveIntensity(float intensity) { m_Desc.EmissiveIntensity = std::max(0.0f, intensity); }

        const glm::vec2& GetUVScale() const { return m_Desc.UVScale; }
        void SetUVScale(const glm::vec2& scale) { m_Desc.UVScale = scale; }

        const glm::vec2& GetUVOffset() const { return m_Desc.UVOffset; }
        void SetUVOffset(const glm::vec2& offset) { m_Desc.UVOffset = offset; }

        RHIShadingModel GetShadingModel() const { return m_Desc.ShadingModel; }
        void SetShadingModel(RHIShadingModel model) { m_Desc.ShadingModel = model; }

        const RHIMaterialDesc& GetDesc() const { return m_Desc; }

        // Texture Slots
        void SetTexture(RHITextureSlot slot, const std::shared_ptr<RHITexture>& texture, const std::string& path = "");
        std::shared_ptr<RHITexture> GetTexture(RHITextureSlot slot) const;
        bool HasTexture(RHITextureSlot slot) const;
        const std::string& GetTexturePath(RHITextureSlot slot) const;

        void SetAlbedoTexture(const std::shared_ptr<RHITexture>& tex, const std::string& path = "") { SetTexture(RHITextureSlot::Albedo, tex, path); }
        void SetNormalTexture(const std::shared_ptr<RHITexture>& tex, const std::string& path = "") { SetTexture(RHITextureSlot::Normal, tex, path); }
        void SetRoughnessTexture(const std::shared_ptr<RHITexture>& tex, const std::string& path = "") { SetTexture(RHITextureSlot::Roughness, tex, path); }
        void SetMetallicTexture(const std::shared_ptr<RHITexture>& tex, const std::string& path = "") { SetTexture(RHITextureSlot::Metallic, tex, path); }
        void SetAOTexture(const std::shared_ptr<RHITexture>& tex, const std::string& path = "") { SetTexture(RHITextureSlot::AO, tex, path); }
        void SetEmissiveTexture(const std::shared_ptr<RHITexture>& tex, const std::string& path = "") { SetTexture(RHITextureSlot::Emissive, tex, path); }

        // Shader Streaming: Applies PBR parameters to 128B RHIObjectPushConstants
        void ApplyToPushConstants(RHIObjectPushConstants& pushConsts) const;

        // Static Default Textures (1x1 GPU resident fallbacks)
        static std::shared_ptr<RHITexture> GetDefaultWhiteTexture();       // 0xFFFFFFFF (Albedo, AO)
        static std::shared_ptr<RHITexture> GetDefaultFlatNormalTexture();   // 0xFFFF8080 (Flat MikkTSpace normal [0, 0, 1])
        static std::shared_ptr<RHITexture> GetDefaultRoughnessTexture();    // 0xFF808080 (0.5 Roughness)
        static std::shared_ptr<RHITexture> GetDefaultBlackTexture();        // 0xFF000000 (0.0 Metallic / Emissive)
        static void ReleaseDefaultTextures();

        // Automotive & Industrial PBR Presets
        static std::shared_ptr<RHIMaterial> CreateCarPaintRed();
        static std::shared_ptr<RHIMaterial> CreateCarPaintBlue();
        static std::shared_ptr<RHIMaterial> CreateCarPaintSilver();
        static std::shared_ptr<RHIMaterial> CreateChrome();
        static std::shared_ptr<RHIMaterial> CreateGold();
        static std::shared_ptr<RHIMaterial> CreateCopper();
        static std::shared_ptr<RHIMaterial> CreateCarbonFiber();
        static std::shared_ptr<RHIMaterial> CreateTireRubber();
        static std::shared_ptr<RHIMaterial> CreateCastIron();
        static std::shared_ptr<RHIMaterial> CreateHeadlightLED();

        // Bindless Tier 2 indexing slot indices
        uint32_t GetBindlessIndex(RHITextureSlot slot) const {
            size_t idx = static_cast<size_t>(slot);
            return idx < m_BindlessIndices.size() ? m_BindlessIndices[idx] : 0;
        }
        void SetBindlessIndex(RHITextureSlot slot, uint32_t index) {
            size_t idx = static_cast<size_t>(slot);
            if (idx < m_BindlessIndices.size()) {
                m_BindlessIndices[idx] = index;
            }
        }

        // Builds or updates a 6-texture DescriptorSet for this material from RHIDescriptorPool
        nri::DescriptorSet* CreateDescriptorSet(RHIDescriptorPool& pool, const RHIPipelineLayout& layout, nri::Descriptor* sampler = nullptr);

        // Serialization
        bool Save(const std::filesystem::path& filepath) const;
        bool Load(const std::filesystem::path& filepath);
        static std::shared_ptr<RHIMaterial> LoadFromFile(const std::filesystem::path& filepath);

    private:
        RHIMaterialDesc m_Desc;
        std::array<std::shared_ptr<RHITexture>, static_cast<size_t>(RHITextureSlot::Count)> m_Textures;
        std::array<uint32_t, static_cast<size_t>(RHITextureSlot::Count)> m_BindlessIndices = { 0, 1, 2, 3, 0, 3 };
    };

}
