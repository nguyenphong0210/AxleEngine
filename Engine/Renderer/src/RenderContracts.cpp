#include <Axle/RenderContracts.hpp>
#include <Axle/Material.hpp>
#include <cmath>
#include <algorithm>

namespace Axle {

    // =========================================================================
    // 1. Canonical Vertex Layout Contracts
    // =========================================================================
    namespace VertexLayoutContract {

        RHI::RHIVertexInputDesc GetCanonicalVertexInputDesc() {
            RHI::RHIVertexInputDesc desc;

            // Stream 0: Static 64-byte PBR vertex
            RHI::RHIVertexStream stream0;
            stream0.BindingSlot = 0;
            stream0.Stride = static_cast<uint16_t>(sizeof(RHI::RHIVertex3D));
            stream0.StepRate = nri::VertexStreamStepRate::PER_VERTEX;
            desc.Streams.push_back(stream0);

            // 0: Position (float3)
            RHI::RHIVertexLayoutAttribute pos;
            pos.SemanticName = "POSITION";
            pos.SemanticIndex = 0;
            pos.Location = 0;
            pos.Offset = offsetof(RHI::RHIVertex3D, Position);
            pos.Format = nri::Format::RGB32_SFLOAT;
            pos.StreamIndex = 0;
            desc.Attributes.push_back(pos);

            // 1: Normal (float3)
            RHI::RHIVertexLayoutAttribute norm;
            norm.SemanticName = "NORMAL";
            norm.SemanticIndex = 0;
            norm.Location = 1;
            norm.Offset = offsetof(RHI::RHIVertex3D, Normal);
            norm.Format = nri::Format::RGB32_SFLOAT;
            norm.StreamIndex = 0;
            desc.Attributes.push_back(norm);

            // 2: Tangent (float4)
            RHI::RHIVertexLayoutAttribute tan;
            tan.SemanticName = "TANGENT";
            tan.SemanticIndex = 0;
            tan.Location = 2;
            tan.Offset = offsetof(RHI::RHIVertex3D, Tangent);
            tan.Format = nri::Format::RGBA32_SFLOAT;
            tan.StreamIndex = 0;
            desc.Attributes.push_back(tan);

            // 3: TexCoord (float2)
            RHI::RHIVertexLayoutAttribute uv;
            uv.SemanticName = "TEXCOORD";
            uv.SemanticIndex = 0;
            uv.Location = 3;
            uv.Offset = offsetof(RHI::RHIVertex3D, TexCoord);
            uv.Format = nri::Format::RG32_SFLOAT;
            uv.StreamIndex = 0;
            desc.Attributes.push_back(uv);

            // 4: Color (float4)
            RHI::RHIVertexLayoutAttribute col;
            col.SemanticName = "COLOR";
            col.SemanticIndex = 0;
            col.Location = 4;
            col.Offset = offsetof(RHI::RHIVertex3D, Color);
            col.Format = nri::Format::RGBA32_SFLOAT;
            col.StreamIndex = 0;
            desc.Attributes.push_back(col);

            return desc;
        }

        bool VerifyVertexLayout(const RHI::RHIVertexInputDesc& desc) {
            if (desc.Streams.empty() || desc.Attributes.size() < 5) return false;
            if (desc.Streams[0].Stride != StaticVertexStride) return false;

            bool hasPos = false, hasNorm = false, hasTan = false, hasUV = false, hasCol = false;
            for (const auto& attr : desc.Attributes) {
                if (attr.SemanticName == "POSITION" && attr.Offset == PositionOffset && attr.Format == nri::Format::RGB32_SFLOAT) hasPos = true;
                if (attr.SemanticName == "NORMAL"   && attr.Offset == NormalOffset   && attr.Format == nri::Format::RGB32_SFLOAT) hasNorm = true;
                if (attr.SemanticName == "TANGENT"  && attr.Offset == TangentOffset  && attr.Format == nri::Format::RGBA32_SFLOAT) hasTan = true;
                if (attr.SemanticName == "TEXCOORD" && attr.Offset == TexCoordOffset && attr.Format == nri::Format::RG32_SFLOAT)   hasUV = true;
                if (attr.SemanticName == "COLOR"    && attr.Offset == ColorOffset    && attr.Format == nri::Format::RGBA32_SFLOAT) hasCol = true;
            }
            return hasPos && hasNorm && hasTan && hasUV && hasCol;
        }

    } // namespace VertexLayoutContract

