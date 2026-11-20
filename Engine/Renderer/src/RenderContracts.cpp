#include <glm/gtc/packing.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderBackendPolicy.hpp>
#include <Axle/Material.hpp>
#include <Axle/ResolvedSurface.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <bit>
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_set>

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
            static_assert(offsetof(RHI::RHIMaterialConstants, Sheen) == 144);
            static_assert(offsetof(RHI::RHIMaterialConstants, Subsurface) == 160);
            static_assert(offsetof(RHI::RHIMaterialConstants, Absorption) == 176);
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

    // =========================================================================
    // 8. Occlusion and Shading Separation Contracts (M15-U4)
    // =========================================================================
    namespace OcclusionContract {

        namespace {
            bool HasSeparatedLighting(const ShadingComponents& comp) {
                return comp.SunDiffuse != glm::vec3(0) || comp.SunSpecular != glm::vec3(0) ||
                       comp.LocalDiffuse != glm::vec3(0) || comp.LocalSpecular != glm::vec3(0);
            }
        }

        float CombineDirectionalVisibility(float csmVisibility, float contactShadow,
                                           bool enableContactShadows, bool enableCSM) {
            const float csm = enableCSM ? std::clamp(csmVisibility, 0.0f, 1.0f) : 1.0f;
            const float cs  = enableContactShadows ? std::clamp(contactShadow, 0.0f, 1.0f) : 1.0f;
            // The minimum operator represents that both CSM and screen-space contact shadows
            // sample the occlusion of the same directional sun light source.
            // Using min(csm, cs) guarantees that:
            // 1. A point fully shadowed by CSM (csm = 0) remains 0 (no redundant/double darkening).
            // 2. Penumbra regions do not compound attenuation (e.g. 0.5 * 0.5 = 0.25 is avoided).
            // 3. When contact shadows are disabled or cs = 1.0, csm is preserved untouched.
            return std::min(csm, cs);
        }

        glm::vec3 EvaluateShading(const ShadingComponents& comp, float ambientOcclusion, float contactShadow) {
            const float ao = comp.EnableAO ? std::clamp(ambientOcclusion, 0.0f, 1.0f) : 1.0f;
            const glm::vec3 ambientTerm = comp.Ambient * ao;

            // Determine sun and local components with backwards-compatibility for U4-A:
            glm::vec3 sunDiff = comp.SunDiffuse;
            glm::vec3 sunSpec = comp.SunSpecular;
            glm::vec3 localDiff = comp.LocalDiffuse;
            glm::vec3 localSpec = comp.LocalSpecular;

            if (!HasSeparatedLighting(comp)) {
                // Backwards compatibility with U4-A: route the legacy direct
                // fields to the only available direct-light class. When a
                // caller explicitly says that no sun exists, those fields
                // represent local direct light rather than disappearing.
                if (comp.HasSun) {
                    sunDiff = comp.DirectDiffuse;
                    sunSpec = comp.DirectSpecular;
                } else {
                    localDiff = comp.DirectDiffuse;
                    localSpec = comp.DirectSpecular;
                }
            }

            // Directional sun contribution:
            glm::vec3 sunTerm{0.0f};
            if (comp.HasSun) {
                const float sunVisibility = CombineDirectionalVisibility(
                    comp.CSMVisibility, contactShadow, comp.EnableContactShadows, comp.EnableCSM);
                sunTerm = (sunDiff + sunSpec) * sunVisibility;
            }

            // Local light contribution (point, spot): NEVER attenuated by directional contact shadow
            const glm::vec3 localTerm = localDiff + localSpec;

            return ambientTerm + sunTerm + localTerm + comp.Emissive;
        }

        bool IsSeparateIndirectTargetRequired(DataflowMode mode) {
            // Under PreLightingSeparated forward shading, the forward pixel shader scales
            // ambient in GPU registers before accumulating with direct light and emissive.
            // A separate physical render target is NOT required.
            if (mode == DataflowMode::PreLightingSeparated) {
                return false;
            }
            // Under deferred post-lighting compositing, a separate indirect target is required
            // to avoid multiplying the direct and emissive terms by ambient occlusion.
            return true;
        }

        std::string_view GetIndirectTargetDecisionRationale() {
            return "DECISION: A separate physical indirect render target is NOT required for forward shading under the pre-lighting occlusion contract. "
                   "Evaluating ambient occlusion prior to or within forward shading allows in-place register modulation (Color = Ambient * AO + Direct + Emissive), "
                   "saving 16-36 MB of VRAM (RGBA16_FLOAT) and eliminating Multiple Render Target (MRT) write bandwidth and fullscreen composite blits. "
                   "Auxiliary indirect targets remain available optionally for decoupled diagnostics and debug readback.";
        }

        bool VerifyAmbientOnlyModulation(const ShadingComponents& comp) {
            const glm::vec3 shadedAt0 = EvaluateShading(comp, 0.0f, 1.0f);
            const glm::vec3 shadedAt1 = EvaluateShading(comp, 1.0f, 1.0f);

            // Emissive radiance must be completely unchanged by AO variation
            ShadingComponents emissiveOnly{};
            emissiveOnly.Emissive = comp.Emissive;
            if (EvaluateShading(emissiveOnly, 0.0f, 1.0f) != EvaluateShading(emissiveOnly, 1.0f, 1.0f)) {
                return false;
            }

            // Direct light (diffuse + specular) must be completely unchanged by AO variation
            ShadingComponents directOnly{};
            directOnly.DirectDiffuse = comp.DirectDiffuse;
            directOnly.DirectSpecular = comp.DirectSpecular;
            if (EvaluateShading(directOnly, 0.0f, 1.0f) != EvaluateShading(directOnly, 1.0f, 1.0f)) {
                return false;
            }

            // Difference between AO=1 and AO=0 must strictly equal the Ambient term
            const glm::vec3 delta = shadedAt1 - shadedAt0;
            constexpr float epsilon = 1e-6f;
            return std::abs(delta.x - comp.Ambient.x) < epsilon &&
                   std::abs(delta.y - comp.Ambient.y) < epsilon &&
                   std::abs(delta.z - comp.Ambient.z) < epsilon;
        }

        bool VerifySunOnlyContactModulation(const ShadingComponents& comp) {
            const glm::vec3 atContact1 = EvaluateShading(comp, 1.0f, 1.0f);
            const glm::vec3 atContact0 = EvaluateShading(comp, 1.0f, 0.0f);

            // 1. Ambient term must be completely unchanged by contact shadows
            ShadingComponents ambOnly{};
            ambOnly.Ambient = comp.Ambient;
            if (EvaluateShading(ambOnly, 1.0f, 1.0f) != EvaluateShading(ambOnly, 1.0f, 0.0f)) {
                return false;
            }

            // 2. Emissive term must be completely unchanged by contact shadows
            ShadingComponents emOnly{};
            emOnly.Emissive = comp.Emissive;
            if (EvaluateShading(emOnly, 1.0f, 1.0f) != EvaluateShading(emOnly, 1.0f, 0.0f)) {
                return false;
            }

            // 3. Local lights (point, spot) must be completely unchanged by contact shadows
            ShadingComponents localOnly{};
            localOnly.LocalDiffuse = comp.LocalDiffuse;
            localOnly.LocalSpecular = comp.LocalSpecular;
            localOnly.HasSun = false;
            if (EvaluateShading(localOnly, 1.0f, 1.0f) != EvaluateShading(localOnly, 1.0f, 0.0f)) {
                return false;
            }

            // 4. If Sun is present and contact shadows enabled, difference must affect only sun contribution
            if (comp.HasSun && comp.EnableContactShadows) {
                glm::vec3 sunContribution = comp.SunDiffuse + comp.SunSpecular;
                if (!HasSeparatedLighting(comp)) {
                    sunContribution = comp.DirectDiffuse + comp.DirectSpecular;
                }
                const glm::vec3 diff = atContact1 - atContact0;
                const float effectiveCSM = comp.EnableCSM ? std::clamp(comp.CSMVisibility, 0.0f, 1.0f) : 1.0f;
                const glm::vec3 expectedDrop = sunContribution * effectiveCSM;
                constexpr float eps = 1e-5f;
                if (std::abs(diff.x - expectedDrop.x) > eps ||
                    std::abs(diff.y - expectedDrop.y) > eps ||
                    std::abs(diff.z - expectedDrop.z) > eps) {
                    return false;
                }
            }
            return true;
        }

        bool VerifyNoCSMDoubleAttenuation(float csmVisibility, float contactShadow) {
            // Test 1: Fully shadowed by CSM (csm = 0.0) -> contact shadow cannot double-darken
            const float visAtZeroCSM = CombineDirectionalVisibility(0.0f, contactShadow, true, true);
            if (visAtZeroCSM != 0.0f) return false;

            // Test 2: In penumbra (csm = 0.5, cs = 0.5) -> combined must be 0.5, NOT 0.25 (avoids double attenuation)
            const float visPenumbra = CombineDirectionalVisibility(0.5f, 0.5f, true, true);
            if (visPenumbra != 0.5f) return false;

            // Test 3: Unshadowed CSM (csm = 1.0) -> contact shadow applies cleanly
            const float visAtFullCSM = CombineDirectionalVisibility(1.0f, contactShadow, true, true);
            if (std::abs(visAtFullCSM - std::clamp(contactShadow, 0.0f, 1.0f)) > 1e-6f) return false;

            // Test 4: Contact shadows disabled -> returns pure CSM visibility
            const float visDisabled = CombineDirectionalVisibility(csmVisibility, contactShadow, false, true);
            if (std::abs(visDisabled - std::clamp(csmVisibility, 0.0f, 1.0f)) > 1e-6f) return false;

            // Test 5: CSM disabled -> returns pure contact shadow visibility
            const float visCSMDisabled = CombineDirectionalVisibility(csmVisibility, contactShadow, true, false);
            if (std::abs(visCSMDisabled - std::clamp(contactShadow, 0.0f, 1.0f)) > 1e-6f) return false;

            return true;
        }

        bool VerifySunAbsenceBehavior(const ShadingComponents& comp) {
            ShadingComponents noSun = comp;
            noSun.HasSun = false;
            const glm::vec3 at1 = EvaluateShading(noSun, 1.0f, 1.0f);
            const glm::vec3 at0 = EvaluateShading(noSun, 1.0f, 0.0f);
            return at1 == at0;
        }

        bool VerifyContactDisabledBehavior(const ShadingComponents& comp) {
            ShadingComponents disabled = comp;
            disabled.EnableContactShadows = false;
            const glm::vec3 at1 = EvaluateShading(disabled, 1.0f, 1.0f);
            const glm::vec3 at0 = EvaluateShading(disabled, 1.0f, 0.0f);
            return at1 == at0;
        }

        // =====================================================================
        // M15-U4-C: Half-Resolution, Bilateral Reconstruction & Depth Edge Protection
        // =====================================================================
        Resolution2D ComputeHalfResolution(uint32_t fullWidth, uint32_t fullHeight) {
            return Resolution2D{
                std::max(1u, fullWidth >> 1),
                std::max(1u, fullHeight >> 1)
            };
        }

        BilateralTapResult EvaluateBilateralWeight(float centerZ, float tapZ, int tapOffsetX, int tapOffsetY) {
            static constexpr float spatialWeights[5] = { 0.06136f, 0.24477f, 0.38774f, 0.24477f, 0.06136f };
            const int clampedX = std::clamp(tapOffsetX, -2, 2) + 2;
            const int clampedY = std::clamp(tapOffsetY, -2, 2) + 2;
            const float spatialWeight = spatialWeights[clampedX] * spatialWeights[clampedY];

            if (!std::isfinite(centerZ) || !std::isfinite(tapZ))
                return {spatialWeight, 0.0f, 0.0f, true};

            const float depthDiff = std::abs(centerZ - tapZ);
            const float denom = 0.05f * std::max(std::abs(centerZ), 0.1f) + 1e-4f;
            const float depthWeight = std::exp(-depthDiff / denom);
            const float totalWeight = depthWeight < 1e-3f ? 0.0f : depthWeight * spatialWeight;

            BilateralTapResult result{};
            result.SpatialWeight = spatialWeight;
            result.DepthWeight = depthWeight;
            result.TotalWeight = totalWeight;
            result.IsEdgeRejected = (depthWeight < 1e-3f);
            return result;
        }

        bool VerifyHalfResolutionDimensions(uint32_t fullW, uint32_t fullH) {
            if (fullW == 0 || fullH == 0) return false;
            const Resolution2D half = ComputeHalfResolution(fullW, fullH);
            if (half.Width == 0 || half.Height == 0) return false;
            if (fullW > 1 && half.Width != (fullW >> 1)) return false;
            if (fullH > 1 && half.Height != (fullH >> 1)) return false;
            if (fullW == 1 && half.Width != 1) return false;
            if (fullH == 1 && half.Height != 1) return false;
            return true;
        }

        bool VerifyBilateralReconstructionStability() {
            // 1. Spatial kernel must integrate to 1.0
            float spatialSum = 0.0f;
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    spatialSum += EvaluateBilateralWeight(1.0f, 1.0f, x, y).SpatialWeight;
                }
            }
            if (std::abs(spatialSum - 1.0f) > 1e-4f) return false;

            // 2. On a flat/smooth surface (depthDiff = 0), depth weight is 1.0 and total weight sum is 1.0
            float totalFlatSum = 0.0f;
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    const auto tap = EvaluateBilateralWeight(2.5f, 2.5f, x, y);
                    if (std::abs(tap.DepthWeight - 1.0f) > 1e-5f) return false;
                    totalFlatSum += tap.TotalWeight;
                }
            }
            if (std::abs(totalFlatSum - 1.0f) > 1e-4f) return false;

            // 3. Peak weight must be at center (0, 0)
            const auto centerTap = EvaluateBilateralWeight(2.0f, 2.0f, 0, 0);
            const auto cornerTap = EvaluateBilateralWeight(2.0f, 2.0f, 2, 2);
            if (centerTap.TotalWeight <= cornerTap.TotalWeight) return false;

            return true;
        }

        bool VerifyDepthEdgeProtection(float centerZ, float foregroundZ, float backgroundZ) {
            // When center is foreground:
            // Foreground neighbor tap must have strong weight (near 1.0)
            const auto fgTap = EvaluateBilateralWeight(centerZ, foregroundZ, 1, 0);
            if (fgTap.DepthWeight < 0.8f || fgTap.IsEdgeRejected) return false;

            // Background neighbor tap across a depth discontinuity must be strongly rejected
            const auto bgTap = EvaluateBilateralWeight(centerZ, backgroundZ, 1, 0);
            if (!bgTap.IsEdgeRejected || bgTap.DepthWeight > 1e-3f) return false;

            return true;
        }

        bool VerifyOddResolutionSafety(uint32_t oddW, uint32_t oddH) {
            if (oddW == 0 || oddH == 0) return false;
            const Resolution2D half = ComputeHalfResolution(oddW, oddH);
            if (half.Width == 0 || half.Height == 0) return false;

            // Half-resolution must be non-zero and bounded by full resolution
            if (half.Width > oddW || half.Height > oddH) return false;

            // Compute dispatch thread groups: (halfW + 7) / 8
            const uint32_t groupsX = (half.Width + 7) / 8;
            const uint32_t groupsY = (half.Height + 7) / 8;
            const uint32_t totalThreadsX = groupsX * 8;
            const uint32_t totalThreadsY = groupsY * 8;
            if (totalThreadsX < half.Width || totalThreadsY < half.Height) return false;

            // Verify reciprocal texel size is positive and finite
            const float rcpW = 1.0f / static_cast<float>(half.Width);
            const float rcpH = 1.0f / static_cast<float>(half.Height);
            if (rcpW <= 0.0f || rcpH <= 0.0f || std::isinf(rcpW) || std::isinf(rcpH)) return false;

            // Boundary UV check: pixel (oddW - 1, oddH - 1)
            const float lastFullU = (static_cast<float>(oddW - 1) + 0.5f) / static_cast<float>(oddW);
            const float lastFullV = (static_cast<float>(oddH - 1) + 0.5f) / static_cast<float>(oddH);
            if (lastFullU < 0.0f || lastFullU > 1.0f || lastFullV < 0.0f || lastFullV > 1.0f) return false;

            return true;
        }

        // =====================================================================
        // M15-U4-D: Regression Fixtures & Invariance Contracts (U4-06.1 - U4-06.5)
        // =====================================================================
        bool VerifyNoSunRegression(const ShadingComponents& comp) {
            ShadingComponents noSun = comp;
            noSun.HasSun = false;
            const glm::vec3 at1 = EvaluateShading(noSun, 1.0f, 1.0f);
            const glm::vec3 at0 = EvaluateShading(noSun, 1.0f, 0.0f);
            const glm::vec3 atHalf = EvaluateShading(noSun, 0.5f, 0.0f);
            if (at1 != at0) return false;
            if (!std::isfinite(at1.x) || !std::isfinite(at1.y) || !std::isfinite(at1.z)) return false;
            if (!std::isfinite(atHalf.x) || !std::isfinite(atHalf.y) || !std::isfinite(atHalf.z)) return false;

            // Expected radiance is ambient * ao + local + emissive
            glm::vec3 expectedLocal = comp.LocalDiffuse + comp.LocalSpecular;
            if (!HasSeparatedLighting(comp)) {
                expectedLocal = comp.DirectDiffuse + comp.DirectSpecular;
            }
            const glm::vec3 expected = comp.Ambient + expectedLocal + comp.Emissive;
            constexpr float eps = 1e-5f;
            return glm::distance(at1, expected) < eps;
        }

        bool VerifyNoAORegression(const ShadingComponents& comp) {
            ShadingComponents noAO = comp;
            noAO.EnableAO = false;
            const glm::vec3 at0 = EvaluateShading(noAO, 0.0f, 1.0f);
            const glm::vec3 at1 = EvaluateShading(noAO, 1.0f, 1.0f);
            if (at0 != at1) return false;
            if (!std::isfinite(at0.x) || !std::isfinite(at0.y) || !std::isfinite(at0.z)) return false;

            // When AO is disabled, ambient is unattenuated (identical to AO=1 with EnableAO=true)
            ShadingComponents withAO = comp;
            withAO.EnableAO = true;
            const glm::vec3 normalAt1 = EvaluateShading(withAO, 1.0f, 1.0f);
            return at0 == normalAt1;
        }

        bool VerifyNoContactRegression(const ShadingComponents& comp) {
            ShadingComponents noContact = comp;
            noContact.EnableContactShadows = false;
            const glm::vec3 at0 = EvaluateShading(noContact, 1.0f, 0.0f);
            const glm::vec3 at1 = EvaluateShading(noContact, 1.0f, 1.0f);
            if (at0 != at1) return false;
            if (!std::isfinite(at0.x) || !std::isfinite(at0.y) || !std::isfinite(at0.z)) return false;

            // When contact is disabled, directional sun visibility is purely CSM
            ShadingComponents withContact = comp;
            withContact.EnableContactShadows = true;
            const glm::vec3 normalAt1 = EvaluateShading(withContact, 1.0f, 1.0f);
            return at0 == normalAt1;
        }

        bool VerifyEmissiveInvariance(const ShadingComponents& comp) {
            // 1. Pure emissive surface must remain strictly unchanged regardless of AO or contact shadows
            ShadingComponents emissiveOnly{};
            emissiveOnly.Emissive = comp.Emissive;
            const glm::vec3 e00 = EvaluateShading(emissiveOnly, 0.0f, 0.0f);
            const glm::vec3 e10 = EvaluateShading(emissiveOnly, 1.0f, 0.0f);
            const glm::vec3 e01 = EvaluateShading(emissiveOnly, 0.0f, 1.0f);
            const glm::vec3 e11 = EvaluateShading(emissiveOnly, 1.0f, 1.0f);
            if (e00 != comp.Emissive || e10 != comp.Emissive || e01 != comp.Emissive || e11 != comp.Emissive) {
                return false;
            }
            if (!std::isfinite(e00.x) || !std::isfinite(e00.y) || !std::isfinite(e00.z)) return false;

            // Compare the mixed scene against the same lighting without emission.
            // Finiteness alone would also accept occlusion-dependent emission.
            ShadingComponents withoutEmission = comp;
            withoutEmission.Emissive = glm::vec3(0.0f);
            for (float ao : {0.0f, 0.5f, 1.0f}) for (float contact : {0.0f, 0.5f, 1.0f}) {
                const auto actual = EvaluateShading(comp, ao, contact);
                const auto expected = EvaluateShading(withoutEmission, ao, contact) + comp.Emissive;
                for (int channel = 0; channel < 3; ++channel) {
                    if (!std::isfinite(actual[channel]) || !std::isfinite(expected[channel]) ||
                        std::abs(actual[channel] - expected[channel]) > 1e-5f * std::max(1.0f, std::abs(expected[channel]))) return false;
                }
            }

            return true;
        }

        bool VerifyUnoccludedSpecularInvariance(const ShadingComponents& comp) {
            // Specular highlight from directional sun or local light (unoccluded: CSM=1, contact=1)
            ShadingComponents specComp = comp;
            specComp.SunSpecular = comp.SunSpecular;
            specComp.LocalSpecular = comp.LocalSpecular;
            specComp.CSMVisibility = 1.0f;
            specComp.EnableCSM = true;
            specComp.EnableContactShadows = true;

            // Direct specular must be 100% invariant under AO variation
            const glm::vec3 sAtAO0 = EvaluateShading(specComp, 0.0f, 1.0f);
            const glm::vec3 sAtAO1 = EvaluateShading(specComp, 1.0f, 1.0f);
            // Delta between AO=0 and AO=1 must affect only Ambient, NOT specular
            const glm::vec3 delta = sAtAO1 - sAtAO0;
            constexpr float eps = 1e-5f;
            const auto expectedDelta = specComp.EnableAO ? specComp.Ambient : glm::vec3(0.0f);
            for (int channel = 0; channel < 3; ++channel) {
                if (!std::isfinite(sAtAO0[channel]) || !std::isfinite(sAtAO1[channel]) ||
                    std::abs(delta[channel] - expectedDelta[channel]) > eps * std::max(1.0f, std::abs(sAtAO1[channel]))) return false;
            }

            // Pure unoccluded specular highlights:
            ShadingComponents pureSpecular{};
            pureSpecular.SunSpecular = comp.SunSpecular;
            pureSpecular.LocalSpecular = comp.LocalSpecular;
            if (!HasSeparatedLighting(comp)) pureSpecular.DirectSpecular = comp.DirectSpecular;
            pureSpecular.HasSun = comp.HasSun;
            pureSpecular.CSMVisibility = 1.0f;
            const glm::vec3 p0 = EvaluateShading(pureSpecular, 0.0f, 1.0f);
            const glm::vec3 p1 = EvaluateShading(pureSpecular, 1.0f, 1.0f);
            if (p0 != p1) return false;
            if (!std::isfinite(p0.x) || !std::isfinite(p0.y) || !std::isfinite(p0.z)) return false;

            return true;
        }

        // =====================================================================
        // M15-U4-E: Normal-Map & Animated/Deformed Mesh Contracts (U4-07.1 - U4-07.2)
        // =====================================================================

        glm::vec3 EvaluateTangentSpaceNormal(const NormalMapSample& sample) {
            if (!std::isfinite(sample.TangentRG.x) || !std::isfinite(sample.TangentRG.y) ||
                !std::isfinite(sample.Strength)) return glm::vec3(0, 0, 1);

            const glm::vec2 xy = sample.TangentRG * 2.0f - 1.0f;
            const float z = std::sqrt(std::max(0.0f, 1.0f - glm::dot(xy, xy)));
            const glm::vec3 tangentN = glm::normalize(glm::vec3(xy * std::max(0.0f, sample.Strength), std::max(z, 1e-4f)));

            glm::vec3 N = sample.GeometricNormal;
            if (glm::dot(N, N) > 1e-12f) N = glm::normalize(N);
            else N = glm::vec3(0, 0, 1);

            glm::vec3 T = glm::vec3(sample.Tangent) - N * glm::dot(N, glm::vec3(sample.Tangent));
            if (glm::dot(T, T) > 1e-12f) {
                T = glm::normalize(T);
            } else {
                T = std::abs(N.z) < 0.99f ? glm::normalize(glm::cross(glm::vec3(0, 0, 1), N))
                                          : glm::normalize(glm::cross(glm::vec3(0, 1, 0), N));
            }

            const glm::vec3 B = glm::cross(N, T) * (sample.Tangent.w < 0.0f ? -1.0f : 1.0f);
            const glm::vec3 perturbed = T * tangentN.x + B * tangentN.y + N * tangentN.z;
            return glm::dot(perturbed, perturbed) > 1e-12f ? glm::normalize(perturbed) : N;
        }

        bool VerifyNormalMapOcclusionStability(const NormalMapSample& sample, const glm::mat4& viewMatrix,
                                               const glm::vec3& viewDir, const glm::vec3& lightDirView) {
            for (int c = 0; c < 3; ++c)
                if (!std::isfinite(viewDir[c]) || !std::isfinite(lightDirView[c])) return false;
            if (glm::length(viewDir) < 1e-6f || glm::length(lightDirView) < 1e-6f) return false;
            const auto V = glm::normalize(viewDir);
            const auto L = glm::normalize(lightDirView);
            const glm::vec3 worldN = EvaluateTangentSpaceNormal(sample);
            for (int c = 0; c < 3; ++c) if (!std::isfinite(worldN[c])) return false;

            const glm::vec3 viewN = glm::mat3(viewMatrix) * worldN;
            for (int c = 0; c < 3; ++c) if (!std::isfinite(viewN[c])) return false;
            const float viewNLen = glm::length(viewN);
            if (viewNLen < 1e-6f) return false;
            const glm::vec3 normViewN = viewN / viewNLen;

            // Check grazing angle / backfacing normal perturbation:
            // When normal map perturbs normal away from view (NdotV <= 0),
            // GTAO contract falls back to neutral ao = 1.0 to prevent black faceting.
            const float NdotV = glm::dot(normViewN, V);
            if (NdotV > 0.0f) {
                const auto axis = std::abs(V.y) < .9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                const glm::vec3 orthoAxis = glm::normalize(glm::cross(V, axis));
                const glm::vec3 projN = normViewN - orthoAxis * glm::dot(normViewN, orthoAxis);
                const float projLen = glm::length(projN);
                if (projLen >= 1e-4f) {
                    const float gamma = std::atan2(glm::dot(projN / projLen, glm::cross(orthoAxis, V)),
                                                   std::max(glm::dot(projN / projLen, V), 1e-6f));
                    if (!std::isfinite(gamma)) return false;
                }
            }

            // Contact shadow ray launch validity:
            const float NdotL = glm::dot(normViewN, L);
            const float bias = 0.01f;
            const glm::vec3 rayOffset = normViewN * (bias * 2.0f);
            for (int c = 0; c < 3; ++c) if (!std::isfinite(rayOffset[c])) return false;

            // Shading composition with normal-mapped lighting components:
            ShadingComponents comp{};
            comp.Ambient = glm::vec3(0.15f);
            comp.SunDiffuse = glm::vec3(0.8f) * std::max(0.0f, NdotL);
            comp.SunSpecular = glm::vec3(0.4f);
            comp.Emissive = glm::vec3(0.05f);
            comp.HasSun = true;
            comp.EnableAO = true;
            comp.EnableContactShadows = true;

            const glm::vec3 rad0 = EvaluateShading(comp, 0.0f, 1.0f);
            const glm::vec3 rad1 = EvaluateShading(comp, 1.0f, 1.0f);
            for (int c = 0; c < 3; ++c) {
                if (!std::isfinite(rad0[c]) || !std::isfinite(rad1[c])) return false;
            }
            if (glm::distance(rad1 - rad0, comp.Ambient) > 1e-5f) return false;

            return true;
        }

        void EvaluateDeformedVertex(const DeformedVertexSample& sample,
                                    glm::vec3& outPosition, glm::vec3& outNormal, glm::vec4& outTangent) {
            outPosition = glm::vec3(sample.DeformationTransform * glm::vec4(sample.RestPosition, 1.0f));

            const glm::mat3 M = glm::mat3(sample.DeformationTransform);
            const glm::vec3 a = M * glm::vec3(1, 0, 0);
            const glm::vec3 b = M * glm::vec3(0, 1, 0);
            const glm::vec3 c = M * glm::vec3(0, 0, 1);
            const float det = glm::dot(a, glm::cross(b, c));
            const float handedness = det < 0.0f ? -1.0f : 1.0f;

            const glm::vec3 nTrans = (glm::cross(b, c) * sample.RestNormal.x +
                                      glm::cross(c, a) * sample.RestNormal.y +
                                      glm::cross(a, b) * sample.RestNormal.z) * handedness;
            outNormal = glm::dot(nTrans, nTrans) > 1e-12f ? glm::normalize(nTrans) : glm::vec3(0, 1, 0);

            const glm::vec3 tTrans = M * glm::vec3(sample.RestTangent);
            outTangent = glm::vec4(glm::dot(tTrans, tTrans) > 1e-12f ? glm::normalize(tTrans) : glm::vec3(1, 0, 0),
                                   sample.RestTangent.w * handedness);
        }

        bool VerifyDeformedMeshOcclusionStability(const DeformedVertexSample& sample, float backgroundDepth,
                                                 const glm::mat4& viewMatrix, const glm::mat4& projMatrix) {
            if (!std::isfinite(backgroundDepth) || backgroundDepth <= 0) return false;
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
                if (!std::isfinite(projMatrix[c][r]) || !std::isfinite(viewMatrix[c][r]) ||
                    !std::isfinite(sample.DeformationTransform[c][r])) return false;
            const float determinant = glm::determinant(projMatrix);
            if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f) return false;
            glm::vec3 P{0.0f}, N{0.0f};
            glm::vec4 T{0.0f};
            EvaluateDeformedVertex(sample, P, N, T);

            for (int c = 0; c < 3; ++c) {
                if (!std::isfinite(P[c]) || !std::isfinite(N[c]) || !std::isfinite(T[c])) return false;
            }
            if (!std::isfinite(T.w)) return false;
            if (std::abs(glm::dot(N, glm::vec3(T))) > 1e-3f) return false;

            const glm::vec3 Pview = glm::vec3(viewMatrix * glm::vec4(P, 1.0f));
            const float Z = -Pview.z;
            if (!std::isfinite(Z) || Z <= 0.0f) return false;

            const auto clip = projMatrix * glm::vec4(Pview, 1);
            if (!std::isfinite(clip.w) || clip.w <= 1e-6f) return false;
            const auto reconstructed = glm::inverse(projMatrix) * glm::vec4(glm::vec3(clip) / clip.w, 1);
            if (!std::isfinite(reconstructed.w) || std::abs(reconstructed.w) < 1e-6f) return false;
            const auto error = glm::distance(glm::vec3(reconstructed) / reconstructed.w, Pview);
            if (!std::isfinite(error) || error > 1e-4f * std::max(1.0f, glm::length(Pview))) return false;

            // Bilateral filter edge rejection across disocclusion boundaries
            const auto bgTap = EvaluateBilateralWeight(Z, backgroundDepth, 1, 0);
            if (!bgTap.IsEdgeRejected || bgTap.DepthWeight > 0.01f) return false;

            // Bilateral filter continuous reconstruction on surface
            const auto contTap = EvaluateBilateralWeight(Z, Z, 1, 0);
            if (contTap.IsEdgeRejected || contTap.TotalWeight <= 0.0f) return false;

            return true;
        }

        float EvaluateGTAODistanceFalloff(float distance, float worldRadius, float falloffStartFraction) {
            if (!std::isfinite(distance) || !std::isfinite(worldRadius) || !std::isfinite(falloffStartFraction) ||
                distance <= 0.0f || worldRadius <= 0.0f) {
                return 0.0f;
            }
            const float safeRadius = std::max(worldRadius, 0.05f);
            if (distance >= safeRadius) return 0.0f;
            const float startFrac = std::clamp(falloffStartFraction, 0.05f, 0.95f);
            const float startDist = safeRadius * startFrac;
            const float t = std::clamp((safeRadius - distance) / std::max(safeRadius - startDist, 1e-4f), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float ComputeSpecularOcclusion(float NdotV, float ao, float roughness) {
            if (!std::isfinite(NdotV) || !std::isfinite(ao) || !std::isfinite(roughness)) return 1.0f;
            const float safeAO = std::clamp(ao, 0.0f, 1.0f);
            const float safeNdotV = std::clamp(NdotV, 0.0f, 1.0f);
            const float safeRoughness = std::clamp(roughness, 0.04f, 1.0f);
            const float exponent = std::exp2(-16.0f * safeRoughness - 1.0f);
            const float term = std::pow(std::max(safeNdotV + safeAO, 1e-5f), exponent) - 1.0f + safeAO;
            return std::clamp(term, 0.0f, 1.0f);
        }

        glm::vec3 ComputeMultiBounceAO(float ao, const glm::vec3& albedo, float strength) {
            if (!std::isfinite(ao) || !std::isfinite(strength)) return glm::vec3(1.0f);
            const float safeAO = std::clamp(ao, 0.0f, 1.0f);
            const float safeStrength = std::clamp(strength, 0.0f, 1.0f);
            const glm::vec3 safeAlbedo = glm::clamp(glm::vec3(
                std::isfinite(albedo.x) ? albedo.x : 0.0f,
                std::isfinite(albedo.y) ? albedo.y : 0.0f,
                std::isfinite(albedo.z) ? albedo.z : 0.0f), glm::vec3(0.0f), glm::vec3(1.0f));
            const glm::vec3 a = 2.0404f * safeAlbedo - 0.3324f;
            const glm::vec3 b = -4.7951f * safeAlbedo + 0.6417f;
            const glm::vec3 c = 2.7552f * safeAlbedo + 0.6903f;
            const glm::vec3 bounced = glm::clamp(glm::max(glm::vec3(safeAO), ((safeAO * a + b) * safeAO + c) * safeAO),
                                                 glm::vec3(0.0f), glm::vec3(1.0f));
            return glm::mix(glm::vec3(safeAO), bounced, safeStrength);
        }

        NormalDepthBilateralTapResult EvaluateNormalDepthBilateralWeight(
            const glm::vec3& centerPos, const glm::vec3& centerNormal,
            const glm::vec3& tapPos, const glm::vec3& tapNormal, bool hasTapNormal,
            const glm::vec2& deltaFullResPx) {
            NormalDepthBilateralTapResult result{};
            for (int i = 0; i < 3; ++i) {
                if (!std::isfinite(centerPos[i]) || !std::isfinite(centerNormal[i]) ||
                    !std::isfinite(tapPos[i]) || (hasTapNormal && !std::isfinite(tapNormal[i]))) {
                    result.IsEdgeRejected = true;
                    return result;
                }
            }
            if (!std::isfinite(deltaFullResPx.x) || !std::isfinite(deltaFullResPx.y) ||
                glm::dot(centerNormal, centerNormal) < 1e-8f) {
                result.IsEdgeRejected = true;
                return result;
            }
            const glm::vec3 nCenter = glm::normalize(centerNormal);
            result.SpatialWeight = std::exp(-glm::dot(deltaFullResPx, deltaFullResPx) / (2.0f * 2.8f * 2.8f));

            const float centerZ = std::abs(centerPos.z);
            const float depthDiff = std::abs(centerZ - std::abs(tapPos.z));
            const float planeDist = std::abs(glm::dot(tapPos - centerPos, nCenter));
            const float effectiveDist = std::min(planeDist, depthDiff);
            const float depthSigma = 0.02f * std::max(centerZ, 0.1f) + 0.005f;
            result.DepthWeight = std::exp(-effectiveDist / depthSigma);

            float normalWeight = 1.0f;
            if (hasTapNormal && glm::dot(tapNormal, tapNormal) >= 1e-8f) {
                const float ndot = glm::dot(nCenter, glm::normalize(tapNormal));
                if (ndot < 0.25f) {
                    result.NormalWeight = 0.0f;
                    result.IsEdgeRejected = true;
                    return result;
                }
                normalWeight = std::pow(std::clamp(ndot, 0.0f, 1.0f), 12.0f);
            }
            result.NormalWeight = normalWeight;

            if (result.DepthWeight < 1e-3f) {
                result.IsEdgeRejected = true;
                return result;
            }
            result.TotalWeight = result.SpatialWeight * result.DepthWeight * result.NormalWeight;
            return result;
        }

    } // namespace OcclusionContract

    // =========================================================================
    // 9. Hi-Z Depth Pyramid Contracts (M15-U5-A)
    // =========================================================================
    namespace HiZContract {

        uint32_t ComputeUsefulLevelCount(uint32_t fullWidth, uint32_t fullHeight) {
            return RHI::RHIViewTargets::ComputeUsefulHiZLevelCount(fullWidth, fullHeight);
        }

        uint32_t ComputeAllocatedLevelCount(uint32_t fullWidth, uint32_t fullHeight) {
            return RHI::RHIViewTargets::ComputeAllocatedHiZLevelCount(fullWidth, fullHeight);
        }

        PyramidExtent ComputeLevelExtent(uint32_t fullWidth, uint32_t fullHeight, uint32_t level) {
            if (fullWidth == 0 || fullHeight == 0 || level >= MaxLevels) return {};
            return PyramidExtent{
                std::max(1u, fullWidth >> level),
                std::max(1u, fullHeight >> level)
            };
        }

        std::string_view GetDepthConventionSummary() {
            return "Modern RHI depth [0.0, 1.0]: 0.0 is near plane, 1.0 is far/sky, and Hi-Z stores the conservative minimum depth (nearest blocker) across covered texels.";
        }

        std::string_view GetEdgePolicySummary() {
            return "Level extents halve via max(1u, prev >> 1). When source extent > 1 is odd, the final destination column/row folds source coordinate (2 * dst + 2 = src - 1) into the min reduction (up to 3x3 at bottom-right corners); out-of-range taps on 1-pixel axes clamp to src - 1.";
        }

        bool BuildMinReductionPyramid(const std::vector<float>& depthLevel0,
                                      uint32_t width, uint32_t height, uint32_t levelCount,
                                      std::vector<std::vector<float>>& outLevels,
                                      std::vector<PyramidExtent>& outExtents) {
            outLevels.clear();
            outExtents.clear();
            if (width == 0 || height == 0 || levelCount == 0 || levelCount > MaxLevels) return false;
            const size_t expectedCount = size_t(width) * size_t(height);
            if (depthLevel0.size() != expectedCount) return false;
            for (float d : depthLevel0) {
                if (!std::isfinite(d) || d < NearDepth || d > FarDepth) return false;
            }

            outLevels.reserve(levelCount);
            outExtents.reserve(levelCount);
            outLevels.push_back(depthLevel0);
            outExtents.push_back({width, height});

            for (uint32_t level = 1; level < levelCount; ++level) {
                const auto& src = outLevels[level - 1];
                const auto srcExt = outExtents[level - 1];
                const uint32_t dstW = std::max(1u, srcExt.Width >> 1);
                const uint32_t dstH = std::max(1u, srcExt.Height >> 1);
                std::vector<float> dst(size_t(dstW) * size_t(dstH), FarDepth);

                if (srcExt.Width == 1 && srcExt.Height == 1) {
                    dst[0] = src[0];
                } else {
                    auto loadSrc = [&](uint32_t sx, uint32_t sy) {
                        const uint32_t cx = std::min(sx, srcExt.Width - 1u);
                        const uint32_t cy = std::min(sy, srcExt.Height - 1u);
                        return src[size_t(cy) * srcExt.Width + cx];
                    };
                    const bool oddW = srcExt.Width > 1u && (srcExt.Width & 1u) != 0u;
                    const bool oddH = srcExt.Height > 1u && (srcExt.Height & 1u) != 0u;
                    for (uint32_t y = 0; y < dstH; ++y) {
                        const bool extraY = oddH && (y + 1u == dstH);
                        const uint32_t maxOY = extraY ? 2u : 1u;
                        for (uint32_t x = 0; x < dstW; ++x) {
                            const bool extraX = oddW && (x + 1u == dstW);
                            const uint32_t maxOX = extraX ? 2u : 1u;
                            float minVal = FarDepth;
                            for (uint32_t oy = 0; oy <= maxOY; ++oy) {
                                for (uint32_t ox = 0; ox <= maxOX; ++ox) {
                                    minVal = std::min(minVal, loadSrc(x * 2u + ox, y * 2u + oy));
                                }
                            }
                            dst[size_t(y) * dstW + x] = minVal;
                        }
                    }
                }
                outLevels.push_back(std::move(dst));
                outExtents.push_back({dstW, dstH});
            }
            return true;
        }

        bool VerifyConservativeMinReduction(const std::vector<float>& depthLevel0,
                                            uint32_t width, uint32_t height) {
            const uint32_t useful = ComputeUsefulLevelCount(width, height);
            if (useful == 0) return false;
            std::vector<std::vector<float>> levels;
            std::vector<PyramidExtent> extents;
            if (!BuildMinReductionPyramid(depthLevel0, width, height, useful, levels, extents)) return false;

            const auto& lastExt = extents.back();
            if (lastExt.Width != 1 || lastExt.Height != 1) return false;

            float baseMin = FarDepth;
            for (float d : levels[0]) baseMin = std::min(baseMin, d);

            for (size_t l = 1; l < levels.size(); ++l) {
                const auto srcExt = extents[l - 1];
                const auto dstExt = extents[l];
                const bool oddW = srcExt.Width > 1u && (srcExt.Width & 1u) != 0u;
                const bool oddH = srcExt.Height > 1u && (srcExt.Height & 1u) != 0u;

                float levelMin = FarDepth;
                for (uint32_t y = 0; y < dstExt.Height; ++y) {
                    const uint32_t syEnd = (oddH && y + 1u == dstExt.Height) ? (srcExt.Height - 1u) : std::min(srcExt.Height - 1u, y * 2u + 1u);
                    for (uint32_t x = 0; x < dstExt.Width; ++x) {
                        const uint32_t sxEnd = (oddW && x + 1u == dstExt.Width) ? (srcExt.Width - 1u) : std::min(srcExt.Width - 1u, x * 2u + 1u);
                        const float coarseVal = levels[l][size_t(y) * dstExt.Width + x];
                        if (!std::isfinite(coarseVal) || coarseVal < NearDepth || coarseVal > FarDepth) return false;
                        levelMin = std::min(levelMin, coarseVal);
                        for (uint32_t sy = y * 2u; sy <= syEnd; ++sy) {
                            for (uint32_t sx = x * 2u; sx <= sxEnd; ++sx) {
                                if (coarseVal > levels[l - 1][size_t(sy) * srcExt.Width + sx] + 1e-6f) {
                                    return false;
                                }
                            }
                        }
                    }
                }
                if (std::abs(levelMin - baseMin) > 1e-6f) return false;
            }
            return true;
        }

        bool VerifyOddResolutionPyramid(uint32_t width, uint32_t height) {
            const uint32_t useful = ComputeUsefulLevelCount(width, height);
            const uint32_t allocated = ComputeAllocatedLevelCount(width, height);
            if (useful == 0 || allocated < SSRBindingLevels || allocated < useful) return false;
            if (ComputeLevelExtent(width, height, useful - 1) != PyramidExtent{1, 1}) return false;

            std::vector<float> testDepth(size_t(width) * size_t(height), 0.85f);
            // Place nearest blocker strictly on the bottom-right odd border corner.
            testDepth[size_t(height - 1u) * width + (width - 1u)] = 0.04f;
            if (!VerifyConservativeMinReduction(testDepth, width, height)) return false;

            std::vector<std::vector<float>> levels;
            std::vector<PyramidExtent> extents;
            if (!BuildMinReductionPyramid(testDepth, width, height, allocated, levels, extents)) return false;
            for (uint32_t l = 0; l < allocated; ++l) {
                if (extents[l] != ComputeLevelExtent(width, height, l)) return false;
                if (levels[l].back() != 0.04f) return false;
            }
            return true;
        }

    } // namespace HiZContract

    // =========================================================================
    // Reflection Capture Contract (M15-U5-B)
    // =========================================================================
    namespace ReflectionCaptureContract {
        namespace {
            bool IsFiniteVec3(const glm::vec3& v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            }

            void SetReason(std::string* outReason, const char* msg) {
                if (outReason) {
                    *outReason = msg;
                }
            }
        } // namespace

        CubeFaceBasis GetCubeFaceBasis(uint32_t faceIndex) {
            CubeFaceBasis basis;
            basis.FaceIndex = faceIndex;
            basis.FlipHorizontalForCubemapCopy = true;
            switch (faceIndex) {
                case 0u: // +X
                    basis.Forward = { 1.0f, 0.0f, 0.0f };
                    basis.Up = { 0.0f, 1.0f, 0.0f };
                    basis.Right = { 0.0f, 0.0f, -1.0f };
                    break;
                case 1u: // -X
                    basis.Forward = { -1.0f, 0.0f, 0.0f };
                    basis.Up = { 0.0f, 1.0f, 0.0f };
                    basis.Right = { 0.0f, 0.0f, 1.0f };
                    break;
                case 2u: // +Y
                    basis.Forward = { 0.0f, 1.0f, 0.0f };
                    basis.Up = { 0.0f, 0.0f, -1.0f };
                    basis.Right = { 1.0f, 0.0f, 0.0f };
                    break;
                case 3u: // -Y
                    basis.Forward = { 0.0f, -1.0f, 0.0f };
                    basis.Up = { 0.0f, 0.0f, 1.0f };
                    basis.Right = { 1.0f, 0.0f, 0.0f };
                    break;
                case 4u: // +Z
                    basis.Forward = { 0.0f, 0.0f, 1.0f };
                    basis.Up = { 0.0f, 1.0f, 0.0f };
                    basis.Right = { 1.0f, 0.0f, 0.0f };
                    break;
                case 5u: // -Z
                    basis.Forward = { 0.0f, 0.0f, -1.0f };
                    basis.Up = { 0.0f, 1.0f, 0.0f };
                    basis.Right = { -1.0f, 0.0f, 0.0f };
                    break;
                default:
                    basis.Forward = glm::vec3(0.0f);
                    basis.Up = glm::vec3(0.0f);
                    basis.Right = glm::vec3(0.0f);
                    basis.FlipHorizontalForCubemapCopy = false;
                    break;
            }
            return basis;
        }

        bool ValidateCubeFaceBasis(const CubeFaceBasis& basis, std::string* outReason) {
            if (basis.FaceIndex >= FaceCount) {
                SetReason(outReason, "cube face index out of range [0, 5]");
                return false;
            }
            if (!IsFiniteVec3(basis.Forward) || !IsFiniteVec3(basis.Up) || !IsFiniteVec3(basis.Right)) {
                SetReason(outReason, "cube face basis contains non-finite vector components");
                return false;
            }
            const float fLen = glm::length(basis.Forward);
            const float uLen = glm::length(basis.Up);
            const float rLen = glm::length(basis.Right);
            if (std::abs(fLen - 1.0f) > 1e-4f || std::abs(uLen - 1.0f) > 1e-4f || std::abs(rLen - 1.0f) > 1e-4f) {
                SetReason(outReason, "cube face basis vectors must have unit length");
                return false;
            }
            if (std::abs(glm::dot(basis.Forward, basis.Up)) > 1e-4f ||
                std::abs(glm::dot(basis.Forward, basis.Right)) > 1e-4f ||
                std::abs(glm::dot(basis.Up, basis.Right)) > 1e-4f) {
                SetReason(outReason, "cube face basis vectors must be mutually orthogonal");
                return false;
            }
            // D3D12/Vulkan TextureCube faces use a left-handed tangent frame: Right == Up x Forward.
            // Rasterization uses right-handed lookAtRH (+X_cam = Forward x Up = -Right), so
            // FlipHorizontalForCubemapCopy must be enabled to align captured faces into TextureCube.
            const glm::vec3 expectedCubemapRight = glm::cross(basis.Up, basis.Forward);
            if (glm::dot(expectedCubemapRight, basis.Right) <= 0.999f) {
                SetReason(outReason, "cube face right vector does not match left-handed cubemap tangent (Up x Forward)");
                return false;
            }
            if (!basis.FlipHorizontalForCubemapCopy) {
                SetReason(outReason, "right-handed cubemap capture requires FlipHorizontalForCubemapCopy");
                return false;
            }
            const CubeFaceBasis canonical = GetCubeFaceBasis(basis.FaceIndex);
            if (glm::dot(basis.Forward, canonical.Forward) < 0.9999f ||
                glm::dot(basis.Up, canonical.Up) < 0.9999f ||
                glm::dot(basis.Right, canonical.Right) < 0.9999f) {
                SetReason(outReason, "cube face basis does not match canonical TextureCube face orientation");
                return false;
            }
            SetReason(outReason, "");
            return true;
        }

        bool ValidateCapturePolicy(const ReflectionCapturePolicy& policy, std::string* outReason) {
            if (policy.SampleCount != 1u) {
                SetReason(outReason, "reflection capture target must be single-sample (SampleCount == 1)");
                return false;
            }
            if (policy.ColorFormat != nri::Format::RGBA16_SFLOAT) {
                SetReason(outReason, "reflection capture color target must be linear HDR RGBA16_FLOAT");
                return false;
            }
            if (policy.DepthFormat != nri::Format::D32_SFLOAT) {
                SetReason(outReason, "reflection capture depth target must be D32_FLOAT");
                return false;
            }
            const bool isPowerOfTwo = (policy.Resolution != 0u) && ((policy.Resolution & (policy.Resolution - 1u)) == 0u);
            if (policy.Resolution < MinResolution || policy.Resolution > MaxResolution || !isPowerOfTwo) {
                SetReason(outReason, "reflection capture resolution must be a power of two in [16, 1024]");
                return false;
            }
            if (!std::isfinite(policy.NearPlane) || !std::isfinite(policy.FarPlane) ||
                policy.NearPlane < MinNearPlane || policy.FarPlane <= policy.NearPlane) {
                SetReason(outReason, "reflection capture clip planes must satisfy 0.01 <= NearPlane < FarPlane");
                return false;
            }
            if (policy.AllowSSR || policy.AllowProbeSampling) {
                SetReason(outReason, "reflection capture must isolate SSR and recursive reflection probe sampling");
                return false;
            }
            if (!policy.IsolateMainViewTAA) {
                SetReason(outReason, "reflection capture must isolate main-view TAA history");
                return false;
            }
            if (policy.IncludeEditorOverlays) {
                SetReason(outReason, "reflection capture must exclude Editor overlays, wireframes, and debug lines");
                return false;
            }
            SetReason(outReason, "");
            return true;
        }

        PerspectiveCamera BuildCubeFaceCamera(
            const glm::vec3& probePosition,
            uint32_t faceIndex,
            float nearPlane,
            float farPlane) {
            const CubeFaceBasis basis = GetCubeFaceBasis(faceIndex < FaceCount ? faceIndex : 0u);
            const float safeNear = std::isfinite(nearPlane) ? std::max(nearPlane, MinNearPlane) : 0.1f;
            const float safeFar = (std::isfinite(farPlane) && farPlane > safeNear) ? farPlane : (safeNear + 100.0f);
            PerspectiveCamera cam(FaceFovDegrees, FaceAspectRatio, safeNear, safeFar);
            const glm::mat4 view = glm::lookAtRH(probePosition, probePosition + basis.Forward, basis.Up);
            cam.SetViewMatrix(view, probePosition);
            return cam;
        }

        SceneRenderSettings SanitizeCaptureSettings(
            const SceneRenderSettings& mainSettings,
            const ReflectionCapturePolicy& policy) {
            SceneRenderSettings out = mainSettings;
            out.MSAASamples = 1u;
            out.EnableToneMapping = false;
            out.ToneMappingMode = 3; // Linear bypass (shaders also observe capture linear HDR mode)
            out.EnableFXAA = false;
            out.EnableBloom = false;
            out.EnableAutoExposure = false;
            out.EnableTAA = false;
            out.EnableSSR = policy.AllowSSR;
            out.EnableGTAO = false;
            out.EnableContactShadows = false;
            out.EnableVolumetricFog = false;
            out.EnableDDGI = false;
            out.HiZVisualizationLevel = -1;
            out.Wireframe = policy.IncludeEditorOverlays ? mainSettings.Wireframe : false;
            out.ShowPhysicsDebug = policy.IncludeEditorOverlays ? mainSettings.ShowPhysicsDebug : false;
            out.EnableSkybox = policy.IncludeSkybox && mainSettings.EnableSkybox;
            out.EnableParticles = policy.IncludeParticles && mainSettings.EnableParticles;
            out.EnableShadows = policy.IncludeSelectedShadows && mainSettings.EnableShadows;
            return out;
        }

        RenderWorld FilterCaptureWorld(
            const RenderWorld& sourceWorld,
            const ReflectionCapturePolicy& policy) {
            RenderWorld out;
            out.Camera = sourceWorld.Camera;
            out.DirectionalLight = sourceWorld.DirectionalLight;
            out.AdditionalDirectionalLights = sourceWorld.AdditionalDirectionalLights;
            out.PointLights = sourceWorld.PointLights;
            out.SpotLights = sourceWorld.SpotLights;
            out.RectLights = sourceWorld.RectLights;
            out.Skybox = sourceWorld.Skybox;
            out.Skybox.Enabled = policy.IncludeSkybox && sourceWorld.Skybox.Enabled;
            out.EntityToStableUUID = sourceWorld.EntityToStableUUID;
            out.SceneRevision = sourceWorld.SceneRevision;
            out.SceneInstanceID = sourceWorld.SceneInstanceID;
            out.SceneLifetimeEpoch = sourceWorld.SceneLifetimeEpoch;
            out.TotalEntities = sourceWorld.TotalEntities;

            if (!policy.AllowProbeSampling) {
                out.ReflectionProbes.clear();
            } else {
                out.ReflectionProbes = sourceWorld.ReflectionProbes;
            }
            if (policy.IncludeParticles) {
                out.Particles = sourceWorld.Particles;
            }
            if (policy.IncludeEditorOverlays) {
                out.DebugLines = sourceWorld.DebugLines;
            }

            if (!(policy.IncludeSelectedShadows && policy.IncludeDirectionalShadows)) {
                if (out.DirectionalLight.has_value()) {
                    out.DirectionalLight->CastsShadows = false;
                }
                for (auto& dl : out.AdditionalDirectionalLights) {
                    dl.CastsShadows = false;
                }
            }
            if (!(policy.IncludeSelectedShadows && policy.IncludeLocalShadows)) {
                for (auto& pl : out.PointLights) {
                    pl.Shadows.CastShadows = false;
                }
                for (auto& sl : out.SpotLights) {
                    sl.Shadows.CastShadows = false;
                }
            }

            std::unordered_set<uint64_t> excludedIds;
            if (policy.ExcludeEntityID != 0u) {
                excludedIds.insert(policy.ExcludeEntityID);
            }
            for (uint64_t id : policy.AdditionalExcludedEntityIDs) {
                if (id != 0u) {
                    excludedIds.insert(id);
                }
            }

            if (policy.IncludeOpaqueAndCutout) {
                out.Skins = sourceWorld.Skins;
            }

            out.Meshes.reserve(sourceWorld.Meshes.size());
            for (const auto& mesh : sourceWorld.Meshes) {
                if (mesh.EntityID != 0u && excludedIds.count(static_cast<uint64_t>(mesh.EntityID)) > 0u) {
                    continue;
                }
                const ResolvedSurface surface = MaterialResolver::Resolve(mesh.Material);
                const bool isTransparent =
                    (surface.Surface == SurfaceType::Transparent || surface.Surface == SurfaceType::Additive);
                if (isTransparent && !policy.IncludeTransparent) {
                    continue;
                }
                if (!isTransparent && !policy.IncludeOpaqueAndCutout) {
                    continue;
                }
                out.Meshes.push_back(mesh);
            }

            return out;
        }

        uint32_t ComputePrefilterMipCount(uint32_t resolution) {
            if (resolution == 0u) {
                return 0u;
            }
            uint32_t chainDepth = 0u;
            for (uint32_t dim = resolution; dim > 0u; dim >>= 1u) {
                ++chainDepth;
            }
            return std::min(MaxPrefilterMipCount, std::max(1u, chainDepth));
        }

        float ComputePrefilterRoughness(uint32_t mip, uint32_t mipCount) {
            if (mipCount <= 1u || mip == 0u) {
                return 0.0f;
            }
            const uint32_t clampedMip = std::min(mip, mipCount - 1u);
            return std::clamp(
                static_cast<float>(clampedMip) / static_cast<float>(mipCount - 1u),
                0.0f, 1.0f);
        }

        bool ValidatePrefilterPolicy(const ReflectionPrefilterPolicy& policy, std::string* outReason) {
            if (policy.ColorFormat != nri::Format::RGBA16_SFLOAT) {
                SetReason(outReason, "reflection prefilter cubemap must use linear HDR RGBA16_FLOAT");
                return false;
            }
            const bool isPowerOfTwo = (policy.Resolution != 0u) && ((policy.Resolution & (policy.Resolution - 1u)) == 0u);
            if (policy.Resolution < MinResolution || policy.Resolution > MaxResolution || !isPowerOfTwo) {
                SetReason(outReason, "reflection prefilter resolution must be a power of two in [16, 1024]");
                return false;
            }
            uint32_t fullChainDepth = 0u;
            for (uint32_t dim = policy.Resolution; dim > 0u; dim >>= 1u) {
                ++fullChainDepth;
            }
            const uint32_t maxAllowedMips = std::min(MaxPrefilterMipCount, fullChainDepth);
            if (policy.MipCount == 0u || policy.MipCount > maxAllowedMips) {
                SetReason(outReason, "reflection prefilter mip count exceeds allowed [1, 5] cube chain");
                return false;
            }
            if (policy.SampleCount < MinPrefilterSampleCount || policy.SampleCount > MaxPrefilterSampleCount) {
                SetReason(outReason, "reflection prefilter sample count out of bounds [16, 1024]");
                return false;
            }
            SetReason(outReason, "");
            return true;
        }

        glm::vec3 ComputeCubeTexelDirection(uint32_t faceIndex, float u, float v) {
            if (faceIndex >= FaceCount || !std::isfinite(u) || !std::isfinite(v)) {
                return glm::vec3(0.0f);
            }
            const CubeFaceBasis basis = GetCubeFaceBasis(faceIndex);
            const float x = 2.0f * u - 1.0f;
            const float y = 2.0f * v - 1.0f;
            return glm::normalize(basis.Forward + x * basis.Right - y * basis.Up);
        }

        glm::vec3 SampleCubemapBilinear3D(
            const std::array<std::vector<glm::vec3>, FaceCount>& faces,
            uint32_t faceResolution,
            const glm::vec3& direction) {
            if (faceResolution == 0u) {
                return glm::vec3(0.0f);
            }
            const size_t expectedPixels = static_cast<size_t>(faceResolution) * faceResolution;
            for (const auto& face : faces) {
                if (face.size() != expectedPixels) {
                    return glm::vec3(0.0f);
                }
            }
            const float len = glm::length(direction);
            if (!std::isfinite(len) || len <= 1e-7f) {
                return glm::vec3(0.0f);
            }
            const glm::vec3 d = direction / len;
            const float absX = std::abs(d.x);
            const float absY = std::abs(d.y);
            const float absZ = std::abs(d.z);

            uint32_t face = 0u;
            float sc = 0.0f;
            float tc = 0.0f;
            float ma = 1.0f;
            if (absX >= absY && absX >= absZ) {
                ma = absX;
                if (d.x >= 0.0f) {
                    face = 0u; sc = -d.z; tc = -d.y;
                } else {
                    face = 1u; sc = d.z; tc = -d.y;
                }
            } else if (absY >= absX && absY >= absZ) {
                ma = absY;
                if (d.y >= 0.0f) {
                    face = 2u; sc = d.x; tc = d.z;
                } else {
                    face = 3u; sc = d.x; tc = -d.z;
                }
            } else {
                ma = absZ;
                if (d.z >= 0.0f) {
                    face = 4u; sc = d.x; tc = -d.y;
                } else {
                    face = 5u; sc = -d.x; tc = -d.y;
                }
            }

            const float u = std::clamp(0.5f * (sc / ma + 1.0f), 0.0f, 1.0f);
            const float v = std::clamp(0.5f * (tc / ma + 1.0f), 0.0f, 1.0f);
            const float fx = u * static_cast<float>(faceResolution) - 0.5f;
            const float fy = v * static_cast<float>(faceResolution) - 0.5f;
            const int x0 = static_cast<int>(std::floor(fx));
            const int y0 = static_cast<int>(std::floor(fy));
            const int x1 = x0 + 1;
            const int y1 = y0 + 1;
            const float tx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
            const float ty = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);

            const auto tap = [&](int x, int y) {
                if (x >= 0 && y >= 0 && x < int(faceResolution) && y < int(faceResolution))
                    return faces[face][size_t(y) * faceResolution + x];
                const auto faceTap = [&](const glm::vec3& dir, uint32_t f) {
                    const auto basis = GetCubeFaceBasis(f);
                    const float major = glm::dot(dir, basis.Forward);
                    const int ix = std::clamp(int((glm::dot(dir, basis.Right) / major + 1) * .5f * faceResolution), 0, int(faceResolution)-1);
                    const int iy = std::clamp(int((1 - glm::dot(dir, basis.Up) / major) * .5f * faceResolution), 0, int(faceResolution)-1);
                    return faces[f][size_t(iy) * faceResolution + ix];
                };
                if ((x < 0 || x >= int(faceResolution)) && (y < 0 || y >= int(faceResolution))) {
                    // A cube corner meets three faces. Average their corner texels
                    // symmetrically so the limit is independent of the major face.
                    const auto corner = ComputeCubeTexelDirection(face, x < 0 ? 0.0f : 1.0f, y < 0 ? 0.0f : 1.0f);
                    glm::vec3 sum(0);
                    for (uint32_t f = 0; f < FaceCount; ++f)
                        if (glm::dot(corner, GetCubeFaceBasis(f).Forward) > 0) sum += faceTap(corner, f);
                    return sum / 3.0f;
                }
                // Extend the bilinear footprint across the cube edge, rather
                // than clamping it to one face and introducing a reference seam.
                const auto direction = ComputeCubeTexelDirection(face, (x + .5f) / faceResolution, (y + .5f) / faceResolution);
                uint32_t adjacent = 0;
                float major = -1;
                for (uint32_t f = 0; f < FaceCount; ++f) {
                    const float alignment = glm::dot(direction, GetCubeFaceBasis(f).Forward);
                    if (alignment > major) { major = alignment; adjacent = f; }
                }
                return faceTap(direction, adjacent);
            };
            const glm::vec3 c00 = tap(x0, y0), c10 = tap(x1, y0);
            const glm::vec3 c01 = tap(x0, y1), c11 = tap(x1, y1);
            return glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);
        }

        glm::vec3 EvaluatePrefilterDirectionCPU(
            const std::array<std::vector<glm::vec3>, FaceCount>& sourceFaces,
            uint32_t sourceResolution,
            const glm::vec3& normalDir,
            float roughness,
            uint32_t sampleCount) {
            if (sourceResolution == 0u || sampleCount == 0u || !IsFiniteVec3(normalDir) || !std::isfinite(roughness)) {
                return glm::vec3(0.0f);
            }
            const float nLen = glm::length(normalDir);
            if (!std::isfinite(nLen) || nLen <= 1e-7f) {
                return glm::vec3(0.0f);
            }
            const glm::vec3 N = normalDir / nLen;
            const float clampedRough = std::clamp(roughness, 0.0f, 1.0f);
            if (clampedRough <= 0.0f) {
                return glm::max(SampleCubemapBilinear3D(sourceFaces, sourceResolution, N), glm::vec3(0.0f));
            }

            const auto radicalInverse = [](uint32_t bits) -> float {
                bits = (bits << 16u) | (bits >> 16u);
                bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
                bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
                bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
                bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
                return static_cast<float>(bits) * 2.3283064365386963e-10f;
            };

            const glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 tangent = glm::normalize(glm::cross(up, N));
            const glm::vec3 bitangent = glm::cross(N, tangent);
            const float a = clampedRough * clampedRough;
            const float a2 = a * a;
            constexpr float kPi = 3.14159265358979323846f;

            glm::vec3 prefiltered(0.0f);
            float totalWeight = 0.0f;
            for (uint32_t i = 0u; i < sampleCount; ++i) {
                const float xiX = static_cast<float>(i) / static_cast<float>(sampleCount);
                const float xiY = radicalInverse(i);
                const float cosTheta = std::sqrt((1.0f - xiY) / std::max(1.0f + (a2 - 1.0f) * xiY, 1e-6f));
                const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
                const float phi = 2.0f * kPi * xiX;
                const glm::vec3 H = glm::normalize(
                    tangent * (std::cos(phi) * sinTheta) +
                    bitangent * (std::sin(phi) * sinTheta) +
                    N * cosTheta);
                const glm::vec3 L = glm::normalize(2.0f * glm::dot(N, H) * H - N);
                const float NdotL = glm::dot(N, L);
                if (NdotL > 0.0f) {
                    const glm::vec3 radiance = glm::max(SampleCubemapBilinear3D(sourceFaces, sourceResolution, L), glm::vec3(0.0f));
                    prefiltered += radiance * NdotL;
                    totalWeight += NdotL;
                }
            }
            return totalWeight > 0.0f ? (prefiltered / totalWeight) : glm::vec3(0.0f);
        }

        uint64_t EstimateCubemapTextureBytes(
            uint32_t resolution,
            uint32_t mipCount,
            nri::Format colorFormat) {
            if (resolution == 0u || mipCount == 0u) {
                return 0ull;
            }
            const uint64_t bytesPerTexel = (colorFormat == nri::Format::RGBA32_SFLOAT) ? 16ull : 8ull;
            uint64_t totalTexelsPerFace = 0ull;
            uint32_t dim = resolution;
            for (uint32_t mip = 0u; mip < mipCount; ++mip) {
                totalTexelsPerFace += static_cast<uint64_t>(dim) * dim;
                dim = std::max(1u, dim >> 1u);
            }
            return totalTexelsPerFace * FaceCount * bytesPerTexel;
        }

        uint64_t EstimateDynamicProbeCacheBytes(
            uint32_t resolution,
            uint32_t mipCount,
            nri::Format colorFormat) {
            // Each dynamic probe generation owns both its raw source cubemap and its GGX prefiltered cubemap.
            return 2ull * EstimateCubemapTextureBytes(resolution, mipCount, colorFormat);
        }

        bool ComputeLRUCacheEvictionPlan(
            const std::vector<CaptureCacheSlotInfo>& entries,
            uint64_t budgetBytes,
            uint64_t incomingBytes,
            std::vector<std::string>* outEvictKeys,
            std::string* outReason) {
            if (outEvictKeys) outEvictKeys->clear();
            if (incomingBytes > budgetBytes) {
                SetReason(outReason, "single dynamic probe capture exceeds cache budget");
                return false;
            }

            uint64_t totalExistingBytes = 0ull;
            std::vector<const CaptureCacheSlotInfo*> evictable;
            evictable.reserve(entries.size());
            for (const auto& entry : entries) {
                totalExistingBytes += entry.ByteSize;
                if (!entry.PinProtected) {
                    evictable.push_back(&entry);
                }
            }

            if (totalExistingBytes + incomingBytes <= budgetBytes) {
                SetReason(outReason, "");
                return true;
            }

            std::sort(evictable.begin(), evictable.end(),
                [](const CaptureCacheSlotInfo* a, const CaptureCacheSlotInfo* b) {
                    if (a->LastUsedSequence != b->LastUsedSequence) {
                        return a->LastUsedSequence < b->LastUsedSequence;
                    }
                    return a->ProbeKey < b->ProbeKey;
                });

            uint64_t currentBytes = totalExistingBytes;
            for (const CaptureCacheSlotInfo* candidate : evictable) {
                if (currentBytes + incomingBytes <= budgetBytes) {
                    break;
                }
                currentBytes = (candidate->ByteSize <= currentBytes) ? (currentBytes - candidate->ByteSize) : 0ull;
                if (outEvictKeys) {
                    outEvictKeys->push_back(candidate->ProbeKey);
                }
            }

            if (currentBytes + incomingBytes > budgetBytes) {
                if (outEvictKeys) outEvictKeys->clear();
                SetReason(outReason, "insufficient evictable cache capacity under active pins");
                return false;
            }
            SetReason(outReason, "");
            return true;
        }

        std::vector<ScheduledCaptureAllocation> ComputeFrameCaptureSchedule(
            const std::vector<ScheduledCaptureWork>& queue,
            uint32_t maxProbesPerFrame,
            uint32_t maxFacesPerFrame) {
            std::vector<ScheduledCaptureAllocation> plan;
            if (queue.empty() || maxProbesPerFrame == 0u || maxFacesPerFrame == 0u) {
                return plan;
            }

            std::vector<ScheduledCaptureWork> ordered = queue;
            std::stable_sort(ordered.begin(), ordered.end(),
                [](const ScheduledCaptureWork& a, const ScheduledCaptureWork& b) {
                    return a.PriorityScore > b.PriorityScore;
                });

            uint32_t remainingFacesBudget = maxFacesPerFrame;
            for (const auto& item : ordered) {
                if (plan.size() >= maxProbesPerFrame || remainingFacesBudget == 0u) {
                    break;
                }
                if (item.RemainingFaces == 0u) {
                    continue;
                }
                const uint32_t facesToRun = std::min(item.RemainingFaces, remainingFacesBudget);
                plan.push_back(ScheduledCaptureAllocation{item.ProbeKey, facesToRun});
                remainingFacesBudget -= facesToRun;
            }
            return plan;
        }
    } // namespace ReflectionCaptureContract

    namespace SSRRaymarchContract {
        static bool IsFiniteVec3(const glm::vec3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
        glm::vec3 ProjectViewToUVZ(const glm::vec3& viewPos, const glm::mat4& projZO) {
            const glm::vec4 clip = projZO * glm::vec4(viewPos, 1.0f);
            const float invW = 1.0f / std::max(std::abs(clip.w), 1e-6f);
            const glm::vec3 ndc = glm::vec3(clip) * invW;
            return glm::vec3(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f, ndc.z);
        }

        glm::vec3 ReconstructViewPos(const glm::vec2& uv, float depth, const glm::mat4& invProjZO) {
            const glm::vec4 clip(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
            const glm::vec4 viewPos = invProjZO * clip;
            const float invW = 1.0f / std::max(std::abs(viewPos.w), 1e-6f);
            return glm::vec3(viewPos) * invW;
        }

        glm::uvec2 ComputeHiZMipSize(uint32_t baseWidth, uint32_t baseHeight, uint32_t mip) {
            return glm::uvec2(
                std::max(1u, baseWidth >> std::min(mip, 15u)),
                std::max(1u, baseHeight >> std::min(mip, 15u)));
        }

        bool ClipScreenRay(const glm::vec3& startUVZ, const glm::vec3& endUVZ, float* outTMax) {
            if (!outTMax) return false;
            *outTMax = 0;
            // This contract returns an exit parameter only; the start must
            // already be inside the frustum, including after normal bias.
            if (!IsFiniteVec3(startUVZ) || !IsFiniteVec3(endUVZ) ||
                startUVZ.x < 0 || startUVZ.x > 1 || startUVZ.y < 0 || startUVZ.y > 1 ||
                startUVZ.z < 0 || startUVZ.z > .9999f) return false;
            float tMin = 0.0f;
            float tMax = 1.0f;
            const glm::vec3 d = endUVZ - startUVZ;
            const float eps = 1e-7f;
            const float mins[3]{0.0f, 0.0f, 0.0f};
            const float maxs[3]{1.0f, 1.0f, 0.9999f};
            for (int c = 0; c < 3; ++c) {
                if (std::abs(d[c]) < eps) {
                    if (startUVZ[c] < mins[c] || startUVZ[c] > maxs[c]) {
                        *outTMax = 0.0f;
                        return false;
                    }
                } else {
                    float t0 = (mins[c] - startUVZ[c]) / d[c];
                    float t1 = (maxs[c] - startUVZ[c]) / d[c];
                    if (t0 > t1) std::swap(t0, t1);
                    tMin = std::max(tMin, t0);
                    tMax = std::min(tMax, t1);
                    if (tMin > tMax) {
                        *outTMax = 0.0f;
                        return false;
                    }
                }
            }
            *outTMax = std::clamp(tMax, 0.0f, 1.0f);
            return *outTMax > 1e-5f;
        }

        RaySetup BuildInitialSSRRay(
            const glm::vec2& uv,
            float depth,
            const glm::vec3& viewNormal,
            const glm::mat4& projZO,
            const glm::mat4& invProjZO,
            uint32_t width,
            uint32_t height,
            uint32_t usefulHiZLevels,
            float maxDistance) {
            RaySetup setup{};
            if (width == 0 || height == 0 || !std::isfinite(depth) || !std::isfinite(maxDistance) ||
                !std::isfinite(uv.x) || !std::isfinite(uv.y) || !IsFiniteVec3(viewNormal) ||
                depth <= 0.0f || depth >= 0.9999f || maxDistance <= 0.0f) {
                return setup;
            }
            const glm::vec3 originView = ReconstructViewPos(uv, depth, invProjZO);
            const float normLen = glm::length(viewNormal);
            const float originLen = glm::length(originView);
            if (!std::isfinite(normLen) || !std::isfinite(originLen) || normLen < 1e-5f || originLen < 1e-5f) {
                return setup;
            }
            const glm::vec3 n = viewNormal / normLen;
            const glm::vec3 vDir = originView / originLen;
            const glm::vec3 reflDir = glm::normalize(glm::reflect(vDir, n));
            if (glm::dot(reflDir, n) <= 1e-4f) return setup;

            const float rcpW = 1.0f / static_cast<float>(width);
            const float rcpH = 1.0f / static_cast<float>(height);
            const float texelWorldSize = std::abs(originView.z) * std::max(
                rcpW / std::max(std::abs(projZO[0][0]), 1e-4f),
                rcpH / std::max(std::abs(projZO[1][1]), 1e-4f));
            const glm::vec3 startView = originView + n * std::max(0.0015f, texelWorldSize * 1.5f);

            const float nearViewZ = std::abs(ReconstructViewPos(glm::vec2(0.5f, 0.5f), 0.0f, invProjZO).z);
            const float safeNear = std::max(nearViewZ * 1.25f, 0.05f);
            if (startView.z >= -safeNear) {
                return setup;
            }

            const float distanceLimit = std::max(maxDistance, 5.0f);
            float traceDist = distanceLimit;
            if (reflDir.z > 1e-5f) {
                traceDist = std::min(traceDist, (-safeNear - startView.z) / reflDir.z);
            }
            if (traceDist <= 0.02f) {
                return setup;
            }

            const glm::vec3 fullEndView = startView + reflDir * traceDist;
            const glm::vec3 rawStartUVZ = ProjectViewToUVZ(startView, projZO);
            const glm::vec3 rawEndUVZ = ProjectViewToUVZ(fullEndView, projZO);
            float tMaxClip = 1.0f;
            if (!ClipScreenRay(rawStartUVZ, rawEndUVZ, &tMaxClip)) {
                return setup;
            }

            setup.OriginView = originView;
            setup.StartView = startView;
            setup.StartUVZ = rawStartUVZ;
            setup.EndUVZ = glm::mix(rawStartUVZ, rawEndUVZ, tMaxClip);
            // UVZ interpolation is projective, not a linear view-space distance.
            setup.EndView = ReconstructViewPos(glm::vec2(setup.EndUVZ), setup.EndUVZ.z, invProjZO);
            setup.MaxTraceDist = traceDist;
            setup.MaxDistance = distanceLimit;
            setup.StartMip = 0u;
            setup.MaxUsefulMip = usefulHiZLevels > 0u ? std::min(usefulHiZLevels - 1u, 7u) : 0u;
            setup.Valid = true;
            return setup;
        }

        HiZTraceResult TraceHiZDepthBuffer(
            const std::vector<std::vector<float>>& hiZPyramid,
            uint32_t width,
            uint32_t height,
            const RaySetup& ray,
            const glm::mat4& projZO,
            const glm::mat4& invProjZO,
            float thickness,
            uint32_t maxSteps) {
            (void)projZO;
            HiZTraceResult out{};
            if (!ray.Valid || hiZPyramid.empty() || width == 0 || height == 0 || maxSteps == 0) {
                return out;
            }
            maxSteps = std::clamp(maxSteps, 16u, 96u);
            const uint32_t maxAvailMip = std::min(
                ray.MaxUsefulMip,
                static_cast<uint32_t>(hiZPyramid.size() - 1u));
            const glm::vec3 dirUVZ = ray.EndUVZ - ray.StartUVZ;
            const float pxLen = std::max(
                1.0f,
                glm::length(glm::vec2(dirUVZ.x * static_cast<float>(width), dirUVZ.y * static_cast<float>(height))));
            const float minOriginT = std::min(0.5f, 1.25f / pxLen);
            float tCurr = minOriginT;
            uint32_t mip = std::min(ray.StartMip, maxAvailMip);
            const float safeThickness = std::max(thickness, 0.05f);

            const auto sampleLevel = [&](uint32_t level, const glm::uvec2& cell) -> float {
                const glm::uvec2 dims = ComputeHiZMipSize(width, height, level);
                if (hiZPyramid[level].size() != size_t(dims.x) * dims.y) return 1.0f;
                const uint32_t cx = std::min(cell.x, dims.x - 1u);
                const uint32_t cy = std::min(cell.y, dims.y - 1u);
                return hiZPyramid[level][size_t(cy) * dims.x + cx];
            };
            const auto trySampleContinuousHiZ0Depth = [&](const glm::vec2& sampleUV, float& outDepth) -> bool {
                const glm::vec2 tc = sampleUV * glm::vec2(static_cast<float>(width), static_cast<float>(height)) - glm::vec2(0.5f);
                const int maxX = static_cast<int>(width) - 1;
                const int maxY = static_cast<int>(height) - 1;
                const int x0 = std::clamp(static_cast<int>(std::floor(tc.x)), 0, maxX);
                const int y0 = std::clamp(static_cast<int>(std::floor(tc.y)), 0, maxY);
                const int x1 = std::min(x0 + 1, maxX);
                const int y1 = std::min(y0 + 1, maxY);
                const float fx = std::clamp(tc.x - static_cast<float>(x0), 0.0f, 1.0f);
                const float fy = std::clamp(tc.y - static_cast<float>(y0), 0.0f, 1.0f);
                const float d00 = sampleLevel(0u, glm::uvec2(x0, y0));
                const float d10 = sampleLevel(0u, glm::uvec2(x1, y0));
                const float d01 = sampleLevel(0u, glm::uvec2(x0, y1));
                const float d11 = sampleLevel(0u, glm::uvec2(x1, y1));
                const float dMin = std::min({d00, d10, d01, d11});
                const float dMax = std::max({d00, d10, d01, d11});
                if (dMax >= 0.9999f || dMin <= 0.0f) return false;
                const float zAtMin = ReconstructViewPos(sampleUV, dMin, invProjZO).z;
                const float zAtMax = ReconstructViewPos(sampleUV, dMax, invProjZO).z;
                const float maxContDiff = std::clamp(std::abs(zAtMin) * 0.08f + 0.05f, 0.05f, std::min(safeThickness * 0.65f, 0.35f));
                if (std::abs(zAtMin - zAtMax) > maxContDiff) return false;
                outDepth = glm::mix(glm::mix(d00, d10, fx), glm::mix(d01, d11, fx), fy);
                return true;
            };

            for (uint32_t step = 0; step < maxSteps && tCurr < 1.0f; ++step) {
                ++out.StepsExecuted;
                out.HighestMipReached = std::max(out.HighestMipReached, mip);

                const glm::vec3 pCurr = ray.StartUVZ + dirUVZ * tCurr;
                if (pCurr.x < 0.0f || pCurr.x > 1.0f || pCurr.y < 0.0f || pCurr.y > 1.0f || pCurr.z >= 0.9999f) {
                    break;
                }

                const glm::uvec2 dims = ComputeHiZMipSize(width, height, mip);
                const float cellScale = float(1u << mip);
                const glm::vec2 cellF(
                    std::clamp(pCurr.x * width / cellScale, 0.0f, static_cast<float>(dims.x) - 1e-4f),
                    std::clamp(pCurr.y * height / cellScale, 0.0f, static_cast<float>(dims.y) - 1e-4f));
                const glm::uvec2 cellIdx(static_cast<uint32_t>(std::floor(cellF.x)), static_cast<uint32_t>(std::floor(cellF.y)));

                // Reduction folds the odd border into the last cell; interior
                // boundaries remain multiples of 2^mip in base-pixel space.
                const float boundaryX = dirUVZ.x >= 0 && cellIdx.x + 1 == dims.x ? float(width) : (float(cellIdx.x) + (dirUVZ.x >= 0 ? 1 : 0)) * cellScale;
                const float boundaryY = dirUVZ.y >= 0 && cellIdx.y + 1 == dims.y ? float(height) : (float(cellIdx.y) + (dirUVZ.y >= 0 ? 1 : 0)) * cellScale;
                const float edgeX = boundaryX / static_cast<float>(width) +
                                    (dirUVZ.x >= 0.0f ? 1e-4f : -1e-4f) / static_cast<float>(width);
                const float edgeY = boundaryY / static_cast<float>(height) +
                                    (dirUVZ.y >= 0.0f ? 1e-4f : -1e-4f) / static_cast<float>(height);
                const float tx = std::abs(dirUVZ.x) > 1e-7f ? (edgeX - ray.StartUVZ.x) / dirUVZ.x : 1.0f;
                const float ty = std::abs(dirUVZ.y) > 1e-7f ? (edgeY - ray.StartUVZ.y) / dirUVZ.y : 1.0f;
                const float tNextCell = std::clamp(std::max(tCurr + 1e-5f, std::min(tx, ty)), tCurr, 1.0f);

                const float zEnter = ray.StartUVZ.z + dirUVZ.z * tCurr;
                const float zExit = ray.StartUVZ.z + dirUVZ.z * tNextCell;
                const float rayZMin = std::min(zEnter, zExit);
                const float rayZMax = std::max(zEnter, zExit);
                const float cellMinZ = sampleLevel(mip, cellIdx);

                if (rayZMax < cellMinZ || cellMinZ >= 0.9999f) {
                    tCurr = tNextCell;
                    mip = std::min(mip + 1u, maxAvailMip);
                    continue;
                }

                if (mip > 0u) {
                    if (zEnter < cellMinZ && dirUVZ.z > 1e-7f) {
                        const float tPlane = (cellMinZ - ray.StartUVZ.z) / dirUVZ.z;
                        tCurr = std::clamp(tPlane, tCurr, tNextCell);
                    }
                    --mip;
                    continue;
                }

                // Mip 0: check view-space penetration thickness and refine the cell crossing
                const glm::vec2 centerUV = (glm::vec2(cellIdx) + 0.5f) / glm::vec2(dims);
                const glm::vec3 sceneView = ReconstructViewPos(centerUV, cellMinZ, invProjZO);
                const glm::vec3 rayEnterView = ReconstructViewPos(glm::vec2(pCurr.x, pCurr.y), zEnter, invProjZO);
                const glm::vec3 rayExitUVZ = ray.StartUVZ + dirUVZ * tNextCell;
                const glm::vec3 rayExitView = ReconstructViewPos(glm::vec2(rayExitUVZ.x, rayExitUVZ.y), zExit, invProjZO);
                const float penEnter = sceneView.z - rayEnterView.z;
                const float penExit = sceneView.z - rayExitView.z;
                const bool crosses = (rayZMin <= cellMinZ && rayZMax >= cellMinZ);
                const bool withinBehind = (std::min(penEnter, penExit) <= safeThickness && std::max(penEnter, penExit) > -1e-4f);

                if (crosses || withinBehind) {
                    // Depth is constant within a mip-0 texel and UVZ is linear
                    // along this cell interval. Solve the crossing exactly for
                    // either depth direction; a fixed binary predicate assumes
                    // increasing depth and can reject camera-facing crossings.
                    const float tHit = crosses && std::abs(dirUVZ.z) > 1e-7f
                        ? std::clamp((cellMinZ-ray.StartUVZ.z)/dirUVZ.z,tCurr,tNextCell)
                        : tCurr;
                    glm::vec3 pFinal = ray.StartUVZ + dirUVZ * tHit;
                    glm::uvec2 fCell(
                        static_cast<uint32_t>(std::clamp(pFinal.x * static_cast<float>(width), 0.0f, static_cast<float>(width) - 1e-4f)),
                        static_cast<uint32_t>(std::clamp(pFinal.y * static_cast<float>(height), 0.0f, static_cast<float>(height) - 1e-4f)));
                    float finalSceneZ = sampleLevel(0u, fCell);
                    if (finalSceneZ < 0.9999f) {
                        glm::vec3 finalSceneView = ReconstructViewPos(glm::vec2(pFinal.x, pFinal.y), finalSceneZ, invProjZO);
                        const glm::vec3 finalRayView = ReconstructViewPos(glm::vec2(pFinal.x, pFinal.y), pFinal.z, invProjZO);
                        float finalPen = finalSceneView.z - finalRayView.z;
                        if (finalPen <= safeThickness && finalPen >= -0.05f) {
                            // Continuous sub-texel refinement across cliff-free mip-0 surfaces to remove
                            // discrete texel-row staircase beats and contact-seam ripples.
                            float tA = std::max(minOriginT, tCurr - 1.25f / pxLen);
                            float tB = tNextCell;
                            const glm::vec3 pA = ray.StartUVZ + dirUVZ * tA;
                            const glm::vec3 pB = ray.StartUVZ + dirUVZ * tB;
                            float contDA = 0.0f, contDB = 0.0f;
                            if (tB > tA + 1e-6f &&
                                trySampleContinuousHiZ0Depth(glm::vec2(pA.x, pA.y), contDA) &&
                                trySampleContinuousHiZ0Depth(glm::vec2(pB.x, pB.y), contDB)) {
                                const float zSceneA = ReconstructViewPos(glm::vec2(pA.x, pA.y), contDA, invProjZO).z;
                                const float zSceneB = ReconstructViewPos(glm::vec2(pB.x, pB.y), contDB, invProjZO).z;
                                float gapA = ReconstructViewPos(glm::vec2(pA.x, pA.y), pA.z, invProjZO).z - zSceneA;
                                float gapB = ReconstructViewPos(glm::vec2(pB.x, pB.y), pB.z, invProjZO).z - zSceneB;
                                if (std::abs(zSceneA - zSceneB) <= safeThickness &&
                                    gapA * gapB <= 0.0f &&
                                    std::abs(gapA) <= safeThickness &&
                                    std::abs(gapB) <= safeThickness &&
                                    std::abs(gapA - gapB) > 1e-6f) {
                                    for (int b = 0; b < 4; ++b) {
                                        const float tMid = 0.5f * (tA + tB);
                                        const glm::vec3 pMid = ray.StartUVZ + dirUVZ * tMid;
                                        float dMid = 0.0f;
                                        if (!trySampleContinuousHiZ0Depth(glm::vec2(pMid.x, pMid.y), dMid)) break;
                                        const float gapMid = ReconstructViewPos(glm::vec2(pMid.x, pMid.y), pMid.z, invProjZO).z -
                                                             ReconstructViewPos(glm::vec2(pMid.x, pMid.y), dMid, invProjZO).z;
                                        if ((gapMid >= 0.0f) == (gapA >= 0.0f)) {
                                            tA = tMid;
                                            gapA = gapMid;
                                        } else {
                                            tB = tMid;
                                            gapB = gapMid;
                                        }
                                    }
                                    const float alpha = std::clamp(std::abs(gapA) / std::max(std::abs(gapA - gapB), 1e-6f), 0.0f, 1.0f);
                                    const float tRefined = tA + (tB - tA) * alpha;
                                    const glm::vec3 pRefined = ray.StartUVZ + dirUVZ * tRefined;
                                    float dRefined = 0.0f;
                                    if (trySampleContinuousHiZ0Depth(glm::vec2(pRefined.x, pRefined.y), dRefined)) {
                                        const glm::vec3 refinedSceneView = ReconstructViewPos(glm::vec2(pRefined.x, pRefined.y), dRefined, invProjZO);
                                        const glm::vec3 refinedRayView = ReconstructViewPos(glm::vec2(pRefined.x, pRefined.y), pRefined.z, invProjZO);
                                        const float refinedPen = refinedSceneView.z - refinedRayView.z;
                                        if (refinedPen <= safeThickness && refinedPen >= -0.05f) {
                                            pFinal = pRefined;
                                            finalSceneZ = dRefined;
                                            finalSceneView = refinedSceneView;
                                            finalPen = refinedPen;
                                            fCell = glm::uvec2(
                                                static_cast<uint32_t>(std::clamp(pFinal.x * static_cast<float>(width), 0.0f, static_cast<float>(width) - 1e-4f)),
                                                static_cast<uint32_t>(std::clamp(pFinal.y * static_cast<float>(height), 0.0f, static_cast<float>(height) - 1e-4f)));
                                        }
                                    }
                                }
                            }
                            const auto smooth = [](float edge0, float edge1, float x) {
                                const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
                                return t * t * (3.0f - 2.0f * t);
                            };
                            const float fadeX = smooth(0.0f, 0.12f, pFinal.x) * (1.0f - smooth(0.88f, 1.0f, pFinal.x));
                            const float fadeY = smooth(0.0f, 0.12f, pFinal.y) * (1.0f - smooth(0.88f, 1.0f, pFinal.y));
                            const float screenFade = std::clamp(fadeX * fadeY, 0.0f, 1.0f);
                            const float stepFade = std::clamp(static_cast<float>(maxSteps - step) / 6.0f, 0.0f, 1.0f);
                            out.Hit = true;
                            out.HitUV = glm::vec2(pFinal.x, pFinal.y);
                            out.HitDepth = finalSceneZ;
                            float supportSum = 0.0f;
                            const float edgeTol = std::max(safeThickness * 1.5f, 0.35f);
                            for (const auto offset : {glm::ivec2(-1,0), glm::ivec2(1,0), glm::ivec2(0,-1), glm::ivec2(0,1)}) {
                                const auto cell = glm::clamp(glm::ivec2(fCell) + offset, glm::ivec2(0), glm::ivec2(width-1,height-1));
                                const float neighborDepth = sampleLevel(0, glm::uvec2(cell));
                                if (neighborDepth < .9999f) {
                                    const float neighborZ = ReconstructViewPos((glm::vec2(cell)+.5f)/glm::vec2(width,height), neighborDepth, invProjZO).z;
                                    supportSum += 1.0f - smooth(edgeTol * 0.35f, edgeTol * 1.35f, std::abs(finalSceneView.z - neighborZ));
                                }
                            }
                            const float disocclusionFade = 0.25f + 0.75f * smooth(0.2f, 2.2f, supportSum);
                            const auto reflection = glm::normalize(ray.EndView-ray.StartView);
                            const float cameraFade = 1-smooth(.45f,.98f,reflection.z);
                            out.HitDistance = glm::length(finalSceneView - ray.OriginView);
                            const float distanceFade = std::clamp(1-out.HitDistance/std::max(ray.MaxDistance,5.0f)*.25f,0.0f,1.0f);
                            out.Confidence = std::clamp(screenFade * stepFade * distanceFade * cameraFade * disocclusionFade, 0.0f, 1.0f);
                            return out;
                        }
                    }
                }

                tCurr = tNextCell;
                mip = 0u;
            }

            return out;
        }

        float EvaluateRoughnessFade(float roughness) {
            const float r = std::clamp(roughness, 0.04f, 1.0f);
            const float t = std::clamp((r - 0.65f) / (0.98f - 0.65f), 0.0f, 1.0f);
            return 1.0f - t * t * (3.0f - 2.0f * t);
        }

        float ComputeRoughnessConePixelRadius(
            float roughness,
            float hitDistanceMeters,
            float hitViewZ,
            const glm::mat4& projZO,
            uint32_t width,
            uint32_t height) {
            const float r = std::clamp(roughness, 0.04f, 1.0f);
            const float coneTan = std::clamp(r * r * 0.75f, 0.0f, 0.75f);
            const float worldRadius = coneTan * std::max(hitDistanceMeters, 0.0f);
            const float projScale = 0.5f * std::max(
                std::abs(projZO[0][0]) * static_cast<float>(width),
                std::abs(projZO[1][1]) * static_cast<float>(height));
            return std::clamp(projScale * worldRadius / std::max(std::abs(hitViewZ), 0.1f), 0.0f, MaxConeRadiusPixels);
        }

        glm::vec3 ComputeSpecularF0(const glm::vec3& baseColor, float metallic) {
            const float m = std::clamp(metallic, 0.0f, 1.0f);
            const glm::vec3 safeBase = glm::max(baseColor, glm::vec3(0.0f));
            return glm::mix(glm::vec3(0.04f), safeBase, m);
        }
    } // namespace SSRRaymarchContract

    namespace AreaLightLTCContract {
        namespace {
            constexpr float kPi = 3.14159265358979323846f;
            constexpr float kLocalIntensityScale = 0.05f;
            // Area-preserving 32-segment disk contour; bounded angular error also for sharp reflections.
            constexpr float kDiskRadiusScale = 1.00322263f; // sqrt(pi / (16 * sin(pi/16)))

            float SafeClampRoughness(float r) {
                return std::isfinite(r) ? std::clamp(r, 0.04f, 1.0f) : 0.5f;
            }

            float SafeClampNdotV(float nv) {
                return std::isfinite(nv) ? std::clamp(nv, 0.0f, 1.0f) : 0.5f;
            }

            glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
                const float lenSq = glm::dot(v, v);
                if (!std::isfinite(lenSq) || lenSq <= 1e-12f) {
                    return fallback;
                }
                return v * (1.0f / std::sqrt(lenSq));
            }

            float EvaluateLTCContourIntegral(
                const std::vector<glm::vec3>& shadingPoly,
                const glm::vec4& ltc1,
                bool twoSided) {
                if (shadingPoly.size() < 3) {
                    return 0.0f;
                }

                // First clip against the physical surface horizon (L.z >= 0 in shading tangent frame).
                std::vector<glm::vec3> upperPoly = ClipPolygonToHorizon(shadingPoly);
                if (upperPoly.size() < 3) {
                    return 0.0f;
                }

                // Apply inverse LTC matrix M^-1 (identity if ltc1 == (1, 0, 0, 1)).
                std::vector<glm::vec3> warpedPoly;
                warpedPoly.reserve(upperPoly.size());
                for (const glm::vec3& p : upperPoly) {
                    warpedPoly.emplace_back(
                        ltc1.x * p.x + ltc1.y * p.z,
                        p.y,
                        ltc1.z * p.x + ltc1.w * p.z);
                }

                // Clip against the warped clamped-cosine support plane (w.z >= 0).
                std::vector<glm::vec3> clipped = ClipPolygonToHorizon(warpedPoly);
                if (clipped.size() < 3) {
                    return 0.0f;
                }

                for (glm::vec3& v : clipped) {
                    const float lenSq = glm::dot(v, v);
                    v = (lenSq > 1e-12f) ? (v / std::sqrt(lenSq)) : glm::vec3(0.0f, 0.0f, 1.0f);
                }

                glm::vec3 sum(0.0f);
                const size_t n = clipped.size();
                for (size_t i = 0; i < n; ++i) {
                    sum += IntegrateEdgeVector(clipped[i], clipped[(i + 1) % n]);
                }

                const float formFactor = sum.z;
                if (!std::isfinite(formFactor)) {
                    return 0.0f;
                }
                return twoSided ? std::abs(formFactor) : std::max(0.0f, formFactor);
            }
        } // namespace

        namespace {
#define LTC_UINT3 glm::uvec3
#include "../../Resources/Shaders/LTCTables.inc"
#undef LTC_UINT3
        }

        glm::vec4 EvaluateLTC1GridPoint(uint32_t roughIdx, uint32_t viewIdx) {
            const auto p = kLTCPacked[std::min(viewIdx, kTableDim - 1u) * kTableDim + std::min(roughIdx, kTableDim - 1u)];
            return glm::vec4(glm::unpackHalf2x16(p.x), glm::unpackHalf2x16(p.y));
        }

        glm::vec4 EvaluateLTC2GridPoint(uint32_t roughIdx, uint32_t viewIdx) {
            const auto p = kLTCPacked[std::min(viewIdx, kTableDim - 1u) * kTableDim + std::min(roughIdx, kTableDim - 1u)];
            return glm::vec4(glm::unpackHalf2x16(p.z), 0.0f, 0.0f);
        }

        LTCTables BuildValidatedLTCTables() {
            LTCTables out;
            for (uint32_t iy = 0; iy < kTableDim; ++iy) {
                for (uint32_t ix = 0; ix < kTableDim; ++ix) {
                    const size_t idx = static_cast<size_t>(iy) * kTableDim + ix;
                    out.LTC1[idx] = EvaluateLTC1GridPoint(ix, iy);
                    out.LTC2[idx] = EvaluateLTC2GridPoint(ix, iy);
                }
            }
            return out;
        }

        bool ValidateLTCTables(const LTCTables& tables, std::string* outReason) {
            for (uint32_t iy = 0; iy < kTableDim; ++iy) {
                for (uint32_t ix = 0; ix < kTableDim; ++ix) {
                    const size_t idx = static_cast<size_t>(iy) * kTableDim + ix;
                    const glm::vec4& l1 = tables.LTC1[idx];
                    const glm::vec4& l2 = tables.LTC2[idx];
                    for (int c = 0; c < 4; ++c) {
                        if (!std::isfinite(l1[c]) || !std::isfinite(l2[c])) {
                            if (outReason) *outReason = "non-finite LTC table entry";
                            return false;
                        }
                    }
                    const float det = l1.x * l1.w - l1.y * l1.z;
                    if (!std::isfinite(det) || det <= 0.0f) {
                        if (outReason) *outReason = "singular or inverted LTC1 transform matrix";
                        return false;
                    }
                    if (l2.x <= 0.0f || l2.x > 1.02f || l2.y < 0.0f || l2.y > 1.0001f) {
                        if (outReason) *outReason = "LTC2 directional albedo out of physical [0, 1] bounds";
                        return false;
                    }
                }
            }
            // A GGX fit at roughness=1 is not a diffuse identity transform.
            // Accuracy is checked against independent GGX quadrature in contract tests.
            if (outReason) outReason->clear();
            return true;
        }

        glm::vec4 SampleLTC1(float roughness, float NdotV) {
            const float u = std::clamp(SafeClampRoughness(roughness), 0.0f, 1.0f);
            const float v = std::clamp(std::sqrt(std::max(0.0f, 1.0f - SafeClampNdotV(NdotV))), 0.0f, 1.0f);
            const float gx = u * static_cast<float>(kTableDim - 1u);
            const float gy = v * static_cast<float>(kTableDim - 1u);
            const uint32_t ix0 = std::min(static_cast<uint32_t>(std::floor(gx)), kTableDim - 1u);
            const uint32_t iy0 = std::min(static_cast<uint32_t>(std::floor(gy)), kTableDim - 1u);
            const uint32_t ix1 = std::min(ix0 + 1u, kTableDim - 1u);
            const uint32_t iy1 = std::min(iy0 + 1u, kTableDim - 1u);
            const float fx = gx - static_cast<float>(ix0);
            const float fy = gy - static_cast<float>(iy0);

            const glm::vec4 c00 = EvaluateLTC1GridPoint(ix0, iy0);
            const glm::vec4 c10 = EvaluateLTC1GridPoint(ix1, iy0);
            const glm::vec4 c01 = EvaluateLTC1GridPoint(ix0, iy1);
            const glm::vec4 c11 = EvaluateLTC1GridPoint(ix1, iy1);
            return glm::mix(glm::mix(c00, c10, fx), glm::mix(c01, c11, fx), fy);
        }

        glm::vec4 SampleLTC2(float roughness, float NdotV) {
            const float u = std::clamp(SafeClampRoughness(roughness), 0.0f, 1.0f);
            const float v = std::clamp(std::sqrt(std::max(0.0f, 1.0f - SafeClampNdotV(NdotV))), 0.0f, 1.0f);
            const float gx = u * static_cast<float>(kTableDim - 1u);
            const float gy = v * static_cast<float>(kTableDim - 1u);
            const uint32_t ix0 = std::min(static_cast<uint32_t>(std::floor(gx)), kTableDim - 1u);
            const uint32_t iy0 = std::min(static_cast<uint32_t>(std::floor(gy)), kTableDim - 1u);
            const uint32_t ix1 = std::min(ix0 + 1u, kTableDim - 1u);
            const uint32_t iy1 = std::min(iy0 + 1u, kTableDim - 1u);
            const float fx = gx - static_cast<float>(ix0);
            const float fy = gy - static_cast<float>(iy0);

            const glm::vec4 c00 = EvaluateLTC2GridPoint(ix0, iy0);
            const glm::vec4 c10 = EvaluateLTC2GridPoint(ix1, iy0);
            const glm::vec4 c01 = EvaluateLTC2GridPoint(ix0, iy1);
            const glm::vec4 c11 = EvaluateLTC2GridPoint(ix1, iy1);
            return glm::mix(glm::mix(c00, c10, fx), glm::mix(c01, c11, fx), fy);
        }

        glm::vec3 IntegrateEdgeVector(const glm::vec3& v1, const glm::vec3& v2) {
            const float x = std::clamp(glm::dot(v1, v2), -0.999999f, 0.999999f);
            const float y = std::abs(x);
            const float a = 0.8543985f + (0.4965155f + 0.0145206f * y) * y;
            const float b = 3.4175940f + (4.1616724f + y) * y;
            const float v = a / b;
            const float thetaSinTheta = (x > 0.0f)
                ? v
                : (0.5f / std::sqrt(std::max(1.0f - x * x, 1e-7f)) - v);
            return glm::cross(v1, v2) * thetaSinTheta;
        }

        std::vector<glm::vec3> ClipPolygonToHorizon(const std::vector<glm::vec3>& poly) {
            std::vector<glm::vec3> out;
            const size_t n = poly.size();
            if (n < 3) {
                return out;
            }
            out.reserve(n + 4);
            for (size_t i = 0; i < n; ++i) {
                const glm::vec3& curr = poly[i];
                const glm::vec3& next = poly[(i + 1) % n];
                const bool currAbove = (curr.z >= 0.0f);
                const bool nextAbove = (next.z >= 0.0f);
                if (currAbove) {
                    out.push_back(curr);
                    if (!nextAbove) {
                        const float denom = curr.z - next.z;
                        const float t = (std::abs(denom) > 1e-7f) ? (curr.z / denom) : 0.0f;
                        glm::vec3 clipped = glm::mix(curr, next, std::clamp(t, 0.0f, 1.0f));
                        clipped.z = 0.0f;
                        out.push_back(clipped);
                    }
                } else if (nextAbove) {
                    const float denom = next.z - curr.z;
                    const float t = (std::abs(denom) > 1e-7f) ? (-curr.z / denom) : 0.0f;
                    glm::vec3 clipped = glm::mix(curr, next, std::clamp(t, 0.0f, 1.0f));
                    clipped.z = 0.0f;
                    out.push_back(clipped);
                }
            }
            return out;
        }

        void ComputeOrthonormalAreaBasis(
            const glm::vec3& rawDir,
            const glm::vec3& rawUp,
            glm::vec3& outRight,
            glm::vec3& outUp,
            glm::vec3& outDir) {
            outDir = SafeNormalize(rawDir, glm::vec3(0.0f, 0.0f, -1.0f));
            const glm::vec3 fallbackUpAxis = (std::abs(outDir.y) < 0.99f)
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 orthoCandidate = rawUp - outDir * glm::dot(rawUp, outDir);
            outUp = SafeNormalize(orthoCandidate, SafeNormalize(glm::cross(fallbackUpAxis, outDir), glm::vec3(0.0f, 1.0f, 0.0f)));
            outRight = SafeNormalize(glm::cross(outDir, outUp), glm::vec3(1.0f, 0.0f, 0.0f));
            outUp = SafeNormalize(glm::cross(outRight, outDir), outUp);
        }

        float ComputeEmitterClosestDistance(const LightingRectFrame& light, const glm::vec3& worldPos) {
            glm::vec3 right, up, dir;
            ComputeOrthonormalAreaBasis(light.Direction, light.Up, right, up, dir);
            const float halfW = std::max(std::isfinite(light.Width) ? light.Width * 0.5f : 0.005f, 0.005f);
            const float halfH = std::max(std::isfinite(light.Height) ? light.Height * 0.5f : 0.005f, 0.005f);
            const glm::vec3 rel = worldPos - light.Position;
            float u = glm::dot(rel, right);
            float v = glm::dot(rel, up);
            if (light.Shape == AreaLightShape::Disk) {
                const float nx = u / halfW;
                const float ny = v / halfH;
                if (nx * nx + ny * ny > 1.0f) {
                    // Euclidean projection onto an ellipse, not radial projection.
                    const float a2 = halfW * halfW;
                    const float b2 = halfH * halfH;
                    float lo = 0.0f;
                    float hi = glm::length(glm::vec2(halfW * u, halfH * v));
                    for (int iteration = 0; iteration < 24; ++iteration) {
                        const float lambda = (lo + hi) * 0.5f;
                        const float x = halfW * u / (lambda + a2);
                        const float y = halfH * v / (lambda + b2);
                        if (x * x + y * y > 1.0f) lo = lambda;
                        else hi = lambda;
                    }
                    u = a2 * u / (hi + a2);
                    v = b2 * v / (hi + b2);
                }
            } else {
                u = std::clamp(u, -halfW, halfW);
                v = std::clamp(v, -halfH, halfH);
            }
            const glm::vec3 closestPt = light.Position + right * u + up * v;
            const float d = glm::length(worldPos - closestPt);
            return std::isfinite(d) ? d : 0.0f;
        }

        AreaLightEvaluation EvaluateLTCAreaLightCPU(
            const LightingRectFrame& light,
            const glm::vec3& worldPos,
            const glm::vec3& normalWS,
            const glm::vec3& viewDirWS,
            const glm::vec3& baseColor,
            float metallic,
            float roughness) {
            AreaLightEvaluation result;
            const float range = std::isfinite(light.Range) ? light.Range : 0.0f;
            const float absIntensity = std::isfinite(light.SignedIntensity) ? std::abs(light.SignedIntensity) : 0.0f;
            if (range <= 1e-3f || absIntensity <= 0.0f) {
                return result;
            }

            glm::vec3 lightRight, lightUp, lightDir;
            ComputeOrthonormalAreaBasis(light.Direction, light.Up, lightRight, lightUp, lightDir);

            const bool twoSided = (light.SignedIntensity < 0.0f);
            const float dirDot = glm::dot(worldPos - light.Position, lightDir);
            if (!twoSided && dirDot <= 0.0f) {
                return result;
            }

            const float closestDist = ComputeEmitterClosestDistance(light, worldPos);
            if (closestDist >= range) {
                return result;
            }
            const float ratio = std::clamp(closestDist / range, 0.0f, 1.0f);
            const float win = std::max(0.0f, 1.0f - ratio * ratio * ratio * ratio);
            result.RangeAttenuation = win * win;

            const glm::vec3 N = SafeNormalize(normalWS, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 V = SafeNormalize(viewDirWS, N);
            const float NdotV = std::clamp(glm::dot(N, V), 0.001f, 1.0f);
            const float safeRoughness = SafeClampRoughness(roughness);
            const float safeMetallic = std::clamp(metallic, 0.0f, 1.0f);

            const glm::vec3 tangentRaw = V - N * glm::dot(N, V);
            const glm::vec3 fallbackTangentAxis = (std::abs(N.y) < 0.99f)
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 T1 = SafeNormalize(tangentRaw, SafeNormalize(glm::cross(fallbackTangentAxis, N), glm::vec3(1.0f, 0.0f, 0.0f)));
            const glm::vec3 T2 = SafeNormalize(glm::cross(N, T1), glm::vec3(0.0f, 0.0f, 1.0f));

            const float halfW = std::max(std::isfinite(light.Width) ? light.Width * 0.5f : 0.005f, 0.005f);
            const float halfH = std::max(std::isfinite(light.Height) ? light.Height * 0.5f : 0.005f, 0.005f);

            std::vector<glm::vec3> shadingPoly;
            auto pushWorldVertex = [&](const glm::vec3& wp) {
                const glm::vec3 rel = wp - worldPos;
                shadingPoly.emplace_back(glm::dot(rel, T1), glm::dot(rel, T2), glm::dot(rel, N));
            };

            if (light.Shape == AreaLightShape::Disk) {
                shadingPoly.reserve(32);
                for (int k = 0; k < 32; ++k) {
                    const float angle = (static_cast<float>(k) + 0.5f) * (kPi * 0.0625f);
                    const float cx = std::cos(angle) * kDiskRadiusScale;
                    const float cy = std::sin(angle) * kDiskRadiusScale;
                    pushWorldVertex(light.Position + lightRight * (halfW * cx) + lightUp * (halfH * cy));
                }
            } else {
                shadingPoly.reserve(4);
                pushWorldVertex(light.Position - lightRight * halfW - lightUp * halfH);
                pushWorldVertex(light.Position + lightRight * halfW - lightUp * halfH);
                pushWorldVertex(light.Position + lightRight * halfW + lightUp * halfH);
                pushWorldVertex(light.Position - lightRight * halfW + lightUp * halfH);
            }

            result.DiffuseFormFactor = EvaluateLTCContourIntegral(shadingPoly, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), twoSided);
            const glm::vec4 ltc1 = SampleLTC1(safeRoughness, NdotV);
            const glm::vec4 ltc2 = SampleLTC2(safeRoughness, NdotV);
            result.SpecularFormFactor = EvaluateLTCContourIntegral(shadingPoly, ltc1, twoSided);

            const glm::vec3 safeAlbedo = glm::max(baseColor, glm::vec3(0.0f));
            const glm::vec3 F0 = SSRRaymarchContract::ComputeSpecularF0(safeAlbedo, safeMetallic);
            result.SpecularAlbedo = F0 * ltc2.x + (glm::vec3(1.0f) - F0) * ltc2.y;

            const glm::vec3 kD = glm::max(glm::vec3(1.0f) - result.SpecularAlbedo, glm::vec3(0.0f)) * (1.0f - safeMetallic);
            const glm::vec3 lightRadiance = glm::max(light.Color, glm::vec3(0.0f)) * (absIntensity * result.RangeAttenuation * kLocalIntensityScale);

            result.DiffuseRadiance = kD * safeAlbedo * result.DiffuseFormFactor * lightRadiance;
            result.SpecularRadiance = result.SpecularAlbedo * result.SpecularFormFactor * lightRadiance;
            result.TotalRadiance = result.DiffuseRadiance + result.SpecularRadiance;
            return result;
        }

        AreaLightEvaluation EvaluateAreaLightNumericalReferenceCPU(
            const LightingRectFrame& light,
            const glm::vec3& worldPos,
            const glm::vec3& normalWS,
            const glm::vec3& viewDirWS,
            const glm::vec3& baseColor,
            float metallic,
            float roughness,
            uint32_t samplesPerAxis) {
            AreaLightEvaluation result;
            const uint32_t gridN = std::clamp(samplesPerAxis, 8u, 128u);
            const float range = std::isfinite(light.Range) ? light.Range : 0.0f;
            const float absIntensity = std::isfinite(light.SignedIntensity) ? std::abs(light.SignedIntensity) : 0.0f;
            if (range <= 1e-3f || absIntensity <= 0.0f) {
                return result;
            }

            glm::vec3 lightRight, lightUp, lightDir;
            ComputeOrthonormalAreaBasis(light.Direction, light.Up, lightRight, lightUp, lightDir);

            const bool twoSided = (light.SignedIntensity < 0.0f);
            const float dirDot = glm::dot(worldPos - light.Position, lightDir);
            if (!twoSided && dirDot <= 0.0f) {
                return result;
            }

            const float closestDist = ComputeEmitterClosestDistance(light, worldPos);
            if (closestDist >= range) {
                return result;
            }
            const float ratio = std::clamp(closestDist / range, 0.0f, 1.0f);
            const float win = std::max(0.0f, 1.0f - ratio * ratio * ratio * ratio);
            result.RangeAttenuation = win * win;

            const glm::vec3 N = SafeNormalize(normalWS, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 V = SafeNormalize(viewDirWS, N);
            const float NdotV = std::clamp(glm::dot(N, V), 0.001f, 1.0f);
            const float safeRoughness = SafeClampRoughness(roughness);
            const float safeMetallic = std::clamp(metallic, 0.0f, 1.0f);
            const float alpha = safeRoughness * safeRoughness;
            const float a2 = alpha * alpha;

            const glm::vec3 safeAlbedo = glm::max(baseColor, glm::vec3(0.0f));
            const glm::vec3 F0 = SSRRaymarchContract::ComputeSpecularF0(safeAlbedo, safeMetallic);
            const float halfW = std::max(std::isfinite(light.Width) ? light.Width * 0.5f : 0.005f, 0.005f);
            const float halfH = std::max(std::isfinite(light.Height) ? light.Height * 0.5f : 0.005f, 0.005f);

            float diffuseSum = 0.0f;
            glm::vec3 specIntegral(0.0f);

            for (uint32_t iy = 0; iy < gridN; ++iy) {
                for (uint32_t ix = 0; ix < gridN; ++ix) {
                    glm::vec3 samplePos;
                    float sampleArea = 0.0f;
                    if (light.Shape == AreaLightShape::Disk) {
                        const float u = (static_cast<float>(ix) + 0.5f) / static_cast<float>(gridN);
                        const float v = (static_cast<float>(iy) + 0.5f) / static_cast<float>(gridN);
                        const float angle = 2.0f * kPi * v;
                        const float lx = halfW * u * std::cos(angle);
                        const float ly = halfH * u * std::sin(angle);
                        samplePos = light.Position + lightRight * lx + lightUp * ly;
                        const float dU = 1.0f / static_cast<float>(gridN);
                        const float dTheta = (2.0f * kPi) / static_cast<float>(gridN);
                        sampleArea = (halfW * halfH) * u * dU * dTheta;
                    } else {
                        const float u = ((static_cast<float>(ix) + 0.5f) / static_cast<float>(gridN)) * 2.0f - 1.0f;
                        const float v = ((static_cast<float>(iy) + 0.5f) / static_cast<float>(gridN)) * 2.0f - 1.0f;
                        samplePos = light.Position + lightRight * (halfW * u) + lightUp * (halfH * v);
                        sampleArea = (4.0f * halfW * halfH) / static_cast<float>(gridN * gridN);
                    }

                    const glm::vec3 toSample = samplePos - worldPos;
                    const float distSq = glm::dot(toSample, toSample);
                    if (distSq <= 1e-8f) {
                        continue;
                    }
                    const float invDist = 1.0f / std::sqrt(distSq);
                    const glm::vec3 L = toSample * invDist;
                    const float NdotL = std::max(glm::dot(N, L), 0.0f);
                    const float rawEmitterCos = glm::dot(-L, lightDir);
                    const float emitterCos = twoSided ? std::abs(rawEmitterCos) : std::max(rawEmitterCos, 0.0f);
                    if (NdotL <= 0.0f || emitterCos <= 0.0f) {
                        continue;
                    }

                    const float dOmega = (emitterCos * sampleArea) / distSq;
                    diffuseSum += (NdotL * dOmega) / kPi;

                    const glm::vec3 H = SafeNormalize(V + L, N);
                    const float NdotH = std::max(glm::dot(N, H), 0.0f);
                    const float VdotH = std::max(glm::dot(V, H), 0.0f);
                    const float denomD = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
                    const float D = a2 / std::max(kPi * denomD * denomD, 1e-8f);
                    const float ggxV = NdotL * std::sqrt(NdotV * NdotV * (1.0f - a2) + a2);
                    const float ggxL = NdotV * std::sqrt(NdotL * NdotL * (1.0f - a2) + a2);
                    const float vis = 0.5f / std::max(ggxV + ggxL, 1e-5f);
                    const glm::vec3 F = F0 + (glm::vec3(1.0f) - F0) * std::pow(std::clamp(1.0f - VdotH, 0.0f, 1.0f), 5.0f);
                    specIntegral += (D * vis * F) * NdotL * dOmega;
                }
            }

            result.DiffuseFormFactor = diffuseSum;
            const glm::vec4 ltc2 = SampleLTC2(safeRoughness, NdotV);
            result.SpecularAlbedo = F0 * ltc2.x + (glm::vec3(1.0f) - F0) * ltc2.y;
            const glm::vec3 safeDenom = glm::max(result.SpecularAlbedo, glm::vec3(1e-4f));
            result.SpecularFormFactor = (specIntegral.r / safeDenom.r + specIntegral.g / safeDenom.g + specIntegral.b / safeDenom.b) / 3.0f;

            const glm::vec3 kD = glm::max(glm::vec3(1.0f) - result.SpecularAlbedo, glm::vec3(0.0f)) * (1.0f - safeMetallic);
            const glm::vec3 lightRadiance = glm::max(light.Color, glm::vec3(0.0f)) * (absIntensity * result.RangeAttenuation * kLocalIntensityScale);

            result.DiffuseRadiance = kD * safeAlbedo * result.DiffuseFormFactor * lightRadiance;
            result.SpecularRadiance = specIntegral * lightRadiance;
            result.TotalRadiance = result.DiffuseRadiance + result.SpecularRadiance;
            return result;
        }
    } // namespace AreaLightLTCContract

    // =========================================================================
    // 9. Planar Reflection Surface, Mirrored Camera & Clipping Contracts (M15-U5-46)
    // =========================================================================
    namespace PlanarReflectionContract {
        namespace {
            bool IsFiniteVec3(const glm::vec3& v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            }

            bool IsFiniteMat4(const glm::mat4& m) {
                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 4; ++r) {
                        if (!std::isfinite(m[c][r])) return false;
                    }
                }
                return true;
            }

            glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
                if (!IsFiniteVec3(v)) return fallback;
                const float lenSq = glm::dot(v, v);
                if (!std::isfinite(lenSq) || lenSq <= 1e-12f) return fallback;
                return v * (1.0f / std::sqrt(lenSq));
            }
        } // namespace

        PlanarReflectionSurface BuildSurfaceFromReflector(const RenderPlanarReflector& reflector) {
            PlanarReflectionSurface surface;
            surface.EntityID = reflector.EntityID;
            surface.StableUUID = reflector.StableUUID;
            surface.WorldTransform = reflector.WorldTransform;
            surface.Position = IsFiniteVec3(reflector.Position) ? reflector.Position : glm::vec3(0.0f);

            const glm::vec3 n = SafeNormalize(reflector.Normal, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 rawT = IsFiniteVec3(reflector.Tangent) ? reflector.Tangent : glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 orthoT = rawT - n * glm::dot(rawT, n);
            if (glm::dot(orthoT, orthoT) <= 1e-10f) {
                const glm::vec3 fallbackAxis = (std::abs(n.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                orthoT = glm::cross(fallbackAxis, n);
            }
            surface.Normal = n;
            surface.Tangent = SafeNormalize(orthoT, glm::vec3(1.0f, 0.0f, 0.0f));
            surface.Bitangent = SafeNormalize(glm::cross(surface.Normal, surface.Tangent), glm::vec3(0.0f, 0.0f, 1.0f));
            surface.HalfExtents = glm::vec2(
                std::isfinite(reflector.HalfExtents.x) ? std::max(0.01f, reflector.HalfExtents.x) : 5.0f,
                std::isfinite(reflector.HalfExtents.y) ? std::max(0.01f, reflector.HalfExtents.y) : 5.0f);
            surface.CaptureNearPlane = std::isfinite(reflector.CaptureNearPlane) ? std::max(0.01f, reflector.CaptureNearPlane) : 0.1f;
            surface.CaptureFarPlane = std::isfinite(reflector.CaptureFarPlane) ? std::max(surface.CaptureNearPlane + 0.1f, reflector.CaptureFarPlane) : 1000.0f;
            surface.ClipPlaneOffset = std::isfinite(reflector.ClipPlaneOffset) ? reflector.ClipPlaneOffset : 0.0f;
            surface.EdgeFadeDistance = std::isfinite(reflector.EdgeFadeDistance) ? std::max(0.0f, reflector.EdgeFadeDistance) : DefaultEdgeFadeDistance;
            surface.MaxRoughness = std::isfinite(reflector.MaxRoughness) ? std::clamp(reflector.MaxRoughness, 0.02f, 1.0f) : DefaultMaxRoughness;
            surface.Intensity = std::isfinite(reflector.Intensity) ? std::clamp(reflector.Intensity, 0.0f, 10.0f) : 1.0f;
            surface.ClipToExtents = reflector.ClipToExtents;
            surface.Valid = reflector.Enabled &&
                            IsFiniteMat4(surface.WorldTransform) &&
                            IsFiniteVec3(reflector.Position) &&
                            IsFiniteVec3(reflector.Normal);
            return surface;
        }

        float SignedDistanceToPlane(const PlanarReflectionSurface& surface, const glm::vec3& pointWS) {
            if (!surface.Valid || !IsFiniteVec3(pointWS)) return 0.0f;
            return glm::dot(surface.Normal, pointWS - surface.Position);
        }

        glm::vec3 ReflectPoint(const PlanarReflectionSurface& surface, const glm::vec3& pointWS) {
            if (!surface.Valid || !IsFiniteVec3(pointWS)) return pointWS;
            const float dist = glm::dot(surface.Normal, pointWS - surface.Position);
            return pointWS - (2.0f * dist) * surface.Normal;
        }

        glm::vec3 ReflectDirection(const PlanarReflectionSurface& surface, const glm::vec3& dirWS) {
            if (!surface.Valid || !IsFiniteVec3(dirWS)) return dirWS;
            return dirWS - (2.0f * glm::dot(surface.Normal, dirWS)) * surface.Normal;
        }

        glm::mat4 BuildReflectionMatrix(const PlanarReflectionSurface& surface) {
            if (!surface.Valid) return glm::mat4(1.0f);
            const glm::vec3 n = surface.Normal;
            const float d = -glm::dot(n, surface.Position);
            glm::mat4 refl(1.0f);
            refl[0][0] = 1.0f - 2.0f * n.x * n.x;
            refl[1][0] = -2.0f * n.x * n.y;
            refl[2][0] = -2.0f * n.x * n.z;
            refl[3][0] = -2.0f * d * n.x;

            refl[0][1] = -2.0f * n.y * n.x;
            refl[1][1] = 1.0f - 2.0f * n.y * n.y;
            refl[2][1] = -2.0f * n.y * n.z;
            refl[3][1] = -2.0f * d * n.y;

            refl[0][2] = -2.0f * n.z * n.x;
            refl[1][2] = -2.0f * n.z * n.y;
            refl[2][2] = 1.0f - 2.0f * n.z * n.z;
            refl[3][2] = -2.0f * d * n.z;
            return refl;
        }

        glm::mat4 BuildObliqueClipProjectionZO(
            const glm::mat4& projZO,
            const glm::mat4& mirroredView,
            const glm::vec4& worldPlane,
            glm::vec4* outViewPlane,
            bool* outApplied,
            glm::mat4* outObliqueGL) {
            if (outApplied) *outApplied = false;
            if (outObliqueGL) *outObliqueGL = projZO;

            if (!IsFiniteMat4(projZO) || !IsFiniteMat4(mirroredView)) {
                return projZO;
            }
            const float detView = glm::determinant(glm::mat3(mirroredView));
            const float detProj = glm::determinant(projZO);
            if (!std::isfinite(detView) || std::abs(detView) < 1e-6f ||
                !std::isfinite(detProj) || std::abs(detProj) < 1e-12f) {
                return projZO;
            }

            const glm::mat4 invView = glm::inverse(mirroredView);
            const glm::vec4 rawViewPlane = glm::transpose(invView) * worldPlane;
            const float nLen = glm::length(glm::vec3(rawViewPlane));
            if (!std::isfinite(nLen) || nLen < 1e-6f) {
                return projZO;
            }
            const glm::vec4 viewPlane = rawViewPlane / nLen;
            if (outViewPlane) *outViewPlane = viewPlane;

            // Find the opposite far frustum corner in view space corresponding to z_ndc = 1 (ZO far plane).
            const float sgnX = ((viewPlane.x * projZO[0][0]) >= 0.0f) ? 1.0f : -1.0f;
            const float sgnY = ((viewPlane.y * projZO[1][1]) >= 0.0f) ? 1.0f : -1.0f;
            glm::vec4 q = glm::inverse(projZO) * glm::vec4(sgnX, sgnY, 1.0f, 1.0f);
            if (!std::isfinite(q.w) || std::abs(q.w) < 1e-6f) {
                return projZO;
            }
            q /= q.w;

            const glm::vec4 row3(projZO[0][3], projZO[1][3], projZO[2][3], projZO[3][3]);
            const float row3DotQ = glm::dot(row3, q);
            const float planeDotQ = glm::dot(viewPlane, q);
            if (!std::isfinite(row3DotQ) || !std::isfinite(planeDotQ) ||
                row3DotQ <= 1e-4f || planeDotQ <= 1e-4f) {
                return projZO;
            }

            // ZO near plane lies at z_clip = M2 . p = 0 and keeps z_clip >= 0.
            // Scaling viewPlane by (row3 . q) / (viewPlane . q) preserves the far plane at z_clip = w_clip.
            const glm::vec4 scaledPlane = viewPlane * (row3DotQ / planeDotQ);
            glm::mat4 obliqueZO = projZO;
            obliqueZO[0][2] = scaledPlane.x;
            obliqueZO[1][2] = scaledPlane.y;
            obliqueZO[2][2] = scaledPlane.z;
            obliqueZO[3][2] = scaledPlane.w;
            if (!IsFiniteMat4(obliqueZO)) {
                return projZO;
            }

            if (outObliqueGL) {
                glm::mat4 obliqueGL = obliqueZO;
                const glm::vec4 glRow2 = 2.0f * scaledPlane - row3;
                obliqueGL[0][2] = glRow2.x;
                obliqueGL[1][2] = glRow2.y;
                obliqueGL[2][2] = glRow2.z;
                obliqueGL[3][2] = glRow2.w;
                *outObliqueGL = obliqueGL;
            }
            if (outApplied) *outApplied = true;
            return obliqueZO;
        }

        MirroredCameraResult BuildMirroredCamera(
            const PerspectiveCamera& mainCamera,
            const PlanarReflectionSurface& surface,
            const PlanarReflectionPolicy& policy) {
            MirroredCameraResult result;
            if (!surface.Valid) {
                result.Diagnostic = "PLANAR_SURFACE_INVALID: non-finite or disabled reflector surface";
                return result;
            }
            const float signedDist = SignedDistanceToPlane(surface, mainCamera.GetPosition());
            result.CameraSignedDistance = signedDist;
            if (!std::isfinite(signedDist) || signedDist <= std::max(1e-5f, policy.MinCameraPlaneDistance)) {
                result.Diagnostic = "PLANAR_CAMERA_BEHIND_PLANE: camera must remain on the front side of the reflector plane";
                return result;
            }

            const glm::mat4 reflMatrix = BuildReflectionMatrix(surface);
            const glm::mat4 mirroredView = mainCamera.GetViewMatrix() * reflMatrix;
            const glm::vec3 mirroredPos = ReflectPoint(surface, mainCamera.GetPosition());
            if (!IsFiniteMat4(mirroredView) || !IsFiniteVec3(mirroredPos)) {
                result.Diagnostic = "PLANAR_CAMERA_INVALID: mirrored transform is non-finite";
                return result;
            }

            PlanarReflectionSurface clippedSurface = surface;
            clippedSurface.ClipPlaneOffset = surface.ClipPlaneOffset + policy.ClipPlaneOffset;
            const glm::vec4 worldPlane = clippedSurface.WorldPlane();

            PerspectiveCamera mirroredCam = mainCamera;
            // Capture depth range is authored independently from the main view.
            if (mainCamera.GetProjectionType() == CameraProjectionType::Orthographic)
                mirroredCam.SetOrthographic(mainCamera.GetOrthographicSize(), mainCamera.GetAspectRatio(), surface.CaptureNearPlane, surface.CaptureFarPlane);
            else
                mirroredCam.SetPerspective(mainCamera.GetFOV(), mainCamera.GetAspectRatio(), surface.CaptureNearPlane, surface.CaptureFarPlane);
            mirroredCam.SetViewMatrix(mirroredView, mirroredPos);
            const glm::mat4 baseProjZO = mirroredCam.GetUnclippedProjectionMatrixZO();

            glm::vec4 viewPlane{};
            bool obliqueApplied = false;
            glm::mat4 obliqueGL = mainCamera.GetProjectionMatrix();
            const glm::mat4 obliqueZO = BuildObliqueClipProjectionZO(
                baseProjZO, mirroredView, worldPlane, &viewPlane, &obliqueApplied, &obliqueGL);
            if (!obliqueApplied) {
                result.Diagnostic = "PLANAR_CLIP_INVALID: oblique clipping could not be constructed";
                return result;
            }
            mirroredCam.SetCustomProjection(obliqueGL, obliqueZO);

            result.Valid = true;
            result.ObliqueClippingApplied = obliqueApplied;
            result.ReverseWinding = (glm::determinant(glm::mat3(mirroredView)) < 0.0f);
            result.Camera = mirroredCam;
            result.ReflectionMatrix = reflMatrix;
            result.WorldPlane = worldPlane;
            result.ViewPlane = viewPlane;
            return result;
        }

        float EvaluatePlanarReflectionConfidence(
            const PlanarReflectionSurface& surface,
            const glm::vec3& worldPos,
            const glm::vec3& normalWS,
            float roughness,
            float maxRoughnessOverride, float viewDistance, float globalStrength) {
            if (!surface.Valid || !IsFiniteVec3(worldPos) || !IsFiniteVec3(normalWS)) return 0.0f;
            const float nDot = glm::dot(SafeNormalize(normalWS, glm::vec3(0.0f, 1.0f, 0.0f)), surface.Normal);
            auto smooth = [](float lo, float hi, float value) {
                const float t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            };
            const float normalWeight = smooth(0.95f, 0.995f, nDot);
            if (normalWeight <= 0.0f) return 0.0f;

            const float planeDist = std::abs(glm::dot(surface.Normal, worldPos - surface.Position));
            const float thicknessTolerance = std::max(0.02f, 0.002f * std::max(viewDistance, 0.0f));
            const float planeWeight = 1.0f - smooth(thicknessTolerance * 0.5f, thicknessTolerance, planeDist);
            if (planeWeight <= 0.0f) return 0.0f;

            float extentWeight = 1.0f;
            if (surface.ClipToExtents) {
                const glm::vec3 rel = worldPos - surface.Position;
                const float localU = std::abs(glm::dot(rel, surface.Tangent));
                const float localV = std::abs(glm::dot(rel, surface.Bitangent));
                if (localU >= surface.HalfExtents.x || localV >= surface.HalfExtents.y) {
                    return 0.0f;
                }
                const float fade = std::clamp(surface.EdgeFadeDistance, 0.0f, std::min(surface.HalfExtents.x, surface.HalfExtents.y));
                if (fade > 1e-4f) {
                    const float marginU = std::clamp((surface.HalfExtents.x - localU) / fade, 0.0f, 1.0f);
                    const float marginV = std::clamp((surface.HalfExtents.y - localV) / fade, 0.0f, 1.0f);
                    extentWeight = smooth(0.0f, 1.0f, std::min(marginU, marginV));
                }
            }

            const float maxRough = (maxRoughnessOverride > 0.0f)
                ? std::min(surface.MaxRoughness, std::clamp(maxRoughnessOverride, 0.02f, 1.0f))
                : surface.MaxRoughness;
            const float safeRough = std::clamp(roughness, 0.0f, 1.0f);
            const float fadeStart = maxRough * 0.65f;
            float roughWeight = 1.0f;
            if (safeRough >= maxRough) {
                roughWeight = 0.0f;
            } else if (safeRough > fadeStart) {
                const float t = (safeRough - fadeStart) / std::max(maxRough - fadeStart, 1e-4f);
                roughWeight = 1.0f - (t * t * (3.0f - 2.0f * t));
            }

            return std::clamp(normalWeight * planeWeight * extentWeight * roughWeight * std::clamp(surface.Intensity, 0.0f, 2.0f) * std::clamp(globalStrength, 0.0f, 2.0f), 0.0f, 1.0f);
        }

        glm::uvec2 ComputeScaledTargetExtent(uint32_t baseWidth, uint32_t baseHeight, float resolutionScale) {
            return {
                RHI::RHIViewTargets::ComputePlanarTargetDimension(baseWidth, resolutionScale),
                RHI::RHIViewTargets::ComputePlanarTargetDimension(baseHeight, resolutionScale)
            };
        }

        SceneRenderSettings SanitizeCaptureSettings(
            const SceneRenderSettings& mainSettings,
            const PlanarReflectionPolicy& policy) {
            SceneRenderSettings out = mainSettings;
            out.MSAASamples = 1u;
            out.EnableToneMapping = false;
            out.ToneMappingMode = 3; // Linear HDR output preserved in PlanarColor RGBA16F
            out.EnableFXAA = false;
            out.EnableBloom = false;
            out.EnableAutoExposure = false;
            out.EnableTAA = false;
            out.EnableSSR = policy.AllowSSR;
            out.EnablePlanarReflections = policy.AllowPlanarReflections;
            out.EnableGTAO = false;
            out.EnableContactShadows = false;
            out.EnableVolumetricFog = false;
            out.EnableDDGI = false;
            out.HiZVisualizationLevel = -1;
            out.Wireframe = policy.IncludeEditorOverlays ? mainSettings.Wireframe : false;
            out.ShowPhysicsDebug = policy.IncludeEditorOverlays ? mainSettings.ShowPhysicsDebug : false;
            out.EnableSkybox = policy.IncludeSkybox && mainSettings.EnableSkybox;
            out.EnableParticles = policy.IncludeParticles && mainSettings.EnableParticles;
            out.EnableShadows = policy.IncludeShadows && mainSettings.EnableShadows;
            return out;
        }

        RenderWorld FilterCaptureWorld(
            const RenderWorld& sourceWorld,
            const RenderPlanarReflector& reflector,
            const PlanarReflectionPolicy& policy) {
            RenderWorld out;
            out.Camera = sourceWorld.Camera;
            out.DirectionalLight = sourceWorld.DirectionalLight;
            out.AdditionalDirectionalLights = sourceWorld.AdditionalDirectionalLights;
            out.PointLights = sourceWorld.PointLights;
            out.SpotLights = sourceWorld.SpotLights;
            out.RectLights = sourceWorld.RectLights;
            out.Skybox = sourceWorld.Skybox;
            out.Skybox.Enabled = policy.IncludeSkybox && reflector.CaptureSkybox && sourceWorld.Skybox.Enabled;
            out.EntityToStableUUID = sourceWorld.EntityToStableUUID;
            out.SceneRevision = sourceWorld.SceneRevision;
            out.SceneInstanceID = sourceWorld.SceneInstanceID;
            out.SceneLifetimeEpoch = sourceWorld.SceneLifetimeEpoch;
            out.TotalEntities = sourceWorld.TotalEntities;

            // Recursion disabled: nested planar reflectors are stripped inside a planar capture.
            out.PlanarReflectors.clear();
            if (!policy.AllowDynamicProbes) {
                out.ReflectionProbes.clear();
            } else {
                out.ReflectionProbes = sourceWorld.ReflectionProbes;
            }
            if (policy.IncludeParticles) out.Particles = sourceWorld.Particles;
            if (policy.IncludeEditorOverlays) out.DebugLines = sourceWorld.DebugLines;

            if (!(policy.IncludeShadows && reflector.CaptureShadows)) {
                if (out.DirectionalLight.has_value()) out.DirectionalLight->CastsShadows = false;
                for (auto& dl : out.AdditionalDirectionalLights) dl.CastsShadows = false;
                for (auto& pl : out.PointLights) pl.Shadows.CastShadows = false;
                for (auto& sl : out.SpotLights) sl.Shadows.CastShadows = false;
            }

            std::unordered_set<uint64_t> excludedIds;
            if (reflector.EntityID != 0u) excludedIds.insert(reflector.EntityID);
            if (policy.ExcludeEntityID != 0u) excludedIds.insert(policy.ExcludeEntityID);
            for (uint64_t id : policy.AdditionalExcludedEntityIDs) {
                if (id != 0u) excludedIds.insert(id);
            }

            if (policy.IncludeOpaqueAndCutout) out.Skins = sourceWorld.Skins;
            out.Meshes.reserve(sourceWorld.Meshes.size());
            for (const auto& mesh : sourceWorld.Meshes) {
                // EnTT entity index zero is valid; its stable identity still
                // excludes the receiver without excluding anonymous meshes.
                const bool isReflector = reflector.StableUUID != UUID(0) && mesh.StableUUID == reflector.StableUUID;
                if (isReflector || (mesh.EntityID != 0u && excludedIds.count(static_cast<uint64_t>(mesh.EntityID)) > 0u)) {
                    continue;
                }
                const ResolvedSurface surface = MaterialResolver::Resolve(mesh.Material);
                const bool isTransparent =
                    (surface.Surface == SurfaceType::Transparent || surface.Surface == SurfaceType::Additive);
                if (isTransparent && !policy.IncludeTransparent) continue;
                if (!isTransparent && !policy.IncludeOpaqueAndCutout) continue;
                out.Meshes.push_back(mesh);
            }
            return out;
        }

        PlanarReflectionInvalidation EvaluateInvalidation(
            const PlanarReflectionDomainStamps& published,
            const PlanarReflectionDomainStamps& current,
            bool manualRequested) {
            PlanarReflectionInvalidation out;
            out.PlaneChanged = (published.PlaneStamp != current.PlaneStamp);
            out.CameraChanged = (published.CameraStamp != current.CameraStamp);
            out.SceneRevisionChanged = (published.SceneRevision != current.SceneRevision) ||
                                       (published.SceneInstanceID != current.SceneInstanceID) ||
                                       (published.SceneLifetimeEpoch != current.SceneLifetimeEpoch);
            out.GeometryChanged = (published.GeometryStamp != current.GeometryStamp);
            out.MaterialChanged = (published.MaterialStamp != current.MaterialStamp);
            out.LightingChanged = (published.LightingStamp != current.LightingStamp);
            out.AssetChanged = (published.AssetStamp != current.AssetStamp);
            out.SettingsChanged = (published.SettingsStamp != current.SettingsStamp);
            out.TargetExtentChanged = (published.TargetWidth != current.TargetWidth) ||
                                      (published.TargetHeight != current.TargetHeight);
            out.DeviceEpochChanged = (published.DeviceEpoch != current.DeviceEpoch);
            out.ManualRequested = manualRequested;
            return out;
        }
    } // namespace PlanarReflectionContract

    // =========================================================================
    // 10. Volumetric Froxel Fog Resources, Depth Slice & Tier Contracts (M15-U6-A)
    // =========================================================================
    namespace FroxelFogContract {

        namespace {
            static_assert(static_cast<uint8_t>(FroxelFogQuality::High) == RHI::RHIViewTargets::DefaultFroxelQualityTier);
            static_assert(static_cast<uint8_t>(FroxelFogQuality::Ultra) == RHI::RHIViewTargets::MaxFroxelQualityTier);

            uint8_t ResolveQualityTier(FroxelFogQuality quality) {
                const auto tier = static_cast<uint8_t>(quality);
                return tier <= RHI::RHIViewTargets::MaxFroxelQualityTier
                    ? tier : RHI::RHIViewTargets::DefaultFroxelQualityTier;
            }

            float EvaluateExponentialDepth(float nearZ, float farZ, double normalizedSlice) {
                return static_cast<float>(
                    static_cast<double>(nearZ) * std::pow(static_cast<double>(farZ) / static_cast<double>(nearZ), normalizedSlice));
            }
        }

        uint32_t GetQualityTierCount() {
            return RHI::RHIViewTargets::MaxFroxelQualityTier + 1u;
        }

        const RHI::RHIFroxelTierSpec& GetTierSpec(FroxelFogQuality quality) {
            return *RHI::RHIViewTargets::GetFroxelTierSpec(ResolveQualityTier(quality));
        }

        RHI::RHIFroxelGridExtent ComputeGridExtent(uint32_t viewWidth, uint32_t viewHeight, FroxelFogQuality quality) {
            const auto index = static_cast<uint32_t>(quality);
            if (index > RHI::RHIViewTargets::MaxFroxelQualityTier) return {};
            return RHI::RHIViewTargets::ComputeFroxelGridExtent(viewWidth, viewHeight, static_cast<uint8_t>(index));
        }

        bool ResolveDepthRange(float nearDistance, float maxDistance, float& outNear, float& outFar) {
            if (!std::isfinite(nearDistance) || !std::isfinite(maxDistance)) return false;
            if (nearDistance < MinNearDistance || maxDistance <= nearDistance || maxDistance > MaxAllowedDistance) {
                return false;
            }
            // Each half-slice step must exceed one float ULP so both boundaries
            // and centers remain distinct, even at the highest depth resolution.
            static const double minDepthRatio = std::pow(
                1.0 + std::numeric_limits<float>::epsilon(), 2u * GetTierSpec(FroxelFogQuality::Ultra).Limit.Depth);
            if (static_cast<double>(maxDistance) <= static_cast<double>(nearDistance) * minDepthRatio) return false;
            outNear = nearDistance;
            outFar = maxDistance;
            return true;
        }

        float SliceBoundaryToViewDistance(uint32_t sliceBoundary, uint32_t sliceCount, float nearDistance, float maxDistance) {
            float nearZ = 0.0f, farZ = 0.0f;
            if (sliceCount == 0u || sliceBoundary > sliceCount || !ResolveDepthRange(nearDistance, maxDistance, nearZ, farZ)) {
                return 0.0f;
            }
            return EvaluateExponentialDepth(nearZ, farZ, static_cast<double>(sliceBoundary) / static_cast<double>(sliceCount));
        }

        float SliceCenterToViewDistance(uint32_t sliceIndex, uint32_t sliceCount, float nearDistance, float maxDistance) {
            float nearZ = 0.0f, farZ = 0.0f;
            if (sliceCount == 0u || sliceIndex >= sliceCount || !ResolveDepthRange(nearDistance, maxDistance, nearZ, farZ)) {
                return 0.0f;
            }
            return EvaluateExponentialDepth(nearZ, farZ, (static_cast<double>(sliceIndex) + 0.5) / static_cast<double>(sliceCount));
        }

        float EvaluateSliceThickness(uint32_t sliceIndex, uint32_t sliceCount, float nearDistance, float maxDistance) {
            if (sliceCount == 0u || sliceIndex >= sliceCount) return 0.0f;
            const float z0 = SliceBoundaryToViewDistance(sliceIndex, sliceCount, nearDistance, maxDistance);
            const float z1 = SliceBoundaryToViewDistance(sliceIndex + 1u, sliceCount, nearDistance, maxDistance);
            if (!(z1 > z0)) return 0.0f;
            return z1 - z0;
        }

        float ViewDistanceToSliceCoord(float viewDistance, uint32_t sliceCount, float nearDistance, float maxDistance) {
            float nearZ = 0.0f, farZ = 0.0f;
            if (sliceCount == 0u || !std::isfinite(viewDistance) || !ResolveDepthRange(nearDistance, maxDistance, nearZ, farZ)) {
                return 0.0f;
            }
            const double clampedZ = std::clamp(static_cast<double>(viewDistance), static_cast<double>(nearZ), static_cast<double>(farZ));
            const double ratio = std::log(clampedZ / static_cast<double>(nearZ)) / std::log(static_cast<double>(farZ) / static_cast<double>(nearZ));
            return static_cast<float>(std::clamp(ratio, 0.0, 1.0));
        }

        float SliceCoordToViewDistance(float sliceCoord, float nearDistance, float maxDistance) {
            float nearZ = 0.0f, farZ = 0.0f;
            if (!std::isfinite(sliceCoord) || !ResolveDepthRange(nearDistance, maxDistance, nearZ, farZ)) {
                return 0.0f;
            }
            const double clamped = std::clamp(static_cast<double>(sliceCoord), 0.0, 1.0);
            return EvaluateExponentialDepth(nearZ, farZ, clamped);
        }

        FroxelMemoryBudget EstimateMemoryBudget(uint32_t viewWidth, uint32_t viewHeight, FroxelFogQuality quality, bool queryDevice) {
            FroxelMemoryBudget budget;
            budget.Extent = ComputeGridExtent(viewWidth, viewHeight, quality);
            if (!budget.Extent.Width || !budget.Extent.Height || !budget.Extent.Depth) {
                budget.AllocatedVolumeCount = 0u;
                return budget;
            }
            budget.FroxelCount = static_cast<uint64_t>(budget.Extent.Width) *
                                 static_cast<uint64_t>(budget.Extent.Height) *
                                 static_cast<uint64_t>(budget.Extent.Depth);
            budget.VolumeBytesPerTexture = budget.FroxelCount * BytesPerFroxelRGBA16F;
            budget.AllocatedVolumeCount = VolumeCount;
            budget.TotalLogicalBytes = budget.VolumeBytesPerTexture * static_cast<uint64_t>(VolumeCount);

            if (queryDevice && RHI::RHIDevice::Get().CanCreateResources()) {
                nri::MemoryDesc memDesc{};
                const auto texDesc = RHI::RHITextureDesc::StorageTexture3D(
                    budget.Extent.Width, budget.Extent.Height, budget.Extent.Depth,
                    RHI::RHITextureFormat::RGBA16_FLOAT, "FroxelBudgetProbe");
                if (RHI::RHITexture::QueryMemoryRequirements(texDesc, memDesc) && memDesc.size > 0u) {
                    budget.DeviceQueryAvailable = true;
                    budget.DeviceBytesPerTexture = memDesc.size;
                    budget.DeviceAllocationAlignment = memDesc.alignment;
                    budget.TotalDeviceReportedBytes = memDesc.size * static_cast<uint64_t>(VolumeCount);
                }
            }
            return budget;
        }

        bool ValidateResourcePolicy(const RHI::RHIViewTargetDesc& desc, const FroxelFogResourcePolicy& policy, std::string* outReason) {
            auto fail = [&](std::string msg) {
                if (outReason) *outReason = std::move(msg);
                return false;
            };
            if (!desc.Width || !desc.Height) return fail("View extent must be non-zero for froxel resources");
            if (!desc.RenderAuxiliary) return fail("Froxel resources require auxiliary view targets");
            if (!desc.EnableDepth || desc.Samples != 1u) return fail("Froxel resources require single-sample depth");
            if (desc.FroxelQuality > RHI::RHIViewTargets::MaxFroxelQualityTier) {
                return fail("Froxel quality tier is out of supported range [0..3]");
            }
            float nearZ = 0.0f, farZ = 0.0f;
            if (!ResolveDepthRange(policy.NearDistance, policy.MaxDistance, nearZ, farZ)) {
                return fail("Froxel depth range [near, max] is invalid or non-finite");
            }
            const auto budget = EstimateMemoryBudget(desc.Width, desc.Height, static_cast<FroxelFogQuality>(desc.FroxelQuality), false);
            if (!budget.FroxelCount || !budget.TotalLogicalBytes) {
                return fail("Computed froxel grid extent is empty");
            }
            if (policy.MaxBudgetBytes > 0u && budget.TotalLogicalBytes > policy.MaxBudgetBytes) {
                return fail("Froxel logical VRAM budget exceeds configured limit");
            }
            if (outReason) outReason->clear();
            return true;
        }

        namespace {
            float SanitizeNonNegative(float value) {
                return (std::isfinite(value) && value > 0.0f) ? value : 0.0f;
            }

            glm::vec3 SanitizeNonNegativeVec3(const glm::vec3& value) {
                return { SanitizeNonNegative(value.x), SanitizeNonNegative(value.y), SanitizeNonNegative(value.z) };
            }

            float EffectiveDensity(float density, float intensity) {
                constexpr double maxHalf = 65504.0;
                return static_cast<float>(std::min(maxHalf,
                    static_cast<double>(SanitizeNonNegative(density)) * SanitizeNonNegative(intensity)));
            }

            bool ResolveMediaDepthRange(const SceneRenderSettings& settings, const RenderVolumetricFog* authored,
                                        float& nearZ, float& farZ, bool& usedFallback) {
                usedFallback = false;
                if (authored && authored->Enabled) {
                    if (ResolveDepthRange(settings.FogNearDistance, authored->MaxDistance, nearZ, farZ))
                        return true;
                    usedFallback = true;
                }
                return ResolveDepthRange(settings.FogNearDistance, settings.FogMaxDistance, nearZ, farZ);
            }

            void AppendDiagnostic(std::string& dst, std::string_view msg) {
                if (msg.empty()) return;
                if (!dst.empty()) dst += "; ";
                dst.append(msg);
            }

            glm::vec3 ComputeStepInScatterPerMeter(const FroxelMediaState& state,
                                                   float worldY,
                                                   float phaseSun) {
                const float heightAboveGround = std::max(worldY, 0.0f);
                const float localDensity = state.EffectiveBaseDensity * std::exp(-heightAboveGround * state.HeightFalloff);
                if (!(localDensity > 0.0f)) return {};
                const glm::vec3 scatterCoeff = state.Albedo * localDensity;
                const glm::vec3 incomingRadiance =
                    (state.HasDirectionalSun ? state.SunRadiance * phaseSun : glm::vec3(0.0f)) +
                    (state.HasAmbientLight ? state.AmbientRadiance : glm::vec3(0.0f));
                return scatterCoeff * incomingRadiance;
            }
        }

        void ResolveViewTargetOptions(const SceneRenderSettings& settings, bool isSubCapture, bool hasDepth,
                                      bool& outEnable, uint8_t& outQuality, const RenderVolumetricFog* authored) {
            float nearZ = 0.0f, farZ = 0.0f;
            bool usedFallback = false;
            outEnable = settings.EnableVolumetricFog && !isSubCapture && hasDepth && settings.MSAASamples == 1u &&
                ResolveMediaDepthRange(settings, authored, nearZ, farZ, usedFallback);
            outQuality = outEnable ? ResolveQualityTier(settings.FogQuality) : RHI::RHIViewTargets::DefaultFroxelQualityTier;
        }

        bool RequiresLinearColor(const SceneRenderSettings& settings, const RenderVolumetricFog* authored) {
            return authored && authored->Enabled
                ? EffectiveDensity(authored->Density, authored->Intensity) > 0.0f
                : EffectiveDensity(settings.FogDensity, 1.0f) > 0.0f;
        }

        FroxelMediaState ResolveMediaState(const RenderWorld& world,
                                           const SceneRenderSettings& settings,
                                           const LightingFrame& lightingFrame) {
            FroxelMediaState state;
            const bool useWorld = world.VolumetricFog && world.VolumetricFog->Enabled;
            state.FromWorldComponent = useWorld;

            const float rawDensity = useWorld ? world.VolumetricFog->Density : settings.FogDensity;
            const float rawFalloff = useWorld ? world.VolumetricFog->HeightFalloff : settings.FogHeightFalloff;
            const float rawAsymmetry = useWorld ? world.VolumetricFog->ScatteringAsymmetry : settings.FogScattering;
            const glm::vec3 rawColor = useWorld ? world.VolumetricFog->Color : settings.FogColor;
            const float rawIntensity = useWorld ? world.VolumetricFog->Intensity : 1.0f;
            state.Density = SanitizeNonNegative(rawDensity);
            state.HeightFalloff = SanitizeNonNegative(rawFalloff);
            state.ScatteringAsymmetry = std::isfinite(rawAsymmetry)
                ? std::clamp(rawAsymmetry, -MaxAsymmetry, MaxAsymmetry) : 0.0f;
            state.Albedo = glm::min(SanitizeNonNegativeVec3(rawColor), glm::vec3(1.0f));
            state.Intensity = SanitizeNonNegative(rawIntensity);
            state.EffectiveBaseDensity = EffectiveDensity(state.Density, state.Intensity);

            bool usedFallback = false;
            if (!ResolveMediaDepthRange(settings, useWorld ? &*world.VolumetricFog : nullptr,
                                        state.NearDistance, state.MaxDistance, usedFallback)) {
                state.Diagnostic = "froxel_fog_disabled:invalid_depth_range";
                return state;
            }
            if (usedFallback) {
                state.AuthoredMaxDistanceInvalidFallback = true;
                AppendDiagnostic(state.Diagnostic, "froxel_fog_depth_fallback: invalid authored VolumetricFog MaxDistance");
            }
            state.Valid = true;

            const bool authoredSun = (lightingFrame.DirectionalCount > 0u) && world.DirectionalLight.has_value();
            if (authoredSun && std::isfinite(lightingFrame.Sun.Illuminance) && lightingFrame.Sun.Illuminance > 0.0f) {
                const float lenSq = glm::dot(lightingFrame.Sun.Direction, lightingFrame.Sun.Direction);
                if (std::isfinite(lenSq) && lenSq > 1e-12f) {
                    state.HasDirectionalSun = true;
                    state.SunDirectionToLight = lightingFrame.Sun.Direction * (1.0f / std::sqrt(lenSq));
                    state.SunRadiance = SanitizeNonNegativeVec3(lightingFrame.Sun.Color) *
                        (lightingFrame.Sun.Illuminance * DirectLuxToRadiance);
                }
            }

            const auto hasPositiveColor = [](const glm::vec3& c) {
                const glm::vec3 s = SanitizeNonNegativeVec3(c);
                return s.r > 0.0f || s.g > 0.0f || s.b > 0.0f;
            };
            for (uint32_t i = 0u; i < std::min<uint32_t>(lightingFrame.PointCount, MaxLightingPointLights); ++i) {
                const auto& p = lightingFrame.PointLights[i];
                const bool geomOk = p.Directional ? (glm::dot(p.Position, p.Position) > 1e-12f) : (std::isfinite(p.Radius) && p.Radius > 0.0f);
                if (geomOk && std::isfinite(p.Intensity) && p.Intensity > 0.0f && hasPositiveColor(p.Color)) {
                    state.HasLocalLight = true;
                    break;
                }
            }
            if (!state.HasLocalLight) {
                for (uint32_t i = 0u; i < std::min<uint32_t>(lightingFrame.SpotCount, MaxLightingSpotLights); ++i) {
                    const auto& s = lightingFrame.SpotLights[i];
                    if (std::isfinite(s.Range) && s.Range > 0.0f && std::isfinite(s.Intensity) && s.Intensity > 0.0f && hasPositiveColor(s.Color)) {
                        state.HasLocalLight = true;
                        break;
                    }
                }
            }
            if (!state.HasLocalLight) {
                for (uint32_t i = 0u; i < std::min<uint32_t>(lightingFrame.RectCount, MaxLightingRectLights); ++i) {
                    const auto& r = lightingFrame.RectLights[i];
                    if (std::isfinite(r.Range) && r.Range > 1e-3f && std::isfinite(r.SignedIntensity) && std::abs(r.SignedIntensity) > 0.0f && hasPositiveColor(r.Color)) {
                        state.HasLocalLight = true;
                        break;
                    }
                }
            }

            if (std::isfinite(lightingFrame.Sun.AmbientIntensity) && lightingFrame.Sun.AmbientIntensity > 0.0f) {
                const glm::vec3 amb = SanitizeNonNegativeVec3(lightingFrame.Sun.AmbientColor) * lightingFrame.Sun.AmbientIntensity;
                if (amb.r > 0.0f || amb.g > 0.0f || amb.b > 0.0f) {
                    state.HasAmbientLight = true;
                    state.AmbientRadiance = amb;
                }
            }

            if (!state.HasDirectionalSun) {
                AppendDiagnostic(state.Diagnostic,
                    state.HasLocalLight || state.HasAmbientLight
                        ? "froxel_fog_no_directional_sun: scattering uses authored local or environment radiance"
                        : "froxel_fog_no_directional_sun: unlit scattering uses zero radiance");
            }
            if (!state.HasAnyLighting()) {
                AppendDiagnostic(state.Diagnostic,
                    "froxel_fog_unlit_extinction_only: zero in-scattering with Beer-Lambert extinction preserved");
            }
            return state;
        }

        float EvaluateHenyeyGreenstein(float cosTheta, float asymmetryG) {
            constexpr double kPi = 3.14159265358979323846;
            const double c = std::isfinite(cosTheta) ? std::clamp(static_cast<double>(cosTheta), -1.0, 1.0) : 0.0;
            const double g = std::isfinite(asymmetryG)
                ? std::clamp(static_cast<double>(asymmetryG), -static_cast<double>(MaxAsymmetry), static_cast<double>(MaxAsymmetry))
                : 0.0;
            const double g2 = g * g;
            const double denom = std::max(1.0 + g2 - 2.0 * g * c, 1e-12);
            return static_cast<float>((1.0 - g2) / (4.0 * kPi * denom * std::sqrt(denom)));
        }

        FroxelSampleIntegral EvaluateHomogeneousFogStep(const glm::vec3& inScatterPerMeter, float sigmaT, float pathLength) {
            FroxelSampleIntegral result;
            if (!(pathLength > 0.0f) || !(sigmaT > 0.0f)) {
                if (pathLength > 0.0f) {
                    result.InScattering = glm::max(inScatterPerMeter, glm::vec3(0.0f)) * pathLength;
                }
                return result;
            }
            const float opticalDepth = sigmaT * pathLength;
            const float stepT = std::exp(-opticalDepth);
            const float integFactor = (opticalDepth > 1e-4f)
                ? ((1.0f - stepT) / sigmaT)
                : (pathLength * (1.0f - 0.5f * opticalDepth + (1.0f / 6.0f) * opticalDepth * opticalDepth));
            result.InScattering = glm::max(inScatterPerMeter, glm::vec3(0.0f)) * integFactor;
            result.Transmittance = stepT;
            return result;
        }

        FroxelSampleIntegral EvaluateAnalyticHomogeneousFog(const FroxelMediaState& state,
                                                            const glm::vec3& rayDirWS,
                                                            float viewZ,
                                                            float rayLengthPerUnitZ) {
            FroxelSampleIntegral result;
            if (!state.Valid || !(state.EffectiveBaseDensity > 0.0f) || !(viewZ > state.NearDistance)) return result;
            const float dirLenSq = glm::dot(rayDirWS, rayDirWS);
            if (!std::isfinite(dirLenSq) || !(dirLenSq > 1e-12f) || !std::isfinite(rayLengthPerUnitZ) || !(rayLengthPerUnitZ > 0.0f)) {
                return result;
            }
            const glm::vec3 unitRay = rayDirWS * (1.0f / std::sqrt(dirLenSq));
            const float clampedZ = std::min(viewZ, state.MaxDistance);
            const float pathLength = (clampedZ - state.NearDistance) * rayLengthPerUnitZ;
            const float phaseSun = state.HasDirectionalSun
                ? EvaluateHenyeyGreenstein(glm::dot(unitRay, state.SunDirectionToLight), state.ScatteringAsymmetry)
                : 0.0f;
            const glm::vec3 incomingRadiance =
                (state.HasDirectionalSun ? state.SunRadiance * phaseSun : glm::vec3(0.0f)) +
                (state.HasAmbientLight ? state.AmbientRadiance : glm::vec3(0.0f));
            const float sigmaT = state.EffectiveBaseDensity;
            const glm::vec3 inScatterPerMeter = (state.Albedo * sigmaT) * incomingRadiance;
            return EvaluateHomogeneousFogStep(inScatterPerMeter, sigmaT, pathLength);
        }

        FroxelSampleIntegral EvaluateFroxelColumnQuadrature(const FroxelMediaState& state,
                                                            const glm::vec3& rayOriginWS,
                                                            const glm::vec3& rayDirWorldPerUnitZ,
                                                            uint32_t sliceCount,
                                                            float targetViewZ) {
            FroxelSampleIntegral out;
            if (!state.Valid || sliceCount == 0u || !(state.EffectiveBaseDensity > 0.0f) || !(targetViewZ > state.NearDistance)) {
                return out;
            }
            const float rayLenPerUnitZ = glm::length(rayDirWorldPerUnitZ);
            if (!std::isfinite(rayLenPerUnitZ) || !(rayLenPerUnitZ > 0.0f)) return out;
            const glm::vec3 unitRay = rayDirWorldPerUnitZ * (1.0f / rayLenPerUnitZ);
            const float phaseSun = state.HasDirectionalSun
                ? EvaluateHenyeyGreenstein(glm::dot(unitRay, state.SunDirectionToLight), state.ScatteringAsymmetry)
                : 0.0f;

            const float clampedZ = std::min(targetViewZ, state.MaxDistance);
            for (uint32_t iz = 0u; iz < sliceCount; ++iz) {
                const float z0 = SliceBoundaryToViewDistance(iz, sliceCount, state.NearDistance, state.MaxDistance);
                if (z0 >= clampedZ) break;
                const float zCenter = SliceCenterToViewDistance(iz, sliceCount, state.NearDistance, state.MaxDistance);
                const float z1 = SliceBoundaryToViewDistance(iz + 1u, sliceCount, state.NearDistance, state.MaxDistance);
                const float ds = (std::min(z1, clampedZ) - z0) * rayLenPerUnitZ;
                const glm::vec3 sampleWS = rayOriginWS + rayDirWorldPerUnitZ * zCenter;
                const float height = std::max(sampleWS.y, 0.0f);
                const float sigmaT = state.EffectiveBaseDensity * std::exp(-height * state.HeightFalloff);
                const glm::vec3 scatterPerM = ComputeStepInScatterPerMeter(state, sampleWS.y, phaseSun);
                const auto step = EvaluateHomogeneousFogStep(scatterPerM, sigmaT, ds);
                out.InScattering += out.Transmittance * step.InScattering;
                out.Transmittance *= step.Transmittance;
            }
            return out;
        }

        FroxelSampleIntegral EvaluateReferenceRayQuadrature(const FroxelMediaState& state,
                                                            const glm::vec3& rayOriginWS,
                                                            const glm::vec3& rayDirWorldPerUnitZ,
                                                            float targetViewZ,
                                                            uint32_t quadratureSteps) {
            FroxelSampleIntegral accum;
            if (!state.Valid || quadratureSteps == 0u || !(state.EffectiveBaseDensity > 0.0f) || !(targetViewZ > state.NearDistance)) {
                return accum;
            }
            const float rayLenPerUnitZ = glm::length(rayDirWorldPerUnitZ);
            if (!std::isfinite(rayLenPerUnitZ) || !(rayLenPerUnitZ > 0.0f)) return accum;
            const glm::vec3 unitRay = rayDirWorldPerUnitZ * (1.0f / rayLenPerUnitZ);
            const float phaseSun = state.HasDirectionalSun
                ? EvaluateHenyeyGreenstein(glm::dot(unitRay, state.SunDirectionToLight), state.ScatteringAsymmetry)
                : 0.0f;

            const double zStart = static_cast<double>(state.NearDistance);
            const double zEnd = static_cast<double>(std::min(targetViewZ, state.MaxDistance));
            const double dz = (zEnd - zStart) / static_cast<double>(quadratureSteps);
            const double ds = dz * rayLenPerUnitZ;
            const glm::dvec3 source = glm::dvec3(state.Albedo) *
                (glm::dvec3(state.SunRadiance) * static_cast<double>(phaseSun) + glm::dvec3(state.AmbientRadiance));
            double accumT = 1.0;
            glm::dvec3 accumS(0.0);
            for (uint32_t i = 0u; i < quadratureSteps; ++i) {
                const double zMid = zStart + (static_cast<double>(i) + 0.5) * dz;
                const double height = std::max(rayOriginWS.y + rayDirWorldPerUnitZ.y * zMid, 0.0);
                const double tau = state.EffectiveBaseDensity * std::exp(-height * state.HeightFalloff) * ds;
                accumS += accumT * (-std::expm1(-tau)) * source;
                accumT *= std::exp(-tau);
            }
            accum.InScattering = glm::vec3(accumS);
            accum.Transmittance = static_cast<float>(accumT);
            return accum;
        }

        namespace {
            glm::vec3 EvaluateLocalScatteringCoeff(const FroxelMediaState& state, const glm::vec3& sampleWS) {
                if (!state.Valid || !(state.EffectiveBaseDensity > 0.0f)) return {};
                const float heightAboveGround = std::max(sampleWS.y, 0.0f);
                const float localDensity = state.EffectiveBaseDensity * std::exp(-heightAboveGround * state.HeightFalloff);
                return SanitizeNonNegativeVec3(state.Albedo) * std::max(localDensity, 0.0f);
            }
        }

        glm::vec3 EvaluatePointLightInScatter(const FroxelMediaState& state,
                                              const LightingPointFrame& light,
                                              const glm::vec3& sampleWS,
                                              const glm::vec3& unitRayWS,
                                              float shadowVisibility) {
            const glm::vec3 sigmaS = EvaluateLocalScatteringCoeff(state, sampleWS);
            if (!(sigmaS.x > 0.0f || sigmaS.y > 0.0f || sigmaS.z > 0.0f)) return {};
            const float dirLenSq = glm::dot(unitRayWS, unitRayWS);
            if (!std::isfinite(dirLenSq) || !(dirLenSq > 1e-12f)) return {};
            const glm::vec3 rayDir = unitRayWS * (1.0f / std::sqrt(dirLenSq));
            if (light.Directional) {
                const float lLenSq = glm::dot(light.Position, light.Position);
                if (!std::isfinite(lLenSq) || !(lLenSq > 1e-12f)) return {};
                const glm::vec3 L = light.Position * (1.0f / std::sqrt(lLenSq));
                const float phase = EvaluateHenyeyGreenstein(glm::dot(rayDir, L), state.ScatteringAsymmetry);
                return sigmaS * SanitizeNonNegativeVec3(light.Color) * (SanitizeNonNegative(light.Intensity) * phase);
            }
            if (!std::isfinite(light.Radius) || !(light.Radius > 0.0f)) return {};
            const glm::vec3 lightVec = light.Position - sampleWS;
            const float dist = glm::length(lightVec);
            if (!std::isfinite(dist) || !(dist < light.Radius) || !(dist > 1e-4f)) return {};
            const glm::vec3 L = lightVec / dist;
            const float minRadiusSq = std::max(0.0025f, light.MinRadius * light.MinRadius);
            const float distSq = std::max(dist * dist, minRadiusSq);
            const float ratio = dist / light.Radius;
            const float ratio4 = ratio * ratio * ratio * ratio;
            const float window = std::clamp(1.0f - ratio4, 0.0f, 1.0f);
            float attenuation = (window * window) / distSq;
            if (light.Falloff > 0.1f && std::abs(light.Falloff - 1.0f) > 0.01f) {
                attenuation = std::pow(std::max(attenuation, 0.0f), light.Falloff);
            }
            const float phase = EvaluateHenyeyGreenstein(glm::dot(rayDir, L), state.ScatteringAsymmetry);
            const float vis = std::isfinite(shadowVisibility) ? std::clamp(shadowVisibility, 0.0f, 1.0f) : 1.0f;
            return sigmaS * SanitizeNonNegativeVec3(light.Color) *
                (SanitizeNonNegative(light.Intensity) * attenuation * LocalIntensityScale * phase * vis);
        }

        glm::vec3 EvaluateSpotLightInScatter(const FroxelMediaState& state,
                                             const LightingSpotFrame& light,
                                             const glm::vec3& sampleWS,
                                             const glm::vec3& unitRayWS,
                                             float shadowVisibility) {
            const glm::vec3 sigmaS = EvaluateLocalScatteringCoeff(state, sampleWS);
            if (!(sigmaS.x > 0.0f || sigmaS.y > 0.0f || sigmaS.z > 0.0f) ||
                !std::isfinite(light.Range) || !(light.Range > 0.0f)) {
                return {};
            }
            const float dirLenSq = glm::dot(unitRayWS, unitRayWS);
            if (!std::isfinite(dirLenSq) || !(dirLenSq > 1e-12f)) return {};
            const glm::vec3 rayDir = unitRayWS * (1.0f / std::sqrt(dirLenSq));
            const glm::vec3 lightVec = light.Position - sampleWS;
            const float dist = glm::length(lightVec);
            if (!std::isfinite(dist) || !(dist < light.Range) || !(dist > 1e-4f)) return {};
            const glm::vec3 L = lightVec / dist;
            const float spotLen = glm::length(light.Direction);
            if (!std::isfinite(spotLen) || !(spotLen > 1e-5f)) return {};
            const glm::vec3 spotDir = light.Direction / spotLen;
            const float cosCone = glm::dot(-L, spotDir);
            if (!(cosCone > light.CosOuterAngle)) return {};
            float spotAtten = std::clamp(
                (cosCone - light.CosOuterAngle) / std::max(1e-4f, light.CosInnerAngle - light.CosOuterAngle),
                0.0f, 1.0f);
            spotAtten *= spotAtten;
            const float distSq = std::max(dist * dist, 0.0025f);
            const float ratio = dist / light.Range;
            const float ratio4 = ratio * ratio * ratio * ratio;
            const float window = std::clamp(1.0f - ratio4, 0.0f, 1.0f);
            const float distAtten = (window * window) / distSq;
            const float phase = EvaluateHenyeyGreenstein(glm::dot(rayDir, L), state.ScatteringAsymmetry);
            const float vis = std::isfinite(shadowVisibility) ? std::clamp(shadowVisibility, 0.0f, 1.0f) : 1.0f;
            return sigmaS * SanitizeNonNegativeVec3(light.Color) *
                (SanitizeNonNegative(light.Intensity) * distAtten * spotAtten * LocalIntensityScale * phase * vis);
        }

        glm::vec3 EvaluateRectLightInScatter(const FroxelMediaState& state,
                                             const LightingRectFrame& light,
                                             const glm::vec3& sampleWS,
                                             const glm::vec3& unitRayWS) {
            constexpr float kPi = 3.14159265358979323846f;
            const glm::vec3 sigmaS = EvaluateLocalScatteringCoeff(state, sampleWS);
            const float intensity = std::isfinite(light.SignedIntensity) ? std::abs(light.SignedIntensity) : 0.0f;
            const bool twoSided = light.SignedIntensity < 0.0f;
            if (!(sigmaS.x > 0.0f || sigmaS.y > 0.0f || sigmaS.z > 0.0f) ||
                !std::isfinite(light.Range) || !(light.Range > 1e-3f) || !(intensity > 0.0f)) {
                return {};
            }
            const float dirLenSq = glm::dot(unitRayWS, unitRayWS);
            if (!std::isfinite(dirLenSq) || !(dirLenSq > 1e-12f)) return {};
            const glm::vec3 rayDir = unitRayWS * (1.0f / std::sqrt(dirLenSq));
            const glm::vec3 lightVec = light.Position - sampleWS;
            const float dist = glm::length(lightVec);
            if (!std::isfinite(dist) || !(dist < light.Range) || !(dist > 1e-4f)) return {};
            const glm::vec3 L = lightVec / dist;
            const float emitLen = glm::length(light.Direction);
            const glm::vec3 lightDir = (std::isfinite(emitLen) && emitLen > 1e-6f)
                ? (light.Direction / emitLen) : glm::vec3(0.0f, 0.0f, -1.0f);
            const float signedForward = glm::dot(-L, lightDir);
            if (!twoSided && !(signedForward > 0.0f)) return {};
            const float emitCos = twoSided ? std::abs(signedForward) : std::max(signedForward, 0.0f);
            const float w = std::max(light.Width, 0.01f);
            const float h = std::max(light.Height, 0.01f);
            const float area = w * h * ((light.Shape == AreaLightShape::Disk) ? (kPi * 0.25f) : 1.0f);
            const float eqRadiusSq = area / kPi;
            const float slant = std::sqrt(dist * dist + eqRadiusSq);
            const float solidAngleFactor = 2.0f * eqRadiusSq / (slant * (slant + dist)) * emitCos;
            const float ratio = std::clamp(dist / light.Range, 0.0f, 1.0f);
            const float ratio4 = ratio * ratio * ratio * ratio;
            const float win = std::clamp(1.0f - ratio4, 0.0f, 1.0f);
            const float rangeAtten = win * win;
            const float phase = EvaluateHenyeyGreenstein(glm::dot(rayDir, L), state.ScatteringAsymmetry);
            return sigmaS * SanitizeNonNegativeVec3(light.Color) *
                (intensity * rangeAtten * LocalIntensityScale * solidAngleFactor * phase);
        }

    } // namespace FroxelFogContract

    namespace DDGIContract {
        namespace {
            bool IsFiniteVec3(const glm::vec3& v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            }
        }

        RHI::RHIDDGIAtlasExtent ComputeAtlasExtent(const glm::ivec3& dims) {
            if (dims.x <= 0 || dims.y <= 0 || dims.z <= 0) return {};
            return RHI::RHIViewTargets::ComputeDDGIAtlasExtent(
                static_cast<uint32_t>(dims.x),
                static_cast<uint32_t>(dims.y),
                static_cast<uint32_t>(dims.z));
        }

        bool ValidateGrid(const RenderDDGIProbeGrid& grid, std::string* outReason) {
            auto fail = [&](const char* msg) {
                if (outReason) *outReason = msg;
                return false;
            };
            if (!std::isfinite(grid.EnergyMultiplier) || grid.EnergyMultiplier < 0.0f)
                return fail("DDGI energy multiplier must be finite and non-negative");
            if (!IsFiniteVec3(grid.GridOrigin) || !IsFiniteVec3(grid.GridSpacing) ||
                grid.GridSpacing.x <= 0.0f || grid.GridSpacing.y <= 0.0f || grid.GridSpacing.z <= 0.0f)
                return fail("DDGI grid origin/spacing must be finite with strictly positive spacing");
            if (grid.GridDimensions.x <= 0 || grid.GridDimensions.y <= 0 || grid.GridDimensions.z <= 0)
                return fail("DDGI grid dimensions must be strictly positive");
            const auto extent = ComputeAtlasExtent(grid.GridDimensions);
            if (!extent.Valid)
                return fail("DDGI grid exceeds bounded probe/atlas budget (max 16384 probes, max 8192 atlas extent)");
            if (!std::isfinite(grid.Hysteresis) || grid.Hysteresis < 0.0f || grid.Hysteresis > 0.99f)
                return fail("DDGI hysteresis must be in [0, 0.99]");
            if (!std::isfinite(grid.ViewBias) || grid.ViewBias < 0.0f ||
                !std::isfinite(grid.NormalBias) || grid.NormalBias < 0.0f)
                return fail("DDGI view and normal bias must be finite and non-negative");
            if (!std::isfinite(grid.UpdateFraction) || grid.UpdateFraction <= 0.0f || grid.UpdateFraction > 1.0f)
                return fail("DDGI update fraction must be in (0, 1]");
            if (grid.MaxProbesPerFrame <= 0)
                return fail("DDGI max probes per frame must be positive");
            if (outReason) outReason->clear();
            return true;
        }

        glm::ivec3 ProbeIndexToCoord(uint32_t probeIndex, const glm::ivec3& dims) {
            if (dims.x <= 0 || dims.y <= 0 || dims.z <= 0) return {0, 0, 0};
            const uint32_t dx = static_cast<uint32_t>(dims.x);
            const uint32_t dz = static_cast<uint32_t>(dims.z);
            const uint32_t plane = dx * dz;
            const uint32_t iy = probeIndex / plane;
            const uint32_t rem = probeIndex % plane;
            const uint32_t iz = rem / dx;
            const uint32_t ix = rem % dx;
            return { static_cast<int>(ix), static_cast<int>(iy), static_cast<int>(iz) };
        }

        uint32_t ProbeCoordToIndex(const glm::ivec3& coord, const glm::ivec3& dims) {
            const glm::ivec3 c = glm::clamp(coord, glm::ivec3(0), glm::max(dims - 1, glm::ivec3(0)));
            return static_cast<uint32_t>((c.y * dims.z + c.z) * dims.x + c.x);
        }

        glm::vec3 ProbeCoordToWorldPos(const glm::ivec3& coord, const RenderDDGIProbeGrid& grid) {
            return grid.GridOrigin + glm::vec3(coord) * grid.GridSpacing;
        }

        glm::vec3 ProbeIndexToWorldPos(uint32_t probeIndex, const RenderDDGIProbeGrid& grid) {
            return ProbeCoordToWorldPos(ProbeIndexToCoord(probeIndex, grid.GridDimensions), grid);
        }

        glm::ivec2 ProbeIndexToAtlasTile(uint32_t probeIndex, uint32_t atlasCols) {
            const uint32_t cols = std::max(1u, atlasCols);
            return { static_cast<int>(probeIndex % cols), static_cast<int>(probeIndex / cols) };
        }

        void ComputeCaptureScratchLayout(uint32_t maxProbesPerFrame, uint32_t& outCols, uint32_t& outRows,
                                         uint32_t& outWidth, uint32_t& outHeight) {
            const uint32_t probes = std::clamp(maxProbesPerFrame, 1u, HardMaxProbesPerFrame);
            const uint32_t totalFaces = probes * FacesPerProbe;
            // Arrange probes cleanly in 4 probe-rows of 6 faces when >= 4 probes, else 1 row per probe
            outCols = probes >= 4u ? 24u : FacesPerProbe * probes;
            outRows = (totalFaces + outCols - 1u) / outCols;
            outWidth = outCols * CaptureFaceResolution;
            outHeight = outRows * CaptureFaceResolution;
        }

        glm::ivec2 CaptureSlotToTile(uint32_t probeSlot, uint32_t faceIndex, uint32_t captureCols) {
            const uint32_t cols = std::max(1u, captureCols);
            const uint32_t linearFace = probeSlot * FacesPerProbe + (faceIndex % FacesPerProbe);
            return { static_cast<int>(linearFace % cols), static_cast<int>(linearFace / cols) };
        }

        uint32_t ComputeStaggeredUpdateCount(uint32_t probeCount, float updateFraction,
                                             uint32_t maxProbesPerFrame, bool resetSeed) {
            if (probeCount == 0u) return 0u;
            const uint32_t hardCap = std::clamp(maxProbesPerFrame, 1u, HardMaxProbesPerFrame);
            const float fraction = std::isfinite(updateFraction) ? std::clamp(updateFraction, 0.01f, 1.0f) : DefaultUpdateFraction;
            if (resetSeed && probeCount <= hardCap) return probeCount;
            const uint32_t desired = std::max(1u, static_cast<uint32_t>(std::ceil(static_cast<double>(probeCount) * fraction)));
            return std::min({ probeCount, hardCap, desired });
        }

        float ComputeProbeMaxDistance(const glm::vec3& gridSpacing) {
            const glm::vec3 safeSpacing = glm::max(gridSpacing, glm::vec3(0.05f));
            return std::clamp(glm::length(safeSpacing) * 1.75f, 4.0f, 120.0f);
        }

        uint64_t ComputeStateHash(const RenderWorld& world, const SceneRenderSettings& settings,
                                  uint64_t materialGeneration) {
            uint64_t hash = 1469598103934665603ull;
            const auto mixBytes = [&](const void* data, size_t size) {
                const auto* ptr = static_cast<const uint8_t*>(data);
                for (size_t i = 0; i < size; ++i) {
                    hash ^= ptr[i];
                    hash *= 1099511628211ull;
                }
            };
            const auto mixVal = [&](const auto& v) { mixBytes(&v, sizeof(v)); };
            mixVal(materialGeneration);
            mixVal(settings.EnableShadows);
            mixVal(settings.EnableSkybox);
            if (world.DDGIProbeGrid) {
                const auto& g = *world.DDGIProbeGrid;
                mixVal(g.Enabled);
                mixVal(g.GridDimensions);
                mixVal(g.GridSpacing);
                mixVal(g.GridOrigin);
            }
            const uint8_t hasDir = world.DirectionalLight.has_value() ? 1u : 0u;
            mixVal(hasDir);
            if (world.DirectionalLight) {
                const auto& d = *world.DirectionalLight;
                mixVal(d.Direction);
                mixVal(d.Color);
                mixVal(d.Intensity);
                mixVal(d.AmbientIntensity);
                mixVal(d.CastsShadows);
            }
            const size_t ptCount = world.PointLights.size();
            mixVal(ptCount);
            for (const auto& p : world.PointLights) {
                mixVal(p.Position); mixVal(p.Color); mixVal(p.Intensity); mixVal(p.Radius); mixVal(p.Falloff);
            }
            const size_t spCount = world.SpotLights.size();
            mixVal(spCount);
            for (const auto& s : world.SpotLights) {
                mixVal(s.Position); mixVal(s.Direction); mixVal(s.Color); mixVal(s.Intensity);
                mixVal(s.Range); mixVal(s.InnerAngle); mixVal(s.OuterAngle);
            }
            const size_t rcCount = world.RectLights.size();
            mixVal(rcCount);
            for (const auto& r : world.RectLights) {
                mixVal(r.Position); mixVal(r.Direction); mixVal(r.Up); mixVal(r.Color);
                mixVal(r.Intensity); mixVal(r.Width); mixVal(r.Height); mixVal(r.Range); mixVal(r.TwoSided);
            }
            mixVal(world.Skybox.Authored);
            mixVal(world.Skybox.Enabled);
            mixVal(world.Skybox.EnvironmentLighting);
            mixVal(world.Skybox.Type);
            mixVal(world.Skybox.Intensity);
            mixVal(world.Skybox.Exposure);
            mixVal(world.Skybox.EnvironmentRotation);
            mixVal(world.Skybox.SunDirection);
            mixVal(world.Skybox.SunColor);
            mixVal(world.Skybox.ZenithColor);
            mixVal(world.Skybox.HorizonColor);
            mixVal(world.Skybox.GroundColor);
            const size_t meshCount = world.Meshes.size();
            mixVal(meshCount);
            for (const auto& m : world.Meshes) {
                mixVal(m.WorldTransform);
                mixVal(m.CastsShadows);
                mixVal(m.Material.AlbedoColor);
                mixVal(m.Material.SecondaryColor);
                mixVal(m.Material.Emission);
                mixVal(m.Material.ContentRevision);
                mixVal(m.Material.Roughness);
                mixVal(m.Material.Metallic);
                mixVal(m.Material.PatternMode);
                mixVal(m.Material.TileScale);
                mixVal(m.Material.UseCustomShader);
                mixBytes(m.Material.CustomShaderPath.data(), m.Material.CustomShaderPath.size());
                mixBytes(m.Material.AlbedoMapPath.data(), m.Material.AlbedoMapPath.size());
                mixBytes(m.Material.EmissiveMapPath.data(), m.Material.EmissiveMapPath.size());
                mixBytes(m.Material.MaterialPath.data(), m.Material.MaterialPath.size());
                if (m.Material.RHIMaterialAsset) {
                    const auto& md = m.Material.RHIMaterialAsset->GetDesc();
                    mixVal(md.BaseColor);
                    mixVal(md.EmissiveColor);
                    mixVal(md.EmissiveIntensity);
                    mixVal(md.Roughness);
                    mixVal(md.Metallic);
                    mixVal(md.ShadingModel);
                }
                const auto* nativeViewPtr = m.NativeView.get();
                mixVal(nativeViewPtr);
                mixVal(m.MeshSourceUUID);
            }
            return hash;
        }

        DDGIMemoryBudget EstimateMemoryBudget(const glm::ivec3& dims, uint32_t maxProbesPerFrame, bool queryDevice) {
            DDGIMemoryBudget budget;
            budget.Extent = ComputeAtlasExtent(dims);
            if (!budget.Extent.Valid) return budget;
            constexpr uint64_t bytesPerRGBA16F = 8ull;
            budget.IrradianceBytes = static_cast<uint64_t>(budget.Extent.IrradianceWidth) *
                budget.Extent.IrradianceHeight * bytesPerRGBA16F;
            budget.DistanceBytes = static_cast<uint64_t>(budget.Extent.DistanceWidth) *
                budget.Extent.DistanceHeight * bytesPerRGBA16F;
            uint32_t capCols = 0, capRows = 0;
            ComputeCaptureScratchLayout(maxProbesPerFrame, capCols, capRows, budget.CaptureWidth, budget.CaptureHeight);
            // Color (RGBA16F = 8 B/px) + Depth (D32F = 4 B/px) shared across views
            budget.CaptureScratchBytes = static_cast<uint64_t>(budget.CaptureWidth) * budget.CaptureHeight * 12ull;
            budget.TotalLogicalBytes = budget.IrradianceBytes + budget.DistanceBytes + budget.CaptureScratchBytes;
            if (queryDevice && RHI::RHIDevice::Get().CanCreateResources()) {
                nri::MemoryDesc irradMem{}, distMem{};
                const auto irradDesc = RHI::RHITextureDesc::StorageTexture2D(
                    budget.Extent.IrradianceWidth, budget.Extent.IrradianceHeight,
                    RHI::RHITextureFormat::RGBA16_FLOAT, "DDGIIrradBudgetProbe");
                const auto distDesc = RHI::RHITextureDesc::StorageTexture2D(
                    budget.Extent.DistanceWidth, budget.Extent.DistanceHeight,
                    RHI::RHITextureFormat::RGBA16_FLOAT, "DDGIDistBudgetProbe");
                if (RHI::RHITexture::QueryMemoryRequirements(irradDesc, irradMem) && irradMem.size > 0u &&
                    RHI::RHITexture::QueryMemoryRequirements(distDesc, distMem) && distMem.size > 0u) {
                    budget.DeviceQueryAvailable = true;
                    budget.DeviceIrradianceBytes = irradMem.size;
                    budget.DeviceDistanceBytes = distMem.size;
                    budget.TotalDeviceReportedBytes = irradMem.size + distMem.size;
                }
            }
            return budget;
        }

        void ResolveViewTargetOptions(const SceneRenderSettings& settings, bool isSubCapture,
                                      const RenderDDGIProbeGrid* authored,
                                      bool& outEnable, std::array<uint32_t, 3>& outGridDims) {
            outGridDims = {0u, 0u, 0u};
            outEnable = settings.EnableDDGI && !isSubCapture && settings.MSAASamples == 1u;
            if (!outEnable) return;
            if (authored) {
                if (!authored->Enabled || !ValidateGrid(*authored)) {
                    outEnable = false;
                    return;
                }
                outGridDims = {
                    static_cast<uint32_t>(authored->GridDimensions.x),
                    static_cast<uint32_t>(authored->GridDimensions.y),
                    static_cast<uint32_t>(authored->GridDimensions.z)
                };
            }
        }
    } // namespace DDGIContract

    namespace AutoExposureContract {
        bool QuantizeLogLuminanceBin(float luminance, float minLogLuma, float maxLogLuma, uint32_t& outBin) {
            outBin = 0u;
            if (!std::isfinite(luminance) || luminance <= BlackPixelEpsilon) return false;
            const float logRange = std::max(maxLogLuma - minLogLuma, 0.01f);
            const float norm = std::clamp((std::log2(luminance) - minLogLuma) / logRange, 0.0f, 1.0f);
            const uint32_t bin = static_cast<uint32_t>(norm * static_cast<float>(HistogramBinCount - 1u) + 0.5f);
            outBin = std::min(bin, HistogramBinCount - 1u);
            return true;
        }

        ReferenceExposureResult ComputeReferenceAdaptation(float prevExposure,
                                                           const float* pixelRGBData,
                                                           uint32_t pixelCount,
                                                           const SceneRenderSettings& settings,
                                                           float deltaSeconds,
                                                           bool resetExposure,
                                                           float minLogLuma,
                                                           float maxLogLuma) {
            ReferenceExposureResult result{};
            const float minExp = std::min(settings.AutoExposureMin, settings.AutoExposureMax);
            const float maxExp = std::max(settings.AutoExposureMin, settings.AutoExposureMax);
            const float fallbackExp = std::clamp(
                (std::isfinite(settings.Exposure) && settings.Exposure > 0.0f) ? settings.Exposure : 1.0f,
                minExp, maxExp);
            float validPrev = resetExposure ? fallbackExp : prevExposure;
            if (!std::isfinite(validPrev) || validPrev <= 0.0f) validPrev = fallbackExp;
            validPrev = std::clamp(validPrev, minExp, maxExp);

            std::array<uint64_t, HistogramBinCount> histogram{};
            uint64_t validCount = 0;
            if (pixelRGBData && pixelCount > 0u) {
                for (uint32_t i = 0; i < pixelCount; ++i) {
                    const float r = pixelRGBData[i * 3u + 0u];
                    const float g = pixelRGBData[i * 3u + 1u];
                    const float b = pixelRGBData[i * 3u + 2u];
                    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) continue;
                    const float luma = std::max(r, 0.0f) * 0.2126f + std::max(g, 0.0f) * 0.7152f + std::max(b, 0.0f) * 0.0722f;
                    uint32_t bin = 0u;
                    if (QuantizeLogLuminanceBin(luma, minLogLuma, maxLogLuma, bin)) {
                        ++histogram[bin];
                        ++validCount;
                    }
                }
            }

            result.ValidPixelCount = validCount;
            if (validCount == 0u) {
                result.BlackFrame = true;
                result.AverageLuminance = 0.0f;
                result.TargetExposure = resetExposure ? fallbackExp : validPrev;
                result.AdaptedExposure = result.TargetExposure;
                return result;
            }

            const float logRange = std::max(maxLogLuma - minLogLuma, 0.01f);
            const double invTotal = 1.0 / static_cast<double>(std::max<uint32_t>(pixelCount, 1u));
            double normWeightSum = 0.0;
            double weightedLogSum = 0.0;
            for (uint32_t bin = 0; bin < HistogramBinCount; ++bin) {
                if (histogram[bin] == 0u) continue;
                const double w = static_cast<double>(histogram[bin]) * invTotal;
                const double binLog = static_cast<double>(minLogLuma) +
                    (static_cast<double>(bin) / static_cast<double>(HistogramBinCount - 1u)) * static_cast<double>(logRange);
                normWeightSum += w;
                weightedLogSum += w * binLog;
            }

            const float avgLuma = static_cast<float>(std::exp2(weightedLogSum / std::max(normWeightSum, 1e-12)));
            const float keyValue = (std::isfinite(settings.AutoExposureKeyValue) && settings.AutoExposureKeyValue > 0.0f)
                ? settings.AutoExposureKeyValue : 0.18f;
            const float targetExp = std::clamp(keyValue / std::max(avgLuma, BlackPixelEpsilon), minExp, maxExp);
            result.AverageLuminance = avgLuma;
            result.TargetExposure = targetExp;

            if (resetExposure) {
                result.AdaptedExposure = targetExp;
            } else {
                const float dt = (std::isfinite(deltaSeconds) && deltaSeconds > 0.0f) ? deltaSeconds : 0.0f;
                const float speed = (std::isfinite(settings.AutoExposureSpeed) && settings.AutoExposureSpeed > 0.0f)
                    ? settings.AutoExposureSpeed : 0.0f;
                const float blend = 1.0f - std::exp(-dt * speed);
                result.AdaptedExposure = std::clamp(validPrev + (targetExp - validPrev) * blend, minExp, maxExp);
            }
            return result;
        }
    } // namespace AutoExposureContract

    namespace BloomContract {
        MipExtent ComputeMipExtent(uint32_t fullWidth, uint32_t fullHeight, uint32_t level) {
            return {
                std::max(1u, fullWidth >> (level + 1u)),
                std::max(1u, fullHeight >> (level + 1u))
            };
        }

        glm::vec3 EvaluatePrefilter(const glm::vec3& color, float threshold, float knee) {
            auto sanitize = [](float v) -> float {
                if (!std::isfinite(v)) return 0.0f;
                return std::clamp(v, 0.0f, 65000.0f);
            };
            const glm::vec3 clean(sanitize(color.r), sanitize(color.g), sanitize(color.b));
            const float brightness = std::max({ clean.r, clean.g, clean.b });
            const float safeThreshold = std::isfinite(threshold) ? threshold : 1.0f;
            const float safeKnee = (std::isfinite(knee) && knee > 0.0f) ? knee : 0.0f;
            const float softKnee = std::max(safeThreshold * safeKnee, 0.0f);
            float rq = std::clamp(brightness - safeThreshold + softKnee, 0.0f, 2.0f * softKnee);
            rq = (rq * rq) / std::max(4.0f * softKnee, 1e-5f);
            const float contribution = std::max(rq, brightness - safeThreshold) / std::max(brightness, 1e-5f);
            return clean * std::clamp(contribution, 0.0f, 1.0f);
        }

        glm::vec3 EvaluateAdditiveApply(const glm::vec3& sceneColor, const glm::vec3& bloomLevel0,
                                        const glm::vec3& tint, float intensity) {
            const glm::vec3 safeTint = glm::max(tint, glm::vec3(0.0f));
            const float safeIntensity = (std::isfinite(intensity) && intensity > 0.0f) ? intensity : 0.0f;
            return sceneColor + bloomLevel0 * (safeTint * safeIntensity * PyramidNormalization);
        }

        bool VerifySixLevelPyramidDimensions(uint32_t fullWidth, uint32_t fullHeight) {
            if (fullWidth == 0u || fullHeight == 0u) return false;
            uint32_t w = fullWidth;
            uint32_t h = fullHeight;
            for (uint32_t level = 0; level < PyramidLevels; ++level) {
                w = std::max(1u, w >> 1);
                h = std::max(1u, h >> 1);
                const MipExtent computed = ComputeMipExtent(fullWidth, fullHeight, level);
                if (computed.Width != w || computed.Height != h) return false;
                if (computed.Width == 0u || computed.Height == 0u) return false;
                if (level > 0u) {
                    const MipExtent prev = ComputeMipExtent(fullWidth, fullHeight, level - 1u);
                    if (computed.Width > prev.Width || computed.Height > prev.Height) return false;
                }
            }
            return true;
        }

        bool VerifyAdditiveEnergyAndSingleTint(const glm::vec3& hdrRadiance,
                                               float threshold, float knee,
                                               const glm::vec3& tint, float intensity) {
            // Threshold runs strictly once at level 0.
            const glm::vec3 level0Filtered = EvaluatePrefilter(hdrRadiance, threshold, knee);
            const glm::vec3 rethresholded = EvaluatePrefilter(level0Filtered, threshold, knee);
            if (threshold > 0.0f && glm::length(level0Filtered) > 1e-4f &&
                glm::length(rethresholded) >= glm::length(level0Filtered) - 1e-4f) {
                return false;
            }
            // Six uniform downsamples summed bottom-up without overwriting finer mips yield 6x level0.
            std::array<glm::vec3, PyramidLevels> mips{};
            mips.fill(level0Filtered);
            for (int level = static_cast<int>(PyramidLevels) - 2; level >= 0; --level) {
                mips[static_cast<size_t>(level)] += mips[static_cast<size_t>(level + 1)];
            }
            const glm::vec3 applied1x = EvaluateAdditiveApply(glm::vec3(0.0f), mips[0], tint, intensity);
            const glm::vec3 applied2x = EvaluateAdditiveApply(glm::vec3(0.0f), mips[0], tint, intensity * 2.0f);
            const glm::vec3 expected = level0Filtered * glm::max(tint, glm::vec3(0.0f)) * std::max(intensity, 0.0f);
            if (glm::length(applied1x - expected) > 1e-4f) return false;
            if (glm::length(applied2x - applied1x * 2.0f) > 1e-4f) return false;
            return true;
        }
    } // namespace BloomContract

    namespace MotionVectorContract {
        glm::vec2 ProjectWorldPointToUV(const glm::mat4& unjitteredViewProjZO,
                                        const glm::vec3& worldPos,
                                        bool& outValid) {
            const glm::vec4 clip = unjitteredViewProjZO * glm::vec4(worldPos, 1.0f);
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) || clip.w <= 1e-6f) {
                outValid = false;
                return { 0.0f, 0.0f };
            }
            const glm::vec2 ndc = glm::vec2(clip.x, clip.y) / clip.w;
            outValid = std::isfinite(ndc.x) && std::isfinite(ndc.y);
            return { ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f };
        }

        glm::vec2 ComputeWorldVelocityUV(const glm::mat4& currUnjitteredVP,
                                         const glm::mat4& prevUnjitteredVP,
                                         const glm::vec3& currWorldPos,
                                         const glm::vec3& prevWorldPos,
                                         bool& outValid) {
            bool currValid = false;
            bool prevValid = false;
            const glm::vec2 currUV = ProjectWorldPointToUV(currUnjitteredVP, currWorldPos, currValid);
            const glm::vec2 prevUV = ProjectWorldPointToUV(prevUnjitteredVP, prevWorldPos, prevValid);
            outValid = currValid && prevValid;
            if (!outValid) return { 0.0f, 0.0f };
            return currUV - prevUV;
        }

        glm::vec2 ComputeObjectVelocityUV(const glm::mat4& currUnjitteredVP,
                                          const glm::mat4& prevUnjitteredVP,
                                          const glm::mat4& currModel,
                                          const glm::mat4& prevModel,
                                          const glm::vec3& localPos,
                                          bool& outValid) {
            const glm::vec3 currWorld = glm::vec3(currModel * glm::vec4(localPos, 1.0f));
            const glm::vec3 prevWorld = glm::vec3(prevModel * glm::vec4(localPos, 1.0f));
            return ComputeWorldVelocityUV(currUnjitteredVP, prevUnjitteredVP, currWorld, prevWorld, outValid);
        }

        bool VerifyCameraAndObjectMotionVectors() {
            const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
            const glm::mat4 prevView = glm::lookAtRH(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
            // Camera pans right (+X): static object at origin moves left in screen space -> negative velocity.x
            const glm::mat4 currViewPan = glm::lookAtRH(glm::vec3(1.0f, 0.0f, 5.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0, 1, 0));
            bool ok = false;
            const glm::vec2 camPanVel = ComputeWorldVelocityUV(proj * currViewPan, proj * prevView, glm::vec3(0.0f), glm::vec3(0.0f), ok);
            if (!ok || camPanVel.x >= -0.05f || std::abs(camPanVel.y) > 1e-4f) return false;
            const glm::vec2 recCamPrev = ReconstructHistoryUV(ProjectWorldPointToUV(proj * currViewPan, glm::vec3(0.0f), ok), camPanVel);
            const glm::vec2 expectedCamPrev = ProjectWorldPointToUV(proj * prevView, glm::vec3(0.0f), ok);
            if (glm::length(recCamPrev - expectedCamPrev) > 1e-5f) return false;

            // Static camera, object translates right (+X): moves right in screen space -> positive velocity.x
            const glm::mat4 prevM(1.0f);
            const glm::mat4 currM = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec2 objVel = ComputeObjectVelocityUV(proj * prevView, proj * prevView, currM, prevM, glm::vec3(0.0f), ok);
            if (!ok || objVel.x <= 0.05f || std::abs(objVel.y) > 1e-4f) return false;
            const glm::vec2 recObjPrev = ReconstructHistoryUV(ProjectWorldPointToUV(proj * prevView, glm::vec3(1.0f, 0.0f, 0.0f), ok), objVel);
            if (glm::length(recObjPrev - glm::vec2(0.5f, 0.5f)) > 1e-5f) return false;

            // Static camera + static object => exact zero velocity
            const glm::vec2 zeroVel = ComputeObjectVelocityUV(proj * prevView, proj * prevView, prevM, prevM, glm::vec3(0.25f, -0.3f, 0.1f), ok);
            return ok && glm::length(zeroVel) < 1e-7f;
        }

        bool VerifyJitterCancellationConsistency() {
            const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
            const glm::mat4 view = glm::lookAtRH(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
            const glm::mat4 unjitteredVP = proj * view;
            // Even when rasterizer uses distinct Halton jitters in prev & curr frames, unjittered
            // motion vectors produce exact zero velocity for static geometry.
            bool ok = false;
            const glm::vec2 staticVel = ComputeObjectVelocityUV(unjitteredVP, unjitteredVP, glm::mat4(1.0f), glm::mat4(1.0f), glm::vec3(0.3f, 0.4f, -0.2f), ok);
            return ok && std::abs(staticVel.x) < 1e-7f && std::abs(staticVel.y) < 1e-7f;
        }
    } // namespace MotionVectorContract

    namespace TAAContract {
        namespace {
            glm::vec3 TonemapWeight(const glm::vec3& c) {
                const float maxC = std::max({ c.r, c.g, c.b, 0.0f });
                return glm::max(c, glm::vec3(0.0f)) / (1.0f + maxC);
            }
            glm::vec3 RGBToYCoCg(const glm::vec3& rgb) {
                return {
                    0.25f * rgb.r + 0.50f * rgb.g + 0.25f * rgb.b,
                    0.50f * rgb.r - 0.50f * rgb.b,
                    -0.25f * rgb.r + 0.50f * rgb.g - 0.25f * rgb.b
                };
            }
        }

        glm::ivec2 SelectClosestDepthOffset3x3(const float depthGrid3x3[9]) {
            float closest = 1.0f;
            glm::ivec2 best(0, 0);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const float d = depthGrid3x3[(dy + 1) * 3 + (dx + 1)];
                    if (d < closest) {
                        closest = d;
                        best = { dx, dy };
                    }
                }
            }
            return best;
        }

        float EvaluateHistoryWeight(const glm::vec3* currentNeighborsRGB3x3,
                                    const glm::vec3& historyRGB,
                                    const glm::vec2& historyUV,
                                    float velocityPixels,
                                    float feedbackMin,
                                    float feedbackMax,
                                    float reactive) {
            if (!currentNeighborsRGB3x3 ||
                !std::isfinite(historyUV.x) || !std::isfinite(historyUV.y) ||
                historyUV.x < 0.0f || historyUV.x > 1.0f || historyUV.y < 0.0f || historyUV.y > 1.0f) {
                return 0.0f;
            }
            glm::vec3 m1(0.0f), m2(0.0f), boxMin(1e5f), boxMax(-1e5f);
            for (int i = 0; i < 9; ++i) {
                const glm::vec3 y = RGBToYCoCg(TonemapWeight(currentNeighborsRGB3x3[i]));
                m1 += y;
                m2 += y * y;
                boxMin = glm::min(boxMin, y);
                boxMax = glm::max(boxMax, y);
            }
            const glm::vec3 mu = m1 / 9.0f;
            const glm::vec3 sigma = glm::sqrt(glm::max(m2 / 9.0f - mu * mu, glm::vec3(0.0f)));
            const glm::vec3 hy = RGBToYCoCg(TonemapWeight(historyRGB));
            const glm::vec3 outOfBox = glm::max(glm::vec3(0.0f), glm::max(boxMin - hy, hy - boxMax));
            const float boxSpread = std::max(glm::length(sigma), 0.02f);
            const float disocclusionRatio = glm::length(outOfBox) / boxSpread;
            const float disocclusionGate = std::exp(-disocclusionRatio * 2.5f);
            const float motionFactor = std::clamp(velocityPixels * 0.25f, 0.0f, 1.0f);
            const float r = std::clamp(reactive, 0.0f, 1.0f);
            const float effectiveMax = feedbackMax + (0.08f - feedbackMax) * r;
            const float effectiveMin = feedbackMin + (0.04f - feedbackMin) * r;
            const float baseFeedback = std::min(effectiveMax + (effectiveMin - effectiveMax) * motionFactor, effectiveMax);
            return std::clamp(baseFeedback * disocclusionGate, 0.0f, 0.96f);
        }

        float ComputeTransparentReactiveWeight(SurfaceType surfaceType,
                                             float alpha,
                                             float emissiveStrength) {
            if (surfaceType == SurfaceType::Additive) {
                return 0.92f;
            }
            if (surfaceType == SurfaceType::Transparent) {
                return std::clamp(std::max(alpha, 0.65f), 0.35f, 0.95f);
            }
            if (emissiveStrength > 1.0f) {
                return std::clamp((emissiveStrength - 1.0f) * 0.25f, 0.0f, 0.90f);
            }
            return 0.0f;
        }

        TemporalOverlayPolicy ResolveTemporalOverlayPolicy(const SceneRenderSettings& settings) {
            TemporalOverlayPolicy policy{};
            const bool taaActive = settings.EnableTAA && settings.MSAASamples == 1;
            policy.ApplySceneJitter = taaActive;
            policy.ExcludeTransparentFromVelocity = true;
            policy.StampReactiveMask = taaActive;
            policy.DeferPhysicsDebugAfterTAA = taaActive;
            policy.ComposeUIAfterPostProcess = true;
            policy.KeepEditorGizmosUnjittered = true;
            return policy;
        }

        bool VerifyClosestDepthDilation() {
            const float depths[9] = {
                0.90f, 0.90f, 0.90f,
                0.90f, 0.85f, 0.20f,
                0.90f, 0.90f, 0.90f
            };
            const glm::ivec2 off = SelectClosestDepthOffset3x3(depths);
            return off.x == 1 && off.y == 0;
        }

        bool VerifyDisocclusionAndBoundsRejection() {
            const glm::vec3 redTile[9] = {
                {0.9f, 0.05f, 0.05f}, {0.88f, 0.06f, 0.05f}, {0.92f, 0.04f, 0.05f},
                {0.89f, 0.05f, 0.05f}, {0.90f, 0.05f, 0.05f}, {0.91f, 0.05f, 0.05f},
                {0.87f, 0.05f, 0.05f}, {0.90f, 0.05f, 0.05f}, {0.93f, 0.05f, 0.05f}
            };
            const float stableW = EvaluateHistoryWeight(redTile, {0.90f, 0.05f, 0.05f}, {0.5f, 0.5f}, 0.0f, 0.88f, 0.96f);
            const float disoccW = EvaluateHistoryWeight(redTile, {0.02f, 0.02f, 0.85f}, {0.5f, 0.5f}, 0.0f, 0.88f, 0.96f);
            const float outOfBoundsW = EvaluateHistoryWeight(redTile, {0.90f, 0.05f, 0.05f}, {-0.01f, 0.5f}, 0.0f, 0.88f, 0.96f);
            return stableW > 0.90f && disoccW < 0.02f && outOfBoundsW == 0.0f;
        }

        bool VerifyNoSilentFXAAOnTAA() {
            SceneRenderSettings s;
            s.EnableTAA = true;
            s.EnableFXAA = true;
            s.MSAASamples = 1;
            const auto resolved = RenderBackendPolicy::FromBackend(RendererBackendType::ModernRHI_Vulkan).ResolveSettings(s);
            return resolved.EnableTAA && !resolved.EnableFXAA;
        }

        bool VerifyTransparentAndReactivePolicy() {
            glm::vec3 neighbors[9];
            for (int i = 0; i < 9; ++i) neighbors[i] = glm::vec3(0.4f);
            const float wOpaqueStatic = EvaluateHistoryWeight(neighbors, glm::vec3(0.4f), glm::vec2(0.5f), 0.0f, 0.55f, 0.90f, 0.0f);
            const float reactiveGlass = ComputeTransparentReactiveWeight(SurfaceType::Transparent, 0.35f, 0.0f);
            const float reactiveAdditive = ComputeTransparentReactiveWeight(SurfaceType::Additive, 0.10f, 0.0f);
            const float reactiveEmissive = ComputeTransparentReactiveWeight(SurfaceType::Opaque, 1.0f, 3.0f);
            const float wGlass = EvaluateHistoryWeight(neighbors, glm::vec3(0.4f), glm::vec2(0.5f), 0.0f, 0.55f, 0.90f, reactiveGlass);
            const float wAdditive = EvaluateHistoryWeight(neighbors, glm::vec3(0.4f), glm::vec2(0.5f), 0.0f, 0.55f, 0.90f, reactiveAdditive);
            SceneRenderSettings s;
            s.EnableTAA = true;
            s.MSAASamples = 1;
            const auto policy = ResolveTemporalOverlayPolicy(s);
            return wOpaqueStatic > 0.85f &&
                   reactiveGlass >= 0.64f && reactiveAdditive >= 0.90f && reactiveEmissive >= 0.45f &&
                   wGlass < 0.40f && wAdditive < 0.20f &&
                   policy.ExcludeTransparentFromVelocity && policy.StampReactiveMask;
        }

        bool VerifyUnjitteredOverlayPolicy() {
            SceneRenderSettings taaOn;
            taaOn.EnableTAA = true;
            taaOn.MSAASamples = 1;
            const auto pOn = ResolveTemporalOverlayPolicy(taaOn);

            SceneRenderSettings taaOff;
            taaOff.EnableTAA = false;
            const auto pOff = ResolveTemporalOverlayPolicy(taaOff);

            return pOn.ApplySceneJitter && pOn.DeferPhysicsDebugAfterTAA &&
                   pOn.ComposeUIAfterPostProcess && pOn.KeepEditorGizmosUnjittered &&
                   !pOff.ApplySceneJitter && !pOff.DeferPhysicsDebugAfterTAA &&
                   pOff.ComposeUIAfterPostProcess && pOff.KeepEditorGizmosUnjittered;
        }
    } // namespace TAAContract

    // =========================================================================
    // M15 Unit 9 (S9.1 / U9-A): Charlie Sheen BRDF & Energy Conservation Contract
    // =========================================================================
    namespace SheenContract {
        namespace {
            constexpr double kPi = 3.14159265358979323846;

            float CharlieLambdaL(float x, float r) {
                const float a = (1.0f - r) * (1.0f - r);
                const float aP = 21.5473f * (1.0f - a) + 25.3245f * a;
                const float bP = 3.82987f * (1.0f - a) + 3.32435f * a;
                const float cP = 0.19823f * (1.0f - a) + 0.16801f * a;
                const float dP = -1.97760f * (1.0f - a) + (-1.27393f) * a;
                const float eP = -4.32054f * (1.0f - a) + (-4.85967f) * a;
                return aP / (1.0f + bP * std::pow(std::max(x, 1e-4f), cP)) + dP * x + eP;
            }

            float ComputeSpecularOcclusionCPU(float nDotV, float ao, float roughness) {
                const float safeAO = std::clamp(ao, 0.0f, 1.0f);
                const float safeNV = std::clamp(nDotV, 0.0f, 1.0f);
                const float safeR = std::clamp(roughness, 0.04f, 1.0f);
                const float exponent = std::exp2(-16.0f * safeR - 1.0f);
                return std::clamp(std::pow(std::max(safeNV + safeAO, 1e-5f), exponent) - 1.0f + safeAO, 0.0f, 1.0f);
            }

            constexpr float kCharlieDirectionalAlbedo[81] = {
                0.94686f, 0.80236f, 0.51774f, 0.29026f, 0.13585f, 0.04851f, 0.01113f, 0.00107f, 0.00000f,
                0.77604f, 0.71234f, 0.52575f, 0.35491f, 0.21707f, 0.11480f, 0.04814f, 0.01327f, 0.00054f,
                0.71266f, 0.68189f, 0.53754f, 0.39638f, 0.27422f, 0.17260f, 0.09351f, 0.03958f, 0.00634f,
                0.70532f, 0.69193f, 0.56630f, 0.43882f, 0.32503f, 0.22493f, 0.13973f, 0.07394f, 0.02236f,
                0.73129f, 0.72829f, 0.60932f, 0.48622f, 0.37505f, 0.27452f, 0.18490f, 0.11150f, 0.04746f,
                0.77572f, 0.77877f, 0.65960f, 0.53552f, 0.42334f, 0.32072f, 0.22719f, 0.14864f, 0.07726f,
                0.82342f, 0.82936f, 0.70687f, 0.57972f, 0.46529f, 0.36033f, 0.26385f, 0.18221f, 0.10728f,
                0.85550f, 0.86255f, 0.73777f, 0.60912f, 0.49406f, 0.38864f, 0.29149f, 0.20930f, 0.13393f,
                0.85011f, 0.85837f, 0.73713f, 0.61256f, 0.50165f, 0.40012f, 0.30630f, 0.22712f, 0.15451f
            };
        } // namespace

        float EvaluateCharlieNDF(float nDotH, float roughness) {
            const float r = std::clamp(roughness, 0.04f, 1.0f);
            const float invAlpha = 1.0f / r;
            const float nh = std::clamp(nDotH, 0.0f, 1.0f);
            const float sin2H = std::max(1.0f - nh * nh, 0.0078125f);
            return (2.0f + invAlpha) / (2.0f * static_cast<float>(kPi)) * std::pow(sin2H, 0.5f * invAlpha);
        }

        float EvaluateCharlieLambda(float nDotW, float roughness) {
            const float r = std::clamp(roughness, 0.04f, 1.0f);
            const float x = std::clamp(nDotW, 1e-4f, 1.0f);
            if (x < 0.5f) {
                return std::exp(CharlieLambdaL(x, r));
            }
            return std::exp(2.0f * CharlieLambdaL(0.5f, r) - CharlieLambdaL(1.0f - x, r));
        }

        float EvaluateCharlieVisibility(float nDotL, float nDotV, float roughness) {
            const float r = std::clamp(roughness, 0.04f, 1.0f);
            const float nl = std::max(nDotL, 1e-4f);
            const float nv = std::max(nDotV, 1e-4f);
            const float lambdaV = EvaluateCharlieLambda(nv, r);
            const float lambdaL = std::pow(EvaluateCharlieLambda(nl, r),
                                           1.0f + 2.0f * std::pow(std::max(0.0f, 1.0f - nl), 8.0f));
            return 1.0f / std::max(4.0f * nl * nv * (1.0f + lambdaV + lambdaL), 1e-4f);
        }

        float EvaluateCharlieDirectionalAlbedo(float cosTheta, float roughness) {
            const float safeCos = std::clamp(cosTheta, 0.0f, 1.0f);
            const float safeR = std::clamp(roughness, 0.04f, 1.0f);
            const float u = std::clamp(std::sqrt(safeCos), 0.0f, 1.0f) * 8.0f;
            const float v = std::clamp((std::sqrt(safeR) - 0.2f) / 0.8f, 0.0f, 1.0f) * 8.0f;
            const uint32_t x0 = std::min(static_cast<uint32_t>(std::floor(u)), 7u);
            const uint32_t y0 = std::min(static_cast<uint32_t>(std::floor(v)), 7u);
            const float tx = u - static_cast<float>(x0);
            const float ty = v - static_cast<float>(y0);
            const uint32_t row0 = y0 * 9u;
            const uint32_t row1 = (y0 + 1u) * 9u;
            const float c00 = kCharlieDirectionalAlbedo[row0 + x0];
            const float c10 = kCharlieDirectionalAlbedo[row0 + x0 + 1u];
            const float c01 = kCharlieDirectionalAlbedo[row1 + x0];
            const float c11 = kCharlieDirectionalAlbedo[row1 + x0 + 1u];
            const float rowA = c00 + (c10 - c00) * tx;
            const float rowB = c01 + (c11 - c01) * tx;
            return std::clamp(rowA + (rowB - rowA) * ty, 0.0f, 1.0f);
        }

        float ComputeSheenBaseAttenuation(const glm::vec3& sheenColor,
                                          float nDotV,
                                          float nDotL,
                                          float sheenRoughness,
                                          bool toggleEnabled) {
            if (!toggleEnabled || sheenRoughness < 0.0f) {
                return 1.0f;
            }
            const glm::vec3 sc = glm::clamp(sheenColor, glm::vec3(0.0f), glm::vec3(1.0f));
            const float maxSheen = std::max(sc.r, std::max(sc.g, sc.b));
            if (maxSheen <= 0.0f) {
                return 1.0f;
            }
            const float r = std::clamp(sheenRoughness, 0.04f, 1.0f);
            const float eV = EvaluateCharlieDirectionalAlbedo(nDotV, r);
            const float eL = EvaluateCharlieDirectionalAlbedo(nDotL, r);
            return std::clamp(1.0f - maxSheen * std::max(eV, eL), 0.0f, 1.0f);
        }

        glm::vec3 ApplyCharlieSheenDirect(const glm::vec3& baseLighting,
                                          const glm::vec3& N,
                                          const glm::vec3& V,
                                          const glm::vec3& L,
                                          bool sheenEnabled,
                                          const glm::vec3& sheenColor,
                                          float sheenRoughness,
                                          bool specularHighlightsEnabled) {
            if (!sheenEnabled || !specularHighlightsEnabled || sheenRoughness < 0.0f) {
                return baseLighting;
            }
            const float nl = std::max(glm::dot(N, L), 0.0f);
            if (nl <= 0.0f) {
                return baseLighting;
            }
            const glm::vec3 sc = glm::clamp(sheenColor, glm::vec3(0.0f), glm::vec3(1.0f));
            const float maxSheen = std::max(sc.r, std::max(sc.g, sc.b));
            if (maxSheen <= 0.0f) {
                return baseLighting;
            }
            const float nv = std::clamp(glm::dot(N, V), 0.001f, 1.0f);
            const float r = std::clamp(sheenRoughness, 0.04f, 1.0f);
            const glm::vec3 sum = V + L;
            const float sumLenSq = glm::dot(sum, sum);
            const glm::vec3 H = sumLenSq > 1e-12f ? (sum / std::sqrt(sumLenSq)) : N;
            const float dSheen = EvaluateCharlieNDF(std::max(glm::dot(N, H), 0.0f), r);
            const float vSheen = EvaluateCharlieVisibility(nl, nv, r);
            const float atten = ComputeSheenBaseAttenuation(sc, nv, nl, r, true);
            return baseLighting * atten + sc * (dSheen * vSheen * nl);
        }

        glm::vec3 ApplyCharlieSheenIBL(const glm::vec3& ambientBase,
                                       const glm::vec3& sheenEnv,
                                       float nDotV,
                                       float ao,
                                       bool sheenEnabled,
                                       const glm::vec3& sheenColor,
                                       float sheenRoughness,
                                       bool environmentReflectionsEnabled) {
            if (!sheenEnabled || !environmentReflectionsEnabled || sheenRoughness < 0.0f) {
                return ambientBase;
            }
            const glm::vec3 sc = glm::clamp(sheenColor, glm::vec3(0.0f), glm::vec3(1.0f));
            const float maxSheen = std::max(sc.r, std::max(sc.g, sc.b));
            if (maxSheen <= 0.0f) {
                return ambientBase;
            }
            const float r = std::clamp(sheenRoughness, 0.04f, 1.0f);
            const float nv = std::clamp(nDotV, 0.001f, 1.0f);
            const float eSheen = EvaluateCharlieDirectionalAlbedo(nv, r);
            const float atten = std::clamp(1.0f - maxSheen * eSheen, 0.0f, 1.0f);
            const float specAO = ComputeSpecularOcclusionCPU(nv, ao, r);
            return ambientBase * atten + sheenEnv * sc * (eSheen * specAO);
        }

        double IntegrateCharlieDirectionalAlbedoReference(double nDotV, double roughness) {
            const double nv = std::clamp(nDotV, 1e-4, 1.0);
            const double r = std::clamp(roughness, 0.04, 1.0);
            constexpr int kTheta = 64;
            constexpr int kPhi = 128;
            const double dTheta = (0.5 * kPi) / kTheta;
            const double dPhi = (2.0 * kPi) / kPhi;
            const glm::dvec3 V(std::sqrt(std::max(0.0, 1.0 - nv * nv)), 0.0, nv);
            double sum = 0.0;
            for (int t = 0; t < kTheta; ++t) {
                const double theta = (t + 0.5) * dTheta;
                const double nl = std::cos(theta);
                const double sl = std::sin(theta);
                const float vTerm = EvaluateCharlieVisibility(static_cast<float>(nl), static_cast<float>(nv), static_cast<float>(r));
                for (int p = 0; p < kPhi; ++p) {
                    const double phi = (p + 0.5) * dPhi;
                    const glm::dvec3 L(sl * std::cos(phi), sl * std::sin(phi), nl);
                    const glm::dvec3 H = glm::normalize(V + L);
                    const float nh = static_cast<float>(std::max(0.0, H.z));
                    const float dTerm = EvaluateCharlieNDF(nh, static_cast<float>(r));
                    sum += static_cast<double>(dTerm) * static_cast<double>(vTerm) * nl * sl * dTheta * dPhi;
                }
            }
            return sum;
        }

        bool VerifyCharlieAlbedoAndEnergyConservation() {
            const float roughnesses[] = {0.04f, 0.15f, 0.35f, 0.65f, 0.95f};
            const float nvs[] = {0.03f, 0.10f, 0.25f, 0.55f, 0.90f};
            const glm::dvec3 N(0.0, 0.0, 1.0);
            constexpr int kTheta = 48;
            constexpr int kPhi = 96;
            const double dTheta = (0.5 * kPi) / kTheta;
            const double dPhi = (2.0 * kPi) / kPhi;

            for (float r : roughnesses) {
                for (float nv : nvs) {
                    const float estAlbedo = EvaluateCharlieDirectionalAlbedo(nv, r);
                    const double refAlbedo = IntegrateCharlieDirectionalAlbedoReference(nv, r);
                    if (!std::isfinite(estAlbedo) || estAlbedo < 0.0f || estAlbedo > 1.0f ||
                        std::abs(static_cast<double>(estAlbedo) - refAlbedo) > 0.015) {
                        return false;
                    }

                    // Furnace test with worst-case pure white Lambertian diffuse base (albedo = 1.0 -> integral = 1.0)
                    // plus pure white Charlie sheen layered on top: total exitance must never exceed 1.0 (+ quad tol).
                    const glm::dvec3 V(std::sqrt(std::max(0.0, 1.0 - nv * nv)), 0.0, nv);
                    glm::dvec3 totalWhiteFurnace(0.0);
                    for (int t = 0; t < kTheta; ++t) {
                        const double theta = (t + 0.5) * dTheta;
                        const double nl = std::cos(theta);
                        const double sl = std::sin(theta);
                        const glm::vec3 baseLambert(static_cast<float>(nl / kPi));
                        for (int p = 0; p < kPhi; ++p) {
                            const double phi = (p + 0.5) * dPhi;
                            const glm::dvec3 L(sl * std::cos(phi), sl * std::sin(phi), nl);
                            const glm::vec3 layered = ApplyCharlieSheenDirect(
                                baseLambert, glm::vec3(N), glm::vec3(V), glm::vec3(L),
                                true, glm::vec3(1.0f), r, true);
                            if (!std::isfinite(layered.r) || layered.r < -1e-6f) {
                                return false;
                            }
                            totalWhiteFurnace += glm::dvec3(layered) * sl * dTheta * dPhi;
                        }
                    }
                    if (totalWhiteFurnace.r > 1.015 || totalWhiteFurnace.g > 1.015 || totalWhiteFurnace.b > 1.015) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool VerifyDirectVsIBLUniformHemisphereParity() {
            const glm::vec3 N(0.0f, 0.0f, 1.0f);
            const glm::vec3 sheenColor(0.85f, 0.45f, 0.30f);
            const float roughnesses[] = {0.10f, 0.30f, 0.70f};
            const float nvs[] = {0.08f, 0.35f, 0.80f};
            constexpr int kTheta = 48;
            constexpr int kPhi = 96;
            const double dTheta = (0.5 * kPi) / kTheta;
            const double dPhi = (2.0 * kPi) / kPhi;

            for (float r : roughnesses) {
                for (float nv : nvs) {
                    const glm::vec3 V(std::sqrt(std::max(0.0f, 1.0f - nv * nv)), 0.0f, nv);
                    const glm::vec3 baseAmbient(0.25f, 0.25f, 0.25f);
                    const glm::vec3 iblResult = ApplyCharlieSheenIBL(
                        baseAmbient, glm::vec3(1.0f), nv, 1.0f, true, sheenColor, r, true);

                    glm::dvec3 directIntegral(0.0);
                    for (int t = 0; t < kTheta; ++t) {
                        const double theta = (t + 0.5) * dTheta;
                        const double nl = std::cos(theta);
                        const double sl = std::sin(theta);
                        // Integrate only the sheen lobe for strict hemisphere integral parity with IBL sheen term.
                        for (int p = 0; p < kPhi; ++p) {
                            const double phi = (p + 0.5) * dPhi;
                            const glm::vec3 L(static_cast<float>(sl * std::cos(phi)),
                                              static_cast<float>(sl * std::sin(phi)),
                                              static_cast<float>(nl));
                            const glm::vec3 directOnlySheen = ApplyCharlieSheenDirect(
                                glm::vec3(0.0f), N, V, L, true, sheenColor, r, true);
                            directIntegral += glm::dvec3(directOnlySheen) * sl * dTheta * dPhi;
                        }
                    }
                    const float eSheen = EvaluateCharlieDirectionalAlbedo(nv, r);
                    const float maxSheen = std::max(sheenColor.r, std::max(sheenColor.g, sheenColor.b));
                    const glm::vec3 expectedIBL = baseAmbient * (1.0f - maxSheen * eSheen) + glm::vec3(directIntegral);
                    if (glm::length(iblResult - expectedIBL) > 0.02f) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool VerifyGrazingRimAndRoughnessNarrowing() {
            const float centerSoft = EvaluateCharlieDirectionalAlbedo(0.95f, 0.45f);
            const float grazingSoft = EvaluateCharlieDirectionalAlbedo(0.08f, 0.45f);
            if (!(grazingSoft > centerSoft * 3.0f)) {
                return false;
            }
            // Lower roughness narrows grazing highlight relative to mid-angle (0.45):
            const float narrowRatio = EvaluateCharlieDirectionalAlbedo(0.06f, 0.08f) /
                                      std::max(EvaluateCharlieDirectionalAlbedo(0.45f, 0.08f), 1e-4f);
            const float wideRatio = EvaluateCharlieDirectionalAlbedo(0.06f, 0.80f) /
                                    std::max(EvaluateCharlieDirectionalAlbedo(0.45f, 0.80f), 1e-4f);
            return narrowRatio > wideRatio * 2.0f;
        }

        bool VerifyDisabledAndEdgeBehavior() {
            const glm::vec3 N(0.0f, 0.0f, 1.0f);
            const glm::vec3 V(0.0f, 0.0f, 1.0f);
            const glm::vec3 L = glm::normalize(glm::vec3(0.4f, 0.0f, 0.9f));
            const glm::vec3 base(0.42f, 0.18f, 0.09f);

            const glm::vec3 disabledDirect = ApplyCharlieSheenDirect(base, N, V, L, false, glm::vec3(1.0f), 0.3f, true);
            const glm::vec3 specOffDirect = ApplyCharlieSheenDirect(base, N, V, L, true, glm::vec3(1.0f), 0.3f, false);
            const glm::vec3 negRoughDirect = ApplyCharlieSheenDirect(base, N, V, L, true, glm::vec3(1.0f), -1.0f, true);
            const glm::vec3 disabledIBL = ApplyCharlieSheenIBL(base, glm::vec3(1.0f), 0.2f, 1.0f, false, glm::vec3(1.0f), 0.3f, true);
            const glm::vec3 envOffIBL = ApplyCharlieSheenIBL(base, glm::vec3(1.0f), 0.2f, 1.0f, true, glm::vec3(1.0f), 0.3f, false);
            if (glm::length(disabledDirect - base) > 1e-6f ||
                glm::length(specOffDirect - base) > 1e-6f ||
                glm::length(negRoughDirect - base) > 1e-6f ||
                glm::length(disabledIBL - base) > 1e-6f ||
                glm::length(envOffIBL - base) > 1e-6f) {
                return false;
            }
            const glm::vec3 belowHorizon = ApplyCharlieSheenDirect(base, N, V, glm::vec3(0.0f, 0.0f, -1.0f),
                true, glm::vec3(1.0f), 0.3f, true);
            return glm::length(belowHorizon - base) < 1e-6f &&
                   std::isfinite(EvaluateCharlieVisibility(1e-6f, 1e-6f, 0.01f));
        }
    } // namespace SheenContract

    // =========================================================================
    // M15 Unit 9 (S9.2 / U9-B): Beer-Lambert Absorption & Cauchy Dispersion Contract
    // =========================================================================
    namespace AbsorptionDispersionContract {
        float EvaluateSnellTransmissionCosine(float nDotV, float ior) {
            const float safeIOR = std::clamp(ior, 1.0f, 3.5f);
            const float invIOR = 1.0f / safeIOR;
            const float nv = std::clamp(nDotV, 0.0f, 1.0f);
            const float sin2I = std::max(0.0f, 1.0f - nv * nv);
            return std::sqrt(std::max(0.0f, 1.0f - invIOR * invIOR * sin2I));
        }

        float EvaluateAbsorptionPathLength(float thickness, float nDotV, float ior) {
            const float safeThickness = std::clamp(thickness, 0.0f, MaxThickness);
            const float cosThetaT = EvaluateSnellTransmissionCosine(nDotV, ior);
            return safeThickness / std::max(cosThetaT, MinTransmissionCos);
        }

        glm::vec3 EvaluateBeerLambertTransmittance(const glm::vec3& absorptionSigma,
                                                   float thickness,
                                                   float nDotV,
                                                   float ior) {
            const glm::vec3 safeSigma = glm::max(absorptionSigma, glm::vec3(0.0f));
            const float dist = EvaluateAbsorptionPathLength(thickness, nDotV, ior);
            return glm::exp(-safeSigma * dist);
        }

        glm::vec3 EvaluateCauchyIORs(float baseIOR, float dispersion) {
            const float iorG = std::clamp(baseIOR, 1.0f, 3.0f);
            const float bEff = (iorG > 1.001f) ? (std::clamp(dispersion, 0.0f, 1.0f) * 0.25f) : 0.0f;
            const float iorR = std::clamp(iorG - 0.9389f * bEff, 1.0f, 3.5f);
            const float iorB = std::clamp(iorG + 1.6325f * bEff, 1.0f, 3.5f);
            return glm::vec3(iorR, iorG, iorB);
        }

        bool VerifyBeerLambertExponentialAndClamps() {
            // Normal incidence with IOR=1.5 -> optical path == thickness
            const glm::vec3 sigma(2.5f, 0.5f, 1.25f);
            const float thicknesses[] = {0.0f, 0.25f, 0.5f, 1.0f, 2.0f};
            glm::vec3 prev(1.0f);
            for (float t : thicknesses) {
                const glm::vec3 T = EvaluateBeerLambertTransmittance(sigma, t, 1.0f, 1.5f);
                const glm::vec3 expected = glm::exp(-sigma * t);
                if (glm::length(T - expected) > 1e-5f) {
                    return false;
                }
                if (t > 0.0f && !(T.r < prev.r && T.g < prev.g && T.b < prev.b)) {
                    return false;
                }
                prev = T;
            }
            // Zero/negative absorption or zero thickness clamps to unit transmittance
            const glm::vec3 negT = EvaluateBeerLambertTransmittance(glm::vec3(-4.0f, -0.1f, 0.0f), 2.0f, 0.5f, 1.5f);
            const glm::vec3 zeroT = EvaluateBeerLambertTransmittance(sigma, -1.0f, 0.2f, 1.5f);
            if (glm::length(negT - glm::vec3(1.0f)) > 1e-5f || glm::length(zeroT - glm::vec3(1.0f)) > 1e-5f) {
                return false;
            }
            // Grazing angle edge clamp caps path length at <= thickness / MinTransmissionCos (5x)
            const float maxGrazingPath = EvaluateAbsorptionPathLength(1.0f, 0.0f, 1.0f);
            return std::isfinite(maxGrazingPath) && std::abs(maxGrazingPath - (1.0f / MinTransmissionCos)) < 1e-4f;
        }

        bool VerifyCauchyWavelengthDispersion() {
            const glm::vec3 zeroDisp = EvaluateCauchyIORs(1.52f, 0.0f);
            if (std::abs(zeroDisp.r - zeroDisp.g) > 1e-6f || std::abs(zeroDisp.b - zeroDisp.g) > 1e-6f) {
                return false;
            }
            const glm::vec3 vacuum = EvaluateCauchyIORs(1.0f, 0.8f);
            if (std::abs(vacuum.r - 1.0f) > 1e-6f || std::abs(vacuum.b - 1.0f) > 1e-6f) {
                return false;
            }
            const glm::vec3 flint = EvaluateCauchyIORs(1.62f, 0.35f);
            if (!(flint.r < flint.g && flint.g < flint.b)) {
                return false;
            }
            const glm::vec3 clamped = EvaluateCauchyIORs(1.62f, 5.0f);
            const glm::vec3 unitMax = EvaluateCauchyIORs(1.62f, 1.0f);
            return glm::length(clamped - unitMax) < 1e-6f;
        }

        bool VerifyOpaqueFallbackUnaffected() {
            // CPU surface constant check: non-refractive opaque material emits zero absorption & dispersion if disabled
            ResolvedSurface opaque;
            opaque.Surface = SurfaceType::Opaque;
            opaque.RefractionEnabled = false;
            opaque.AbsorptionEnabled = false;
            opaque.AbsorptionColor = glm::vec3(2.0f, 1.0f, 0.5f);
            opaque.Dispersion = 0.4f;
            const auto constants = opaque.ToMaterialConstants();
            return constants.Absorption == glm::vec4(0.0f) && constants.Refraction.w == 0.0f;
        }
    } // namespace AbsorptionDispersionContract

} // namespace Axle