    // =========================================================================
    // 2. Constant Buffer Contracts
    // =========================================================================
    namespace ConstantBufferContract {

        bool VerifyMaterialConstantPacking() {
            static_assert(sizeof(RHI::RHIMaterialConstants) == 256, "RHIMaterialConstants must be 256 bytes");
            static_assert(offsetof(RHI::RHIMaterialConstants, UVTransform) == 0);
            static_assert(offsetof(RHI::RHIMaterialConstants, Surface) == 16);
            static_assert(offsetof(RHI::RHIMaterialConstants, CoatAnisotropy) == 32);
            static_assert(offsetof(RHI::RHIMaterialConstants, Flags) == 48);
            static_assert(offsetof(RHI::RHIMaterialConstants, DetailUVTransform) == 64);
            static_assert(offsetof(RHI::RHIMaterialConstants, DetailParameters) == 80);
            static_assert(offsetof(RHI::RHIMaterialConstants, Refraction) == 96);
            static_assert(offsetof(RHI::RHIMaterialConstants, RefractionFrame) == 128);
            return true;
        }

    } // namespace ConstantBufferContract

    // =========================================================================
    // 3. Texture Slot Contracts
    // =========================================================================
    namespace TextureSlotContract {

        static const TextureSlotContractInfo s_SlotTable[SlotCount] = {
            { RHI::RHITextureSlot::Albedo,         "Albedo",         "Base Color / Diffuse Reflectance",        TextureColorSpace::sRGB,   nri::Format::RGBA8_SRGB },
            { RHI::RHITextureSlot::Normal,         "Normal",         "Tangent Space Normal (+Y MikkTSpace)",    TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::Roughness,      "Roughness",      "Microfacet Roughness (Channel R)",        TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::Metallic,       "Metallic",       "Conductor / Dielectric Mask (Channel R)", TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::AO,             "AO",             "Ambient Occlusion (Channel R)",           TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::Emissive,       "Emissive",       "Self-Illumination Radiance",              TextureColorSpace::sRGB,   nri::Format::RGBA8_SRGB },
            { RHI::RHITextureSlot::Clearcoat,      "Clearcoat",      "Clearcoat Strength (R) / Gloss (G)",      TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::AnisotropyFlow, "AnisotropyFlow", "Anisotropy Direction Flow (RG)",          TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::DetailNormal,   "DetailNormal",   "Secondary Detail Normal Perturbation",    TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::DetailMask,     "DetailMask",     "Secondary Detail Blend Mask",             TextureColorSpace::Linear, nri::Format::RGBA8_UNORM }
        };

        const TextureSlotContractInfo& GetSlotInfo(RHI::RHITextureSlot slot) {
            uint32_t idx = static_cast<uint32_t>(slot);
            if (idx >= SlotCount) idx = 0;
            return s_SlotTable[idx];
        }

        bool IsSRGBSlot(RHI::RHITextureSlot slot) {
            return GetSlotInfo(slot).ColorSpace == TextureColorSpace::sRGB;
        }

        std::string_view GetSlotName(RHI::RHITextureSlot slot) {
            return GetSlotInfo(slot).Name;
        }

    } // namespace TextureSlotContract

    // =========================================================================
    // 4. Color Space & Tone Mapping Ownership Contracts
    // =========================================================================
    namespace ColorSpaceContract {

        float LinearToSRGB(float linearValue) {
            if (linearValue <= 0.0031308f) {
                return linearValue * 12.92f;
            }
            return 1.055f * std::pow(linearValue, 1.0f / 2.4f) - 0.055f;
        }

        float SRGBToLinear(float srgbValue) {
            if (srgbValue <= 0.04045f) {
                return srgbValue / 12.92f;
            }
            return std::pow((srgbValue + 0.055f) / 1.055f, 2.4f);
        }

        glm::vec3 LinearToSRGB(const glm::vec3& linearColor) {
            return glm::vec3(
                LinearToSRGB(linearColor.r),
                LinearToSRGB(linearColor.g),
                LinearToSRGB(linearColor.b)
            );
        }

        glm::vec3 SRGBToLinear(const glm::vec3& srgbColor) {
            return glm::vec3(
                SRGBToLinear(srgbColor.r),
                SRGBToLinear(srgbColor.g),
                SRGBToLinear(srgbColor.b)
            );
        }

        glm::vec4 LinearToSRGB(const glm::vec4& linearColor) {
            return glm::vec4(
                LinearToSRGB(linearColor.r),
                LinearToSRGB(linearColor.g),
                LinearToSRGB(linearColor.b),
                linearColor.a // Alpha is always stored linear
            );
        }

        glm::vec4 SRGBToLinear(const glm::vec4& srgbColor) {
            return glm::vec4(
                SRGBToLinear(srgbColor.r),
                SRGBToLinear(srgbColor.g),
                SRGBToLinear(srgbColor.b),
                srgbColor.a // Alpha is always stored linear
            );
        }

    } // namespace ColorSpaceContract

    // =========================================================================
    // 5. Depth, Clip and Coordinate System Contracts
    // =========================================================================
    namespace DepthClipContract {

        void GetDepthRange(RendererBackendType backend, float& outNearZ, float& outFarZ) {
            if (backend == RendererBackendType::OpenGL_Compatibility) {
                outNearZ = -1.0f;
                outFarZ = 1.0f;
            } else {
                outNearZ = 0.0f;
                outFarZ = 1.0f;
            }
        }

        nri::CompareOp GetDefaultDepthCompareOp() {
            return nri::CompareOp::LESS;
        }

    } // namespace DepthClipContract

    // =========================================================================
    // 6. Surface Mode, Blending and Culling Contracts
    // =========================================================================
    namespace SurfaceBlendContract {

        RHI::RHIBlendAttachmentDesc GetBlendAttachmentDesc(SurfaceType surface) {
            switch (surface) {
                case SurfaceType::Opaque:
                case SurfaceType::Cutout:
                    return RHI::RHIBlendAttachmentDesc::Opaque();

                case SurfaceType::Transparent:
                    return RHI::RHIBlendAttachmentDesc::AlphaBlend();

                case SurfaceType::Additive:
                    return RHI::RHIBlendAttachmentDesc::Additive();

                default:
                    return RHI::RHIBlendAttachmentDesc::Opaque();
            }
        }

        RHI::RHIDepthStencilDesc GetDepthStencilDesc(SurfaceType surface, bool depthWriteOverride) {
            RHI::RHIDepthStencilDesc desc;
            desc.DepthTestEnable = true;
            desc.DepthCompareOp = nri::CompareOp::LESS;
            desc.DepthBoundsTest = false;
            desc.StencilEnable = false;
            desc.DepthStencilFormat = nri::Format::D32_SFLOAT;

            // Only Opaque and Cutout write depth; Transparent and Additive do not
            if (surface == SurfaceType::Transparent || surface == SurfaceType::Additive) {
                desc.DepthWriteEnable = false;
            } else {
                desc.DepthWriteEnable = depthWriteOverride;
            }

            return desc;
        }

        RHI::RHIRasterizerDesc GetRasterizerDesc(CullMode cull, bool wireframe) {
            RHI::RHIRasterizerDesc desc;
            desc.FillMode = wireframe ? nri::FillMode::WIREFRAME : nri::FillMode::SOLID;
            desc.FrontCounterClockwise = DepthClipContract::FrontCounterClockwise;
            desc.DepthClamp = false;
            desc.LineSmoothing = false;
            desc.ConservativeRaster = false;

            switch (cull) {
                case CullMode::Back:
                    desc.CullMode = nri::CullMode::BACK;
                    break;
                case CullMode::Front:
                    desc.CullMode = nri::CullMode::FRONT;
                    break;
                case CullMode::None:
                    desc.CullMode = nri::CullMode::NONE;
                    break;
                default:
                    desc.CullMode = nri::CullMode::BACK;
                    break;
            }

            return desc;
        }

    } // namespace SurfaceBlendContract

    // =========================================================================
    // 7. Capability and Fallback Reporting Contract
    // =========================================================================
    namespace CapabilityContract {

        bool ValidateMaterialCompatibility(RendererBackendType backend, const Material& material, std::string& outDiagnostic) {
            outDiagnostic.clear();

            if (backend == RendererBackendType::OpenGL_Compatibility) {
                if (material.GetUseCustomShader()) {
                    outDiagnostic = "OpenGL Compatibility: Custom HLSL shaders are not supported. Authored shader properties preserved; rendering via OpenGL standard forward PBR.";
                    return false;
                }
                if (material.GetRefractionEnabled()) {
                    outDiagnostic = "OpenGL Compatibility: Screen-space refraction copies are not supported. Authored refraction settings preserved; rendering solid dielectric.";
                    return false;
                }
            }

            return true;
        }

    } // namespace CapabilityContract

} // namespace Axle
